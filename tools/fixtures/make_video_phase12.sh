#!/usr/bin/env bash
# Vidéo de test du lecteur vidéo (phase 12) : video-phase12.mkv dans le dossier donné (6 s, mire 320 × 240,
# H.264), avec deux pistes audio AAC (français stéréo « Original », anglais mono « Commentaires ») et une piste de
# sous-titres SubRip intégrée (anglais). Sert aux essais instrumentés (VideoPlayerInstrumentedTest) et aux essais
# manuels. Usage :
#   tools/fixtures/make_video_phase12.sh <dossier>
#   adb push <dossier>/video-phase12.mkv /data/local/tmp/fluluks/fixtures/
set -euo pipefail
OUT="${1:?usage : make_video_phase12.sh <dossier>}"
mkdir -p "$OUT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
printf '1\n00:00:00,000 --> 00:00:05,000\nEmbedded English subtitle\n' > "$TMP/en.srt"
ffmpeg -nostdin -hide_banner -loglevel error -y \
  -f lavfi -i testsrc=size=320x240:rate=25:duration=6 \
  -f lavfi -i sine=frequency=440:sample_rate=44100:duration=6 \
  -f lavfi -i sine=frequency=880:sample_rate=44100:duration=6 \
  -i "$TMP/en.srt" \
  -map 0:v -map 1:a -map 2:a -map 3:s \
  -c:v libx264 -pix_fmt yuv420p -preset veryfast \
  -c:a aac -b:a 96k -ac:a:0 2 -ac:a:1 1 \
  -c:s srt \
  -metadata:s:a:0 language=fra -metadata:s:a:0 title=Original \
  -metadata:s:a:1 language=eng -metadata:s:a:1 title=Commentaires \
  -metadata:s:s:0 language=eng \
  "$OUT/video-phase12.mkv"
# contrôle : 1 vidéo, 2 audio, 1 sous-titre
got="$(ffprobe -v error -show_entries stream=codec_type -of csv=p=0 "$OUT/video-phase12.mkv" | sort | uniq -c | tr -s ' ' | tr '\n' ',')"
[ "$got" = " 2 audio, 1 subtitle, 1 video," ] || { echo "pistes inattendues : $got" >&2; exit 1; }
echo "$OUT/video-phase12.mkv"
