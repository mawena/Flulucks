/* Pont JNI du moteur FluLuks : conversion des arguments, le travail est fait par handles.c.
 *
 * Règles :
 *   - chaînes vers Java : ASCII seulement via NewStringUTF (version) ; tout le reste en byte[] UTF-8
 *     (JSON, messages d'erreur), car le « modified UTF-8 » de JNI casse les émojis ;
 *   - accès aux tableaux par GetByteArrayRegion / SetByteArrayRegion uniquement (jamais d'accès
 *     « critique » pendant une E/S, qui bloquerait le ramasse-miettes) ;
 *   - la phrase de passe est copiée en mémoire sécurisée puis le tableau Java est mis à zéro, sur tous
 *     les chemins (succès, erreur, poignée invalide) ; elle n'est jamais journalisée ;
 *   - erreurs : codes fl_err (< 0) ; les fonctions à JSON rendent {"err":<code>}. */
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "fluluks/error.h"
#include "fluluks/secure.h"
#include "handles.h"
#include "json_out.h"

#define FL_JNI_VERSION "fluluks 0.1.0"
#define PASS_MAX 8192   /* comme fluluks-cli */
#define PATH_BYTES 4096 /* chemins : 4095 octets au plus (fs.h) */

#define JNI_FN(name) Java_com_mawena_fluluks_engine_NativeEngine_##name

static jbyteArray to_bytes(JNIEnv *env, const char *s, size_t n) {
  if (n > (size_t)INT32_MAX) return NULL;
  jbyteArray a = (*env)->NewByteArray(env, (jsize)n);
  if (!a) return NULL; /* OutOfMemoryError en attente côté Java */
  if (n) (*env)->SetByteArrayRegion(env, a, 0, (jsize)n, (const jbyte *)s);
  return a;
}

/* Rend le JSON (ou {"err":rc}) et libère j. */
static jbyteArray json_result(JNIEnv *env, int rc, fl_json *j) {
  if (rc == FL_OK && j->err != FL_OK) rc = j->err;
  if (rc == FL_OK && !j->buf) rc = FL_EIO;
  if (rc != FL_OK) fl_json_error(j, rc);
  jbyteArray a = j->err == FL_OK && j->buf ? to_bytes(env, j->buf, j->len) : to_bytes(env, "{\"err\":-3}", 10);
  fl_json_free(j);
  return a;
}

/* Copie un byte[] en chaîne C terminée par NUL (malloc). NULL et *err si absent, trop long ou avec un NUL. */
static char *cstr_from(JNIEnv *env, jbyteArray a, int *err) {
  if (!a) {
    *err = FL_EINVAL;
    return NULL;
  }
  jsize n = (*env)->GetArrayLength(env, a);
  if (n < 0 || n > PATH_BYTES) {
    *err = FL_EINVAL;
    return NULL;
  }
  char *s = malloc((size_t)n + 1);
  if (!s) {
    *err = FL_ENOMEM;
    return NULL;
  }
  if (n) (*env)->GetByteArrayRegion(env, a, 0, n, (jbyte *)s);
  s[n] = 0;
  if (memchr(s, 0, (size_t)n)) {
    free(s);
    *err = FL_EINVAL;
    return NULL;
  }
  return s;
}

static void zero_array(JNIEnv *env, jbyteArray a, jsize n) {
  static const jbyte zeros[4096];
  for (jsize off = 0; off < n;) {
    jsize k = n - off < (jsize)sizeof zeros ? n - off : (jsize)sizeof zeros;
    (*env)->SetByteArrayRegion(env, a, off, k, zeros);
    off += k;
  }
}

