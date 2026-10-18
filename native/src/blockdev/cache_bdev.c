#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "fluluks/blockdev.h"
#include "fluluks/cache.h"
#include "fluluks/error.h"

/* Cache de blocs alignés, LRU (horodatage), lecture anticipée séquentielle.
 *
 * Invariants (tout l'état ci-dessous est protégé par `m`, sauf les octets des blocs) :
 *  - FREE    : emplacement libre.
 *  - LOADING : un fil lit le parent ; il est le seul à toucher aux octets du bloc. Jamais évincé.
 *  - READY   : octets valides et immuables tant que refs > 0 (les lecteurs copient hors verrou). Évincible
 *              seulement si refs == 0.
 *  - dead    : un écrivain a touché un bloc épinglé ; il est ignoré par la recherche et libéré au dernier
 *              désépinglage.
 *  - stale   : une écriture a croisé un chargement en cours ; à la fin du chargement le bloc est jeté
 *              (les octets lus ont pu précéder l'écriture).
 * Les blocs d'un même appel parent sont lus dans un tampon temporaire puis recopiés dans leurs
 * emplacements (non contigus). */

enum { S_FREE = 0, S_LOADING, S_READY };

typedef struct {
  uint64_t idx; /* numéro de bloc (secteur / spb) */
  uint64_t tick;
  uint32_t refs;
  uint8_t state;
  uint8_t dead;
  uint8_t stale;
} slot;

typedef struct {
  fl_bdev *parent;
  uint32_t block_bytes;
  uint32_t nblocks;
  uint32_t spb;       /* secteurs par bloc */
  uint64_t total_blk; /* nombre de blocs couvrant le parent (dernier éventuellement partiel) */
  uint8_t *data;
  slot *slots;
  pthread_mutex_t m;
  pthread_cond_t cv;
  uint64_t tick;
} cache;

/* m tenu. NULL si absent. */
static slot *find(cache *c, uint64_t idx) {
  for (uint32_t i = 0; i < c->nblocks; i++) {
    slot *s = &c->slots[i];
    if (s->state != S_FREE && !s->dead && s->idx == idx) return s;
  }
  return NULL;
}

/* m tenu. Emplacement libre ou LRU évincible (READY non épinglé), ou NULL. */
static slot *victim(cache *c) {
  slot *best = NULL;
  for (uint32_t i = 0; i < c->nblocks; i++) {
    slot *s = &c->slots[i];
    if (s->state == S_FREE) return s;
    if (s->state == S_READY && s->refs == 0 && (!best || s->tick < best->tick)) best = s;
  }
  return best;
}

static uint8_t *slot_data(cache *c, const slot *s) { return c->data + (size_t)(s - c->slots) * c->block_bytes; }

/* m tenu. */
static void unpin(cache *c, slot *s) {
  if (--s->refs == 0) {
    if (s->dead) {
      s->state = S_FREE;
      s->dead = 0;
    }
    pthread_cond_broadcast(&c->cv);
  }
}

/* m tenu. Vrai si le bloc idx-1 est présent (prêt ou en chargement) : le flux est séquentiel. */
static int prev_present(cache *c, uint64_t idx) { return idx > 0 && find(c, idx - 1) != NULL; }

/* m tenu. Réserve un emplacement LOADING pour le bloc idx. */
static void claim(slot *s, uint64_t idx) {
  s->state = S_LOADING;
  s->idx = idx;
  s->refs = 0;
  s->dead = s->stale = 0;
}

/* Lit n blocs consécutifs depuis idx en UN appel parent, recopie la part demandée du premier bloc vers out
 * puis remplit les emplacements réservés. N'ajuste pas l'état des emplacements. */
static int load_blocks(cache *c, uint64_t idx, uint32_t n, slot **claimed, size_t off, size_t len, uint8_t *out) {
  uint64_t first = idx * c->spb;
  uint64_t nsec = (uint64_t)n * c->spb;
  if (nsec > c->parent->sector_count - first) nsec = c->parent->sector_count - first;
  size_t bytes = (size_t)nsec * c->parent->sector_size;
  uint8_t *tmp = malloc((size_t)n * c->block_bytes);
  int rc = tmp ? fl_bdev_read(c->parent, first, (uint32_t)nsec, tmp) : FL_ENOMEM;
  if (rc == FL_OK) {
    memcpy(out, tmp + off, len);
    for (uint32_t k = 0; k < n; k++) {
      size_t boff = (size_t)k * c->block_bytes;
      size_t blen = bytes - boff < c->block_bytes ? bytes - boff : c->block_bytes;
      memcpy(slot_data(c, claimed[k]), tmp + boff, blen);
    }
  }
  free(tmp);
  return rc;
}

