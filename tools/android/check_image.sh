#!/usr/bin/env bash
# Rapatrie une image écrite par les tests instrumentés (cache de l'application, build debug) et la
# vérifie sur le PC avec e2fsck -fn (aucune écriture). Code de sortie : celui d'e2fsck (0 = propre).
# Images : engine-ecriture.img (EngineTest, défaut), provider-plein.img (ProviderWriteTest, disque plein),
# provider-ecriture.img (dernier test de ProviderWriteTest).
# Usage : check_image.sh <serial> [image-du-cache] [image-locale]
set -euo pipefail
SERIAL="${1:?usage: check_image.sh <serial> [image-du-cache] [image-locale]}"
NAME="${2:-engine-ecriture.img}"
[[ "$NAME" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "nom d'image inattendu : $NAME" >&2; exit 2; }
DST="${3:-$(mktemp --suffix=.img)}"
ANDROID_HOME="${ANDROID_HOME:-$HOME/Android}"
ADB="$ANDROID_HOME/platform-tools/adb"; [ -x "$ADB" ] || ADB=adb
E2FSCK="$(command -v e2fsck || echo /sbin/e2fsck)"
"$ADB" -s "$SERIAL" exec-out run-as com.mawena.fluluks cat "cache/$NAME" > "$DST"
[ -s "$DST" ] || { echo "image absente : lancer d'abord connectedDebugAndroidTest" >&2; exit 1; }
echo "image : $DST ($(($(stat -c %s "$DST") / 1048576)) Mio)"
"$E2FSCK" -fn "$DST"
