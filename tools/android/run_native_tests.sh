#!/usr/bin/env bash
# Lance les tests natifs arm64 sur un téléphone Android via adb.
# Usage : run_native_tests.sh <serial> [--clean] [--no-build]
#   <serial>    numéro de série adb (USB ou ip:port)
#   --clean     supprime /data/local/tmp/fluluks/ sur le téléphone et s'arrête
#   --no-build  ne recompile pas build/android-arm64
# Variables : ANDROID_HOME (défaut ~/Android), FL_FIXTURES_SRC (défaut build/native/fixtures).
# N'écrit que sous /data/local/tmp/fluluks/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SERIAL=""; CLEAN=0; BUILD=1
for a in "$@"; do
  case "$a" in
    --clean) CLEAN=1 ;;
    --no-build) BUILD=0 ;;
    -*) echo "option inconnue : $a" >&2; exit 2 ;;
    *) [ -z "$SERIAL" ] || { echo "serial en double : $a" >&2; exit 2; }; SERIAL="$a" ;;
  esac
done
[ -n "$SERIAL" ] || { echo "usage: run_native_tests.sh <serial> [--clean] [--no-build]" >&2; exit 2; }
ANDROID_HOME="${ANDROID_HOME:-$HOME/Android}"
NDK="$ANDROID_HOME/ndk/28.2.13676358"
ADB="$ANDROID_HOME/platform-tools/adb"; [ -x "$ADB" ] || ADB=adb
adb_() { "$ADB" -s "$SERIAL" "$@"; }
REMOTE=/data/local/tmp/fluluks
BUILD_DIR="$ROOT/build/android-arm64"
FIX_SRC="${FL_FIXTURES_SRC:-$ROOT/build/native/fixtures}"

if [ "$CLEAN" = 1 ]; then
  adb_ shell "rm -rf $REMOTE"
  echo "$REMOTE supprimé sur $SERIAL"; exit 0
fi

if [ "$BUILD" = 1 ]; then
  cmake -S "$ROOT/native" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=26 -DCMAKE_BUILD_TYPE=Release \
    -DFL_TESTS=ON -DFL_SANITIZE=OFF >/dev/null
  cmake --build "$BUILD_DIR"
fi

# Tests, dans l'ordre ; test_luks2_lent est le plus long (Argon2 aux paramètres exacts du disque).
# Une entrée peut porter un argument après le nom du binaire (comme dans CTest : test_crc32c_debit).
TESTS=(test_error test_crypto test_luks1 test_luks2 test_crc32c "test_crc32c debit" test_ext4 test_fat test_fat_hostile test_ntfs test_ntfs_hostile test_usb
       test_cache test_banc test_luks2_lent)
# Binaires à pousser, sans doublon.
BINS=()
for t in "${TESTS[@]}"; do
  b="${t%% *}"
  case " ${BINS[*]} " in *" $b "*) ;; *) BINS+=("$b") ;; esac
done
# Images et fichiers nécessaires (sous-ensemble : le Wi-Fi est lent).
FIXTURES=(ext4-plain.img ext4-rw-1k.img gpt-luks2.img whole-luks2-4k.img whole-luks2-2slots.img whole-luks1-xts.img
          mbr-luks1-essiv.img whole-luks2-exact.img luks2-entete-abime.img luks-tronque.img
          whole-luks2-impair.img pass.txt pass2.txt cipher-kat.json
          ext4-rw-reel.img ext4-nom255.img ext4-trou-dossier.img ext4-trou-dossier.blocs ext4-indirect.img ext4-entrees-hostiles.img
          tree/sous-dossier/données.bin
          fat12-arbre.img fat16-arbre.img fat32-arbre.img fat32-4k.img exfat-arbre.img luks2-fat32.img
          fat16-sale.img fat32-sale.img fat32-fat1-sale.img fat32-media.img exfat-sale.img exfat-media.img
          exfat-somme.img fat32-512-sur-4k.img sig-exfat.img
          ntfs-arbre.img ntfs-4k.img ntfs-512-sur-4k.img ntfs-sale.img ntfs-chkdsk.img ntfs-hiber.img
          ntfs-journal.img ntfs-chiffre.img ntfs-casse.img luks2-ntfs.img fat32.img
          ntfs-hostile-runlist-hors.img ntfs-hostile-runlist-mft.img ntfs-hostile-base-soi.img
          ntfs-hostile-attr-longueur.img ntfs-hostile-lznt1.img ntfs-hostile-index-zero.img
          ntfs-hostile-index-boucle.img ntfs-hostile-utf16.img)