/* Copie [off, off+len) du bloc idx vers out. `*seq` : -1 inconnu, 0 non, 1 oui (flux séquentiel : un défaut
 * de cache lit jusqu'à FL_CACHE_READAHEAD blocs en un appel). `req_left` : blocs restant à lire pour la
 * requête, dont celui-ci ; sans flux séquentiel on ne lit jamais plus que ce que la requête demande. */
static int read_in_block(cache *c, uint64_t idx, size_t off, size_t len, uint8_t *out, int *seq, int first_blk,
                         uint64_t req_left) {
  slot *claimed[FL_CACHE_READAHEAD];
  uint32_t n = 0;

  pthread_mutex_lock(&c->m);
  for (;;) {
    slot *s = find(c, idx);
    if (s) {
      if (s->state == S_READY) {
        s->refs++;
        s->tick = ++c->tick;
        pthread_mutex_unlock(&c->m);
        memcpy(out, slot_data(c, s) + off, len);
        pthread_mutex_lock(&c->m);
        unpin(c, s);
        pthread_mutex_unlock(&c->m);
        return FL_OK;
      }
      pthread_cond_wait(&c->cv, &c->m); /* en cours de chargement par un autre fil */
      continue;
    }
    slot *v = victim(c);
    if (!v) { /* tout est épinglé ou en chargement */
      pthread_cond_wait(&c->cv, &c->m);
      continue;
    }
    if (*seq < 0) *seq = first_blk ? prev_present(c, idx) : 0;
    uint64_t want = *seq ? FL_CACHE_READAHEAD : (req_left < FL_CACHE_READAHEAD ? req_left : FL_CACHE_READAHEAD);
    if (want > c->total_blk - idx) want = c->total_blk - idx;
    claim(v, idx);
    claimed[0] = v;
    n = 1;
    while (n < want) { /* blocs consécutifs absents du cache, tant qu'il reste des emplacements */
      if (find(c, idx + n)) break;
      slot *w = victim(c);
      if (!w) break;
      claim(w, idx + n);
      claimed[n++] = w;
    }
    break;
  }
  pthread_mutex_unlock(&c->m);

  /* Lecture parent hors verrou, en un seul appel. */
  int rc = load_blocks(c, idx, n, claimed, off, len, out);
  if (rc != FL_OK && n > 1) {
    /* Une erreur dans un bloc spéculatif ne doit pas faire échouer le bloc demandé : on libère les
     * réservations anticipées puis on relit le bloc demandé seul. */
    pthread_mutex_lock(&c->m);
    for (uint32_t k = 1; k < n; k++) {
      claimed[k]->state = S_FREE;
      claimed[k]->stale = 0;
    }
    pthread_cond_broadcast(&c->cv);
    pthread_mutex_unlock(&c->m);
    n = 1;
    rc = load_blocks(c, idx, 1, claimed, off, len, out);
  }

  pthread_mutex_lock(&c->m);
  for (uint32_t k = 0; k < n; k++) {
    slot *s = claimed[k];
    if (rc == FL_OK && !s->stale) {
      s->state = S_READY;
      s->tick = ++c->tick;
    } else {
      s->state = S_FREE;
    }
    s->stale = 0;
  }
  pthread_cond_broadcast(&c->cv);
  pthread_mutex_unlock(&c->m);
  return rc;
}

static int cache_read(fl_bdev *d, uint64_t lba, uint32_t count, void *buf) {
  cache *c = d->priv;
  const uint32_t ss = d->sector_size;
  uint8_t *out = buf;
  uint64_t cur = lba;
  uint32_t left = count;
  int seq = -1;
  while (left) {
    uint64_t idx = cur / c->spb;
    uint32_t off = (uint32_t)(cur % c->spb);
    uint32_t n = c->spb - off < left ? c->spb - off : left;
    uint64_t req_blocks = (cur + left - 1) / c->spb - idx + 1;
    int rc = read_in_block(c, idx, (size_t)off * ss, (size_t)n * ss, out, &seq, cur == lba, req_blocks);
    if (rc != FL_OK) return rc;
    out += (size_t)n * ss;
    cur += n;
    left -= n;
  }
  return FL_OK;
}

