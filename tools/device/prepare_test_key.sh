#!/usr/bin/env bash
# Prépare une clé USB SACRIFIÉE pour les essais de FluLuks : elle reproduit le format du vrai
# disque (GPT, une partition LUKS2 argon2id/aes-xts-plain64, ext4 aux mêmes options).
#
#   *** CE SCRIPT EFFACE TOUT LE PÉRIPHÉRIQUE DONNÉ. TOUTES LES DONNÉES SONT PERDUES. ***
#
# Usage : sudo prepare_test_key.sh /dev/sdX [--video fichier.mp4] [--dry-run]
#   --video    copie ce fichier à la racine du système de fichiers de la clé
#   --dry-run  fait toutes les vérifications, affiche le résumé et les commandes, n'écrit rien
# La phrase de passe est saisie au clavier, deux fois, aux invites de cryptsetup (jamais en argument).
set -euo pipefail

MAX_BYTES=256000000000
MAP="fluluks-testkey-$$"
STAGING=""
MAP_OPEN=0

die() { echo "REFUS : $*" >&2; exit 1; }

cleanup() {
  local rc=$?
  trap - EXIT
  if [ "$MAP_OPEN" = 1 ] && [ -e "/dev/mapper/$MAP" ]; then
    cryptsetup close "$MAP" || echo "ATTENTION : fermeture de /dev/mapper/$MAP impossible" >&2
  fi
  if [ -n "$STAGING" ] && [ -d "$STAGING" ]; then rm -rf -- "$STAGING"; fi
  [ "$rc" -eq 0 ] || echo "Arrêt (code $rc) ; nettoyage effectué." >&2
  exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURES="$HERE/../fixtures"
DEV=""; VIDEO=""; DRY=0
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY=1 ;;
    --video) shift; [ $# -gt 0 ] || die "--video demande un fichier"; VIDEO="$1" ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    -*) die "option inconnue : $1" ;;
    *) [ -z "$DEV" ] || die "un seul périphérique attendu"; DEV="$1" ;;
  esac
  shift
done
[ -n "$DEV" ] || die "usage : sudo $0 /dev/sdX [--video fichier.mp4] [--dry-run]"

# --- Vérifications (aucune écriture, aucune invite avant la fin) -------------------------
if [ "$DRY" = 0 ] && [ "$(id -u)" -ne 0 ]; then die "à lancer avec sudo (ou ajoutez --dry-run)"; fi

missing=""
for t in cryptsetup sfdisk mkfs.ext4 wipefs udevadm python3 lsblk blockdev; do
  command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
done
[ -z "$missing" ] || die "outils manquants :$missing"

[ -b "$DEV" ] || die "« $DEV » n'est pas un périphérique bloc"
DEV="$(readlink -f -- "$DEV")"
[ -b "$DEV" ] || die "« $DEV » n'est pas un périphérique bloc"
NAME="$(basename "$DEV")"

[ "$(lsblk -dno TYPE "$DEV" | head -n1 | tr -d ' ')" = "disk" ] \
  || die "« $DEV » n'est pas un disque entier (partition, loop, mapping… refusé)"

case "$NAME" in
  nvme*|mmcblk*|loop*|zram*|dm-*|md*|sr*) die "« $NAME » est un type de périphérique interdit (nvme, mmcblk, loop, zram, dm, md, sr)" ;;
esac

TRAN="$(lsblk -dno TRAN "$DEV" | head -n1 | tr -d ' ')"
[ "$TRAN" = "usb" ] || die "« $DEV » n'est pas sur le bus USB (transport : « ${TRAN:-inconnu} »)"

SIZE="$(lsblk -bdno SIZE "$DEV" | head -n1 | tr -d ' ')"
case "$SIZE" in ''|*[!0-9]*) die "taille illisible pour $DEV" ;; esac
[ "$SIZE" -le "$MAX_BYTES" ] || die "« $DEV » fait $SIZE octets, plus que la limite de $MAX_BYTES (256 Go)"

