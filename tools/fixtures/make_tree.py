#!/usr/bin/env python3
"""Génère l'arborescence de référence (tree/) dans le dossier donné.

make_tree.py --fat <dossier> : arborescence valide sur FAT (tree-fat/, phase 4), sans nom hors du plan
multilingue de base : mtools 4.0 ne sait pas les écrire.
make_tree.py --exfat <dossier> : la même, plus « 😀.bin » (tree-exfat/, remplie par fatjuge.py).
make_tree.py --ntfs <dossier> : la même que --exfat, plus « été ☀/dedans.txt » (tree-ntfs/, phase 5, remplie
par ntfs-remplir)."""
import os, random, sys

FAT = sys.argv[1] in ("--fat", "--exfat", "--ntfs")
root = sys.argv[2] if FAT else sys.argv[1]
os.makedirs(root, exist_ok=True)


def w(rel, data):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data)


def pat(n):
    return bytes((i * 31 + 7) & 0xFF for i in range(n))


if FAT:
    # Horodatage fixe et impair : la lecture FAT doit l'arrondir aux 2 s inférieures.
    MTIME = 1760000001
    noms = {
        "été ☀.txt": "été\n".encode(),
        "é" * 125 + "n.txt": b"nom de 255 octets\n",
        "vide.txt": b"",
        "gros.bin": random.Random(1234).randbytes(3 << 20),
        "MiXeD.TxT": b"casse\n",
        "abc.txt": b"minuscules\n",
        "UPPER.TXT": b"majuscules\n",
        "nom long 1.txt": b"1\n",
        "nom long 2.txt": b"2\n",
        "/".join("p%d" % i for i in range(1, 21)) + "/fond.txt": b"profond\n",
    }
    if sys.argv[1] in ("--exfat", "--ntfs"):
        noms["😀.bin"] = b"\xf0\x9f\x98\x80\n"
    if sys.argv[1] == "--ntfs":
        noms["été ☀/dedans.txt"] = b"dossier accentu\xc3\xa9\n"
    for i in range(1000):
        noms["mille/f%04d.txt" % i] = ("f%04d\n" % i).encode()
    assert len(("é" * 125 + "n.txt").encode()) == 255
    for rel, data in noms.items():
        w(rel, data)
        os.utime(os.path.join(root, rel), (MTIME, MTIME))
    sys.exit(0)

w("hello.txt", "Bonjour FluLuks\n".encode())
w("vide.txt", b"")
w("accentué é ☀.txt", b"utf8\n")
w("sous-dossier/données.bin", pat(1 << 20))
w("gros.bin", pat(5 << 20))
for i in range(300):
    w("beaucoup/f%03d.txt" % i, ("f%03d.txt\n" % i).encode())
SIZE = 5368709120
with open(os.path.join(root, "clairsemé.bin"), "wb") as f:
    f.truncate(SIZE)
    f.seek(SIZE - 11)
    f.write(b"FIN-FLULUKS")
