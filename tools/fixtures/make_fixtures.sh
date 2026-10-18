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
# Accepte : dossier vide, marqueur .fixtures-dir, .stamp, ou reste de l'ancienne version (pass.txt + work/).
if [ -n "$(ls -A "$OUT")" ] && [ ! -f "$OUT/.stamp" ] && [ ! -f "$OUT/.fixtures-dir" ] \
   && ! { [ -f "$OUT/pass.txt" ] && [ -d "$OUT/work" ]; }; then
  echo "make_fixtures.sh : $OUT n'est pas vide et ne ressemble pas à un dossier de fixtures ; abandon" >&2
  exit 2
fi
# Le marqueur est posé AVANT toute suppression : un kill à n'importe quel moment reste reprenable.
: > "$OUT/.fixtures-dir"
find "$OUT" -mindepth 1 ! -name .fixtures-dir -delete 2>/dev/null || find "$OUT" -mindepth 1 -maxdepth 1 ! -name .fixtures-dir -exec rm -rf {} +
mkdir -p "$OUT/work"
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

# --- ext4 : dettes de lecture de lwext4 (phase 3, T1) ---------------------
# Mêmes options que le vrai disque. Arborescences de travail dans work/, supprimées à la fin.
# UUID et graine de hachage fixes : même disposition (et mêmes feuilles htree) à chaque génération.
MKFS_DISQUE=(mkfs.ext4 -q -F -T default -b 4096 -I 256 -O ^orphan_file,^metadata_csum_seed)
fixe() { echo "-U 5eed0000-0000-4000-8000-00000000000$1 -E hash_seed=5eed0000-0000-4000-8000-0000000000a$1"; }
DBG() { debugfs "$@" 2>/dev/null; }

# ext4-nom255.img : composants de exactement 255 octets, posés par mkfs -d (comme le noyau).
#   /<N255>          N255 = « é » × 100 + « n » × 51 + « .txt » (255 octets UTF-8), contenu « nom de 255 octets\n »
#   /<D255>/f.txt    D255 = « d » × 255, contenu « profond\n »
N255="$(printf 'é%.0s' $(seq 100))$(printf 'n%.0s' $(seq 51)).txt"
D255="$(printf 'd%.0s' $(seq 255))"
[ "$(printf %s "$N255" | wc -c)" = 255 ] && [ "${#D255}" = 255 ] || { echo "noms de 255 octets faux" >&2; exit 1; }
mkdir -p "work/t255/$D255"
printf 'nom de 255 octets\n' > "work/t255/$N255"
printf 'profond\n' > "work/t255/$D255/f.txt"
truncate -s 8M ext4-nom255.img
"${MKFS_DISQUE[@]}" $(fixe 1) -L FLU255 -d work/t255 ext4-nom255.img
e2fsck -fn ext4-nom255.img >/dev/null

# ext4-trou-dossier.img : dossiers dont un bloc logique n'a pas de bloc physique.
#   /h : 300 fichiers, transformé en htree par « e2fsck -fyD » (racine d'index au bloc 0, feuilles 1..5).
#   /d : dossier linéaire de 300 fichiers (4 blocs), créé ensuite par debugfs (mkdir + write), qui
#        ne l'indexe pas.
#   Puis « debugfs -w -R 'punch /d 1 1' » et « punch /h 3 3 » libèrent un bloc au milieu de chaque
#   dossier sans changer sa taille : un trou, que le noyau signale comme une corruption
#   (« directory contains a hole »). lwext4 y lisait le bloc physique 0.
mkdir -p work/trou/h
NOMS=()
for i in $(seq -w 1 300); do NOMS+=("fichier-$i-abcdefghijklmnopqrstuvwxyz"); done
for n in "${NOMS[@]}"; do printf '%s\n' "$n" > "work/trou/h/$n"; done
truncate -s 8M ext4-trou-dossier.img
"${MKFS_DISQUE[@]}" $(fixe 2) -N 1024 -L FLUTROU -d work/trou ext4-trou-dossier.img
e2fsck -fyD ext4-trou-dossier.img >/dev/null 2>&1 || [ $? -eq 1 ]  # 1 = optimisé, rien d'autre
DBG -R "htree /h" ext4-trou-dossier.img | grep -q "Indirect levels: 0" || { echo "/h n'est pas un htree" >&2; exit 1; }
printf 'contenu\n' > work/trou/src.txt
{ echo "mkdir /d"; echo "cd /d"; for n in "${NOMS[@]}"; do echo "write work/trou/src.txt $n"; done; } > work/trou/cmds
DBG -w -f work/trou/cmds ext4-trou-dossier.img >/dev/null
e2fsck -fn ext4-trou-dossier.img >/dev/null
DBG -w -R "punch /d 1 1" ext4-trou-dossier.img
DBG -w -R "punch /h 3 3" ext4-trou-dossier.img
for t in "/d 1" "/h 3"; do
  [ "$(DBG -R "bmap $t" ext4-trou-dossier.img)" = 0 ] || { echo "pas de trou en $t" >&2; exit 1; }
