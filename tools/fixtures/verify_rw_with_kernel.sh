#!/usr/bin/env bash
# Vérification par le noyau Linux (root requis) des images écrites par le moteur (phase 3, T8) : monte
# en lecture seule chaque <nom>.ecrite.img laissée par test_batterie (LUKS : cryptsetup open --readonly
# avec <nom>.pass) et compare son contenu à <nom>.manifest (chemin, type, taille, condensat, uid, gid,
# mode ; format : native/tests/support/tree_model.h, tm_write_manifest). Les <nom>.echec.img (preuves
# laissées par une batterie en échec) ne sont jamais vérifiées.
# Usage : sudo tools/fixtures/verify_rw_with_kernel.sh build/native/batterie
set -uo pipefail
[ "$(id -u)" -eq 0 ] || { echo "à lancer avec sudo" >&2; exit 2; }
DIR="$(cd "${1:?usage: verify_rw_with_kernel.sh <dossier-batterie>}" && pwd)" || exit 2
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" || exit 2
MAP="fluluks-rw-verify-$$"
MNT=""; LOOP=""

cleanup() {
  [ -n "$MNT" ] && mountpoint -q "$MNT" && umount "$MNT"
  [ -e "/dev/mapper/$MAP" ] && cryptsetup close "$MAP"
  [ -n "$LOOP" ] && losetup -d "$LOOP"
  [ -n "$MNT" ] && rmdir "$MNT"
  MNT=""; LOOP=""
}
trap cleanup EXIT

# Manifeste du dossier monté, au format de tm_write_manifest (lost+found à la racine exclu).
manifeste() { python3 "$SCRIPT_DIR/manifest.py" "$1"; }

# Lignes « EXT4-fs error|warning » du journal du noyau (pour comparer avant/après un montage).
dmesg_ext4() { dmesg 2>/dev/null | grep -E 'EXT4-fs (error|warning)' || true; }

# check <image>
check() {
  local img="$1" base="${1%.ecrite.img}" dev
  LOOP="$(losetup -r -f --show "$img")" || return 1
  dev="$LOOP"
  if cryptsetup isLuks "$LOOP"; then
    cryptsetup open --readonly --key-file "$base.pass" "$LOOP" "$MAP" || return 1
    dev="/dev/mapper/$MAP"
  fi
  MNT="$(mktemp -d)"
  # noload : jamais de rejeu du journal (l'image doit être propre telle quelle)
  mount -t ext4 -o ro,noload "$dev" "$MNT" || return 1
  # toute ligne EXT4-fs error/warning apparue depuis le début du montage est un échec
  local apres nouvelles
  apres="$(dmesg_ext4)"
  nouvelles="$(comm -13 <(printf '%s\n' "$AVANT" | sort -u) <(printf '%s\n' "$apres" | sort -u))"
  if [ -n "$nouvelles" ]; then
    echo "le noyau signale des erreurs ext4 :" >&2
    printf '%s\n' "$nouvelles" >&2
    return 1
  fi
  diff <(sort "$base.manifest") <(manifeste "$MNT" | sort) || return 1
}

n=0
for img in "$DIR"/*.ecrite.img; do
  [ -e "$img" ] || { echo "aucune image *.ecrite.img dans $DIR" >&2; exit 1; }
  # seulement des fichiers réguliers, jamais un lien ; le manifeste est obligatoire
  if [ ! -f "$img" ] || [ -L "$img" ]; then
    echo "NOYAU ÉCHEC $(basename "$img") (pas un fichier régulier)"; n=$((n + 1)); continue
  fi
  if [ ! -f "${img%.ecrite.img}.manifest" ]; then
    echo "NOYAU ÉCHEC $(basename "$img") (manifeste absent)"; n=$((n + 1)); continue
  fi
  AVANT="$(dmesg_ext4)"
  if check "$img"; then echo "NOYAU OK $(basename "$img")"
  else echo "NOYAU ÉCHEC $(basename "$img")"; n=$((n + 1)); fi
  cleanup
done
exit $((n > 0))
