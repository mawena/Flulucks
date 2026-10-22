#!/usr/bin/env bash
# Essais de la phase 7 (robustesse et performances) sur le Pixel, avec journaux dans le dépôt.
# Usage : run_phase7_tests.sh <serial> [classe…]
#   sans classe : PerfInstrumentedTest puis toute la suite instrumentée.
# Les APK sont installées par adb (l'installation par Gradle expire sur un Wi-Fi lent), puis lancées par
# `am instrument`. Les mesures (étiquette PortailPerf) et les résultats sont écrits dans
# docs/superpowers/research/2026-10-22-essais-phase7/ (en .txt : *.log est ignoré par git).
# N'écrit sur le téléphone que sous /data/local/tmp/fluluks/, dans les dossiers de l'application et dans
# Download/portail-tests/ (supprimé par les tests). À la fin, l'APK de production est réinstallée.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SERIAL="${1:-}"; shift || true
[ -n "$SERIAL" ] || { echo "usage: run_phase7_tests.sh <serial> [classe…]" >&2; exit 2; }
ANDROID_HOME="${ANDROID_HOME:-$HOME/Android}"
ADB="$ANDROID_HOME/platform-tools/adb"; [ -x "$ADB" ] || ADB=adb
adb_() { "$ADB" -s "$SERIAL" "$@"; }
OUT="$ROOT/docs/superpowers/research/2026-10-22-essais-phase7"
mkdir -p "$OUT"
STAMP="$(date +%Y%m%d-%H%M%S)"
PKG=com.mawena.fluluks

CLASSES=("$@")
[ ${#CLASSES[@]} -gt 0 ] || CLASSES=(PerfInstrumentedTest "")

LOG="$OUT/instrumentes-$STAMP.txt"
exec > >(tee "$LOG") 2>&1
echo "# essais instrumentés $STAMP"
echo "# commit $(git -C "$ROOT" rev-parse --short HEAD) — $(adb_ shell getprop ro.build.fingerprint | tr -d '\r')"

FIX=/data/local/tmp/fluluks/fixtures
for f in ext4-rw-reel.img; do
  if ! adb_ shell "test -f $FIX/$f"; then
    SRC="${FL_FIXTURES_SRC:-$ROOT/build/native/fixtures}/$f"
    [ -f "$SRC" ] || { echo "fixture absente : $SRC (lancer tools/android/run_native_tests.sh)"; exit 1; }
    adb_ shell "mkdir -p $FIX"
    adb_ push "$SRC" "$FIX/$f"
  fi
done

echo "## construction et installation des APK de test"
(cd "$ROOT/android" && ./gradlew -q assembleDebug assembleDebugAndroidTest)
adb_ install -r -t "$ROOT/build/app/outputs/apk/debug/app-debug.apk"
adb_ install -r -t "$ROOT/build/app/outputs/apk/androidTest/debug/app-debug-androidTest.apk"

RC=0
adb_ logcat -c
for c in "${CLASSES[@]}"; do
  if [ -n "$c" ]; then
    echo "## $c"
    ARGS=(-e class "$PKG.$c")
  else
    echo "## suite instrumentée complète"
    ARGS=()
  fi
  R="$(adb_ shell am instrument -w -r "${ARGS[@]}" "$PKG.test/androidx.test.runner.AndroidJUnitRunner" | tr -d '\r')"
  echo "$R" | grep -E "^INSTRUMENTATION_STATUS: (class|test)=|^INSTRUMENTATION_STATUS: stack=|^(OK|FAILURES|Tests run)" || true
  echo "$R" | grep -qE "^OK \(" || RC=1
done

echo "## mesures (PortailPerf)"
adb_ logcat -d -s PortailPerf:I | grep PortailPerf || echo "(aucune mesure)"

echo "## réinstallation de l'APK de production"
adb_ uninstall "$PKG.test" >/dev/null || true
(cd "$ROOT" && flutter build apk --release >/dev/null)
adb_ install -r "$ROOT/build/app/outputs/flutter-apk/app-release.apk"
echo "## fin : $([ $RC -eq 0 ] && echo 'tout est vert' || echo 'ÉCHECS')"
exit $RC
