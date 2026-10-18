#ifndef FLULUKS_JNI_JSON_OUT_H
#define FLULUKS_JNI_JSON_OUT_H

/* Écriture JSON minimale pour le pont JNI (sans dépendance à jni.h : compilé aussi dans les tests PC).
 *
 * Les virgules sont posées automatiquement entre éléments et entre paires clé/valeur.
 * Les chaînes sont échappées (`"`, `\`, caractères de contrôle en \u00XX) ; toute séquence UTF-8
 * invalide (octet isolé, séquence tronquée, forme trop longue, surrogate, > U+10FFFF) est remplacée
 * par U+FFFD, une fois par sous-partie maximale (comme l'algorithme recommandé par Unicode).
 * La sortie est donc toujours de l'UTF-8 valide.
 *
 * En cas d'échec d'allocation ou de dépassement du plafond (FL_JSON_MAX, ou celui de
 * fl_json_set_max), j->err passe à FL_ENOMEM et les écritures suivantes sont ignorées. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_JSON_MAX (64u * 1024u * 1024u)
/* Plafond dédié de `list` : un dossier énorme rend FL_ENOMEM plutôt qu'un tableau Java géant. */
#define FL_JSON_LIST_MAX (16u * 1024u * 1024u)

typedef struct {
  char *buf;   /* terminé par NUL quand len > 0 */
  size_t len, cap;
  size_t max;  /* plafond de len ; 0 = FL_JSON_MAX */
  int err;     /* FL_OK ou FL_ENOMEM */
  bool comma;  /* une virgule doit précéder le prochain élément */
} fl_json;

void fl_json_init(fl_json *j);
void fl_json_free(fl_json *j);
/* Vide le contenu (garde la mémoire et le plafond) et remet err à FL_OK. */
void fl_json_reset(fl_json *j);
/* Plafond de la sortie, borné à FL_JSON_MAX (0 = FL_JSON_MAX). */
void fl_json_set_max(fl_json *j, size_t max);

void fl_json_obj_begin(fl_json *j);
void fl_json_obj_end(fl_json *j);
void fl_json_arr_begin(fl_json *j);
void fl_json_arr_end(fl_json *j);
/* Clé d'objet (chaîne C) ; la valeur suit. */
void fl_json_key(fl_json *j, const char *k);
/* Chaîne de n octets (UTF-8 non vérifié en entrée, peut contenir des NUL : échappés en \u0000). */
void fl_json_str(fl_json *j, const char *s, size_t n);
void fl_json_cstr(fl_json *j, const char *s);
void fl_json_u64(fl_json *j, uint64_t v);
void fl_json_i64(fl_json *j, int64_t v);
void fl_json_bool(fl_json *j, bool v);

/* Remplace le contenu par {"err":<code>}. */
void fl_json_error(fl_json *j, int code);

#ifdef __cplusplus
}
#endif

#endif
