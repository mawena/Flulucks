#ifndef FLULUKS_ERROR_H
#define FLULUKS_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FL_OK = 0,
  FL_EIO = -1,
  FL_EINVAL = -2,
  FL_ENOMEM = -3,
  FL_ENOTSUP = -4,
  FL_EBADPASS = -5,
  FL_ECORRUPT = -6,
  FL_EROFS = -7,
  FL_ENOENT = -8,
  FL_EEXIST = -9,
  FL_ENOTDIR = -10,
  FL_EISDIR = -11,
  FL_ENOTEMPTY = -12,
  FL_ENOSPC = -13,
  FL_ENODEV = -14,    /* disque débranché */
  FL_ETIMEDOUT = -15, /* le disque ne répond pas */
  FL_ESTALL = -16,    /* transfert USB bloqué (interne au transport USB) */
  FL_EBUSY = -17,     /* volume occupé */
  FL_EFBIG = -18      /* fichier trop gros pour ce système de fichiers (4 Gio - 1 en FAT) */
} fl_err;

/* Message lisible pour un code d'erreur. Jamais NULL ; code inconnu -> "erreur inconnue". */
const char *fl_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif
