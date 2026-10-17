#!/usr/bin/env bash
# Génère toutes les images de test, sans root. Usage : make_fixtures.sh <dossier-sortie>
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:?usage: make_fixtures.sh <dossier-sortie>}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

STAMP="$(cat "$HERE"/make_tree.py "$HERE"/luks_encrypt.py "$HERE"/craft.py "$HERE"/make_fixtures.sh | sha256sum | cut -d' ' -f1)"
if [ -f "$OUT/.stamp" ] && [ "$(cat "$OUT/.stamp")" = "$STAMP" ]; then
  echo "fixtures à jour"; exit 0
fi
# Garde-fou : on n'efface que un dossier vide ou une sortie précédente de ce script.
if [ -n "$(ls -A "$OUT")" ] && [ ! -f "$OUT/.stamp" ] && [ ! -f "$OUT/.fixtures-dir" ]; then
  echo "make_fixtures.sh : $OUT n'est pas vide et ne ressemble pas à un dossier de fixtures ; abandon" >&2
  exit 2
fi
rm -rf "$OUT"
mkdir -p "$OUT/work"
: > "$OUT/.fixtures-dir"   # marqueur posé en tout premier : une génération interrompue reste reprenable
cd "$OUT"
trap '[ -f "$OUT/.stamp" ] || rm -rf "$OUT/work"' EXIT
PY="python3 -I"
MIB=$((1024 * 1024))
DATA=$((64 * MIB))
PASS="$OUT/pass.txt"; PASS2="$OUT/pass2.txt"

printf '%s' 'motdepasse-FluLuks' > pass.txt
printf '%s' 'mot de passe été ☀' > pass2.txt

# --- ext4 de référence -------------------------------------------------
$PY "$HERE/make_tree.py" "$OUT/tree"
truncate -s "$DATA" ext4-plain.img
mkfs.ext4 -q -F -T default -b 4096 -I 256 -O ^orphan_file,^metadata_csum_seed \
  -L FLUTEST -U 0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0 -d tree/ ext4-plain.img
if ! debugfs -R "stat /clairsemé.bin" ext4-plain.img 2>/dev/null | grep -q 5368709120; then
  echo "clairsemé.bin absent de l'image ext4" >&2; exit 1
fi
e2fsck -fn ext4-plain.img >/dev/null

# --- LUKS ----------------------------------------------------------------
# luks_offset <fichier> : décalage des données en octets
luks_offset() {
  local f="$1" o
  if cryptsetup luksDump "$f" | grep -q '^Version:[[:space:]]*1'; then
    o=$(cryptsetup luksDump "$f" | awk '/^Payload offset:/ {print $3*512}')
  else
    o=$(cryptsetup luksDump "$f" | awk '/^Data segments:/ {s=1} s && /offset:/ {print $2; exit}')
  fi
  echo "$o"
}

# make_luks <nom> <mode-luks_encrypt> <taille-secteur> <vk-octets> <args luksFormat...>
# produit work/<nom>.luks (conteneur de taille exacte offset + 64 Mio) et <nom>.vk
make_luks() {
  local name="$1" mode="$2" ss="$3" vkb="$4"; shift 4
  local c="work/$name.luks" off
  head -c "$vkb" /dev/urandom > "$name.vk"
  truncate -s 200M "$c"
  cryptsetup luksFormat -q --volume-key-file "$name.vk" --key-file pass.txt "$@" "$c"
  off=$(luks_offset "$c")
  truncate -s $((off + DATA)) "$c"
  $PY "$HERE/luks_encrypt.py" encrypt --image "$c" --offset "$off" --sector-size "$ss" \
    --mode "$mode" --key-file "$name.vk" --plain ext4-plain.img
  echo "$off" > "work/$name.off"
}

# conteneurs
make_luks gpt-luks2 xts-plain64 512 64 --type luks2 --cipher aes-xts-plain64 --key-size 512 \
  --sector-size 512 --pbkdf argon2id --pbkdf-force-iterations 4 --pbkdf-memory 65536 --pbkdf-parallel 4
make_luks whole-luks2-4k xts-plain64 4096 64 --type luks2 --cipher aes-xts-plain64 --key-size 512 \
  --sector-size 4096 --pbkdf argon2i --pbkdf-force-iterations 4 --pbkdf-memory 32768 --pbkdf-parallel 2
