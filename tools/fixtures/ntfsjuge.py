#!/usr/bin/env python3
"""Juge NTFS indépendant du moteur (phase 5) : lit et vérifie une image sans libntfs-3g.

Usage :
  ntfsjuge.py check <img>                 cohérence ; sortie 0 sans rien afficher, sinon 3 et une ligne
                                          par défaut sur la sortie standard
  ntfsjuge.py manifest <img> <sortie>     manifeste au format de tm_write_manifest (tree_model.h)
  ntfsjuge.py free <img>                  octets libres selon $Bitmap (clusters libres × taille de cluster)
  ntfsjuge.py flags <img>                 drapeaux de $Volume ($VOLUME_INFORMATION), en hexadécimal
  ntfsjuge.py set-flags <img> <hex>       les remplace (dans $MFT et $MFTMirr)
  ntfsjuge.py set-logfile-sale <img>      $LogFile laissé « ouvert » par Windows (coupure)
  ntfsjuge.py set-chiffre <img> <chemin>  marque le fichier comme chiffré (EFS)
  ntfsjuge.py locate <img> <chemin>       décalage (octets) de l'enregistrement MFT du fichier
  ntfsjuge.py abime <img> <cas>           défaut posé exprès (bitmap, miroir, liens, nom) pour les tests

Manifeste : une ligne par entrée, triée par chemin (octets UTF-8), séparée par des tabulations : type
(d dossier, f fichier, l lien symbolique ou jonction, o autre point d'analyse), taille, condensat (« - »
sauf pour f), uid (0), gid (0), mode octal (100644, 100444 avec l'attribut lecture seule, 40755, 120777,
100000), chemin. Condensat = sha256 de la suite des sha256 de chaque tranche de 1 Mio du contenu. Les
enregistrements MFT < 16, les noms DOS et les flux nommés n'apparaissent pas.

Structure illisible (enregistrement sans « FILE », correction de fin de secteur fausse, runlist hors du
disque, boucle) : message sur la sortie d'erreur, code 3, quelle que soit la commande.
"""
import hashlib
import os
import struct
import sys

CHUNK = 1 << 20
FILE_FIRST_USER = 16
NS_POSIX, NS_WIN32, NS_DOS, NS_BOTH = 0, 1, 2, 3
AT_STD, AT_LIST, AT_FN, AT_VOLINFO, AT_DATA = 0x10, 0x20, 0x30, 0x70, 0x80
AT_IROOT, AT_IALLOC, AT_BITMAP, AT_REPARSE = 0x90, 0xA0, 0xB0, 0xC0
TAG_SYMLINK, TAG_MOUNT = 0xA000000C, 0xA0000003
FA_READONLY, FA_REPARSE, FA_ENCRYPTED = 0x1, 0x400, 0x4000


class Bad(Exception):
    pass


def u8(b, o):
    return b[o]


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


ZERO = bytes(CHUNK)
ZERO_SHA = hashlib.sha256(ZERO).digest()


def digest_chunks(chunks):
    acc = hashlib.sha256()
    for c in chunks:
        acc.update(ZERO_SHA if c is ZERO else hashlib.sha256(c).digest())
    return acc.hexdigest()


def unfix(raw, what):
    """Applique les corrections de fin de secteur (USA) ; rend une copie corrigée."""
    b = bytearray(raw)
    ofs, cnt = u16(b, 4), u16(b, 6)
    if cnt == 0 or ofs + 2 * cnt > len(b) or (cnt - 1) * 512 > len(b):
        raise Bad("%s : tableau de corrections hors bornes" % what)
    usn = b[ofs:ofs + 2]
    for i in range(1, cnt):
        end = i * 512 - 2
        if b[end:end + 2] != usn:
            raise Bad("%s : correction de fin de secteur %d fausse" % (what, i - 1))
        b[end:end + 2] = b[ofs + 2 * i:ofs + 2 * i + 2]
    return b


def refix(fixed):
    """Inverse d'unfix (même numéro de séquence)."""
    b = bytearray(fixed)
    ofs, cnt = u16(b, 4), u16(b, 6)
    usn = b[ofs:ofs + 2]
    for i in range(1, cnt):
        end = i * 512 - 2
        b[ofs + 2 * i:ofs + 2 * i + 2] = b[end:end + 2]
        b[end:end + 2] = usn
    return bytes(b)


