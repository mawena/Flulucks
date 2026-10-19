#!/usr/bin/env python3
"""Juge FAT12/16/32 et exFAT indépendant du moteur (phase 4) : lit une image sans aucune bibliothèque FAT.

Usage :
  fatjuge.py manifest <img> <sortie>    manifeste au format de tm_write_manifest (tree_model.h)
  fatjuge.py free <img>                 octets libres (clusters libres × taille de cluster)
  fatjuge.py locate <img> <chemin>      offset (octets) du premier cluster de données du fichier
  fatjuge.py set-ro <img> <chemin>      pose l'attribut lecture seule (0x01) sur l'entrée du fichier
  fatjuge.py build-exfat <img> <dossier>  remplit une image fraîche de mkfs.exfat avec le dossier

Toute incohérence de structure (chaîne en boucle ou trop courte, cluster hors bornes, somme exFAT
fausse, hachage de nom faux) : message sur stderr, code de sortie 3.

Manifeste : une ligne par entrée, triée par chemin (octets UTF-8), champs séparés par des tabulations :
type (d/f), taille, condensat (« - » pour un dossier), uid (0), gid (0), mode octal (100644, 100444 si
attribut lecture seule, 40755 pour un dossier), chemin. Condensat = sha256 de la suite des sha256 de
chaque tranche de 1 Mio du contenu (dernière tranche plus courte).
"""
import hashlib
import os
import struct
import sys
import time

CHUNK = 1 << 20


class Bad(Exception):
    pass


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


class Image:
    def __init__(self, path, writable=False):
        self.f = open(path, "r+b" if writable else "rb")
        self.size = os.fstat(self.f.fileno()).st_size

    def read(self, off, n):
        if off < 0 or off + n > self.size:
            raise Bad("lecture hors de l'image : %d+%d" % (off, n))
        self.f.seek(off)
        return self.f.read(n)

    def write(self, off, data):
        if off < 0 or off + len(data) > self.size:
            raise Bad("écriture hors de l'image")
        self.f.seek(off)
        self.f.write(data)


def digest_chunks(chunks):
    """chunks : itérable de tranches de CHUNK octets (la dernière plus courte)."""
    acc = hashlib.sha256()
    for c in chunks:
        acc.update(hashlib.sha256(c).digest())
    return acc.hexdigest()


# --------------------------------------------------------------------------------------------------
# FAT12 / FAT16 / FAT32
# --------------------------------------------------------------------------------------------------

