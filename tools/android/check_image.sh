#!/usr/bin/env bash
# Rapatrie l'image écrite par EngineTest.writeThenReadBack (cache de l'application, build debug) et la
# vérifie sur le PC avec e2fsck -fn (aucune écriture). Code de sortie : celui d'e2fsck (0 = propre).
# Usage : check_image.sh <serial> [image-locale]
set -euo pipefail
SERIAL="${1:?usage: check_image.sh <serial> [image-locale]}"
DST="${2:-$(mktemp --suffix=.img)}"
ANDROID_HOME="${ANDROID_HOME:-$HOME/Android}"
ADB="$ANDROID_HOME/platform-tools/adb"; [ -x "$ADB" ] || ADB=adb
E2FSCK="$(command -v e2fsck || echo /sbin/e2fsck)"
"$ADB" -s "$SERIAL" exec-out run-as com.mawena.fluluks cat cache/engine-ecriture.img > "$DST"
[ -s "$DST" ] || { echo "image absente : lancer d'abord connectedDebugAndroidTest (EngineTest)" >&2; exit 1; }
echo "image : $DST ($(($(stat -c %s "$DST") / 1048576)) Mio)"
"$E2FSCK" -fn "$DST"
