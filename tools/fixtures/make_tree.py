#!/usr/bin/env python3
"""Génère l'arborescence de référence (tree/) dans le dossier donné."""
import os, sys

root = sys.argv[1]
os.makedirs(root, exist_ok=True)


def w(rel, data):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data)


def pat(n):
    return bytes((i * 31 + 7) & 0xFF for i in range(n))


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
