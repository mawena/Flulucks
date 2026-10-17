#!/usr/bin/env python3
"""Chiffre / déchiffre un segment de données LUKS dans un fichier image, sans noyau.

  luks_encrypt.py encrypt|decrypt --image F --offset O --sector-size S --mode M
                  --key-file K --plain P [--length N] [--large-iv]
  luks_encrypt.py kat --out cipher-kat.json

Modes : xts-plain64, cbc-essiv:sha256, cbc-plain64.
Numéro de secteur pour l'IV : unités de 512 octets (comportement par défaut de dm-crypt,
LUKS1 et LUKS2 confondus) ; avec --large-iv, unités de sector_size (flag iv_large_sectors).
`--plain -` : stdin (encrypt) ou stdout (decrypt).
"""
import argparse, hashlib, json, os, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

MODES = ("xts-plain64", "cbc-essiv:sha256", "cbc-plain64")


def plain64(n):
    return n.to_bytes(8, "little") + b"\0" * 8


class SectorCipher:
    def __init__(self, mode, key):
        if mode not in MODES:
            raise ValueError("mode inconnu: " + mode)
        self.mode, self.key = mode, key
        if mode == "cbc-essiv:sha256":
            self.essiv = Cipher(algorithms.AES(hashlib.sha256(key).digest()), modes.ECB()).encryptor()

    def _iv(self, n):
        if self.mode == "cbc-essiv:sha256":
            return self.essiv.update(plain64(n))
        return plain64(n)

    def _cipher(self, n):
        iv = self._iv(n)
        if self.mode == "xts-plain64":
            return Cipher(algorithms.AES(self.key), modes.XTS(iv))
        return Cipher(algorithms.AES(self.key), modes.CBC(iv))

    def encrypt(self, n, data):
        e = self._cipher(n).encryptor()
        return e.update(data) + e.finalize()

    def decrypt(self, n, data):
        d = self._cipher(n).decryptor()
        return d.update(data) + d.finalize()


def run(a):
    key = open(a.key_file, "rb").read()
    sc = SectorCipher(a.mode, key)
    ss = a.sector_size
    unit = ss if a.large_iv else 512
    step = ss // unit
    enc = a.cmd == "encrypt"
    if enc:
        src = sys.stdin.buffer if a.plain == "-" else open(a.plain, "rb")
        img = open(a.image, "r+b")
        img.seek(a.offset)
    else:
        img = open(a.image, "rb")
        img.seek(0, 2)
        length = a.length if a.length is not None else (img.tell() - a.offset) // ss * ss
        img.seek(a.offset)
        dst = sys.stdout.buffer if a.plain == "-" else open(a.plain, "wb")
    n = 0
    remaining = None if enc else length
    while True:
        if enc:
            blk = src.read(ss)
            if not blk:
                break
            if len(blk) != ss:
                sys.exit("entrée non multiple de la taille de secteur")
            img.write(sc.encrypt(n * step, blk))
        else:
            if remaining <= 0:
                break
            blk = img.read(ss)
            if len(blk) != ss:
                sys.exit("image trop courte")
            dst.write(sc.decrypt(n * step, blk))
            remaining -= ss
        n += 1
    img.close()
    if not enc:
        dst.flush()


def kat(out):
    def pat(n, mul, add):
        return bytes((i * mul + add) & 0xFF for i in range(n))
    cases = [
        ("xts-plain64", 64, 512), ("xts-plain64", 64, 4096),
        ("cbc-essiv:sha256", 32, 512), ("cbc-plain64", 32, 512),
    ]
    res = []
    for mode, klen, ss in cases:
        key = (hashlib.sha256(("kat-a-" + mode).encode()).digest()
               + hashlib.sha256(("kat-b-" + mode).encode()).digest())[:klen]
        plain = pat(2 * ss, 31, 7)
        sc = SectorCipher(mode, key)
        step = ss // 512  # IV en unités de 512 octets (dm-crypt par défaut)
        iv0 = 1000
        cipher = b"".join(sc.encrypt(iv0 + k * step, plain[k * ss:(k + 1) * ss]) for k in range(2))
        res.append({"mode": mode, "key_bits": klen * 8, "key": key.hex(), "sector_size": ss,
                    "iv_sector": iv0, "iv_step": step, "plain": plain.hex(), "cipher": cipher.hex()})
    with open(out, "w") as f:
        json.dump(res, f, indent=1)


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    for c in ("encrypt", "decrypt"):
        s = sub.add_parser(c)
        s.add_argument("--image", required=True)
        s.add_argument("--offset", type=int, required=True)
        s.add_argument("--sector-size", type=int, default=512)
        s.add_argument("--mode", required=True, choices=MODES)
        s.add_argument("--key-file", required=True)
        s.add_argument("--plain", required=True)
        s.add_argument("--length", type=int)
        s.add_argument("--large-iv", action="store_true")
    k = sub.add_parser("kat")
    k.add_argument("--out", required=True)
    a = p.parse_args()
    kat(a.out) if a.cmd == "kat" else run(a)