done
DBG -R "stat /d" ext4-trou-dossier.img | grep -q "Size: 16384" || { echo "taille de /d changée" >&2; exit 1; }
# ext4-trou-dossier.blocs : blocs physiques que test_ext4_erreur_es_dossier fait échouer en lecture
#   h_feuille = feuille htree de /h (bloc logique 2), h_etb = bloc d'index d'extents de /h (l'inode a
#   plus de 4 extents), d_bloc0 = bloc logique 0 de /d.
h_etb=$(DBG -R "stat /h" ext4-trou-dossier.img | grep -o '(ETB0):[0-9]*' | cut -d: -f2)
[ -n "$h_etb" ] || { echo "/h sans bloc d'index d'extents" >&2; exit 1; }
printf 'h_feuille %s\nh_etb %s\nd_bloc0 %s\n' "$(DBG -R "bmap /h 2" ext4-trou-dossier.img)" "$h_etb" \
  "$(DBG -R "bmap /d 0" ext4-trou-dossier.img)" > ext4-trou-dossier.blocs

# ext4-indirect.img : sans extents (blocs adressés par l'inode, comme ext2/ext3), dir_index actif.
#   /r : 300 fichiers (4 blocs), puis « punch /r 1 1 » : un trou au bloc logique 1.
#   /vide : dossier vide d'un seul bloc.
#   ext4_dir_dx_init (appelé par ext4_dir_rm via ext4_trunc_dir) demande les blocs logiques 0 et 1 :
#   sans extents, ext4_fs_init_inode_dblk_idx n'alloue rien et rendait 0, puis lwext4 écrivait une
#   racine d'index dans le bloc 0 du disque (superbloc primaire).
mkdir -p work/indirect/r work/indirect/vide
for n in "${NOMS[@]}"; do printf '%s\n' "$n" > "work/indirect/r/$n"; done
truncate -s 8M ext4-indirect.img
mkfs.ext4 -q -F -T default -b 4096 -I 256 -O ^extent,^64bit,^orphan_file,^metadata_csum_seed $(fixe 4) \
  -N 1024 -L FLUIND -d work/indirect ext4-indirect.img
e2fsck -fn ext4-indirect.img >/dev/null
DBG -R "stat /r" ext4-indirect.img | grep -q "^BLOCKS:" || { echo "/r n'est pas en blocs adressés" >&2; exit 1; }
DBG -w -R "punch /r 1 1" ext4-indirect.img
[ "$(DBG -R "bmap /r 1" ext4-indirect.img)" = 0 ] || { echo "pas de trou en /r 1" >&2; exit 1; }
DBG -R "stat /vide" ext4-indirect.img | grep -q "Size: 4096" || { echo "/vide : un bloc attendu" >&2; exit 1; }

# ext4-entrees-hostiles.img : /hostile, un bloc de dossier dont craft.py abîme les entrées (inode
# hors bornes, « / » dans le nom, NUL dans le nom, nom vide), puis e-valide.txt intacte ; la somme
# metadata_csum du bloc est recalculée. mkfs -d range les entrées dans l'ordre alphabétique.
mkdir -p work/hostile/hostile
for n in a-hors-bornes b-barre c-nul d-vide e-valide.txt; do printf '%s\n' "$n" > "work/hostile/hostile/$n"; done
truncate -s 8M ext4-entrees-hostiles.img
"${MKFS_DISQUE[@]}" $(fixe 3) -L FLUHOST -d work/hostile ext4-entrees-hostiles.img
h_ino=$(DBG -R "stat /hostile" ext4-entrees-hostiles.img | awk '/^Inode:/ {print $2}')
h_gen=$(DBG -R "stat /hostile" ext4-entrees-hostiles.img | awk '/^Generation:/ {print $2}')
h_blk=$(DBG -R "bmap /hostile 0" ext4-entrees-hostiles.img)
$PY "$HERE/craft.py" ext4-entrees-hostiles ext4-entrees-hostiles.img "$h_blk" "$h_ino" "$h_gen"
h_ls="$(DBG -R "ls /hostile" ext4-entrees-hostiles.img)"
h_hors=$(( $(dumpe2fs -h ext4-entrees-hostiles.img 2>/dev/null | awk -F: '/^Inode count:/ {print $2}') + 1 ))
if ! grep -Eq "(^| )$h_hors  \([0-9]+\) a-hors-bornes " <<<"$h_ls" || grep -Eq 'b-barre|c-nul|d-vide' <<<"$h_ls" \
   || ! grep -q 'e-valide.txt' <<<"$h_ls"; then
  echo "entrées hostiles non posées" >&2; exit 1
