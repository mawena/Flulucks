#!/usr/bin/env bash
# Vérifie par le noyau Linux une image FAT/exFAT écrite par FluLuks (phase 4). À lancer par l'utilisateur,
# avec sudo (montage loop) ; jamais par un agent.
#
# Usage : sudo tools/fixtures/verify_fat_with_kernel.sh <image> <manifeste>
#   <image>      image gardée par la batterie (FL_KEEP=1 ctest -R test_fat_batterie) :
#                build/native/tmp/fat-batterie-<type>.img
#   <manifeste>  son manifeste attendu : build/native/tmp/fat-batterie-<type>.manifest
#
# Monte l'image en lecture seule (vfat ou exfat, heure UTC), calcule le manifeste vu par le noyau (type,
# taille, condensat par tranches de 1 Mio, chemin ; FAT n'a ni propriétaire ni droits : ces colonnes ne
# sont pas comparées), le compare au manifeste attendu, puis démonte.
set -euo pipefail
IMG="${1:?usage: verify_fat_with_kernel.sh <image> <manifeste>}"
MAN="${2:?usage: verify_fat_with_kernel.sh <image> <manifeste>}"
[ "$(id -u)" = 0 ] || { echo "à lancer avec sudo" >&2; exit 2; }
[ -f "$IMG" ] && [ -f "$MAN" ] || { echo "image ou manifeste absent" >&2; exit 2; }

if [ "$(dd if="$IMG" bs=1 skip=3 count=8 status=none)" = "EXFAT   " ]; then
  TYPE=exfat; OPTS=ro,loop,iocharset=utf8
else
  TYPE=vfat; OPTS=ro,loop,utf8,tz=UTC,shortname=mixed
fi
MNT="$(mktemp -d)"
trap 'umount "$MNT" 2>/dev/null || true; rmdir "$MNT"' EXIT
mount -t "$TYPE" -o "$OPTS" "$IMG" "$MNT"

python3 -I - "$MNT" "$MAN" <<'EOF'
import hashlib, os, sys

mnt, man = sys.argv[1], sys.argv[2]

def digest(path):
    acc = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            c = f.read(1 << 20)
            if not c:
                break
            acc.update(hashlib.sha256(c).digest())
    return acc.hexdigest()

vu = {"/": ("d", "0", "-")}
for root, dirs, files in os.walk(mnt):
    rel = "/" + os.path.relpath(root, mnt).replace(os.sep, "/") if root != mnt else "/"
    for d in dirs:
        vu[(rel.rstrip("/") + "/" + d)] = ("d", "0", "-")
    for f in files:
        p = os.path.join(root, f)
        vu[(rel.rstrip("/") + "/" + f)] = ("f", str(os.path.getsize(p)), digest(p))

attendu = {}
for line in open(man, encoding="utf-8"):
    t, size, dig, _uid, _gid, _mode, path = line.rstrip("\n").split("\t")
    attendu[path] = (t, size if t == "f" else "0", dig)

ecarts = 0
for p in sorted(set(vu) | set(attendu)):
    if vu.get(p) != attendu.get(p):
        ecarts += 1
        if ecarts <= 20:
            print("écart : %s\n  noyau   : %s\n  attendu : %s" % (p, vu.get(p), attendu.get(p)))
print("%d entrées, %d écart(s)" % (len(attendu), ecarts))
sys.exit(1 if ecarts else 0)
EOF
echo "OK : le noyau ($TYPE) voit exactement le contenu attendu"