# Points de montage du disque et de tous ses enfants, un par ligne (lignes vides comprises).
# MOUNTPOINTS (util-linux >= 2.37) les donne tous ; sinon repli sur MOUNTPOINT. Échec des deux : code 1.
list_mountpoints() {
  local out
  if out="$(lsblk -nro MOUNTPOINTS "$1" 2>/dev/null)"; then printf '%s\n' "$out" | tr ' ' '\n'; return 0; fi
  if out="$(lsblk -nro MOUNTPOINT "$1" 2>/dev/null)"; then printf '%s\n' "$out"; return 0; fi
  return 1
}

# Montages / swap : disque et tous ses enfants (récursif). Un échec de lsblk n'est jamais avalé.
MPS="$(list_mountpoints "$DEV")" || die "impossible de lister les points de montage de $DEV (lsblk)"
while IFS= read -r mp; do
  [ -z "$mp" ] || die "$DEV ou l'une de ses partitions est monté ($mp)"
done <<< "$MPS"
FSTYPES="$(lsblk -nro FSTYPE "$DEV")" || die "impossible de lire les systèmes de fichiers de $DEV (lsblk)"
if printf '%s\n' "$FSTYPES" | grep -qx swap; then die "$DEV contient de l'espace d'échange (swap)"; fi
NAMES="$(lsblk -nro NAME "$DEV")" || die "impossible de lister les partitions de $DEV (lsblk)"
for n in $NAMES; do
  if [ -r /proc/swaps ] && awk 'NR>1{print $1}' /proc/swaps | grep -qx "/dev/$n"; then
    die "/dev/$n est utilisé comme swap"
  fi
done
# Le disque qui porte / ou /boot est couvert par la vérification de montage ci-dessus ;
# on le redit explicitement pour un message clair.
for m in / /boot /boot/efi; do
  src="$(findmnt -nro SOURCE "$m" 2>/dev/null || true)"
  [ -z "$src" ] && continue
  pk="$(lsblk -nro PKNAME "$src" 2>/dev/null | head -n1 || true)"
  if [ "$src" = "$DEV" ] || [ "/dev/$pk" = "$DEV" ]; then die "$DEV porte « $m » (système) : refusé"; fi
done
# Détenu par un autre périphérique (LUKS ouvert, LVM, RAID…).
for h in "/sys/block/$NAME/holders" "/sys/block/$NAME"/*/holders; do
  [ -d "$h" ] || continue
  if [ -n "$(ls -A -- "$h" 2>/dev/null)" ]; then die "$DEV est utilisé par un autre périphérique ($h : $(ls "$h" | tr '\n' ' '))"; fi
done

if [ -n "$VIDEO" ]; then
  [ -f "$VIDEO" ] && [ -r "$VIDEO" ] || die "fichier vidéo absent ou illisible : $VIDEO"
  VIDEO="$(readlink -f -- "$VIDEO")"
fi

# --- Options mkfs : orphan_file n'existe qu'à partir de e2fsprogs 1.47 --------------------
E2VER="$(mke2fs -V 2>&1 | sed -n 's/^mke2fs \([0-9][0-9.]*\).*/\1/p' | head -n1)"
E2MAJ="${E2VER%%.*}"; E2REST="${E2VER#*.}"; E2MIN="${E2REST%%.*}"
if [ "${E2MAJ:-0}" -gt 1 ] || { [ "${E2MAJ:-0}" -eq 1 ] && [ "${E2MIN:-0}" -ge 47 ]; }; then
  MKFS_FEAT="^orphan_file,^metadata_csum_seed"
  FEAT_NOTE="e2fsprogs $E2VER : orphan_file connu, désactivé comme sur le vrai disque"
else
  MKFS_FEAT="^metadata_csum_seed"
  FEAT_NOTE="e2fsprogs ${E2VER:-inconnu} < 1.47 : orphan_file inconnu, seul ^metadata_csum_seed est passé"
fi

# --- Résumé ------------------------------------------------------------------------------
echo "=== Périphérique qui sera EFFACÉ ==="
echo "  Chemin     : $DEV"
echo "  Modèle     : $(lsblk -dno MODEL "$DEV" | sed 's/ *$//')"
echo "  Série      : $(lsblk -dno SERIAL "$DEV" | sed 's/ *$//')"
echo "  Taille     : $SIZE octets ($(( SIZE / 1000000 )) Mo)"
echo "  Transport  : $TRAN"
[ -z "$VIDEO" ] || echo "  Vidéo      : $VIDEO"
echo "  mkfs       : $FEAT_NOTE"
lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEV"