make_luks whole-luks2-2slots cbc-plain64 512 32 --type luks2 --cipher aes-cbc-plain64 --key-size 256 \
  --sector-size 512 --pbkdf pbkdf2 --hash sha256 --pbkdf-force-iterations 1000
cryptsetup luksAddKey -q --volume-key-file whole-luks2-2slots.vk --new-keyfile pass2.txt \
  --type luks2 --pbkdf pbkdf2 --hash sha256 --pbkdf-force-iterations 1000 \
  work/whole-luks2-2slots.luks
make_luks mbr-luks1-essiv cbc-essiv:sha256 512 32 --type luks1 --cipher aes-cbc-essiv:sha256 \
  --key-size 256 --hash sha256 --pbkdf-force-iterations 1000
make_luks whole-luks1-xts xts-plain64 512 64 --type luks1 --cipher aes-xts-plain64 --key-size 512 \
  --hash sha1 --pbkdf-force-iterations 1000
make_luks whole-luks2-exact xts-plain64 512 64 --type luks2 --cipher aes-xts-plain64 --key-size 512 \
  --sector-size 512 --pbkdf argon2id --pbkdf-force-iterations 7 --pbkdf-memory 1048576 --pbkdf-parallel 4

# images sans table
for n in whole-luks2-4k whole-luks2-2slots whole-luks1-xts whole-luks2-exact; do
  mv "work/$n.luks" "$n.img"
done

# images partitionnées : GPT = 1 Mio + conteneur + 1 Mio ; MBR = 1 Mio + conteneur
csize=$(stat -c %s work/gpt-luks2.luks)
truncate -s $((csize + 2 * MIB)) gpt-luks2.img
sgdisk -o -n "1:2048:$(( (csize + 2 * MIB) / 512 - 2048 - 1 ))" -t 1:8300 -c 1:FluData gpt-luks2.img >/dev/null
dd if=work/gpt-luks2.luks of=gpt-luks2.img bs=1M seek=1 conv=notrunc status=none

csize=$(stat -c %s work/mbr-luks1-essiv.luks)
truncate -s $((csize + MIB)) mbr-luks1-essiv.img
printf 'label: dos\nstart=2048, size=%d, type=83\n' $((csize / 512)) | sfdisk -q mbr-luks1-essiv.img
dd if=work/mbr-luks1-essiv.luks of=mbr-luks1-essiv.img bs=1M seek=1 conv=notrunc status=none

# variantes abîmées
cp whole-luks2-4k.img whole-luks2-impair.img
truncate -s $(( $(stat -c %s whole-luks2-4k.img) + 1536 )) whole-luks2-impair.img
cp whole-luks2-4k.img luks2-entete-abime.img
b=$(dd if=luks2-entete-abime.img bs=1 skip=4196 count=1 status=none | od -An -tu1 | tr -d ' ')
printf "$(printf '\\%03o' $((b ^ 255)))" | dd of=luks2-entete-abime.img bs=1 seek=4196 conv=notrunc status=none
head -c 4096 whole-luks2-4k.img > luks-tronque.img

# --- autres systèmes de fichiers et tables --------------------------------
truncate -s 16M mbr-etendue.img;  $PY "$HERE/craft.py" mbr-etendue mbr-etendue.img
$PY "$HERE/craft.py" gpt-secours gpt-secours.img
$PY "$HERE/craft.py" gpt-hors-bornes gpt-hors-bornes.img
truncate -s 32M fat16.img; mkfs.vfat -F 16 -n FLUFAT16 fat16.img >/dev/null
truncate -s 64M fat32.img; mkfs.vfat -F 32 -n FLUFAT32 fat32.img >/dev/null
truncate -s 32M ntfs.img;  mkfs.ntfs -F -f -q -L FLUNTFS ntfs.img
truncate -s 4M swap.img;   mkswap -q swap.img
for s in exfat lvm2 bitlocker btrfs xfs; do $PY "$HERE/craft.py" sig "$s" "sig-$s.img"; done

$PY "$HERE/luks_encrypt.py" kat --out cipher-kat.json

rm -rf work
echo "$STAMP" > .stamp
echo "fixtures générées dans $OUT"