fi
# e2fsck voit exactement ces défauts, et aucune somme de contrôle fausse (craft.py la recalcule).
h_fsck="$(LC_ALL=C e2fsck -fn ext4-entrees-hostiles.img 2>&1 || true)"
for motif in "Entry 'a-hors-bornes' in /hostile ([0-9]*) has invalid inode #: $h_hors" \
             "Entry 'b/barre' in /hostile ([0-9]*) has illegal characters in its name" \
             "Entry 'c^@nul' in /hostile ([0-9]*) has illegal characters in its name" \
             "Entry '' in /hostile ([0-9]*) has a zero-length name"; do
  grep -q "$motif" <<<"$h_fsck" || { echo "e2fsck ne signale pas : $motif" >&2; exit 1; }
done
if grep -qi checksum <<<"$h_fsck"; then echo "somme de contrôle fausse dans /hostile" >&2; exit 1; fi

# --- ext4 : images d'écriture (phase 3, T3) --------------------------------
# Banc de non-corruption : le moteur écrit sur une COPIE de ces images, e2fsck -fn juge le résultat.
# Toutes sont créées avec « -d tree/ » puis passées par l'étape journal. mkfs laisse les options du
# journal vides ; le vrai disque a « journal_incompat_revoke journal_64bit journal_checksum_v3 ».
#   debugfs -w : jo -c (ouvre le journal, somme v3), jw -r 100 (transaction de révocation du bloc 100 :
#   ne réécrit aucun bloc), jc (ferme) ; e2fsck -fy rejoue cette transaction (l'image redevient propre,
#   les options du journal restent posées).
#   Sans metadata_csum (ext4-rw-uninit.img, anciens disques), « jo -c » poserait la somme v1
#   « journal_checksum » (e2fsprogs n'écrit le v3 qu'avec metadata_csum), que FluLuks refuse en écriture
#   (correctif du journal, phase 3) : cette image a un journal sans somme (« jo » sans -c), que lwext4
#   écrit comme jbd2.
journal_step() {
  local img="$1" f csum=journal_checksum_v3 jo='jo -c'
  if [ "$img" = ext4-rw-uninit.img ]; then csum=''; jo='jo'; fi
  printf '%s\njw -r 100\njc\n' "$jo" > work/jcmd
  DBG -w -f work/jcmd "$img" >/dev/null
  e2fsck -fy "$img" >/dev/null 2>&1 || { echo "$img : e2fsck -fy a échoué après l'étape journal" >&2; exit 1; }
  f="$(LC_ALL=C dumpe2fs -h "$img" 2>/dev/null)"
  for opt in journal_incompat_revoke journal_64bit $csum; do
    grep -E '^Journal features:' <<<"$f" | grep -qw "$opt" || { echo "$img : $opt absent du journal" >&2; exit 1; }
  done
  if [ -z "$csum" ] && grep -E '^Journal features:' <<<"$f" | grep -qw journal_checksum; then
    echo "$img : somme v1 posée sur le journal" >&2; exit 1
  fi
  if grep -E '^Filesystem features:' <<<"$f" | grep -q needs_recovery; then
    echo "$img : needs_recovery encore posé" >&2; exit 1
  fi
  LC_ALL=C e2fsck -fn "$img" >/dev/null 2>&1 || { echo "$img : pas propre après l'étape journal" >&2; exit 1; }
}
# mk_rw <nom> <taille> <n-fixe> <options mkfs...>
mk_rw() {
  local name="$1" size="$2" n="$3"; shift 3
  truncate -s "$size" "$name"
  mkfs.ext4 -q -F -T default $(fixe "$n") -L "FLURW" -d tree/ "$@" "$name"
  if [ "$name" != ext4-rw-sans-journal.img ]; then journal_step "$name"; fi
  LC_ALL=C e2fsck -fn "$name" >/dev/null 2>&1 || { echo "$name : e2fsck -fn" >&2; exit 1; }
}
mk_rw ext4-rw-reel.img 256M 5 -b 4096 -I 256 -O ^orphan_file,^metadata_csum_seed
mk_rw ext4-rw-1k.img 64M 6 -b 1024 -I 256 -O ^orphan_file,^metadata_csum_seed
mk_rw ext4-rw-uninit.img 128M 7 -b 4096 -I 256 -O ^metadata_csum,uninit_bg,^orphan_file,^metadata_csum_seed
mk_rw ext4-rw-seed.img 128M 8 -b 4096 -I 256 -O metadata_csum_seed,orphan_file
mk_rw ext4-rw-sans-journal.img 64M 9 -b 4096 -I 256 -O ^has_journal,^orphan_file,^metadata_csum_seed
mk_rw ext4-rw-meta-bg.img 64M a -b 4096 -I 256 -O meta_bg,^resize_inode,^orphan_file,^metadata_csum_seed
mk_rw ext4-rw-gros.img 8G b -b 4096 -I 256 -O ^orphan_file,^metadata_csum_seed   # creux : reste creux sur disque
for img in ext4-rw-*.img; do
  f="$(LC_ALL=C dumpe2fs -h "$img" 2>/dev/null | grep -E '^Filesystem features:')"
  case "$img" in
    ext4-rw-sans-journal.img) ! grep -q has_journal <<<"$f" ;;
    ext4-rw-seed.img) grep -q metadata_csum_seed <<<"$f" && grep -q orphan_file <<<"$f" ;;
    ext4-rw-meta-bg.img) grep -q meta_bg <<<"$f" ;;
    ext4-rw-uninit.img) grep -q uninit_bg <<<"$f" && ! grep -q metadata_csum <<<"$f" ;;
    *) grep -q metadata_csum <<<"$f" && ! grep -q orphan_file <<<"$f" ;;
  esac || { echo "$img : options mkfs inattendues" >&2; exit 1; }
