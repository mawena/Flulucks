#!/usr/bin/env python3
"""Manifeste d'un dossier au format de tm_write_manifest (native/tests/support/tree_model.h).

Usage : manifest.py <dossier>
Une ligne par entrée, séparée par des tabulations : type (d, f ou ? pour tout autre type), taille,
condensat (sha256 de la suite des sha256 de chaque tranche de 1 Mio ; « - » pour un dossier), uid, gid,
mode (octal, st_mode complet), chemin. Trié par chemin (octets). « lost+found » à la racine est exclu.
Utilisé par verify_rw_with_kernel.sh, et vérifié sans root par test_manifeste (native/tests/test_batterie.c :
arborescence écrite par le moteur, extraite par « debugfs rdump », comparée à tm_write_manifest).
"""
import hashlib
import os
import stat
import sys

CH = 1 << 20


def condensat(p):
    digs = b''
    with open(p, 'rb') as f:
        while True:
            b = f.read(CH)
            if not b:
                break
            digs += hashlib.sha256(b).digest()
    return hashlib.sha256(digs).hexdigest()


def main(root):
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
            chemin = rel.rstrip('/') + '/' + n
            if stat.S_ISREG(st.st_mode):
                lignes.append(('f', st.st_size, condensat(p), st.st_uid, st.st_gid, st.st_mode, chemin))
            else:
                lignes.append(('?', st.st_size, '-', st.st_uid, st.st_gid, st.st_mode, chemin))
    lignes.sort(key=lambda l: l[6].encode('utf-8', 'surrogateescape'))
    out = sys.stdout.buffer
    for t, s, h, u, g, m, c in lignes:
        out.write(('%s\t%d\t%s\t%d\t%d\t%o\t%s\n' % (t, s, h, u, g, m, c)).encode('utf-8', 'surrogateescape'))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage : manifest.py <dossier>')
    main(sys.argv[1])
