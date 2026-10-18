#ifndef FL_RO_REASONS_H
#define FL_RO_REASONS_H

/* Raisons de lecture seule affichées à l'utilisateur (fl_statfs.ro_reason), texte exact du plan de la
 * phase 3. Chacune tient en 127 octets (UTF-8, sans le NUL) : test_ext4_ecriture le vérifie. */

#define FL_RO_FEATURES "options ext4 non prises en charge en écriture"
#define FL_RO_RECOVERY "journal à rejouer : branchez le disque sous Ubuntu"
#define FL_RO_ERRORS "système de fichiers marqué en erreur : vérifiez-le sous Ubuntu"
#define FL_RO_CSUM "sommes de contrôle invalides"
#define FL_RO_NOJOURNAL "pas de journal : écriture désactivée par sécurité"
#define FL_RO_BLOCKSIZE "taille de bloc non prise en charge en écriture"
#define FL_RO_WP "disque protégé en écriture"
#define FL_RO_USER "ouvert en lecture seule à votre demande"
#define FL_RO_IOERR "erreur d'écriture : volume passé en lecture seule"

#endif