done

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

# DATA_SZ (taille des données, défaut 64 Mio) et PLAIN (image claire, défaut ext4-plain.img) : variables d'environnement.
# make_luks <nom> <mode-luks_encrypt> <taille-secteur> <vk-octets> <args luksFormat...>
# produit work/<nom>.luks (conteneur de taille exacte offset + 64 Mio) et <nom>.vk
make_luks() {
  local name="$1" mode="$2" ss="$3" vkb="$4"; shift 4
  local c="work/$name.luks" off
  head -c "$vkb" /dev/urandom > "$name.vk"
  truncate -s 200M "$c"
  cryptsetup luksFormat -q --volume-key-file "$name.vk" --key-file pass.txt "$@" "$c"
  off=$(luks_offset "$c")
  truncate -s $((off + ${DATA_SZ:-$DATA})) "$c"
  $PY "$HERE/luks_encrypt.py" encrypt --image "$c" --offset "$off" --sector-size "$ss" \
    --mode "$mode" --key-file "$name.vk" --plain "${PLAIN:-ext4-plain.img}"
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

# luks2-rw-reel.img : 256 Mio de données (copie d'ext4-rw-reel.img), pbkdf2 à 1000 itérations (rapide
# pour la batterie d'écriture), secteurs de 512 octets.
DATA_SZ=$((256 * MIB)) PLAIN=ext4-rw-reel.img make_luks luks2-rw-reel xts-plain64 512 64 --type luks2 \
  --cipher aes-xts-plain64 --key-size 512 --sector-size 512 --pbkdf pbkdf2 --hash sha256 \
  --pbkdf-force-iterations 1000
mv work/luks2-rw-reel.luks luks2-rw-reel.img

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
for v in nent-enorme esize-zero esize-impair entrees-hors-disque; do
  $PY "$HERE/craft.py" gpt-hostile "$v" "gpt-hostile-$v.img"
done
$PY "$HERE/craft.py" ebr-boucle ebr-boucle.img
truncate -s 32M fat16.img; mkfs.vfat -F 16 -n FLUFAT16 fat16.img >/dev/null
truncate -s 64M fat32.img; mkfs.vfat -F 32 -n FLUFAT32 fat32.img >/dev/null
truncate -s 32M ntfs.img;  mkfs.ntfs -F -f -q -L FLUNTFS ntfs.img
truncate -s 4M swap.img;   mkswap -q swap.img
for s in exfat lvm2 bitlocker btrfs xfs; do $PY "$HERE/craft.py" sig "$s" "sig-$s.img"; done

$PY "$HERE/luks_encrypt.py" kat --out cipher-kat.json

rm -rf work
echo "$STAMP" > .stamp
echo "fixtures générées dans $OUT"
