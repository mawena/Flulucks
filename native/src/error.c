#include "fluluks/error.h"

const char *fl_strerror(int err) {
  switch (err) {
    case FL_OK: return "succès";
    case FL_EIO: return "erreur d'entrée/sortie";
    case FL_EINVAL: return "argument invalide";
    case FL_ENOMEM: return "mémoire insuffisante";
    case FL_ENOTSUP: return "format non pris en charge";
    case FL_EBADPASS: return "phrase de passe incorrecte";
    case FL_ECORRUPT: return "données corrompues ou illisibles";
    case FL_EROFS: return "volume en lecture seule";
    case FL_ENOENT: return "fichier ou dossier introuvable";
    case FL_EEXIST: return "existe déjà";
    case FL_ENOTDIR: return "n'est pas un dossier";
    case FL_EISDIR: return "est un dossier";
    case FL_ENOTEMPTY: return "dossier non vide";
    case FL_ENOSPC: return "plus d'espace disponible";
    default: return "erreur inconnue";
  }
}
