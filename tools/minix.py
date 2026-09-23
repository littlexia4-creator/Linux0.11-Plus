#!/usr/bin/env python3
"""
minix.py -- read-only Minix V1 filesystem explorer for Linux 0.11 disk images.

Works on both images in this repo:
  * hdc-0.11.img        hard disk: MBR + one type-0x81 (Minix) partition
  * rootimage-0.11.img  root floppy: raw Minix fs starting at byte 0

The on-disk structures mirror include/linux/fs.h and the code in fs/ --
this file is a host-side reimplementation of what super.c/inode.c/namei.c
do inside the kernel, so it doubles as a readable spec of the format.

Layout recap (all little-endian, block size = 1024 << s_log_zone_size):
  block 0                      boot block (skipped)
  block 1                      superblock: H H H H H H I H  (see _read_super)
  blocks 2 .. 2+imap-1         inode bitmap      (1 bit per inode)
  blocks .. +zmap              zone bitmap       (1 bit per zone)
  blocks ..                    inode table       32 bytes per inode
  firstdatazone ..             data zones

  inode (32 bytes): mode:16 uid:16 size:32 mtime:32 gid:8 nlinks:8 zone[9]:16
  directory entry (16 bytes): inode:16 name[14]

Usage:
  python minix.py ls    [-l] [-R] [PATH]  list a directory (default /)
  python minix.py cat   PATH              print a file to stdout
  python minix.py stat  PATH              show inode details
  python minix.py dump  PATH [DEST]       extract a file to the host
  python minix.py put   LOCAL DEST        copy a host file into the image
  python minix.py mkdir PATH              create a directory
  python minix.py rm    PATH              remove a file / empty directory

Note for Git Bash: MSYS rewrites leading-slash arguments into Windows
paths (/etc -> C:/Program Files/Git/etc). Prefix the command with
MSYS_NO_PATHCONV=1 (or run it from cmd.exe).

WARNING: the write commands (put/mkdir/rm) must not run while Bochs
has the image open -- the guest's buffer cache would clobber your
changes on shutdown. Close the VM first.
"""

import struct
import sys
import time

MINIX_MAGIC = 0x137F          # Minix V1, 14-char filenames
MINIX_PART = 0x81             # old Minix partition type in an MBR

S_IFMT = 0o170000
S_IFDIR = 0o040000
S_IFCHR = 0o020000
S_IFBLK = 0o060000
S_IFREG = 0o100000

FTYPE = {S_IFDIR: "d", S_IFCHR: "c", S_IFBLK: "b", S_IFREG: "-"}


class Inode:
    def __init__(self, nr, mode, uid, size, mtime, gid, nlinks, zones):
        self.nr, self.mode, self.uid, self.size = nr, mode, uid, size
        self.mtime, self.gid, self.nlinks, self.zones = mtime, gid, nlinks, zones

    @property
    def ftype(self):
        return FTYPE.get(self.mode & S_IFMT, "?")

    def is_dir(self):
        return (self.mode & S_IFMT) == S_IFDIR

    def is_regular(self):
        return (self.mode & S_IFMT) == S_IFREG


