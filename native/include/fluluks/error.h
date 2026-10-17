#ifndef FLULUKS_ERROR_H
#define FLULUKS_ERROR_H

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
  FL_ENOSPC = -13
} fl_err;

/* Message lisible pour un code d'erreur. Jamais NULL ; code inconnu -> "erreur inconnue". */
const char *fl_strerror(int err);

#endif
