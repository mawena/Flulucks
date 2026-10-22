#!/usr/bin/env bash
# Vérifie par le noyau Linux les disques formatés par Portail (phase 8). À lancer par l'utilisateur, avec
# sudo (loop, dm-crypt, montage) ; jamais par un agent.
#
# Préparation (sans root) :
#   FL_KEEP=1 ctest --test-dir build/native -R '^test_format$'
#   -> build/native/tmp/format-<fs>-<clair|luks>-<secteur>.img
# Usage :
#   sudo tools/fixtures/verify_format_with_kernel.sh [build/native/tmp]
#
# Pour chaque image : périphérique loop avec ses partitions (losetup -P), une seule partition attendue ;
# LUKS2 ouvert par dm-crypt (cryptsetup, phrase « phrase de passe ») ; système de fichiers monté en
# écriture par le pilote du noyau (vfat, exfat, ext4, ntfs3), un fichier écrit, démontage, puis l'outil de
# référence repasse (fsck.fat, fsck.exfat, e2fsck, ntfsfix) : aucune erreur. Les images à secteurs de
# 4 096 octets passent par un loop à secteurs de 4 096 octets (--sector-size).
set -uo pipefail
DIR="${1:-build/native/tmp}"
[ "$(id -u)" = 0 ] || { echo "à lancer avec sudo" >&2; exit 2; }
shopt -s nullglob
IMAGES=("$DIR"/format-*.img)
[ ${#IMAGES[@]} -gt 0 ] || { echo "aucune image format-*.img dans $DIR (FL_KEEP=1 ctest -R '^test_format\$')" >&2; exit 2; }

PASS="phrase de passe"
MAP=portail-verif
MNT="$(mktemp -d)"
LOOP=""
nettoyer() {
  umount "$MNT" 2>/dev/null || true
  cryptsetup close "$MAP" 2>/dev/null || true
  [ -n "$LOOP" ] && losetup -d "$LOOP" 2>/dev/null || true
  LOOP=""
}
trap 'nettoyer; rmdir "$MNT"' EXIT

echecs=0
for img in "${IMAGES[@]}"; do
  nom="$(basename "$img" .img)"           # format-<fs>-<clair|luks>-<secteur>
  IFS=- read -r _ fs mode secteur <<<"$nom"
  err() { echo "ÉCHEC $nom : $*"; echecs=$((echecs + 1)); nettoyer; }

  LOOP="$(losetup -f -P --sector-size "$secteur" --show "$img")" || { err "losetup"; continue; }
  udevadm settle 2>/dev/null || sleep 1
  part="${LOOP}p1"
  [ -b "$part" ] || { err "partition 1 absente"; continue; }
  dev="$part"
  if [ "$mode" = luks ]; then
    printf '%s' "$PASS" | cryptsetup open --key-file - "$part" "$MAP" || { err "cryptsetup open"; continue; }
    dev="/dev/mapper/$MAP"
  fi
  case "$fs" in
    fat32) type=vfat;  juge=(fsck.fat -n) ;;
    exfat) type=exfat; juge=(fsck.exfat -n) ;;
    ext4)  type=ext4;  juge=(e2fsck -fn) ;;
    ntfs)  type=ntfs3; juge=(ntfsfix -n) ;;
    *) err "système inconnu $fs"; continue ;;
  esac
  mount -t "$type" "$dev" "$MNT" || { err "montage $type"; continue; }
  echo "écrit par le noyau" >"$MNT/noyau.txt" && sync || { err "écriture"; continue; }
  umount "$MNT" || { err "démontage"; continue; }
  sortie="$("${juge[@]}" "$dev" 2>&1)" || { err "${juge[0]} :"; echo "$sortie"; continue; }
  etiquette="$(blkid -p -o value -s LABEL "$dev")"
  nettoyer
  echo "OK     $nom (étiquette « $etiquette »)"
done
echo "${#IMAGES[@]} image(s), $echecs échec(s)"
exit $((echecs > 0))
