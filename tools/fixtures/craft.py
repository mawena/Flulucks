#!/usr/bin/env python3
"""Images forgées : signatures, MBR étendu, GPT abîmé / hors bornes.
  craft.py sig NOM OUT | mbr-etendue OUT | gpt-secours OUT | gpt-hors-bornes OUT
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
    else:
        sys.exit("commande inconnue")


main()
