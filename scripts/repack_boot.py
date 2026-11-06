#!/usr/bin/env python3
"""用原厂 boot.img 的 4096 字节头部模板 + 新内核 Image 重新打包 Android boot 镜像 (v3/v4)。

用法: repack_boot.py <boot_header.bin> <Image> <out.img>

说明:
- 保留原头部的 cmdline / os_version / header_version / 地址字段
- kernel_size 改为新内核大小，ramdisk_size 置 0 (GKI: ramdisk 在 init_boot.img)
- 不包含 AVB 签名，刷入前需 `fastboot --disable-verification flash vbmeta`
"""
import struct
import sys

PAGE = 4096


def main(header_path, kernel_path, out_path):
    hdr = bytearray(open(header_path, "rb").read())
    kern = open(kernel_path, "rb").read()

    if hdr[:8] != b"ANDROID!":
        sys.exit("header template is not an Android boot header (magic != 'ANDROID!')")
    if len(hdr) < PAGE:
        sys.exit(f"header template must be at least {PAGE} bytes, got {len(hdr)}")
    if kern[56:60] != b"ARM" + b"\x64":
        print("warning: kernel does not look like an arm64 Image (magic != 'ARM\\x64')")

    header_version = struct.unpack("<I", hdr[40:44])[0]
    if header_version not in (3, 4):
        sys.exit(f"unsupported boot header version {header_version} (need 3 or 4)")

    old_kernel = struct.unpack("<I", hdr[8:12])[0]
    old_ramdisk = struct.unpack("<I", hdr[12:16])[0]
    print(f"header v{header_version}: old kernel={old_kernel} ramdisk={old_ramdisk}")

    struct.pack_into("<I", hdr, 8, len(kern))   # kernel_size
    struct.pack_into("<I", hdr, 12, 0)          # ramdisk_size -> 0 (GKI)
    struct.pack_into("<I", hdr, 24, 0)          # reserved[0..3]
    struct.pack_into("<I", hdr, 28, 0)
    struct.pack_into("<I", hdr, 32, 0)
    struct.pack_into("<I", hdr, 36, 0)

    out = bytes(hdr[:PAGE]) + kern
    out += b"\0" * ((-len(out)) % PAGE)
    open(out_path, "wb").write(out)
    print(f"wrote {out_path}: {len(out)} bytes (kernel {len(kern)})")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3])