def lznt1(data, out_len):
    """Décompresse un bloc de compression LZNT1 (sous-blocs de 4 Kio)."""
    out = bytearray()
    p = 0
    while p + 2 <= len(data) and len(out) < out_len:
        h = u16(data, p)
        if h == 0:
            break
        size = (h & 0x0FFF) + 1
        blk = data[p + 2:p + 2 + size]
        if len(blk) != size:
            raise Bad("LZNT1 : sous-bloc tronqué")
        p += 2 + size
        if not h & 0x8000:
            out += blk
            continue
        start = len(out)
        q = 0
        while q < len(blk):
            tags = blk[q]
            q += 1
            for bit in range(8):
                if q >= len(blk):
                    break
                if not tags >> bit & 1:
                    out.append(blk[q])
                    q += 1
                    continue
                if q + 2 > len(blk):
                    raise Bad("LZNT1 : jeton tronqué")
                tok = u16(blk, q)
                q += 2
                pos = len(out) - start
                lb = 12
                n = pos - 1
                while n >= 0x10:
                    lb -= 1
                    n >>= 1
                back = (tok >> lb) + 1
                length = (tok & ((1 << lb) - 1)) + 3
                if back > pos:
                    raise Bad("LZNT1 : référence avant le début")
                for _ in range(length):
                    out.append(out[-back])
        if len(out) - start > 4096:
            raise Bad("LZNT1 : sous-bloc de plus de 4 Kio")
        out += bytes(start + 4096 - len(out))  # sous-bloc court : complété par des zéros
    out = out[:out_len]
    return bytes(out) + bytes(out_len - len(out))


class Attr:
    def __init__(self, rec, raw, off):
        self.rec = rec
        self.type = u32(raw, off)
        self.length = u32(raw, off + 4)
        self.nonres = u8(raw, off + 8)
        nl, no = u8(raw, off + 9), u16(raw, off + 10)
        self.flags = u16(raw, off + 12)
        self.name = raw[off + no:off + no + 2 * nl].decode("utf-16-le", "surrogatepass") if nl else ""
        if self.nonres:
            self.lvcn, self.hvcn = u64(raw, off + 16), u64(raw, off + 24)
            mp = u16(raw, off + 32)
            self.cunit = u8(raw, off + 34)
            self.alloc, self.size, self.init = u64(raw, off + 40), u64(raw, off + 48), u64(raw, off + 56)
            self.runs = decode_runs(raw[off + mp:off + self.length], self.lvcn)
            self.value = None
        else:
            vl, vo = u32(raw, off + 16), u16(raw, off + 20)
            if vo + vl > self.length:
                raise Bad("enregistrement %d : valeur résidente hors de l'attribut" % rec)
            self.value = bytes(raw[off + vo:off + vo + vl])
            self.size = vl


def decode_runs(mp, vcn):
    """Liste de (vcn, lcn ou None, longueur) ; lcn None = trou."""
    runs, p, lcn = [], 0, 0
    while p < len(mp) and mp[p]:
        h = mp[p]
        ln, lo = h & 0xF, h >> 4
        if ln == 0 or ln > 8 or lo > 8 or p + 1 + ln + lo > len(mp):
            raise Bad("runlist illisible")
        length = int.from_bytes(mp[p + 1:p + 1 + ln], "little")
        if lo:
            lcn += int.from_bytes(mp[p + 1 + ln:p + 1 + ln + lo], "little", signed=True)
            runs.append((vcn, lcn, length))
        else:
            runs.append((vcn, None, length))
        vcn += length
        p += 1 + ln + lo
    return runs