JNIEXPORT jstring JNICALL JNI_FN(version)(JNIEnv *env, jclass clazz) {
  (void)clazz;
  /* Chaîne ASCII pure : NewStringUTF est sûr ici. */
  return (*env)->NewStringUTF(env, FL_JNI_VERSION);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(strerror)(JNIEnv *env, jclass clazz, jint code) {
  (void)clazz;
  const char *m = fl_strerror(code);
  return to_bytes(env, m, strlen(m));
}

JNIEXPORT jlong JNICALL JNI_FN(openImage)(JNIEnv *env, jclass clazz, jbyteArray path) {
  (void)clazz;
  int err = FL_OK;
  char *p = cstr_from(env, path, &err);
  if (!p) return err;
  jlong h = fj_open_image(p);
  free(p);
  return h;
}

JNIEXPORT jlong JNICALL JNI_FN(openUsb)(JNIEnv *env, jclass clazz, jint fd, jint iface, jint ep_in, jint ep_out) {
  (void)env;
  (void)clazz;
  return fj_open_usb(fd, iface, ep_in, ep_out);
}

JNIEXPORT jint JNICALL JNI_FN(closeDisk)(JNIEnv *env, jclass clazz, jlong disk) {
  (void)env;
  (void)clazz;
  return fj_close_disk(disk);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(scan)(JNIEnv *env, jclass clazz, jlong disk) {
  (void)clazz;
  fl_json j;
  fl_json_init(&j);
  int rc = fj_scan(disk, &j);
  return json_result(env, rc, &j);
}

JNIEXPORT jlong JNICALL JNI_FN(mount)(JNIEnv *env, jclass clazz, jlong disk, jint part, jbyteArray pass) {
  (void)clazz;
  uint8_t *p = NULL;
  size_t plen = 0, alloc = 0;
  int err = FL_OK;
  if (pass) {
    jsize n = (*env)->GetArrayLength(env, pass);
    if (n < 0 || n > PASS_MAX) {
      err = FL_EINVAL;
    } else {
      alloc = n ? (size_t)n : 1;
      p = fl_secure_alloc(alloc);
      if (!p) {
        err = FL_ENOMEM;
      } else {
        if (n) (*env)->GetByteArrayRegion(env, pass, 0, n, (jbyte *)p);
        plen = (size_t)n;
      }
    }
    zero_array(env, pass, n); /* avant toute validation : aussi sur les chemins d'erreur */
  }
  jlong r = err;
  if (err == FL_OK) r = part < 0 ? FL_EINVAL : fj_mount(disk, (uint32_t)part, p, plen);
  fl_secure_free(p, alloc);
  return r;
}

JNIEXPORT jint JNICALL JNI_FN(unmount)(JNIEnv *env, jclass clazz, jlong vol, jboolean force) {
  (void)env;
  (void)clazz;
  return fj_unmount(vol, force == JNI_TRUE);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(statfs)(JNIEnv *env, jclass clazz, jlong vol) {
  (void)clazz;
  fl_json j;
  fl_json_init(&j);
  int rc = fj_statfs(vol, &j);
  return json_result(env, rc, &j);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(stat)(JNIEnv *env, jclass clazz, jlong vol, jbyteArray path) {
  (void)clazz;
  fl_json j;
  fl_json_init(&j);
  int rc = FL_OK;
  char *p = cstr_from(env, path, &rc);
  if (p) rc = fj_stat(vol, p, &j);
  free(p);
  return json_result(env, rc, &j);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(list)(JNIEnv *env, jclass clazz, jlong vol, jbyteArray path) {
  (void)clazz;
  fl_json j;
  fl_json_init(&j);
  int rc = FL_OK;
  char *p = cstr_from(env, path, &rc);
  if (p) rc = fj_list(vol, p, &j);
  free(p);
  return json_result(env, rc, &j);
}

JNIEXPORT jlong JNICALL JNI_FN(openFile)(JNIEnv *env, jclass clazz, jlong vol, jbyteArray path) {
  (void)clazz;
  int err = FL_OK;
  char *p = cstr_from(env, path, &err);
  if (!p) return err;
  jlong h = fj_open_file(vol, p);
  free(p);
  return h;
}

JNIEXPORT jint JNICALL JNI_FN(pread)(JNIEnv *env, jclass clazz, jlong file, jlong offset, jbyteArray buf, jint len) {
  (void)clazz;
  if (!buf || len < 0 || offset < 0) return FL_EINVAL;
  if (len > (*env)->GetArrayLength(env, buf)) return FL_EINVAL;
  size_t n = (size_t)len < FJ_PREAD_MAX ? (size_t)len : FJ_PREAD_MAX;
  uint8_t one;
  uint8_t *tmp = n ? malloc(n) : &one; /* len 0 : valide quand même la poignée */
  if (!tmp) return FL_ENOMEM;
  int64_t r = fj_pread(file, tmp, n, (uint64_t)offset);
  if (r > 0) (*env)->SetByteArrayRegion(env, buf, 0, (jsize)r, (const jbyte *)tmp);
  if (n) free(tmp);
  return (jint)r; /* r <= FJ_PREAD_MAX ou fl_err */
}

JNIEXPORT jint JNICALL JNI_FN(closeFile)(JNIEnv *env, jclass clazz, jlong file) {
  (void)env;
  (void)clazz;
  return fj_close_file(file);
}