# Arborescences entières (modèles des tests FAT et NTFS : tm_from_dir).
TREES=(tree-fat tree-exfat tree-ntfs)

for b in "${BINS[@]}"; do [ -x "$BUILD_DIR/$b" ] || { echo "binaire absent : $BUILD_DIR/$b" >&2; exit 1; }; done
for f in "${FIXTURES[@]}"; do [ -e "$FIX_SRC/$f" ] || { echo "fixture absente : $FIX_SRC/$f (lancer ctest -R generate_fixtures)" >&2; exit 1; }; done

adb_ get-state >/dev/null
echo "Téléphone : $(adb_ shell getprop ro.product.model | tr -d '\r') (Android $(adb_ shell getprop ro.build.version.release | tr -d '\r'), $(adb_ shell getprop ro.product.cpu.abi | tr -d '\r'))"
adb_ shell "mkdir -p $REMOTE/bin $REMOTE/fixtures/tree/sous-dossier $REMOTE/tmp"
for b in "${BINS[@]}"; do adb_ push "$BUILD_DIR/$b" "$REMOTE/bin/$b" >/dev/null; done
adb_ shell "chmod 755 $REMOTE/bin/*"
for f in "${FIXTURES[@]}"; do
  size=$(stat -c %s "$FIX_SRC/$f")
  # ne repousse pas ce qui est déjà là à l'identique (même condensat : une image régénérée garde souvent
  # sa taille)
  sum=$(md5sum "$FIX_SRC/$f" | cut -d' ' -f1)
  have=$(adb_ shell "md5sum '$REMOTE/fixtures/$f' 2>/dev/null | cut -d' ' -f1" | tr -d '\r')
  if [ "$have" != "$sum" ]; then
    echo "push $f ($((size / 1048576)) Mio)"
    adb_ push "$FIX_SRC/$f" "$REMOTE/fixtures/$f" >/dev/null
  fi
done
for t in "${TREES[@]}"; do
  [ -d "$FIX_SRC/$t" ] || { echo "arborescence absente : $FIX_SRC/$t" >&2; exit 1; }
  have=$(adb_ shell "find '$REMOTE/fixtures/$t' -type f 2>/dev/null | wc -l" | tr -d '\r')
  if [ "$have" != "$(find "$FIX_SRC/$t" -type f | wc -l)" ]; then
    echo "push $t/"
    adb_ shell "rm -rf '$REMOTE/fixtures/$t'"
    adb_ push "$FIX_SRC/$t" "$REMOTE/fixtures/" >/dev/null
  fi
done

FAILED=0; SUMMARY=()
for t in "${TESTS[@]}"; do
  echo "=== $t"
  start=$(date +%s.%N)
  set +e
  out=$(adb_ shell "cd $REMOTE/tmp && FL_FIXTURES=$REMOTE/fixtures TMPDIR=$REMOTE/tmp $REMOTE/bin/$t 2>&1; echo __EXIT=\$?")
  set -e
  secs=$(awk -v s="$start" -v e="$(date +%s.%N)" 'BEGIN{printf "%.1f", e - s}')
  code=$(printf '%s\n' "$out" | tr -d '\r' | sed -n 's/^__EXIT=//p' | tail -1)
  clean_out=$(printf '%s\n' "$out" | tr -d '\r' | { grep -v '^__EXIT=' || true; })
  if [ "$code" = 0 ] || [ "$code" = 77 ]; then printf '%s\n' "$clean_out" | tail -n 8; else printf '%s\n' "$clean_out" | tail -n 60; fi
  if [ "$code" = 0 ]; then SUMMARY+=("OK     $t  ${secs} s")
  elif [ "$code" = 77 ] && printf '%s\n' "$clean_out" | grep -q '^\[OK\]'; then
    SUMMARY+=("OK     $t  ${secs} s (juge e2fsck sauté : code 77, réservé au PC)")
  elif [ "$code" = 77 ]; then SUMMARY+=("SAUTÉ  $t  (code 77, banc réservé au PC)")
  else SUMMARY+=("ECHEC  $t  (code ${code:-?}, ${secs} s)"); FAILED=1; fi
done

echo; echo "=== Récapitulatif ($SERIAL)"
printf '%s\n' "${SUMMARY[@]}"
exit $FAILED