class Fat:
    def __init__(self, img):
        self.img = img
        b = img.read(0, 512)
        self.bps = u16(b, 11)
        self.spc = b[13]
        self.rsvd = u16(b, 14)
        self.nfats = b[16]
        self.rootent = u16(b, 17)
        tot = u16(b, 19) or u32(b, 32)
        fatsz = u16(b, 22) or u32(b, 36)
        if self.bps not in (512, 1024, 2048, 4096) or self.spc == 0 or self.spc & (self.spc - 1):
            raise Bad("BPB invalide")
        if self.rsvd == 0 or self.nfats == 0 or fatsz == 0 or tot == 0:
            raise Bad("BPB invalide")
        self.fatsz = fatsz
        rootsec = (self.rootent * 32 + self.bps - 1) // self.bps
        self.root_start = self.rsvd + self.nfats * fatsz
        self.data_start = self.root_start + rootsec
        if tot <= self.data_start:
            raise Bad("volume trop petit")
        self.nclus = (tot - self.data_start) // self.spc
        self.kind = 12 if self.nclus < 4085 else 16 if self.nclus < 65525 else 32
        self.rootclus = u32(b, 44) if self.kind == 32 else 0
        self.clsz = self.bps * self.spc
        self.fat = img.read(self.rsvd * self.bps, fatsz * self.bps)  # première FAT

    def entry(self, c):
        if self.kind == 12:
            o = c + c // 2
            v = u16(self.fat, o)
            return v >> 4 if c & 1 else v & 0xFFF
        if self.kind == 16:
            return u16(self.fat, c * 2)
        return u32(self.fat, c * 4) & 0x0FFFFFFF

    def eoc(self, v):
        return v >= {12: 0xFF8, 16: 0xFFF8, 32: 0x0FFFFFF8}[self.kind]

    def chain(self, start):
        out = []
        c = start
        while True:
            if c < 2 or c >= self.nclus + 2:
                raise Bad("cluster hors bornes : %d" % c)
            out.append(c)
            if len(out) > self.nclus:
                raise Bad("chaîne en boucle depuis %d" % start)
            v = self.entry(c)
            if self.eoc(v):
                return out
            if v in (0, 1) or v == {12: 0xFF7, 16: 0xFFF7, 32: 0x0FFFFFF7}[self.kind]:
                raise Bad("chaîne interrompue après %d" % c)
            c = v

    def cl_off(self, c):
        return (self.data_start + (c - 2) * self.spc) * self.bps

    def data_of(self, start, size):
        """Tranches de CHUNK octets du contenu d'un fichier."""
        if size == 0:
            if start != 0:
                raise Bad("fichier vide avec cluster %d" % start)
            return
        if start == 0:
            raise Bad("fichier non vide sans cluster")
        ch = self.chain(start)
        need = (size + self.clsz - 1) // self.clsz
        if len(ch) != need:
            raise Bad("chaîne de %d clusters pour %d attendus" % (len(ch), need))
        buf = bytearray()
        left = size
        for c in ch:
            k = min(left, self.clsz)
            buf += self.img.read(self.cl_off(c), k)
            left -= k
            while len(buf) >= CHUNK:
                yield bytes(buf[:CHUNK])
                del buf[:CHUNK]
        if buf:
            yield bytes(buf)

    def dir_raw(self, start):
        """(octets du dossier, liste des offsets d'image de chaque entrée)."""
        if start == 0:  # racine FAT12/16
            n = self.rootent * 32
            base = self.root_start * self.bps
            return self.img.read(base, n), [base + i for i in range(0, n, 32)]
        raw = bytearray()
        offs = []
        for c in self.chain(start):
            o = self.cl_off(c)
            raw += self.img.read(o, self.clsz)
            offs += [o + i for i in range(0, self.clsz, 32)]
            if len(raw) > 0x200000:
                raise Bad("dossier de plus de 2 Mio")
        return bytes(raw), offs

    @staticmethod
    def sfn_sum(n11):
        s = 0
        for x in n11:
            s = (((s & 1) << 7) | (s >> 1)) + x & 0xFF
        return s

    def entries(self, start):
        """Entrées utiles d'un dossier : (nom, attr, cluster, taille, offset de l'entrée courte)."""
        raw, offs = self.dir_raw(start)
        lfn = {}
        lfn_sum = None
        out = []
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                lfn = {}
                continue
            attr = e[11]
            if attr & 0x3F == 0x0F:
                seq = e[0] & 0x3F
                if e[0] & 0x40:
                    lfn = {}
                    lfn_sum = e[13]
                if e[13] != lfn_sum:
                    lfn = {}
                    continue
                lfn[seq] = e[1:11] + e[14:26] + e[28:32]
                continue
            if attr & 0x08:  # étiquette de volume
                lfn = {}
                continue
            n11 = bytearray(e[0:11])
            if n11[0] == 0x05:
                n11[0] = 0xE5
            name = None
            if lfn and lfn_sum == self.sfn_sum(e[0:11]) and sorted(lfn) == list(range(1, len(lfn) + 1)):
                u = b"".join(lfn[k] for k in sorted(lfn))
                s = u.decode("utf-16-le", "surrogatepass")
                s = s.split("\x00", 1)[0]
                name = s.encode("utf-16", "surrogatepass").decode("utf-16")
            lfn = {}
            if name is None:
                base = bytes(n11[0:8]).decode("cp437").rstrip(" ")
                ext = bytes(n11[8:11]).decode("cp437").rstrip(" ")
                if e[12] & 0x08:
                    base = base.lower()
                if e[12] & 0x10:
                    ext = ext.lower()
                name = base + ("." + ext if ext else "")
            if name in (".", ".."):
                continue
            clus = u16(e, 26) | (u16(e, 20) << 16 if self.kind == 32 else 0)
            out.append((name, attr, clus, u32(e, 28), offs[i // 32]))
        return out

    def root(self):
        return self.rootclus if self.kind == 32 else 0

    def free_bytes(self):
        return sum(1 for c in range(2, self.nclus + 2) if self.entry(c) == 0) * self.clsz

    def walk(self, start, path, seen, out):
        for name, attr, clus, size, eoff in self.entries(start):
            p = (path.rstrip("/") + "/" + name)
            if attr & 0x10:
                if clus in seen or clus == 0:
                    raise Bad("dossier en boucle ou sans cluster : %s" % p)
                seen.add(clus)
                out.append(("d", 0, "-", 0o40755, p, clus, eoff))
                self.walk(clus, p, seen, out)
            else:
                mode = 0o100444 if attr & 0x01 else 0o100644
                out.append(("f", size, digest_chunks(self.data_of(clus, size)), mode, p, clus, eoff))

    def tree(self):
        out = [("d", 0, "-", 0o40755, "/", self.root(), None)]
        self.walk(self.root(), "/", set(), out)
        return out

    def set_ro(self, eoff):
        a = self.img.read(eoff + 11, 1)[0]
        self.img.write(eoff + 11, bytes([a | 0x01]))


# --------------------------------------------------------------------------------------------------
# exFAT
# --------------------------------------------------------------------------------------------------

def boot_sum(sectors, bps):
    s = 0
    for i, x in enumerate(sectors):
        if i in (106, 107, 112):
            continue
        s = ((0x80000000 if s & 1 else 0) + (s >> 1) + x) & 0xFFFFFFFF
    return s


def set_sum(raw):
    s = 0
    for i, x in enumerate(raw):
        if i in (2, 3):
            continue
        s = ((0x8000 if s & 1 else 0) + (s >> 1) + x) & 0xFFFF
    return s


class ExFat:
    def __init__(self, img):
        self.img = img
        b = img.read(0, 512)
        if b[3:11] != b"EXFAT   ":
            raise Bad("pas exFAT")
        self.bps = 1 << b[108]
        self.spc = 1 << b[109]
        if not 9 <= b[108] <= 12 or b[108] + b[109] > 25:
            raise Bad("géométrie exFAT invalide")
        self.fat_off = u32(b, 80)
        self.fat_len = u32(b, 84)
        self.heap = u32(b, 88)
        self.nclus = u32(b, 92)
        self.rootclus = u32(b, 96)
        self.clsz = self.bps * self.spc
        region = img.read(0, 12 * self.bps)
        s = boot_sum(region[:11 * self.bps], self.bps)
        for k in range(0, self.bps, 4):
            if u32(region, 11 * self.bps + k) != s:
                raise Bad("somme du secteur de démarrage exFAT fausse")
        self.fat = img.read(self.fat_off * self.bps, self.fat_len * self.bps)
        self.upcase = None
        self.bitmap_ent = None
        self._load_root_meta()

    def entry(self, c):
        return u32(self.fat, c * 4)

    def check(self, c):
        if c < 2 or c >= self.nclus + 2:
            raise Bad("cluster hors bornes : %d" % c)

    def chain(self, start):
        out = []
        c = start
        while True:
            self.check(c)
            out.append(c)
            if len(out) > self.nclus:
                raise Bad("chaîne en boucle depuis %d" % start)
            v = self.entry(c)
            if v == 0xFFFFFFFF:
                return out
            if v < 2 or v >= 0xFFFFFFF7:
                raise Bad("chaîne interrompue après %d" % c)
            c = v

    def clusters(self, first, nofat, length):
        n = (length + self.clsz - 1) // self.clsz
        if n == 0:
            return []
        if nofat:
            for c in (first, first + n - 1):
                self.check(c)
            return list(range(first, first + n))
        ch = self.chain(first)
        if len(ch) < n:
            raise Bad("chaîne de %d clusters pour %d attendus" % (len(ch), n))
        return ch[:n]

    def cl_off(self, c):
        return self.heap * self.bps + (c - 2) * self.clsz

    def stream(self, first, nofat, length, valid=None):
        valid = length if valid is None else valid
        buf = bytearray()
        pos = 0
        for c in self.clusters(first, nofat, length):
            k = min(length - pos, self.clsz)
            d = self.img.read(self.cl_off(c), k)
            if pos + k > valid:
                cut = max(0, valid - pos)
                d = d[:cut] + bytes(k - cut)
            buf += d
            pos += k
            while len(buf) >= CHUNK:
                yield bytes(buf[:CHUNK])
                del buf[:CHUNK]
        if buf:
            yield bytes(buf)

    def dir_raw(self, first, nofat, length):
        if length > 0x10000000:
            raise Bad("dossier exFAT démesuré")
        raw = bytearray()
        offs = []
        for c in self.clusters(first, nofat, length):
            o = self.cl_off(c)
            raw += self.img.read(o, self.clsz)
            offs += [o + i for i in range(0, self.clsz, 32)]
        return bytes(raw), offs

    def root_raw(self):
        raw = bytearray()
        offs = []
        for c in self.chain(self.rootclus):
            o = self.cl_off(c)
            raw += self.img.read(o, self.clsz)
            offs += [o + i for i in range(0, self.clsz, 32)]
        return bytes(raw), offs

    def _load_root_meta(self):
        raw, _ = self.root_raw()
        for i in range(0, len(raw), 32):
            t = raw[i]
            if t == 0:
                break
            if t == 0x81:
                self.bitmap_ent = (u32(raw, i + 20), u64(raw, i + 24))
            elif t == 0x82:
                first, length = u32(raw, i + 20), u64(raw, i + 24)
                data = b"".join(self.stream(first, False, length))
                self.upcase = self._expand_upcase(data)
        if self.bitmap_ent is None or self.upcase is None:
            raise Bad("bitmap ou table de majuscules absente")

    @staticmethod
    def _expand_upcase(data):
        table = {}
        i = 0
        ch = 0
        n = len(data) // 2
        while i < n:
            v = u16(data, i * 2)
            if v == 0xFFFF and i + 1 < n:
                ch += u16(data, i * 2 + 2)
                i += 2
                continue
            table[ch] = v
            ch += 1
            i += 1
        return table

    def up(self, c):
        return self.upcase.get(c, c)

    def name_hash(self, name):
        h = 0
        u = name.encode("utf-16-le", "surrogatepass")
        for i in range(0, len(u), 2):
            c = self.up(u16(u, i))
            for x in (c & 0xFF, c >> 8):
                h = ((0x8000 if h & 1 else 0) + (h >> 1) + x) & 0xFFFF
        return h

    def entries(self, raw, offs):
        """(nom, attr, first, nofat, length, valid, offset de l'entrée fichier)."""
        out = []
        i = 0
        n = len(raw) // 32
        while i < n:
            t = raw[i * 32]
            if t == 0:
                break
            if t != 0x85:
                if t & 0x80 and t not in (0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2):
                    raise Bad("entrée primaire inattendue 0x%02x" % t)
                i += 1
                continue
            sc = raw[i * 32 + 1]
            if sc < 2 or i + 1 + sc > n:
                raise Bad("ensemble d'entrées tronqué")
            sraw = raw[i * 32:(i + 1 + sc) * 32]
            if set_sum(sraw) != u16(sraw, 2):
                raise Bad("somme d'ensemble d'entrées fausse")
            attr = u16(sraw, 4)
            st = sraw[32:64]
            if st[0] != 0xC0:
                raise Bad("entrée de flux absente")
            flags, nlen, nhash = st[1], st[3], u16(st, 4)
            valid, first, length = u64(st, 8), u32(st, 20), u64(st, 24)
            name = b""
            for k in range(2, 1 + sc):
                e = sraw[k * 32:(k + 1) * 32]
                if e[0] == 0xC1:
                    name += e[2:32]
            name = name[:nlen * 2].decode("utf-16-le", "surrogatepass")
            name = name.encode("utf-16", "surrogatepass").decode("utf-16")
            if self.name_hash(name) != nhash:
                raise Bad("hachage de nom faux : %r" % name)
            if valid > length:
                raise Bad("ValidDataLength > DataLength")
            if length and not flags & 1:
                raise Bad("données sans allocation")
            if length and (first < 2 or first >= self.nclus + 2):
                raise Bad("cluster hors bornes : %d" % first)
            if length > self.nclus * self.clsz:
                raise Bad("taille plus grande que le volume")
            out.append((name, attr, first, bool(flags & 2), length, valid, offs[i]))
            i += 1 + sc
        return out

    def walk(self, raw, offs, path, seen, out):
        for name, attr, first, nofat, length, valid, eoff in self.entries(raw, offs):
            p = path.rstrip("/") + "/" + name
            if attr & 0x10:
                if first in seen:
                    raise Bad("dossier en boucle : %s" % p)
                seen.add(first)
                out.append(("d", 0, "-", 0o40755, p, first, eoff))
                r2, o2 = self.dir_raw(first, nofat, length)
                self.walk(r2, o2, p, seen, out)
            else:
                mode = 0o100444 if attr & 0x01 else 0o100644
                dig = digest_chunks(self.stream(first, nofat, length, valid))
                out.append(("f", length, dig, mode, p, first, eoff))

    def tree(self):
        out = [("d", 0, "-", 0o40755, "/", self.rootclus, None)]
        raw, offs = self.root_raw()
        self.walk(raw, offs, "/", {self.rootclus}, out)
        return out

    def bitmap(self):
        first, length = self.bitmap_ent
        return bytearray(b"".join(self.stream(first, False, length)))

    def free_bytes(self):
        bm = self.bitmap()
        used = sum(bin(bm[c >> 3] >> (c & 7) & 1).count("1") for c in range(self.nclus))
        return (self.nclus - used) * self.clsz

    def set_ro(self, eoff):
        sc = self.img.read(eoff + 1, 1)[0]
        sraw = bytearray(self.img.read(eoff, (1 + sc) * 32))
        struct.pack_into("<H", sraw, 4, u16(sraw, 4) | 0x01)
        struct.pack_into("<H", sraw, 2, set_sum(sraw))
        self.img.write(eoff, bytes(sraw))


def open_fs(img):
    b = img.read(0, 512)
    return ExFat(img) if b[3:11] == b"EXFAT   " else Fat(img)


def find(fs, path):
    for t, size, dig, mode, p, clus, eoff in fs.tree():
        if p == path:
            return clus, eoff
    raise Bad("chemin absent : %s" % path)


# --------------------------------------------------------------------------------------------------
# build-exfat : remplissage d'une image fraîche de mkfs.exfat, sans FatFs
# --------------------------------------------------------------------------------------------------

def dos_time(t):
    lt = time.gmtime(max(t, 315532800))
    d = ((lt.tm_year - 1980) << 9) | (lt.tm_mon << 5) | lt.tm_mday
    s = (lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec // 2)
    return (d << 16) | s


class Builder:
    def __init__(self, path):
        self.img = Image(path, writable=True)
        self.fs = ExFat(self.img)
        self.bm = self.fs.bitmap()
        self.fat = bytearray(self.fs.fat)
        self.nfiles = 0
        self.next = 2

    def is_free(self, c):
        return not self.bm[(c - 2) >> 3] >> ((c - 2) & 7) & 1

    def mark(self, c):
        self.bm[(c - 2) >> 3] |= 1 << ((c - 2) & 7)

    def alloc_contig(self, n):
        c = 2
        while c + n <= self.fs.nclus + 2:
            if all(self.is_free(c + k) for k in range(n)):
                for k in range(n):
                    self.mark(c + k)
                return c
            c += 1
        raise Bad("plus de place pour %d clusters contigus" % n)

    def alloc_scattered(self, n):
        """n clusters libres, un sur deux : chaîne fragmentée."""
        out = []
        c = 2
        skip = False
        while len(out) < n and c < self.fs.nclus + 2:
            if self.is_free(c):
                if not skip:
                    self.mark(c)
                    out.append(c)
                skip = not skip
            c += 1
        if len(out) < n:
            raise Bad("plus de place")
        return out

    def link(self, chain):
        for a, b in zip(chain, chain[1:] + [None]):
            struct.pack_into("<I", self.fat, a * 4, 0xFFFFFFFF if b is None else b)

    def write_clusters(self, chain, data):
        for k, c in enumerate(chain):
            part = data[k * self.fs.clsz:(k + 1) * self.fs.clsz]
            self.img.write(self.fs.cl_off(c), part + bytes(self.fs.clsz - len(part)))

    def entry_set(self, name, attr, first, nofat, length, alloc, mtime):
        u = name.encode("utf-16-le", "surrogatepass")
        nlen = len(u) // 2
        nname = (nlen + 14) // 15
        sc = 1 + nname
        raw = bytearray(32 * (1 + sc))
        raw[0] = 0x85
        raw[1] = sc
        struct.pack_into("<H", raw, 4, attr)
        ts = dos_time(mtime)
        for o in (8, 12, 16):
            struct.pack_into("<I", raw, o, ts)
        raw[22] = raw[23] = raw[24] = 0x80  # UTC, décalage 0
        st = 32
        raw[st] = 0xC0
        raw[st + 1] = (0x01 if alloc else 0) | (0x02 if nofat else 0)
        raw[st + 3] = nlen
        struct.pack_into("<H", raw, st + 4, self.fs.name_hash(name))
        struct.pack_into("<Q", raw, st + 8, length)
        struct.pack_into("<I", raw, st + 20, first)
        struct.pack_into("<Q", raw, st + 24, length)
        for k in range(nname):
            o = 64 + 32 * k
            raw[o] = 0xC1
            part = u[k * 30:(k + 1) * 30]
            raw[o + 2:o + 2 + len(part)] = part
        struct.pack_into("<H", raw, 2, set_sum(raw))
        return bytes(raw)

    def build_dir(self, src):
        """Crée le contenu d'un dossier ; rend les octets de ses entrées."""
        out = bytearray()
        for name in sorted(os.listdir(src)):
            p = os.path.join(src, name)
            stt = os.lstat(p)
            if os.path.isdir(p):
                body = self.build_dir(p)
                n = max(1, (len(body) + self.fs.clsz - 1) // self.fs.clsz)
                if self.nfiles % 2:
                    ch = self.alloc_scattered(n)
                else:
                    first = self.alloc_contig(n)
                    ch = list(range(first, first + n))
                self.link(ch)  # dossiers : toujours chaînés dans la FAT
                self.write_clusters(ch, bytes(body))
                out += self.entry_set(name, 0x10, ch[0], False, n * self.fs.clsz, True, stt.st_mtime)
            else:
                with open(p, "rb") as f:
                    data = f.read()
                self.nfiles += 1
                n = (len(data) + self.fs.clsz - 1) // self.fs.clsz
                if n == 0:
                    out += self.entry_set(name, 0x20, 0, False, 0, True, stt.st_mtime)
                    continue
                chained = self.nfiles % 3 == 0
                if chained:
                    ch = self.alloc_scattered(n)
                    self.link(ch)
                else:
                    first = self.alloc_contig(n)
                    ch = list(range(first, first + n))
                self.write_clusters(ch, data)
                out += self.entry_set(name, 0x20, ch[0], not chained, len(data), True, stt.st_mtime)
        return out

    def run(self, src):
        body = self.build_dir(src)
        raw, offs = self.fs.root_raw()
        chain = self.fs.chain(self.fs.rootclus)
        end = 0
        while end < len(raw) and raw[end] != 0:
            end += 32
        new = bytearray(raw[:end]) + body
        need = max(1, (len(new) + self.fs.clsz - 1) // self.fs.clsz)
        while len(chain) < need:
            chain.append(self.alloc_contig(1))
        self.link(chain)
        self.write_clusters(chain, bytes(new))
        # FAT (toutes les copies) et bitmap
        nfats = self.img.read(110, 1)[0]
        for k in range(nfats):
            self.img.write((self.fs.fat_off + k * self.fs.fat_len) * self.fs.bps, bytes(self.fat))
        bfirst, blen = self.fs.bitmap_ent
        bchain = self.fs.clusters(bfirst, False, blen)
        self.write_clusters(bchain, bytes(self.bm[:blen]) + bytes(len(bchain) * self.fs.clsz - blen))
        used = sum(bin(x).count("1") for x in self.bm[:(self.fs.nclus + 7) // 8])
        self.img.write(112, bytes([used * 100 // self.fs.nclus]))
        self.img.f.flush()


def main(argv):
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    cmd, path = argv[1], argv[2]
    try:
        if cmd == "build-exfat":
            Builder(path).run(argv[3])
            return 0
        img = Image(path, writable=(cmd == "set-ro"))
        fs = open_fs(img)
        if cmd == "manifest":
            ents = sorted(fs.tree(), key=lambda e: e[4].encode("utf-8", "surrogatepass"))
            with open(argv[3], "w", encoding="utf-8", errors="surrogatepass") as f:
                for t, size, dig, mode, p, clus, eoff in ents:
                    f.write("%s\t%d\t%s\t0\t0\t%o\t%s\n" % (t, size, dig, mode, p))
        elif cmd == "free":
            print(fs.free_bytes())
        elif cmd == "locate":
            clus, eoff = find(fs, argv[3])
            if clus < 2:
                raise Bad("fichier sans cluster")
            print(fs.cl_off(clus))
        elif cmd == "set-ro":
            clus, eoff = find(fs, argv[3])
            if eoff is None:
                raise Bad("la racine n'a pas d'entrée")
            fs.set_ro(eoff)
        else:
            print("commande inconnue : %s" % cmd, file=sys.stderr)
            return 2
    except Bad as e:
        print("fatjuge : %s : %s" % (path, e), file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
