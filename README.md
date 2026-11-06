# K60U (Redmi K60 Ultra / MT6985) — Linux 6.1 内核云端构建

本仓库用 **GitHub Actions** 在真正的 Linux 主机上交叉编译 **Linux 6.1 arm64 内核** 与 **K60U 设备树**，并打包成可刷入的 `boot.img`。全流程约 15–25 分钟（模块约 40–60 分钟）。

## 仓库内容

```
configs/k60u_defconfig                  # 已验证可用的 arm64 内核配置（全量 defconfig）
dts/mt6985-corot-k60u.dts               # 从真机固件还原的 K60U 完整设备树（17,032 行）
bootimg/boot_header.bin                 # 原厂 boot.img 的 4096 字节头部模板
scripts/repack_boot.py                  # boot.img 打包脚本（header + Image，无 AVB）
.github/workflows/build-kernel.yml      # 构建流水线
```

## 设备树来源

1. `vendor_boot.img` 的 dtb 段（自身是 DTBO 表）→ MT6985 平台基础树；
2. `dtbo.img` → K60U(corot) 板级 overlay（87 个 fragment）；
3. 用内核自带 `fdtoverlay` 按 `__symbols__` / `__fixups__` 合并 → 反编译成 `mt6985-corot-k60u.dts`；
4. 已在本地产线验证：6.1 源码树内编译通过，产出 533 KB DTB。

## 运行

- **自动**：每次 push 到 `main` 触发；
- **手动**：Actions → *Build K60U Linux 6.1* → *Run workflow*
  - `kernel_version`：`6.1`（或 `6.1.100` 等稳定小版本）
  - `build_modules`：是否同时编译内核模块

产物在 workflow 的 **Artifacts**：`Image`、`System.map`、`mt6985-corot-k60u.dtb`、`boot-6.1-k60u.img`、`.config`（以及可选模块 tar 包）。

## 刷机（需已解锁 BL）

```bash
fastboot flash boot boot-6.1-k60u.img
fastboot --disable-verity --disable-verification flash vbmeta vbmeta.img   # 关闭 AVB 校验
fastboot reboot
```

> `vendor_boot.img` **无需修改**：基础 DTB 与板级 overlay 由 bootloader 在启动时合并，与内核版本无关。

## 重要限制

本项目只解决「**内核能编译、设备树是真的、boot.img 能刷**」这一层。MTK 平台的显示、触摸、充电、摄像头、音频、调制解调器均为**厂商私有驱动**，主线 Linux 6.1 中没有对应实现，因此刷入后：
- 能进入内核引导流程（串口 / 部分通用外设可用）；
- 屏幕、充电等大概率不工作。

要做到「完美运行」，需把厂商 5.15 驱动（`kernel_device_modules`）逐个移植到 6.1 API，属于数月级工程。
