#!/usr/bin/env bash
# Archives de test (phase 9), sans root. Usage : make_archive_fixtures.sh <dossier-sortie>
# Séparé de make_fixtures.sh : ajouter une archive ne régénère pas les images de disque.
# Les archives 7z et RAR, qu'aucun outil libre ne sait écrire ici, viennent de native/tests/data/archives/
# (fichiers .uu de la suite de tests de libarchive, décodés ici).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DATA="$HERE/../../native/tests/data/archives"
OUT="${1:?usage: make_archive_fixtures.sh <dossier-sortie>}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
shopt -s nullglob
UU=("$DATA"/*.uu)
STAMP="$(cat "$HERE/make_archive_fixtures.sh" "${UU[@]}" | sha256sum | cut -d' ' -f1)"
if [ -f "$OUT/.stamp" ] && [ "$(cat "$OUT/.stamp")" = "$STAMP" ]; then
  echo "archives de test à jour"; exit 0
fi
# Garde-fou : on n'efface qu'un dossier vide ou une sortie précédente de ce script.
if [ -n "$(ls -A "$OUT")" ] && [ ! -f "$OUT/.archives-dir" ]; then
  echo "make_archive_fixtures.sh : $OUT n'est pas vide et ne vient pas de ce script ; abandon" >&2
  exit 2
fi
: > "$OUT/.archives-dir"
find "$OUT" -mindepth 1 ! -name .archives-dir -delete
cd "$OUT"
PY="python3 -I"

# Arborescence de référence : été.txt (« bonjour\n »), dossier/b.bin (100 Kio, octet i = i*7 mod 256).
mkdir -p arbre/dossier
printf 'bonjour\n' > arbre/été.txt
$PY -c 'import sys; sys.stdout.buffer.write(bytes(i * 7 % 256 for i in range(102400)))' > arbre/dossier/b.bin
touch -d '2024-05-01 12:00:00 UTC' arbre/été.txt arbre/dossier/b.bin arbre/dossier

tar -cJf fumee.tar.xz -C arbre été.txt dossier
(cd arbre && zip -qrX ../fumee.zip été.txt dossier)

# Lecture (T2) : mêmes contenus sous d'autres formats.
(cd arbre && zip -qrX0 ../stocke.zip été.txt dossier)
tar -cf arbre.tar -C arbre été.txt dossier
tar -czf arbre.tar.gz -C arbre été.txt dossier
gzip -9nc arbre/été.txt > seul.txt.gz
# Chiffré (ZipCrypto), noms hostiles, nom CP437 sans drapeau UTF-8 (0x82 = « é ») : faits par Python.
(cd arbre && zip -qX -P secret ../chiffre.zip été.txt)
$PY - <<'EOF'
import zipfile
with zipfile.ZipFile("hostile.zip", "w") as z:
    for n in ["../x", "/abs", "a/../../x", "a\\b", "ok.txt"]:
        z.writestr(zipfile.ZipInfo(n, (2024, 5, 1, 12, 0, 0)), b"hostile\n")
with zipfile.ZipFile("cp437.zip", "w") as z:
    z.writestr(zipfile.ZipInfo("Q.txt", (2024, 5, 1, 12, 0, 0)), b"cp437\n")
data = open("cp437.zip", "rb").read()
assert data.count(b"Q.txt") == 2  # en-tête local et répertoire central
open("cp437.zip", "wb").write(data.replace(b"Q.txt", b"\x82.txt"))
EOF
head -c $(( $(stat -c %s fumee.zip) / 2 )) fumee.zip > tronque.zip
head -c $(( $(stat -c %s arbre.tar.gz) / 2 )) arbre.tar.gz > tronque.tar.gz
printf 'pas une archive\n' > texte.txt

# Archives 7z et RAR de libarchive
for f in "${UU[@]}"; do
  $PY - "$f" <<'EOF'
import binascii, os, sys
src = sys.argv[1]
with open(src, "rb") as f:
    lines = f.read().splitlines()
begin = next(i for i, l in enumerate(lines) if l.startswith(b"begin "))
name = lines[begin].split(b" ", 2)[2].decode()
out = bytearray()
for l in lines[begin + 1:]:
    if l.strip() == b"end":
        break
    if l.strip() in (b"", b"`"):
        continue
    out += binascii.a2b_uu(l)
with open(os.path.basename(name), "wb") as f:
    f.write(out)
EOF
done

echo "$STAMP" > .stamp
echo "archives de test générées dans $OUT"
