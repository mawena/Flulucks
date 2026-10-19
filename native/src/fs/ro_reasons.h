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
#define FL_RO_CORRUPT "structure incohérente vue pendant une écriture : vérifiez le disque sous Ubuntu"
#define FL_RO_ABORT "écriture interrompue : volume passé en lecture seule"
/* FAT / exFAT (phase 4) */
#define FL_RO_DIRTY "disque mal éjecté : vérifiez-le sur un ordinateur (chkdsk ou fsck) avant d'y écrire"
#define FL_RO_MEDIA "erreurs de support signalées par le volume : vérifiez-le sur un ordinateur"
#define FL_RO_SECTOR "taille de secteur non prise en charge en écriture"
/* NTFS (phase 5) ; volume sale : FL_RO_DIRTY, secteur plus petit que celui du disque : FL_RO_SECTOR */
#define FL_RO_HIBER "Windows en veille prolongée ou en démarrage rapide : arrêtez complètement Windows avant d'y écrire"
#define FL_RO_LOGFILE "disque mal fermé par Windows : rebranchez-le sous Windows, puis éjectez-le proprement"
#define FL_RO_VERSION "version de NTFS non prise en charge en écriture"

#endif
