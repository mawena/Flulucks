#!/usr/bin/env bash
# Essais de la phase 6 (Portail) sur le Pixel, avec journaux dans le dépôt.
# Usage : run_phase6_tests.sh <serial> [instrumentes | manuel | capture <nom>]
#   instrumentes (défaut) : APK construite et installée, puis tests instrumentés de la phase 6 et du provider
#   manuel                : journal logcat de Portail pendant les essais à la main (Entrée pour finir)
#   capture <nom>         : capture d'écran du téléphone, <nom>.png
# Tout est écrit dans docs/superpowers/research/2026-10-20-essais-phase6/ (en .txt : *.log est ignoré par git).
# N'écrit sur le téléphone que sous /data/local/tmp/fluluks/, dans les dossiers de l'application et dans
# Download/portail-tests/ (supprimé par les tests).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SERIAL="${1:-}"; MODE="${2:-instrumentes}"; NAME="${3:-}"
[ -n "$SERIAL" ] || { echo "usage: run_phase6_tests.sh <serial> [instrumentes | manuel | capture <nom>]" >&2; exit 2; }
ANDROID_HOME="${ANDROID_HOME:-$HOME/Android}"
ADB="$ANDROID_HOME/platform-tools/adb"; [ -x "$ADB" ] || ADB=adb
adb_() { "$ADB" -s "$SERIAL" "$@"; }
OUT="$ROOT/docs/superpowers/research/2026-10-20-essais-phase6"
mkdir -p "$OUT"
STAMP="$(date +%Y%m%d-%H%M%S)"
PKG=com.mawena.fluluks

app_uid() { adb_ shell pm list packages -U "$PKG" | sed -n "s/^package:$PKG uid://p" | tr -d '\r' | head -1; }

case "$MODE" in
  capture)
    [ -n "$NAME" ] || { echo "capture : nom manquant" >&2; exit 2; }
    adb_ exec-out screencap -p > "$OUT/$NAME.png"
    echo "capture : $OUT/$NAME.png"
    exit 0 ;;
  manuel)
    LOG="$OUT/manuel-$STAMP.txt"
    adb_ logcat -c
    echo "Faites les essais sur le téléphone, puis appuyez sur Entrée ici."
    read -r _
    UID_="$(app_uid)"
    { echo "# essais manuels $STAMP — $(adb_ shell getprop ro.build.fingerprint | tr -d '\r')"
      adb_ logcat -d -v threadtime --uid="$UID_" || adb_ logcat -d -v threadtime
      echo "# plantages"
      adb_ logcat -d -v threadtime -b crash || true
    } > "$LOG" 2>&1
    echo "journal : $LOG"
    exit 0 ;;
  instrumentes) ;;
  *) echo "mode inconnu : $MODE" >&2; exit 2 ;;
esac

LOG="$OUT/instrumentes-$STAMP.txt"
exec > >(tee "$LOG") 2>&1
echo "# essais instrumentés $STAMP"
echo "# commit $(git -C "$ROOT" rev-parse --short HEAD) — $(adb_ shell getprop ro.build.fingerprint | tr -d '\r')"

# image de test (poussée par run_native_tests.sh) ; repoussée depuis le PC si elle manque
FIX=/data/local/tmp/fluluks/fixtures/ext4-rw-reel.img
if ! adb_ shell "test -f $FIX"; then
  SRC="${FL_FIXTURES_SRC:-$ROOT/build/native/fixtures}/ext4-rw-reel.img"
  [ -f "$SRC" ] || { echo "fixture absente : $SRC (lancer tools/android/run_native_tests.sh)"; exit 1; }
  adb_ shell "mkdir -p /data/local/tmp/fluluks/fixtures"
  adb_ push "$SRC" "$FIX"
fi

echo "## construction et installation de l'APK"
(cd "$ROOT" && flutter build apk --debug)
adb_ install -r "$ROOT/build/app/outputs/flutter-apk/app-debug.apk"

GRADLE=(./gradlew); [ -x "$ROOT/android/gradlew" ] || GRADLE=(gradle)
CLASSES=(FilesInstrumentedTest ThumbnailsInstrumentedTest ProviderTest ProviderWriteTest AppIdentityTest)
RC=0
adb_ logcat -c
for c in "${CLASSES[@]}"; do
  echo "## $c"
  (cd "$ROOT/android" && ANDROID_SERIAL="$SERIAL" "${GRADLE[@]}" :app:connectedDebugAndroidTest --console=plain \
    -Pandroid.testInstrumentationRunnerArguments.class="com.mawena.fluluks.$c") || RC=1
  # rapports JUnit (échecs avec trace)
  for x in "$ROOT"/build/app/outputs/androidTest-results/connected/debug/*.xml \
           "$ROOT"/build/app/outputs/androidTest-results/connected/*.xml; do
    [ -f "$x" ] || continue
    echo "### rapport $(basename "$x")"
    cat "$x"
  done
done
echo "## logcat (tests, provider, plantages)"
adb_ logcat -d -v threadtime | grep -E "TestRunner|AndroidRuntime|FATAL|fluluks|Fluluks|VolumeManager|Provider|Transfers|Thumbnails" || true
echo "## résultat : $([ $RC = 0 ] && echo SUCCÈS || echo ÉCHEC)"
echo "journal : $LOG"
exit $RC
