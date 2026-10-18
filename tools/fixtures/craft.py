#!/usr/bin/env python3
"""Images forgées : signatures, MBR étendu, GPT abîmé / hors bornes.
  craft.py sig NOM OUT | mbr-etendue OUT | gpt-secours OUT | gpt-hors-bornes OUT
         | ext4-entrees-hostiles IMG BLOC INODE GENERATION
"""
import struct, sys, uuid, zlib

SIGS = {  # nom: [(décalage, octets)]
    "exfat": [(3, b"EXFAT   ")],
    "lvm2": [(512, b"LABELONE"), (536, b"LVM2 001")],
    "bitlocker": [(3, b"-FVE-FS-")],
    "btrfs": [(65600, b"_BHRfS_M")],
    "xfs": [(0, b"XFSB")],
}


def mbr_entry(ptype, start, size, boot=0):
    return struct.pack("<B3sB3sII", boot, b"\xfe\xff\xff", ptype, b"\xfe\xff\xff", start, size)


def mbr_sector(entries):
    e = b"".join(entries).ljust(64, b"\0")
    return (b"\0" * 446) + e + b"\x55\xaa"


def sig(name, out):
    buf = bytearray(1 << 20)
    for off, b in SIGS[name]:
        buf[off:off + len(b)] = b
    open(out, "wb").write(buf)


def mbr_etendue(out):
    img = bytearray(16 << 20)
    img[0:512] = mbr_sector([mbr_entry(0x0C, 2048, 8192), mbr_entry(0x05, 10240, 12288)])
    # EBR 1 en LBA 10240 : p5 (0x83) en 12288, 4096 secteurs ; lien vers EBR 2 (relatif à 10240)
    img[10240 * 512:10241 * 512] = mbr_sector([mbr_entry(0x83, 2048, 4096), mbr_entry(0x05, 6144, 6144)])
    # EBR 2 en LBA 16384 : p6 (0x82) en 18432, 4096 secteurs
    img[16384 * 512:16385 * 512] = mbr_sector([mbr_entry(0x82, 2048, 4096)])
    open(out, "wb").write(img)


LINUX = uuid.UUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4")


def gpt(total_sectors, parts, corrupt_primary_crc=False):
    """parts: [(first, last, nom)]. Retourne l'image (octets)."""
    img = bytearray(total_sectors * 512)
    img[0:512] = mbr_sector([struct.pack("<B3sB3sII", 0, b"\0\2\0", 0xEE, b"\xff\xff\xff", 1,
                                         min(total_sectors - 1, 0xFFFFFFFF))])
    arr = bytearray(128 * 128)
    for i, (first, last, name) in enumerate(parts):
        guid = uuid.uuid5(uuid.NAMESPACE_DNS, "fluluks-part-%d" % i).bytes_le
        nm = name.encode("utf-16-le").ljust(72, b"\0")
        arr[i * 128:(i + 1) * 128] = LINUX.bytes_le + guid + struct.pack("<QQQ", first, last, 0) + nm
    arr_crc = zlib.crc32(arr)
    disk_guid = uuid.uuid5(uuid.NAMESPACE_DNS, "fluluks-disk").bytes_le
    last_lba = total_sectors - 1

    def header(cur, bak, arr_lba):
        h = bytearray(92)
        h[0:8] = b"EFI PART"
        struct.pack_into("<IIII", h, 8, 0x00010000, 92, 0, 0)
        struct.pack_into("<QQQQ", h, 24, cur, bak, 34, last_lba - 33)
        h[56:72] = disk_guid
        struct.pack_into("<QIII", h, 72, arr_lba, 128, 128, arr_crc)
        struct.pack_into("<I", h, 16, zlib.crc32(bytes(h)))
        return h

    prim = header(1, last_lba, 2)
    back = header(last_lba, 1, last_lba - 32)
    if corrupt_primary_crc:
        prim[16] ^= 0xFF
    img[512:512 + 92] = prim
    img[1024:1024 + len(arr)] = arr
    img[(last_lba - 32) * 512:(last_lba - 32) * 512 + len(arr)] = arr
    img[last_lba * 512:last_lba * 512 + 92] = back
    return img


def crc32c(crc, data):
    """crc32c brut (sans inversion finale), comme ext4_chksum du noyau."""
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc


def ext4_entrees_hostiles(img, fblock, ino, gen):
    """Abîme les entrées du bloc de dossier `fblock` (bloc physique, dossier d'inode `ino`,
    génération `gen`) d'une image ext4 faite par mkfs.ext4 -d, puis recalcule la somme de
    contrôle metadata_csum du bloc : un disque hostile sait la calculer.
      a-hors-bornes : numéro d'inode = s_inodes_count + 1
      b-barre       : '/' dans le nom (« b/barre »)
      c-nul         : NUL dans le nom (« c\\0nul »)
      d-vide        : longueur de nom 0, inode valide
      e-valide.txt  : intacte (doit rester listée et ouvrable)"""
    with open(img, "r+b") as f:
        f.seek(1024)
        sb = f.read(1024)
        assert sb[0x38:0x3A] == b"\x53\xef", "pas un superbloc ext4"
        bs = 1024 << struct.unpack_from("<I", sb, 0x18)[0]
        icount = struct.unpack_from("<I", sb, 0)[0]
        ro_compat = struct.unpack_from("<I", sb, 0x64)[0]
        incompat = struct.unpack_from("<I", sb, 0x60)[0]
        assert incompat & 0x2, "filetype attendu"
        assert not incompat & 0x2000, "metadata_csum_seed non géré ici"
        csum = bool(ro_compat & 0x400)
        f.seek(fblock * bs)
        blk = bytearray(f.read(bs))
        end = bs - 12 if csum else bs
        if csum:
            seed = crc32c(0xFFFFFFFF, sb[0x68:0x78])
            iseed = crc32c(crc32c(seed, struct.pack("<I", ino)), struct.pack("<I", gen))
            assert struct.unpack_from("<I", blk, bs - 4)[0] == crc32c(iseed, blk[:end]), \
                "somme du bloc de dossier inattendue (mauvais bloc, inode ou génération ?)"
        vus = set()
        off = 0
        while off < end:
            inode, rec_len, name_len = struct.unpack_from("<IHB", blk, off)
            name = bytes(blk[off + 8:off + 8 + name_len])
            if name == b"a-hors-bornes":
                struct.pack_into("<I", blk, off, icount + 1)
            elif name == b"b-barre":
                blk[off + 9] = ord("/")
            elif name == b"c-nul":
                blk[off + 9] = 0
            elif name == b"d-vide":
                blk[off + 6] = 0
            vus.add(name)
            off += rec_len
        assert {b"a-hors-bornes", b"b-barre", b"c-nul", b"d-vide", b"e-valide.txt"} <= vus, vus
        if csum:
            struct.pack_into("<I", blk, bs - 4, crc32c(iseed, blk[:end]))
        f.seek(fblock * bs)
        f.write(blk)


def main():
    c = sys.argv[1]
    if c == "sig":
        sig(sys.argv[2], sys.argv[3])
    elif c == "mbr-etendue":
        mbr_etendue(sys.argv[2])
    elif c == "gpt-secours":
        open(sys.argv[2], "wb").write(gpt(16384, [(2048, 4095, "un"), (4096, 8191, "deux")], True))
    elif c == "gpt-hors-bornes":
        open(sys.argv[2], "wb").write(gpt(16384, [(2048, 4095, "un"), (4096, 100000, "deux")]))
    elif c == "ext4-entrees-hostiles":
        ext4_entrees_hostiles(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]))
    else:
        sys.exit("commande inconnue")


main()