PART_HINT="${DEV}1"
case "$NAME" in *[0-9]) PART_HINT="${DEV}p1" ;; esac
if [ "$DRY" = 1 ]; then
  cat <<EOT
=== --dry-run : commandes qui seraient exécutées (rien n'est écrit) ===
  wipefs -a $DEV
  printf 'label: gpt\nstart=2048, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4\n' | sfdisk --wipe always $DEV
  udevadm settle ; attente du nœud de partition (≈ $PART_HINT, 10 s max)
  cryptsetup luksFormat --type luks2 --cipher aes-xts-plain64 --key-size 512 --sector-size 512 \\
    --hash sha256 --pbkdf argon2id --pbkdf-force-iterations 7 --pbkdf-memory 1048576 --pbkdf-parallel 4 $PART_HINT
  cryptsetup open $PART_HINT $MAP
  STAGING=\$(mktemp -d) ; python3 -I $FIXTURES/make_tree.py \$STAGING ${VIDEO:+; cp $VIDEO \$STAGING/}
  mkfs.ext4 -q -F -T default -b 4096 -I 256 -O $MKFS_FEAT -L FLUTESTKEY -d \$STAGING /dev/mapper/$MAP
  sync ; dumpe2fs -h /dev/mapper/$MAP ; cryptsetup close $MAP ; rm -rf \$STAGING
EOT
  exit 0
fi

echo
echo "Pour confirmer l'EFFACEMENT COMPLET de $DEV, retapez exactement son chemin :"
IFS= read -r REPLY_DEV || die "aucune confirmation"
[ "$REPLY_DEV" = "$DEV" ] || { echo "Confirmation différente : abandon, rien n'a été écrit." >&2; exit 1; }

# --- 1. Table de partitions -------------------------------------------------------------
wipefs -a "$DEV"
printf 'label: gpt\nstart=2048, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4\n' | sfdisk --wipe always "$DEV"
udevadm settle
PART=""
for _ in $(seq 1 50); do
  child="$(lsblk -nrlo NAME,TYPE "$DEV" | awk '$2=="part"{print $1; exit}')"
  if [ -n "$child" ] && [ -b "/dev/$child" ]; then PART="/dev/$child"; break; fi
  sleep 0.2
done
[ -n "$PART" ] || die "le nœud de partition n'est pas apparu après 10 s"
echo "Partition : $PART"

# --- 2-3. LUKS2 (phrase de passe saisie par l'utilisateur) ---------------------------------
cryptsetup luksFormat --type luks2 --cipher aes-xts-plain64 --key-size 512 --sector-size 512 \
  --hash sha256 --pbkdf argon2id --pbkdf-force-iterations 7 --pbkdf-memory 1048576 --pbkdf-parallel 4 "$PART"
cryptsetup open "$PART" "$MAP"
MAP_OPEN=1

# --- 4. Contenu --------------------------------------------------------------------------
STAGING="$(mktemp -d)"
python3 -I "$FIXTURES/make_tree.py" "$STAGING"
if [ -n "$VIDEO" ]; then cp -- "$VIDEO" "$STAGING/"; fi

# --- 5. ext4 -----------------------------------------------------------------------------
echo "mkfs.ext4 : $FEAT_NOTE"
mkfs.ext4 -q -F -T default -b 4096 -I 256 -O "$MKFS_FEAT" -L FLUTESTKEY -d "$STAGING" "/dev/mapper/$MAP"

# --- 6-7. Fin ----------------------------------------------------------------------------
sync
FEATURES="$(dumpe2fs -h "/dev/mapper/$MAP" 2>/dev/null | grep -i '^Filesystem features' || true)"
cryptsetup close "$MAP"; MAP_OPEN=0
rm -rf -- "$STAGING"; STAGING=""
echo "Clé prête : branchez-la sur le téléphone."
echo "À comparer avec le vrai disque : ${FEATURES:-(dumpe2fs sans résultat)}"