static int cache_write(fl_bdev *d, uint64_t lba, uint32_t count, const void *buf) {
  cache *c = d->priv;
  const uint32_t ss = d->sector_size;
  /* NB : deux écritures concurrentes qui se recouvrent peuvent laisser le parent et le cache dans des
   * ordres différents. Avant la phase 3, il faut un verrou d'ordonnancement des écritures (ou les
   * sérialiser côté appelant). */
  int rc = fl_bdev_write(c->parent, lba, count, buf);
  /* Si le parent échoue, l'écriture a pu être partielle : les blocs touchés sont invalidés ci-dessous. */
  uint64_t end = lba + count; /* exclusif */
  pthread_mutex_lock(&c->m);
  for (uint32_t i = 0; i < c->nblocks; i++) {
    slot *s = &c->slots[i];
    if (s->state == S_FREE || s->dead) continue;
    uint64_t b0 = s->idx * c->spb, b1 = b0 + c->spb;
    if (b1 <= lba || b0 >= end) continue;
    if (s->state == S_LOADING) {
      s->stale = 1;
    } else if (rc != FL_OK || s->refs > 0) {
      if (s->refs > 0) s->dead = 1;
      else s->state = S_FREE;
    } else {
      uint64_t o0 = lba > b0 ? lba : b0, o1 = end < b1 ? end : b1;
      memcpy(slot_data(c, s) + (size_t)(o0 - b0) * ss, (const uint8_t *)buf + (size_t)(o0 - lba) * ss,
             (size_t)(o1 - o0) * ss);
    }
  }
  pthread_mutex_unlock(&c->m);
  return rc;
}

static int cache_flush(fl_bdev *d) { return fl_bdev_flush(((cache *)d->priv)->parent); }

static void cache_destroy(fl_bdev *d) {
  cache *c = d->priv;
  pthread_cond_destroy(&c->cv);
  pthread_mutex_destroy(&c->m);
  free(c->slots);
  free(c->data);
  free(c);
  free(d);
}

static const fl_bdev_ops cache_ops = {cache_read, cache_write, cache_flush, cache_destroy};

int fl_cache_bdev_new(fl_bdev *parent, uint32_t block_bytes, uint32_t nblocks, fl_bdev **out) {
  if (!out) return FL_EINVAL;
  *out = NULL;
  if (!parent || parent->sector_size == 0) return FL_EINVAL;
  if (block_bytes == 0 || (block_bytes & (block_bytes - 1)) != 0 || block_bytes > FL_CACHE_MAX_BLOCK_BYTES ||
      block_bytes % parent->sector_size != 0)
    return FL_EINVAL;
  if (nblocks < FL_CACHE_READAHEAD + 1) return FL_EINVAL;
  if ((size_t)nblocks > SIZE_MAX / block_bytes) return FL_ENOMEM;

  fl_bdev *d = calloc(1, sizeof *d);
  cache *c = calloc(1, sizeof *c);
  if (!d || !c) {
    free(d);
    free(c);
    return FL_ENOMEM;
  }
  c->data = malloc((size_t)nblocks * block_bytes);
  c->slots = calloc(nblocks, sizeof *c->slots);
  if (!c->data || !c->slots) {
    free(c->data);
    free(c->slots);
    free(c);
    free(d);
    return FL_ENOMEM;
  }
  c->parent = parent;
  c->block_bytes = block_bytes;
  c->nblocks = nblocks;
  c->spb = block_bytes / parent->sector_size;
  c->total_blk = parent->sector_count / c->spb + (parent->sector_count % c->spb ? 1 : 0);
  pthread_mutex_init(&c->m, NULL);
  pthread_cond_init(&c->cv, NULL);
  d->ops = &cache_ops;
  d->sector_size = parent->sector_size;
  d->sector_count = parent->sector_count;
  d->read_only = parent->read_only;
  d->priv = c;
  *out = d;
  return FL_OK;
}
