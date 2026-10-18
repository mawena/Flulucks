#!/usr/bin/env bash
# Vérification par le noyau Linux (root requis) des images écrites par le moteur (phase 3, T8) : monte
# en lecture seule chaque <nom>.ecrite.img laissée par test_batterie (LUKS : cryptsetup open --readonly
# avec <nom>.pass) et compare son contenu à <nom>.manifest (chemin, type, taille, condensat, uid, gid,
# mode ; format : native/tests/support/tree_model.h, tm_write_manifest).
# Usage : sudo tools/fixtures/verify_rw_with_kernel.sh build/native/batterie
set -uo pipefail
[ "$(id -u)" -eq 0 ] || { echo "à lancer avec sudo" >&2; exit 2; }
DIR="$(cd "${1:?usage: verify_rw_with_kernel.sh <dossier-batterie>}" && pwd)"
MAP="fluluks-rw-verify-$$"
MNT=""; LOOP=""

cleanup() {
  [ -n "$MNT" ] && mountpoint -q "$MNT" && umount "$MNT"
  [ -e "/dev/mapper/$MAP" ] && cryptsetup close "$MAP"
  [ -n "$LOOP" ] && losetup -d "$LOOP"
  [ -n "$MNT" ] && rmdir "$MNT"
  MNT=""; LOOP=""
}
trap cleanup EXIT

# Manifeste du dossier monté, au format de tm_write_manifest (lost+found à la racine exclu).
manifeste() {
  python3 - "$1" <<'PY'
import hashlib, os, stat, sys
root = sys.argv[1]
CH = 1 << 20
def cond(p, size):
    digs = b''
    with open(p, 'rb') as f:
        while True:
            b = f.read(CH)
            if not b:
                break
            digs += hashlib.sha256(b).digest()
    return hashlib.sha256(digs).hexdigest()
lignes = []
for d, sous, fichiers in os.walk(root):
    rel = '/' + os.path.relpath(d, root) if d != root else '/'
    if rel == '/' and 'lost+found' in sous:
        sous.remove('lost+found')
    st = os.lstat(d)
    lignes.append(('d', 0, '-', st.st_uid, st.st_gid, st.st_mode, rel))
    for n in fichiers:
        p = os.path.join(d, n)
        st = os.lstat(p)
        chemin = (rel.rstrip('/') + '/' + n)
        if stat.S_ISREG(st.st_mode):
            lignes.append(('f', st.st_size, cond(p, st.st_size), st.st_uid, st.st_gid, st.st_mode, chemin))
        else:
            lignes.append(('?', st.st_size, '-', st.st_uid, st.st_gid, st.st_mode, chemin))
lignes.sort(key=lambda l: l[6].encode())
for t, s, h, u, g, m, c in lignes:
    print('%s\t%d\t%s\t%d\t%d\t%o\t%s' % (t, s, h, u, g, m, c))
PY
}

# check <image>
check() {
  local img="$1" base="${1%.ecrite.img}" dev
  LOOP="$(losetup -r -f --show "$img")" || return 1
  dev="$LOOP"
  if cryptsetup isLuks "$LOOP"; then
    cryptsetup open --readonly --key-file "$base.pass" "$LOOP" "$MAP" || return 1
    dev="/dev/mapper/$MAP"
  fi
  MNT="$(mktemp -d)"
  # noload : jamais de rejeu du journal (l'image doit être propre telle quelle)
  mount -o ro,noload "$dev" "$MNT" || return 1
  diff <(sort "$base.manifest") <(manifeste "$MNT" | sort) || return 1
}

n=0
for img in "$DIR"/*.ecrite.img; do
  [ -e "$img" ] || { echo "aucune image *.ecrite.img dans $DIR" >&2; exit 1; }
  if check "$img"; then echo "NOYAU OK $(basename "$img")"
  else echo "NOYAU ÉCHEC $(basename "$img")"; n=$((n + 1)); fi
  cleanup
done
exit $((n > 0))
