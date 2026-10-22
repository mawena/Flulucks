#!/usr/bin/env bash
# Essais de la phase 8 (formatage) sur le PC, en une commande. À lancer par l'utilisateur, depuis
# n'importe où :
#   sudo tools/fixtures/essais_phase8_pc.sh
#
# 1. Sans root (sous l'utilisateur qui a lancé sudo) : test_format formate 10 disques image (FAT32, exFAT,
#    ext4, NTFS, en clair et chiffrés LUKS2 ; secteurs de 512 et de 4 096 octets) et les garde dans
#    build/native/tmp/.
# 2. En root : verify_format_with_kernel.sh monte chaque image par le noyau (loop, dm-crypt), y écrit un
#    fichier, démonte, et repasse l'outil de référence (fsck.fat, fsck.exfat, e2fsck, ntfsfix).
# 3. Les images sont supprimées, le compte rendu est écrit dans
#    docs/superpowers/research/2026-10-22-essais-phase8/pc-<date>.txt.
# Ne touche à aucun vrai disque : seulement aux fichiers image, par des périphériques loop.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[ "$(id -u)" = 0 ] || { echo "à lancer avec sudo : sudo $0" >&2; exit 2; }
USER_NAME="${SUDO_USER:-}"
[ -n "$USER_NAME" ] && [ "$USER_NAME" != root ] || { echo "lancez-le par sudo depuis votre compte (pas en root direct)" >&2; exit 2; }
for outil in ctest losetup cryptsetup blkid fsck.fat fsck.exfat e2fsck ntfsfix udevadm; do
  command -v "$outil" >/dev/null || { echo "outil absent : $outil" >&2; exit 2; }
done
[ -x "$ROOT/build/native/test_format" ] || {
  echo "build/native/test_format absent : construisez d'abord le moteur (cmake --build build/native)" >&2; exit 2; }

OUT="$ROOT/docs/superpowers/research/2026-10-22-essais-phase8"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG="$OUT/pc-$STAMP.txt"
sudo -u "$USER_NAME" mkdir -p "$OUT"
sudo -u "$USER_NAME" touch "$LOG"
exec > >(tee -a "$LOG") 2>&1

echo "# essais de la phase 8 sur le PC, $STAMP"
echo "# commit $(sudo -u "$USER_NAME" git -C "$ROOT" rev-parse --short HEAD), noyau $(uname -r)"

echo "## 1. formatage des images (sans root, utilisateur $USER_NAME)"
rm -f "$ROOT"/build/native/tmp/format-*.img
if ! sudo -u "$USER_NAME" env FL_KEEP=1 ctest --test-dir "$ROOT/build/native" -R '^test_format$' --output-on-failure; then
  echo "## fin : ÉCHEC du formatage des images"
  exit 1
fi

echo "## 2. vérification par le noyau"
"$ROOT/tools/fixtures/verify_format_with_kernel.sh" "$ROOT/build/native/tmp"
RC=$?

echo "## 3. nettoyage"
rm -f "$ROOT"/build/native/tmp/format-*.img
echo "images supprimées"
echo "## fin : $([ $RC -eq 0 ] && echo 'tout est vert' || echo 'ÉCHECS (voir ci-dessus)')"
echo "compte rendu : $LOG"
exit $RC