class Ntfs:
    def __init__(self, path, writable=False):
        self.f = open(path, "r+b" if writable else "rb")
        self.img_size = os.fstat(self.f.fileno()).st_size
        bs = self.read(0, 512)
        if bs[3:11] != b"NTFS    ":
            raise Bad("pas de signature NTFS")
        self.bps = u16(bs, 11)
        spc = u8(bs, 13)
        spc = 1 << (256 - spc) if spc > 0x80 else spc
        if self.bps not in (256, 512, 1024, 2048, 4096) or spc == 0:
            raise Bad("géométrie du secteur de démarrage invalide")
        self.cs = self.bps * spc
        self.nclusters = u64(bs, 40) // spc
        self.mft_lcn, self.mirr_lcn = u64(bs, 48), u64(bs, 56)
        c = struct.unpack_from("<b", bs, 64)[0]
        self.rs = 1 << -c if c < 0 else c * self.cs
        c = struct.unpack_from("<b", bs, 68)[0]
        self.ixs = 1 << -c if c < 0 else c * self.cs
        if self.rs < 512 or self.rs > 65536 or self.ixs < 512:
            raise Bad("taille d'enregistrement ou d'index invalide")
        if self.nclusters * self.cs > self.img_size:
            raise Bad("volume plus grand que l'image")
        r0 = self.raw_record_at(self.mft_lcn * self.cs, 0)
        self.mft_runs = None
        attrs = self.parse_attrs(0, r0)
        data = [a for a in attrs if a.type == AT_DATA and a.name == "" and a.nonres]
        if not data:
            raise Bad("$MFT sans $DATA non résident")
        self.mft_runs = data[0].runs
        self.nrecords = data[0].size // self.rs
        self.records = {}
        self._load_all()

    # --- lecture brute ------------------------------------------------------------------------
    def read(self, off, n):
        if off < 0 or off + n > self.img_size:
            raise Bad("lecture hors de l'image : %d+%d" % (off, n))
        self.f.seek(off)
        return self.f.read(n)

    def write(self, off, data):
        if off < 0 or off + len(data) > self.img_size:
            raise Bad("écriture hors de l'image")
        self.f.seek(off)
        self.f.write(data)

    def runs_read(self, runs, off, n):
        out = bytearray()
        for vcn, lcn, ln in runs:
            a, b = vcn * self.cs, (vcn + ln) * self.cs
            lo, hi = max(a, off), min(b, off + n)
            if lo >= hi:
                continue
            if len(out) < lo - off:
                out += bytes(lo - off - len(out))
            if lcn is None:
                out += bytes(hi - lo)
            else:
                if lcn + ln > self.nclusters:
                    raise Bad("run hors du volume")
                out += self.read(lcn * self.cs + lo - a, hi - lo)
        return bytes(out) + bytes(n - len(out))

    def runs_hole(self, runs, off, n):
        """Vrai si [off, off + n) ne touche aucun cluster réel."""
        for vcn, lcn, ln in runs:
            if lcn is not None and vcn * self.cs < off + n and (vcn + ln) * self.cs > off:
                return False
        return True

    def record_offset(self, n):
        off = n * self.rs
        for vcn, lcn, ln in self.mft_runs:
            if vcn * self.cs <= off < (vcn + ln) * self.cs:
                if lcn is None:
                    raise Bad("enregistrement %d dans un trou de $MFT" % n)
                return lcn * self.cs + off - vcn * self.cs
        raise Bad("enregistrement %d hors de $MFT" % n)

    def raw_record_at(self, off, n):
        raw = self.read(off, self.rs)
        if raw[:4] != b"FILE":
            return None
        return unfix(raw, "enregistrement %d" % n)

    def parse_attrs(self, n, rec):
        out, off = [], u16(rec, 20)
        used = min(u32(rec, 24), self.rs)
        seen = 0
        while off + 8 <= used:
            t = u32(rec, off)
            if t == 0xFFFFFFFF:
                break
            ln = u32(rec, off + 4)
            if ln < 16 or off + ln > used or ln & 7:
                raise Bad("enregistrement %d : longueur d'attribut %d invalide" % (n, ln))
            out.append(Attr(n, rec, off))
            off += ln
            seen += 1
            if seen > 1000:
                raise Bad("enregistrement %d : trop d'attributs" % n)
        return out

    def _load_all(self):
        for n in range(self.nrecords):
            rec = self.raw_record_at(self.record_offset(n), n)
            if rec is None:
                continue
            flags = u16(rec, 22)
            self.records[n] = {
                "raw": rec, "flags": flags, "inuse": bool(flags & 1), "isdir": bool(flags & 2),
                "seq": u16(rec, 16), "links": u16(rec, 18), "base": u64(rec, 32) & 0xFFFFFFFFFFFF,
                "attrs": self.parse_attrs(n, rec) if flags & 1 else [],
            }
        # attributs des enregistrements d'extension rattachés à leur base
        for n, r in self.records.items():
            r["all"] = list(r["attrs"]) if r["inuse"] and r["base"] == 0 else []
        for n, r in self.records.items():
            if r["inuse"] and r["base"]:
                b = self.records.get(r["base"])
                if not b or not b["inuse"] or b["base"]:
                    raise Bad("enregistrement %d : base %d invalide" % (n, r["base"]))
                b["all"] += r["attrs"]

    # --- attributs ----------------------------------------------------------------------------
    def attrs(self, n, t, name=None):
        r = self.records.get(n)
        if not r or not r["inuse"]:
            raise Bad("enregistrement %d absent ou libre" % n)
        return [a for a in r["all"] if a.type == t and (name is None or a.name == name)]

    def stream(self, n, t, name=""):
        """(taille, initialisée, runs fusionnés, compression, valeur résidente) d'un flux."""
        parts = self.attrs(n, t, name)
        if not parts:
            return None
        if not parts[0].nonres:
            return {"size": parts[0].size, "res": parts[0].value, "flags": parts[0].flags}
        parts.sort(key=lambda a: a.lvcn)
        first = parts[0]
        if first.lvcn != 0:
            raise Bad("enregistrement %d : flux sans son début" % n)
        runs, vcn = [], 0
        for a in parts:
            if a.lvcn != vcn:
                raise Bad("enregistrement %d : flux découpé avec un trou" % n)
            for r in a.runs:
                runs.append(r)
            vcn = a.hvcn + 1
        return {"size": first.size, "init": first.init, "alloc": first.alloc, "runs": runs,
                "cunit": first.cunit, "flags": first.flags, "nvcn": vcn, "res": None}

    def content(self, n, t=AT_DATA, name=""):
        """Itérateur de tranches de CHUNK octets du flux (zéros après la taille initialisée)."""
        s = self.stream(n, t, name)
        if s is None:
            return
        if s["res"] is not None:
            for i in range(0, len(s["res"]), CHUNK):
                yield s["res"][i:i + CHUNK]
            return
        size, init = s["size"], s["init"]
        comp = s["flags"] & 0x00FF and s["cunit"]
        if comp:
            cb = (1 << s["cunit"]) * self.cs
            pos = 0
            buf = bytearray()
            while pos < size:
                buf += self.comp_block(s["runs"], pos // self.cs, 1 << s["cunit"], min(cb, size - pos))
                pos += cb
                while len(buf) >= CHUNK:
                    yield bytes(buf[:CHUNK])
                    del buf[:CHUNK]
            if buf:
                yield bytes(buf)
            return
        pos = 0
        while pos < size:
            n2 = min(CHUNK, size - pos)
            if n2 == CHUNK and (pos >= init or self.runs_hole(s["runs"], pos, n2)):
                yield ZERO
                pos += n2
                continue
            d = self.runs_read(s["runs"], pos, n2) if pos < init else bytes(n2)
            if pos < init < pos + n2:
                d = d[:init - pos] + bytes(pos + n2 - init)
            yield d
            pos += n2

    def comp_block(self, runs, vcn0, ncl, want):
        """Bloc de compression de ncl clusters à vcn0 : brut, compressé ou creux."""
        real = 0
        for vcn, lcn, ln in runs:
            lo, hi = max(vcn, vcn0), min(vcn + ln, vcn0 + ncl)
            if lo < hi and lcn is not None:
                real += hi - lo
        raw = self.runs_read(runs, vcn0 * self.cs, ncl * self.cs)
        if real == 0:
            return bytes(want)
        if real == ncl:
            return raw[:want]
        return lznt1(raw[:real * self.cs], want)

    def std_attr(self, n):
        a = self.attrs(n, AT_STD)
        if not a or a[0].nonres or len(a[0].value) < 36:
            raise Bad("enregistrement %d sans $STANDARD_INFORMATION" % n)
        return u32(a[0].value, 32)

    def file_names(self, n):
        out = []
        for a in self.attrs(n, AT_FN):
            v = a.value
            if a.nonres or v is None or len(v) < 66 or 66 + 2 * v[64] > len(v):
                raise Bad("enregistrement %d : $FILE_NAME invalide" % n)
            out.append((u64(v, 0) & 0xFFFFFFFFFFFF, u16(v, 6), v[65], v[66:66 + 2 * v[64]]))
        return out

    def upcase(self):
        if not hasattr(self, "_up"):
            d = b"".join(self.content(10))
            self._up = struct.unpack("<%dH" % (len(d) // 2), d[:len(d) // 2 * 2])
        return self._up

    def ukey(self, name16):
        up = self.upcase()
        return tuple(up[c] if c < len(up) else c for c in struct.unpack("<%dH" % (len(name16) // 2), name16))

    # --- index $I30 ---------------------------------------------------------------------------
    def index_entries(self, n):
        """Entrées de l'index $I30 du dossier n, dans l'ordre de l'arbre : (mref, seq, ns, nom UTF-16).
        Remplit aussi self.ix_used[n] (VCN des blocs INDX parcourus)."""
        roots = self.attrs(n, AT_IROOT, "$I30")
        if not roots or roots[0].nonres:
            raise Bad("dossier %d sans $INDEX_ROOT" % n)
        v = roots[0].value
        alloc = self.stream(n, AT_IALLOC, "$I30")
        bsize = u32(v, 8)
        unit = self.cs if bsize >= self.cs else 512
        used = set()
        out = []

        def node(buf, hdr, depth):
            if depth > 32:
                raise Bad("dossier %d : index trop profond" % n)
            eo, il = u32(buf, hdr), u32(buf, hdr + 4)
            p, end = hdr + eo, hdr + il
            while True:
                if p + 16 > end or p + 16 > len(buf):
                    raise Bad("dossier %d : entrée d'index hors du nœud" % n)
                ln, kl, fl = u16(buf, p + 8), u16(buf, p + 10), u16(buf, p + 12)
                if ln < 16 or p + ln > end:
                    raise Bad("dossier %d : longueur d'entrée d'index %d invalide" % (n, ln))
                if fl & 1:
                    sub = u64(buf, p + ln - 8)
                    if alloc is None or sub in used:
                        raise Bad("dossier %d : sous-nœud %d absent ou en boucle" % (n, sub))
                    used.add(sub)
                    raw = self.runs_read(alloc["runs"], sub * unit, bsize) if alloc["res"] is None else None
                    if raw is None or raw[:4] != b"INDX":
                        raise Bad("dossier %d : bloc INDX %d invalide" % (n, sub))
                    blk = unfix(raw, "bloc INDX %d du dossier %d" % (sub, n))
                    if u64(blk, 16) != sub:
                        raise Bad("dossier %d : bloc INDX %d mal numéroté" % (n, sub))
                    node(blk, 24, depth + 1)
                if fl & 2:
                    break
                if kl < 66:
                    raise Bad("dossier %d : clé d'index trop courte" % n)
                k = buf[p + 16:p + 16 + kl]
                mref = u64(buf, p)
                out.append((mref & 0xFFFFFFFFFFFF, mref >> 48, k[65], bytes(k[66:66 + 2 * k[64]]),
                            u64(k, 0) & 0xFFFFFFFFFFFF))
                p += ln

        node(v, 16, 0)
        if not hasattr(self, "ix_used"):
            self.ix_used = {}
        self.ix_used[n] = used
        return out

    # --- arborescence -------------------------------------------------------------------------
    def kind(self, n):
        attr = self.std_attr(n)
        if attr & FA_REPARSE:
            rp = self.attrs(n, AT_REPARSE)
            tag = u32(rp[0].value, 0) if rp and rp[0].value and len(rp[0].value) >= 4 else 0
            return ("l", 0o120777) if tag in (TAG_SYMLINK, TAG_MOUNT) else ("o", 0o100000)
        if self.records[n]["isdir"]:
            return ("d", 0o40755)
        return ("f", 0o100444 if attr & FA_READONLY else 0o100644)

    def tree(self):
        out = [("d", 0, "-", 0o40755, "/", 5)]
        seen = {5}

        def walk(n, path):
            for mref, seq, ns, name16, parent in self.index_entries(n):
                if ns == NS_DOS or mref < FILE_FIRST_USER:
                    continue
                name = name16.decode("utf-16-le", "surrogatepass")
                p = path.rstrip("/") + "/" + name
                t, mode = self.kind(mref)
                if t == "d":
                    if mref in seen:
                        raise Bad("dossier en boucle : %s" % p)
                    seen.add(mref)
                    out.append(("d", 0, "-", mode, p, mref))
                    walk(mref, p)
                elif t == "f":
                    s = self.stream(mref, AT_DATA)
                    size = s["size"] if s else 0
                    out.append(("f", size, digest_chunks(self.content(mref)), mode, p, mref))
                else:
                    out.append((t, 0, "-", mode, p, mref))

        walk(5, "/")
        return out

    def find(self, path):
        n = 5
        for comp in [c for c in path.split("/") if c]:
            name16 = comp.encode("utf-16-le", "surrogatepass")
            hit = [e for e in self.index_entries(n) if e[3] == name16 and e[2] != NS_DOS]
            if not hit:
                raise Bad("introuvable : %s" % path)
            n = hit[0][0]
        return n

    # --- vérification -------------------------------------------------------------------------
    def bitmap_bits(self, n, t, name=""):
        d = b"".join(self.content(n, t, name))
        return d

    def check(self):
        err = []
        # $MFTMirr = premiers enregistrements de $MFT
        mirr = self.stream(1, AT_DATA)
        nm = (mirr["size"] if mirr else 0) // self.rs
        for i in range(nm):
            a = self.read(self.record_offset(i), self.rs)
            b = self.read(self.mirr_lcn * self.cs + i * self.rs, self.rs)
            if a != b:
                err.append("$MFTMirr différent de $MFT (enregistrement %d)" % i)
        # $MFT:$BITMAP
        mb = self.bitmap_bits(0, AT_BITMAP)
        for n in range(self.nrecords):
            bit = n // 8 < len(mb) and mb[n // 8] >> (n % 8) & 1
            inuse = n in self.records and self.records[n]["inuse"]
            # 0..23 : réservés, marqués utilisés même libres (mkntfs, ntfs-3g et Windows)
            if bit and not inuse and n >= 24:
                err.append("$MFT:$BITMAP : enregistrement %d marqué utilisé mais libre" % n)
            if inuse and not bit:
                err.append("$MFT:$BITMAP : enregistrement %d utilisé mais marqué libre" % n)
        # $Bitmap = union des runlists
        owner = {}
        for n, r in self.records.items():
            if not r["inuse"]:
                continue
            for a in r["attrs"]:
                if not a.nonres:
                    continue
                for vcn, lcn, ln in a.runs:
                    if lcn is None:
                        continue
                    if lcn + ln > self.nclusters:
                        err.append("enregistrement %d : run hors du volume" % n)
                        continue
                    for c in range(lcn, lcn + ln):
                        if c in owner:
                            err.append("cluster %d utilisé deux fois (enregistrements %d et %d)" % (c, owner[c], n))
                        owner[c] = n
        cb = self.bitmap_bits(6, AT_DATA)
        for c in range(self.nclusters):
            bit = cb[c // 8] >> (c % 8) & 1 if c // 8 < len(cb) else 0
            if bit and c not in owner:
                err.append("$Bitmap : cluster %d marqué utilisé sans propriétaire" % c)
            elif c in owner and not bit:
                err.append("$Bitmap : cluster %d (enregistrement %d) marqué libre" % (c, owner[c]))
        # index et $FILE_NAME
        indexed = {}
        for n, r in self.records.items():
            if not r["inuse"] or r["base"] or not r["isdir"] or not self.attrs(n, AT_IROOT, "$I30"):
                continue
            try:
                ents = self.index_entries(n)
            except Bad as e:
                err.append(str(e))
                continue
            prev = None
            for mref, seq, ns, name16, parent in ents:
                key = (self.ukey(name16), struct.unpack("<%dH" % (len(name16) // 2), name16))
                if prev is not None and key <= prev:
                    err.append("dossier %d : entrées d'index mal triées (%s)" % (n, name16.decode("utf-16-le", "replace")))
                prev = key
                t = self.records.get(mref)
                if not t or not t["inuse"] or t["base"]:
                    err.append("dossier %d : entrée vers l'enregistrement libre %d" % (n, mref))
                    continue
                if seq and seq != t["seq"]:
                    err.append("dossier %d : séquence fausse pour %d" % (n, mref))
                fns = [(p, ns2, nm2) for p, _, ns2, nm2 in self.file_names(mref)]
                if (parent, ns, name16) not in fns or parent != n:
                    err.append("dossier %d : nom indexé absent des $FILE_NAME de %d" % (n, mref))
                k = (n, ns, name16, mref)
                if k in indexed:
                    err.append("dossier %d : nom indexé deux fois pour %d" % (n, mref))
                indexed[k] = True
            ib = self.stream(n, AT_BITMAP, "$I30")
            used = self.ix_used.get(n, set())
            if ib is not None:
                bits = b"".join(self.content(n, AT_BITMAP, "$I30"))
                bsize = u32(self.attrs(n, AT_IROOT, "$I30")[0].value, 8)
                unit = self.cs if bsize >= self.cs else 512
                per = bsize // unit
                for i in range(len(bits) * 8):
                    on = bits[i // 8] >> (i % 8) & 1
                    if on and i * per not in used:
                        err.append("dossier %d : bloc INDX %d marqué utilisé mais non relié" % (n, i * per))
                    if not on and i * per in used:
                        err.append("dossier %d : bloc INDX %d relié mais marqué libre" % (n, i * per))
        for n, r in self.records.items():
            if not r["inuse"] or r["base"]:
                continue
            fns = self.file_names(n)
            for parent, _, ns, name16 in fns:
                if (parent, ns, name16, n) not in indexed:
                    err.append("enregistrement %d : nom %s absent de l'index de %d"
                               % (n, name16.decode("utf-16-le", "replace"), parent))
            nondos = sum(1 for f in fns if f[2] != NS_DOS)
            if fns and r["links"] != nondos:
                err.append("enregistrement %d : link_count %d au lieu de %d" % (n, r["links"], nondos))
            for a in r["all"]:
                if a.nonres and a.lvcn == 0 and not (a.init <= a.size <= a.alloc):
                    err.append("enregistrement %d : tailles incohérentes (%d, %d, %d)" % (n, a.init, a.size, a.alloc))
        return err

    # --- modifications ------------------------------------------------------------------------
    def patch_record(self, n, fn):
        """Applique fn(bytearray corrigé) à l'enregistrement n, dans $MFT et, s'il y est, $MFTMirr."""
        off = self.record_offset(n)
        rec = bytearray(self.raw_record_at(off, n))
        fn(rec)
        raw = refix(rec)
        self.write(off, raw)
        mirr = self.stream(1, AT_DATA)
        if mirr and n < mirr["size"] // self.rs:
            self.write(self.mirr_lcn * self.cs + n * self.rs, raw)

    def attr_value_offset(self, rec, t):
        off = u16(rec, 20)
        while off + 8 <= len(rec) and u32(rec, off) != 0xFFFFFFFF:
            if u32(rec, off) == t and not rec[off + 8]:
                return off + u16(rec, off + 20)
            off += u32(rec, off + 4)
        raise Bad("attribut %#x résident introuvable" % t)

    def attr_offset(self, rec, t):
        off = u16(rec, 20)
        while off + 8 <= len(rec) and u32(rec, off) != 0xFFFFFFFF:
            if u32(rec, off) == t:
                return off
            off += u32(rec, off + 4)
        raise Bad("attribut %#x introuvable" % t)

    def volume_flags(self):
        a = self.attrs(3, AT_VOLINFO)
        return u16(a[0].value, 10)

    def set_volume_flags(self, flags):
        def fn(rec):
            struct.pack_into("<H", rec, self.attr_value_offset(rec, AT_VOLINFO) + 10, flags)
        self.patch_record(3, fn)

    def set_logfile_dirty(self):
        s = self.stream(2, AT_DATA)
        if not s or s["res"] is not None:
            raise Bad("$LogFile sans $DATA non résident")
        size = s["size"]
        page = bytearray(4096)
        page[0:4] = b"RSTR"
        struct.pack_into("<HHQIIHhh", page, 4, 30, 9, 0, 4096, 4096, 48, 1, 1)
        ra = 48
        bits = size.bit_length()
        struct.pack_into("<QHHHHIHHqIHHI", page, ra, 1000, 1, 0xFFFF, 0, 0, 67 - bits, 48 + 160, 48, size,
                         0, 48, 64, 1)
        cr = ra + 48
        struct.pack_into("<QQHHH", page, cr, 1000, 1000, 0xFFFF, 0xFFFF, 0)
        struct.pack_into("<I", page, cr + 28, 8)
        page[cr + 32:cr + 40] = "NTFS".encode("utf-16-le")
        struct.pack_into("<H", page, 30, 1)
        raw = refix(page)
        for pos in (0, 4096):
            done = 0
            for vcn, lcn, ln in s["runs"]:
                a, b = vcn * self.cs, (vcn + ln) * self.cs
                lo, hi = max(a, pos), min(b, pos + 4096)
                if lo < hi and lcn is not None:
                    self.write(lcn * self.cs + lo - a, raw[lo - pos:hi - pos])
                    done += hi - lo
            if done != 4096:
                raise Bad("$LogFile trop court ou creux")

    def set_encrypted(self, n):
        def fn(rec):
            v = self.attr_value_offset(rec, AT_STD)
            struct.pack_into("<I", rec, v + 32, u32(rec, v + 32) | FA_ENCRYPTED)
            a = self.attr_offset(rec, AT_DATA)
            struct.pack_into("<H", rec, a + 12, u16(rec, a + 12) | 0x4000)
        self.patch_record(n, fn)

    def damage(self, case):
        """Défauts simples posés à la main, pour vérifier que check les voit (test_ntfs_banc)."""
        if case == "bitmap":  # cluster 0 ($Boot) marqué libre
            s = self.stream(6, AT_DATA)
            vcn, lcn, ln = s["runs"][0]
            off = lcn * self.cs
            self.write(off, bytes([self.read(off, 1)[0] & 0xFE]))
        elif case == "miroir":  # un octet de $MFTMirr (enregistrement 0, hors corrections)
            off = self.mirr_lcn * self.cs + 100
            self.write(off, bytes([self.read(off, 1)[0] ^ 0xFF]))
        elif case == "liens":  # link_count + 1 sur /abc.txt
            n = self.find("/abc.txt")
            self.patch_record(n, lambda rec: struct.pack_into("<H", rec, 18, u16(rec, 18) + 1))
        elif case == "nom":  # une lettre du $FILE_NAME de /abc.txt changée : l'index ne correspond plus
            n = self.find("/abc.txt")

            def fn(rec):
                v = self.attr_value_offset(rec, AT_FN)
                struct.pack_into("<H", rec, v + 66, ord("x"))
            self.patch_record(n, fn)
        else:
            raise Bad("cas inconnu : %s" % case)

    def free_bytes(self):
        cb = self.bitmap_bits(6, AT_DATA)
        used = sum(1 for c in range(self.nclusters) if cb[c // 8] >> (c % 8) & 1)
        return (self.nclusters - used) * self.cs


def main(argv):
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    cmd, path = argv[1], argv[2]
    try:
        writable = cmd in ("set-flags", "set-logfile-sale", "set-chiffre", "abime")
        fs = Ntfs(path, writable)
        if cmd == "check":
            err = fs.check()
            for e in err[:50]:
                print(e)
            return 3 if err else 0
        if cmd == "manifest":
            ents = sorted(fs.tree(), key=lambda e: e[4].encode("utf-8", "surrogatepass"))
            with open(argv[3], "w", encoding="utf-8", errors="surrogatepass") as f:
                for t, size, dig, mode, p, _ in ents:
                    f.write("%s\t%d\t%s\t0\t0\t%o\t%s\n" % (t, size, dig, mode, p))
        elif cmd == "free":
            print(fs.free_bytes())
        elif cmd == "flags":
            print("%#06x" % fs.volume_flags())
        elif cmd == "set-flags":
            fs.set_volume_flags(int(argv[3], 16))
        elif cmd == "set-logfile-sale":
            fs.set_logfile_dirty()
        elif cmd == "set-chiffre":
            fs.set_encrypted(fs.find(argv[3]))
        elif cmd == "abime":
            fs.damage(argv[3])
        elif cmd == "locate":
            print(fs.record_offset(fs.find(argv[3])))
        else:
            print("commande inconnue : %s" % cmd, file=sys.stderr)
            return 2
    except Bad as e:
        print("ntfsjuge : %s : %s" % (path, e), file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