class MinixV1:
    def __init__(self, image):
        self.f = open(image, "r+b")
        self.base = self._find_fs_start()
        self._read_super()
        self._load_bitmaps()

    # ---- mounting ----------------------------------------------------

    def _magic_at(self, byte_off):
        self.f.seek(byte_off + 1024 + 16)
        return struct.unpack("<H", self.f.read(2))[0]

    def _find_fs_start(self):
        """Return the byte offset where the Minix fs starts."""
        if self._magic_at(0) == MINIX_MAGIC:
            return 0                                  # raw fs (floppy image)
        self.f.seek(446)                              # MBR partition table
        for _ in range(4):
            entry = self.f.read(16)
            lba = struct.unpack("<I", entry[8:12])[0]
            if entry[4] == MINIX_PART and self._magic_at(lba * 512) == MINIX_MAGIC:
                return lba * 512                      # partitioned disk image
        sys.exit("error: no Minix V1 filesystem found in image")

    def _read_super(self):
        self.f.seek(self.base + 1024)
        (self.ninodes, self.nzones, self.imap_blocks, self.zmap_blocks,
         self.firstdatazone, self.log_zone_size) = struct.unpack("<HHHHHH", self.f.read(12))
        self.max_size, self.magic = struct.unpack("<IH", self.f.read(6))
        if self.magic != MINIX_MAGIC:
            sys.exit("error: bad superblock magic %#x" % self.magic)
        self.blocksize = 1024 << self.log_zone_size
        self.inode_table = 2 + self.imap_blocks + self.zmap_blocks

    def _load_bitmaps(self):
        self.f.seek(self.base + 2 * self.blocksize)
        self.imap = bytearray(self.f.read(self.imap_blocks * self.blocksize))
        self.zmap = bytearray(self.f.read(self.zmap_blocks * self.blocksize))

    # ---- bitmaps (LSB-first per byte; see set_bit/find_first_zero in
    # fs/bitmap.c -- btsl/bsfl on little-endian) -------------------------

    @staticmethod
    def _bit_get(bm, i):
        return (bm[i >> 3] >> (i & 7)) & 1

    @staticmethod
    def _bit_set(bm, i):
        bm[i >> 3] |= 1 << (i & 7)

    @staticmethod
    def _bit_clear(bm, i):
        bm[i >> 3] &= ~(1 << (i & 7)) & 0xFF

    def alloc_inode_nr(self):
        """First free inode.  Bit 0 is reserved ('inode 0' never exists);
        bit i maps directly to inode i (bitmap.c:130,165)."""
        for i in range(1, self.ninodes + 1):
            if not self._bit_get(self.imap, i):
                self._bit_set(self.imap, i)
                return i
        sys.exit("error: filesystem full: no free inodes")

    def alloc_zone(self):
        """First free data zone.  zmap bit b maps to zone b + firstdatazone
        - 1 (bitmap.c:67,93), so bit 0 covers the reserved pre-data area."""
        for b in range(1, self.nzones):
            if not self._bit_get(self.zmap, b):
                z = b + self.firstdatazone - 1
                if z >= self.nzones:
                    break
                self._bit_set(self.zmap, b)
                return z
        sys.exit("error: filesystem full: no free zones")

    def free_zone(self, z):
        if z < self.firstdatazone or z >= self.nzones:
            sys.exit("error: refusing to free invalid zone %d" % z)
        self._bit_clear(self.zmap, z - self.firstdatazone + 1)

    def free_inode_nr(self, nr):
        self._bit_clear(self.imap, nr)

    def flush_bitmaps(self):
        self.f.seek(self.base + 2 * self.blocksize)
        self.f.write(self.imap)
        self.f.write(self.zmap)

    # ---- low level ---------------------------------------------------

    def block(self, nr):
        self.f.seek(self.base + nr * self.blocksize)
        return self.f.read(self.blocksize)

    def inode(self, nr):
        """Read inode nr (1-based); nr 0 means 'unused slot' in a dirent."""
        off = self.base + self.inode_table * self.blocksize + (nr - 1) * 32
        self.f.seek(off)
        buf = self.f.read(32)
        mode, uid, size, mtime, gid, nlinks = struct.unpack("<HHIIBB", buf[:14])
        zones = list(struct.unpack("<9H", buf[14:32]))
        return Inode(nr, mode, uid, size, mtime, gid, nlinks, zones)

    def write_inode(self, ino):
        off = self.base + self.inode_table * self.blocksize + (ino.nr - 1) * 32
        self.f.seek(off)
        self.f.write(struct.pack("<HHIIBB", ino.mode, ino.uid, ino.size,
                                 ino.mtime, ino.gid, ino.nlinks))
        self.f.write(struct.pack("<9H", *ino.zones))

    def zero_inode(self, nr):
        off = self.base + self.inode_table * self.blocksize + (nr - 1) * 32
        self.f.seek(off)
        self.f.write(b"\x00" * 32)

    def write_block(self, nr, data):
        self.f.seek(self.base + nr * self.blocksize)
        self.f.write(bytes(data).ljust(self.blocksize, b"\x00"))

    def zones_of(self, ino):
        """Yield data zone numbers covering ino.size bytes, following
        zone[7] (single indirect) and zone[8] (double indirect)."""
        n = (ino.size + self.blocksize - 1) // self.blocksize
        yield from ino.zones[:7]
        n -= 7
        if n <= 0:
            return
        ptrs_per_block = self.blocksize // 2
        yield from struct.unpack("<%dH" % ptrs_per_block,
                                 self.block(ino.zones[7])[:ptrs_per_block * 2])[:n] \
            if ino.zones[7] else [0] * min(n, ptrs_per_block)
        n -= ptrs_per_block
        if n <= 0:
            return
        for ind in struct.unpack("<%dH" % ptrs_per_block,
                                 self.block(ino.zones[8])[:ptrs_per_block * 2]):
            if not ind:
                yield from [0] * min(n, ptrs_per_block)
            else:
                yield from struct.unpack("<%dH" % ptrs_per_block,
                                         self.block(ind)[:ptrs_per_block * 2])[:n]
            n -= ptrs_per_block
            if n <= 0:
                return

    def read_data(self, ino):
        """Full contents of a file (None for devices/special inodes)."""
        if not (ino.is_regular() or ino.is_dir()):
            return None
        out = bytearray()
        for z in self.zones_of(ino):
            if z:
                out += self.block(z)
            else:
                out += b"\x00" * self.blocksize          # sparse hole
        return bytes(out[:ino.size])

    # ---- zone chains (direct + zone[7] single + zone[8] double indirect) --

    def data_zone_at(self, ino, k):
        """Zone holding the k-th data block of ino (0 if hole/unmapped)."""
        if k < 7:
            return ino.zones[k]
        k -= 7
        if not ino.zones[7] or k >= 512:
            # fall through to double indirect check below when k >= 512
            if k < 512 or not ino.zones[8]:
                return 0
        if k < 512:
            return struct.unpack("<512H", self.block(ino.zones[7]))[k]
        k -= 512
        if not ino.zones[8]:
            return 0
        dptrs = struct.unpack("<512H", self.block(ino.zones[8]))
        if not dptrs[k // 512]:
            return 0
        return struct.unpack("<512H", self.block(dptrs[k // 512]))[k % 512]

    def append_zone(self, ino, z):
        """Attach freshly allocated zone z as ino's next data block."""
        k = ino.size // self.blocksize
        if k < 7:
            ino.zones[k] = z
        elif k < 7 + 512:
            if not ino.zones[7]:
                ino.zones[7] = self.alloc_zone()
                self.write_block(ino.zones[7], b"")
            blk = bytearray(self.block(ino.zones[7]))
            struct.pack_into("<H", blk, (k - 7) * 2, z)
            self.write_block(ino.zones[7], bytes(blk))
        else:
            sys.exit("error: directory too large (> 512 KB)")

    def free_file_zones(self, ino):
        """Release every block the inode owns: data zones plus the indirect
        blocks themselves (mirrors fs/truncate.c truncate())."""
        blocks = [z for z in ino.zones[:7] if z]
        if ino.zones[7]:
            blocks.append(ino.zones[7])
            blocks += [p for p in struct.unpack("<512H", self.block(ino.zones[7])) if p]
        if ino.zones[8]:
            blocks.append(ino.zones[8])
            for ind in struct.unpack("<512H", self.block(ino.zones[8])):
                if ind:
                    blocks.append(ind)
                    blocks += [p for p in struct.unpack("<512H", self.block(ind)) if p]
        for b in blocks:
            self.free_zone(b)

    def write_file_data(self, ino, data):
        """(Re)write ino's contents with fresh zones, freeing whatever it
        owned before.  Handles grow and shrink uniformly."""
        self.free_file_zones(ino)
        nblocks = (len(data) + self.blocksize - 1) // self.blocksize
        data_zones = []
        for i in range(nblocks):
            z = self.alloc_zone()
            self.write_block(z, data[i * self.blocksize:(i + 1) * self.blocksize])
            data_zones.append(z)
        ino.zones = [0] * 9
        for i, z in enumerate(data_zones[:7]):
            ino.zones[i] = z
        rest = data_zones[7:]
        if rest:
            ind = self.alloc_zone()
            first = rest[:512]
            self.write_block(ind, struct.pack("<512H", *(first + [0] * (512 - len(first)))))
            ino.zones[7] = ind
            rest = rest[512:]
        if rest:
            dind = self.alloc_zone()
            dptrs = []
            while rest:
                chunk, rest = rest[:512], rest[512:]
                ind = self.alloc_zone()
                self.write_block(ind, struct.pack("<512H", *(chunk + [0] * (512 - len(chunk)))))
                dptrs.append(ind)
            self.write_block(dind, struct.pack("<512H", *(dptrs + [0] * (512 - len(dptrs)))))
            ino.zones[8] = dind
        ino.size = len(data)

    # ---- directories ----------------------------------------------------

    def dir_slots(self, ino):
        """Yield (offset, inode_nr, name) for every dirent slot, including
        holes left by deleted entries (inode_nr == 0)."""
        data = self.read_data(ino)
        for off in range(0, len(data), 16):
            nr, name = struct.unpack("<H14s", data[off:off + 16])
            end = name.find(b"\x00")
            yield off, nr, (name[:end] if end >= 0 else name).decode("latin-1")

    def write_dirent(self, dir_ino, offset, nr, name):
        z = self.data_zone_at(dir_ino, offset // self.blocksize)
        if not z:
            sys.exit("error: no zone backing dirent offset %d" % offset)
        blk = bytearray(self.block(z))
        struct.pack_into("<H14s", blk, offset % self.blocksize,
                         nr, name.encode("latin-1").ljust(14, b"\x00"))
        self.write_block(z, bytes(blk))

    def dir_add(self, dir_ino, name, nr):
        """Insert dirent (name -> inode nr) into dir_ino: reuse a hole if
        present, else append (growing the directory by a zone if needed)."""
        hole = None
        for off, n, nm in self.dir_slots(dir_ino):
            if n and nm == name:
                sys.exit("error: %s already exists" % name)
            if n == 0 and hole is None and off >= 32:
                hole = off
        if hole is not None:
            self.write_dirent(dir_ino, hole, nr, name)
        else:
            off = dir_ino.size
            if off % self.blocksize == 0:
                self.append_zone(dir_ino, self.alloc_zone())
            dir_ino.size = off + 32
            self.write_dirent(dir_ino, off, nr, name)
        self.write_inode(dir_ino)

    # ---- namei ---------------------------------------------------------

    def readdir(self, ino):
        """Yield (inode_nr, name) for every used dirent."""
        data = self.read_data(ino)
        for i in range(0, len(data), 16):
            nr, name = struct.unpack("<H14s", data[i:i + 16])
            if nr:
                end = name.find(b"\x00")
                yield nr, (name[:end] if end >= 0 else name).decode("latin-1")

    def lookup(self, path):
        """Resolve a path like /etc/rc; returns Inode or exits with error."""
        stack = []
        for comp in path.split("/"):
            if comp in ("", "."):
                continue
            if comp == "..":
                if not stack:
                    sys.exit("error: %s: invalid path" % path)
                stack.pop()
                continue
            stack.append(comp)
        cur = self.inode(1)                                # root inode
        for comp in stack:
            if not cur.is_dir():
                sys.exit("error: %s: not a directory" % path)
            for nr, name in self.readdir(cur):
                if name == comp:
                    cur = self.inode(nr)
                    break
            else:
                sys.exit("error: %s: no such file or directory" % path)
        return cur

    def walk(self, ino, path=""):
        """Yield (path, inode) for inode and, recursively, everything below."""
        yield path or "/", ino
        if ino.is_dir():
            for nr, name in sorted(self.readdir(ino), key=lambda e: e[1]):
                if name in (".", ".."):
                    continue
                yield from self.walk(self.inode(nr), path + "/" + name)


# ---- commands -----------------------------------------------------------

def mode_string(ino):
    m, s = ino.mode, ino.ftype
    for bit, ch in ((0o400, "r"), (0o200, "w"), (0o100, "x"),
                    (0o040, "r"), (0o020, "w"), (0o010, "x"),
                    (0o004, "r"), (0o002, "w"), (0o001, "x")):
        s += ch if m & bit else "-"
    return s


def cmd_ls(fs, args):
    recursive = "-R" in args
    long_ = "-l" in args
    path = next((a for a in args if not a.startswith("-")), "/")
    ino = fs.lookup(path)

    def line(node, name):
        if long_:
            return "%s %4d %5d %10d  %s  %s" % (
                mode_string(node), node.nlinks, node.uid, node.size,
                time.strftime("%Y-%m-%d %H:%M", time.localtime(node.mtime)), name)
        return name + ("/" if node.is_dir() else "")

    if not recursive:
        if ino.is_dir():
            for nr, name in sorted(fs.readdir(ino), key=lambda e: e[1]):
                print(line(fs.inode(nr), name))
        else:
            print(line(ino, path.rstrip("/").split("/")[-1] or "/"))
    else:
        for p, node in fs.walk(ino, path.rstrip("/")):
            depth = p.count("/") - path.rstrip("/").count("/")
            print("  " * depth + line(node, p.split("/")[-1] or "/"))


def cmd_cat(fs, args):
    ino = fs.lookup(args[0])
    data = fs.read_data(ino)
    if data is None:
        sys.exit("error: %s is a special inode (mode %#o), no data" % (args[0], ino.mode))
    sys.stdout.buffer.write(data)


def cmd_stat(fs, args):
    ino = fs.lookup(args[0])
    print("inode:    %d" % ino.nr)
    print("type:     %s (mode %#o)" % (ino.ftype, ino.mode))
    print("size:     %d" % ino.size)
    print("links:    %d" % ino.nlinks)
    print("uid/gid:  %d/%d" % (ino.uid, ino.gid))
    print("mtime:    %s" % time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ino.mtime)))
    print("zones:    %s" % (list(ino.zones[:7]),))


def cmd_dump(fs, args):
    ino = fs.lookup(args[0])
    data = fs.read_data(ino)
    if data is None:
        sys.exit("error: %s is a special inode, nothing to dump" % args[0])
    dest = args[1] if len(args) > 1 else args[0].rstrip("/").split("/")[-1]
    with open(dest, "wb") as out:
        out.write(data)
    print("dumped %s (%d bytes) -> %s" % (args[0], len(data), dest))


def _split_dest(path):
    """'/hostdir/f.txt' -> ('/hostdir', 'f.txt'); validates the name."""
    dest = "/" + path.strip("/")
    parent, name = dest.rsplit("/", 1)
    if name in ("", ".", ".."):
        sys.exit("error: bad path %r" % path)
    if len(name.encode("latin-1")) > 14:
        sys.exit("error: name %r longer than 14 characters (Minix V1 limit)" % name)
    return parent or "/", name


def cmd_put(fs, args):
    if len(args) != 2:
        sys.exit("usage: minix.py IMAGE put LOCAL DEST")
    with open(args[0], "rb") as fh:
        data = fh.read()
    parent_path, name = _split_dest(args[1])
    parent = fs.lookup(parent_path)
    if not parent.is_dir():
        sys.exit("error: %s: not a directory" % parent_path)
    existing = None
    for _off, nr, nm in fs.dir_slots(parent):
        if nr and nm == name:
            existing = fs.inode(nr)
            break
    if existing is not None:
        if not existing.is_regular():
            sys.exit("error: %s exists and is not a regular file" % args[1])
        ino, created = existing, False          # replace in place, same inode
    else:
        ino, created = Inode(fs.alloc_inode_nr(), 0o100644, 0, 0, 0, 0, 1,
                             [0] * 9), True
    ino.mode, ino.uid, ino.gid, ino.mtime = 0o100644, 0, 0, int(time.time())
    fs.write_file_data(ino, data)
    fs.write_inode(ino)
    if created:
        fs.dir_add(parent, name, ino.nr)
    fs.flush_bitmaps()
    print("put %s -> %s (%d bytes, inode %d, %d zones%s)"
          % (args[0], "/" + args[1].strip("/"), len(data), ino.nr,
             (len(data) + 1023) // 1024, ", replaced" if not created else ""))


def cmd_mkdir(fs, args):
    parent_path, name = _split_dest(args[0])
    parent = fs.lookup(parent_path)
    if not parent.is_dir():
        sys.exit("error: %s: not a directory" % parent_path)
    ino = Inode(fs.alloc_inode_nr(), 0o40755, 0, 32, int(time.time()), 0, 2, [0] * 9)
    z = fs.alloc_zone()
    fs.write_block(z, struct.pack("<H14s", ino.nr, b".") +
                       struct.pack("<H14s", parent.nr, b".."))
    ino.zones[0] = z
    fs.write_inode(ino)
    fs.dir_add(parent, name, ino.nr)
    parent.nlinks += 1                          # child's ".." references us
    fs.write_inode(parent)
    fs.flush_bitmaps()
    print("mkdir %s (inode %d)" % ("/" + args[0].strip("/"), ino.nr))


def cmd_rm(fs, args):
    if args[0].strip("/") == "":
        sys.exit("error: refusing to remove the root directory")
    parent_path, name = _split_dest(args[0])
    parent = fs.lookup(parent_path)
    slot = None
    for off, nr, nm in fs.dir_slots(parent):
        if nr and nm == name:
            slot = (off, nr)
            break
    if slot is None:
        sys.exit("error: %s: no such file or directory" % args[0])
    off, nr = slot
    target = fs.inode(nr)
    if target.is_dir():
        if any(n and nm not in (".", "..") for _o, n, nm in fs.dir_slots(target)):
            sys.exit("error: %s: directory not empty" % args[0])
        fs.free_file_zones(target)
        parent.nlinks -= 1
    elif target.is_regular():
        fs.free_file_zones(target)
    # char/block devices store their device number in zone[0] -- never a
    # real zone, so for those we free nothing but the inode itself.
    fs.free_inode_nr(nr)
    fs.zero_inode(nr)
    fs.write_dirent(parent, off, 0, "")         # leave a reusable hole
    fs.write_inode(parent)
    fs.flush_bitmaps()
    print("removed %s (inode %d)" % ("/" + args[0].strip("/"), nr))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit("usage: python minix.py IMAGE COMMAND [ARGS]")
    fs = MinixV1(sys.argv[1])
    cmd, args = sys.argv[2], sys.argv[3:]
    {"ls": cmd_ls, "cat": cmd_cat, "stat": cmd_stat, "dump": cmd_dump,
     "put": cmd_put, "mkdir": cmd_mkdir, "rm": cmd_rm}[cmd](fs, args)


if __name__ == "__main__":
    main()
