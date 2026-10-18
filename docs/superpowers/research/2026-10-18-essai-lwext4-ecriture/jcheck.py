# jcheck.py IMG : verify jbd2 csum v3 of transactions in the log (journal inode 8, contiguous assumed via debugfs bmap)
import sys, struct, subprocess
img = sys.argv[1]
T = []
for i in range(256):
    c = i
    for _ in range(8): c = (c >> 1) ^ 0x82F63B78 if c & 1 else c >> 1
    T.append(c)
def crc(c, b):
    for x in b: c = T[(c ^ x) & 0xFF] ^ (c >> 8)
    return c
BS = 4096
f = open(img, 'rb')
def jblk(n):
    out = subprocess.run(['debugfs', '-R', 'bmap <8> %d' % n, img], capture_output=True, text=True).stdout.split()
    p = int(out[-1]); f.seek(p * BS); return f.read(BS), p
sb, _ = jblk(0)
magic, btype, seq = struct.unpack('>III', sb[:12])
start = struct.unpack('>I', sb[28:32])[0]; sseq = struct.unpack('>I', sb[24:28])[0]
uuid = sb[48:64]
seed = crc(0xFFFFFFFF, uuid)
print('jsb start', start, 'sequence', sseq, 'feat_incompat 0x%x' % struct.unpack('>I', sb[40:44])[0])
n = int(sys.argv[2]) if len(sys.argv) > 2 else start
expect = int(sys.argv[3]) if len(sys.argv) > 3 else sseq
while True:
    b, p = jblk(n)
    m, t, s = struct.unpack('>III', b[:12])
    if m != 0xC03B3998: print('blk', n, 'no magic'); break
    if s != expect: print('blk', n, 'seq', s, '!= expected', expect, 'type', t); break
    if t == 1:
        z = b[:-4] + b'\0' * 4
        ok = crc(seed, z) == struct.unpack('>I', b[-4:])[0]
        print('blk', n, 'descriptor seq', s, 'tail csum ok' if ok else 'TAIL CSUM BAD')
        off = 12; jb = n
        while off + 16 <= BS - 4:
            blo, fl, bhi, cs = struct.unpack('>IIII', b[off:off+16])
            off += 16
            if not (fl & 2): off += 16  # uuid follows
            jb += 1
            d, _ = jblk(jb)
            c = crc(crc(seed, struct.pack('>I', s)), d)
            print('   fsblk', (bhi << 32) | blo, 'flags 0x%x' % fl, 'jblk', jb, 'csum', 'ok' if c == cs else 'BAD (tag %08x calc %08x)' % (cs, c))
            if fl & 8: break
        n = jb + 1
    elif t == 2:
        z = bytearray(b); csoff = 16  # commit: h_chksum_type(1) size(1) pad(2) chksum[8]x4 (offset 16)
        stored = struct.unpack('>I', b[16:20])[0]
        z[16:20] = b'\0' * 4
        print('blk', n, 'commit seq', s, 'csum', 'ok' if crc(seed, bytes(z)) == stored else 'BAD stored %08x calc %08x' % (stored, crc(seed, bytes(z))))
        n += 1; expect += 1
    elif t == 5:
        print('blk', n, 'revoke seq', s); n += 1
    else:
        print('blk', n, 'type', t); n += 1
