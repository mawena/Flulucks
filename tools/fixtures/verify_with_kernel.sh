#!/usr/bin/env bash
# Vérification par le noyau Linux (root requis) : monte chaque image LUKS en lecture seule et
# compare le contenu à tree/. Usage : sudo verify_with_kernel.sh <dossier-fixtures>
set -uo pipefail
[ "$(id -u)" -eq 0 ] || { echo "à lancer avec sudo" >&2; exit 2; }
FX="$(cd "${1:?usage: verify_with_kernel.sh <dossier-fixtures>}" && pwd)"
MAP="fluluks-verify-$$"
MNT=""; LOOP=""

cleanup() {
  [ -n "$MNT" ] && mountpoint -q "$MNT" && umount "$MNT"
  [ -e "/dev/mapper/$MAP" ] && cryptsetup close "$MAP"
  [ -n "$LOOP" ] && losetup -d "$LOOP"
  [ -n "$MNT" ] && rmdir "$MNT"
  MNT=""; LOOP=""
}
trap cleanup EXIT

# check <image> <partitionnée 0|1> <fichier-mot-de-passe>
check() {
  local img="$1" part="$2" pass="$3" dev
  LOOP="$(losetup -r -P -f --show "$FX/$img")" || return 1
  dev="$LOOP"
  if [ "$part" = 1 ]; then
    dev="${LOOP}p1"
    for _ in 1 2 3 4 5 6 7 8 9 10; do [ -b "$dev" ] && break; sleep 0.2; done
  fi
  cryptsetup open --readonly --key-file "$FX/$pass" "$dev" "$MAP" || return 1
  MNT="$(mktemp -d)"
  mount -o ro,noload "/dev/mapper/$MAP" "$MNT" || return 1
  # lost+found est créé par mkfs.ext4 et absent de tree/ : exclu de la comparaison, mais exigé.
  [ -d "$MNT/lost+found" ] || return 1
  diff -r --exclude='clairsemé.bin' --exclude='lost+found' "$FX/tree" "$MNT" || return 1
  [ "$(stat -c %s "$MNT/clairsemé.bin")" = 5368709120 ] || return 1
  [ "$(tail -c 11 "$MNT/clairsemé.bin")" = "FIN-FLULUKS" ] || return 1
}

run() {
  if check "$@"; then echo "NOYAU OK : $1"; cleanup
  else echo "NOYAU ÉCHEC : $1"; exit 1; fi
}

run gpt-luks2.img            1 pass.txt
run whole-luks2-4k.img       0 pass.txt
run whole-luks2-2slots.img   0 pass.txt
run whole-luks2-2slots.img   0 pass2.txt
run mbr-luks1-essiv.img      1 pass.txt
run whole-luks1-xts.img      0 pass.txt
