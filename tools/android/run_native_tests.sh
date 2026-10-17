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
SERIAL="${1:?usage: run_native_tests.sh <serial> [--clean] [--no-build]}"; shift || true
CLEAN=0; BUILD=1
for a in "$@"; do
  case "$a" in
    --clean) CLEAN=1 ;;
    --no-build) BUILD=0 ;;
    *) echo "option inconnue : $a" >&2; exit 2 ;;
  esac
done
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
TESTS=(test_error test_crypto test_luks1 test_luks2 test_ext4 test_usb test_cache test_luks2_lent)
# Images et fichiers nécessaires (sous-ensemble : le Wi-Fi est lent).
FIXTURES=(ext4-plain.img gpt-luks2.img whole-luks2-4k.img whole-luks2-2slots.img whole-luks1-xts.img
          mbr-luks1-essiv.img whole-luks2-exact.img luks2-entete-abime.img luks-tronque.img
          whole-luks2-impair.img pass.txt pass2.txt cipher-kat.json
          tree/sous-dossier/données.bin)

for t in "${TESTS[@]}"; do [ -x "$BUILD_DIR/$t" ] || { echo "binaire absent : $BUILD_DIR/$t" >&2; exit 1; }; done
for f in "${FIXTURES[@]}"; do [ -e "$FIX_SRC/$f" ] || { echo "fixture absente : $FIX_SRC/$f (lancer ctest -R generate_fixtures)" >&2; exit 1; }; done

adb_ get-state >/dev/null
echo "Téléphone : $(adb_ shell getprop ro.product.model | tr -d '\r') (Android $(adb_ shell getprop ro.build.version.release | tr -d '\r'), $(adb_ shell getprop ro.product.cpu.abi | tr -d '\r'))"
adb_ shell "mkdir -p $REMOTE/bin $REMOTE/fixtures/tree/sous-dossier $REMOTE/tmp"
for t in "${TESTS[@]}"; do adb_ push "$BUILD_DIR/$t" "$REMOTE/bin/$t" >/dev/null; done
adb_ shell "chmod 755 $REMOTE/bin/*"
for f in "${FIXTURES[@]}"; do
  size=$(stat -c %s "$FIX_SRC/$f")
  # ne repousse pas ce qui est déjà là avec la même taille
  have=$(adb_ shell "stat -c %s '$REMOTE/fixtures/$f' 2>/dev/null || echo -1" | tr -d '\r')
  if [ "$have" != "$size" ]; then
    echo "push $f ($((size / 1048576)) Mio)"
    adb_ push "$FIX_SRC/$f" "$REMOTE/fixtures/$f" >/dev/null
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
  printf '%s\n' "$out" | tr -d '\r' | grep -v '^__EXIT=' | tail -n 8
  if [ "$code" = 0 ]; then SUMMARY+=("OK     $t  ${secs} s"); else SUMMARY+=("ECHEC  $t  (code ${code:-?}, ${secs} s)"); FAILED=1; fi
done

echo; echo "=== Récapitulatif ($SERIAL)"
printf '%s\n' "${SUMMARY[@]}"
exit $FAILED
