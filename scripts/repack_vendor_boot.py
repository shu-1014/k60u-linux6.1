#!/usr/bin/env python3
"""替换 Android vendor_boot (v4) 里的 DTB 段为自建 DTB。

原厂 dtb 段本身就是一张 DTBO 表（magic 0xd7b7ab1e），这里保持同样格式，
只把里面唯一的 dtb 换成新的，并修正头部尺寸字段。

用法:
  repack_vendor_boot.py <原厂 vendor_boot.img> <新 dtb> <输出 vendor_boot.img>
"""
import struct
import sys

PAGE = 4096
DTBO_MAGIC = 0xD7B7AB1E


def align(n, a=PAGE):
    return (n + a - 1) // a * a


def main(src, dtb_path, dst):
    data = open(src, "rb").read()
    new_dtb = open(dtb_path, "rb").read()

    if data[:8] != b"VNDRBOOT":
        sys.exit("not a vendor_boot image")
    hdr_ver, page_size = struct.unpack("<2I", data[8:16])
    if hdr_ver != 4:
        sys.exit(f"only vendor_boot header v4 supported, got v{hdr_ver}")
    vramdisk_size = struct.unpack("<I", data[24:28])[0]
    header_size, dtb_size = struct.unpack("<2I", data[2096:2104])
    vrt_size, vrt_num, vrt_ent_size = struct.unpack("<3I", data[2112:2124])
    bootconfig_size = struct.unpack("<I", data[2124:2128])[0]
    print(f"orig: header={header_size} vramdisk={vramdisk_size} dtb={dtb_size} "
          f"vrt={vrt_size}/{vrt_num} bootconfig={bootconfig_size}")

    # 原镜像各段偏移
    off_vramdisk = page_size
    off_dtb = off_vramdisk + align(vramdisk_size, page_size)
    off_vrt = off_dtb + align(dtb_size, page_size)
    off_bc = off_vrt + align(vrt_size, page_size)

    vramdisk = data[off_vramdisk:off_vramdisk + vramdisk_size]
    vrt = data[off_vrt:off_vrt + vrt_size]
    bootconfig = data[off_bc:off_bc + bootconfig_size]

    # 校验原 dtb 段确实是 DTBO 表，取出它的 page_size/version 以保持一致
    magic, total, hsz, esz, cnt, eoff, tpage, ver = struct.unpack(">8I", data[off_dtb:off_dtb + 32])
    if magic != DTBO_MAGIC:
        sys.exit(f"unexpected dtb section (magic={magic:#x}), not a DTBO table")
    print(f"dtb section: dtbo table entries={cnt} page={tpage} ver={ver}")

    # 构造新的 DTBO 表（单条目，dtb 紧跟在表项之后）
    entry_off = hsz + esz           # 64
    new_total = entry_off + len(new_dtb)
    sec = bytearray(new_total)
    struct.pack_into(">8I", sec, 0, DTBO_MAGIC, new_total, hsz, esz, 1, hsz, tpage, ver)
    struct.pack_into(">4I", sec, hsz, len(new_dtb), entry_off, 0, 0)
    sec[entry_off:entry_off + len(new_dtb)] = new_dtb
    print(f"new dtb section: {new_total} bytes (dtb {len(new_dtb)})")

    # 头部：更新尺寸字段
    hdr = bytearray(data[:page_size])
    struct.pack_into("<I", hdr, 2100, new_total)          # dtb_size
    struct.pack_into("<I", hdr, 2124, bootconfig_size)    # bootconfig_size 不变

    out = bytearray()
    out += hdr
    out += vramdisk + b"\0" * (align(len(vramdisk), page_size) - len(vramdisk))
    out += bytes(sec) + b"\0" * (align(len(sec), page_size) - len(sec))
    out += vrt + b"\0" * (align(len(vrt), page_size) - len(vrt))
    if bootconfig:
        out += bootconfig + b"\0" * (align(len(bootconfig), page_size) - len(bootconfig))

    open(dst, "wb").write(out)
    print(f"wrote {dst}: {len(out)} bytes (was {len(data)})")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3])
