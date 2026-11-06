# Track A 引导验证说明（Linux 6.1 on K60U）

## 这套东西是什么

| 文件 | 作用 |
|---|---|
| `out/ci-linux-host/boot-6.1-k60u.img` | 内核 boot 镜像：Linux 6.1 `Image` + 原厂头部（无 AVB），**推荐用这个** |
| `out/vendor_boot-6.1-bringup.img` | 替换 DTB 为自建 bringup 树（2.4 KB），并追加了 `earlycon`/`clk_ignore_unused` 启动参数 |
| `out/mt6985-corot-k60u-bringup.dtb` | bringup 设备树：`memory`/`psci`/`gic`/`timer`/`cpus`/`ns16550a` UART |
| `out/mt6985-corot-k60u.dtb` | 完整真机设备树（6.1 树内编译），供逐步补驱动时用 |

为什么有两个 boot 相关镜像：这台机器是 GKI 机型，**内核在 `boot`，设备树在 `vendor_boot`**。
原厂 `boot.img` 的 dtb 段是空的（0 字节），所以想让 6.1 用我们的精简设备树，必须同时换 `vendor_boot`。

## bringup 设备树做了什么取舍

| 项 | 真机原值 | bringup 处理 | 原因 |
|---|---|---|---|
| UART | `mediatek,mt6577-uart` + 3 个 clocks | 改 `ns16550a` + `clock-frequency=26MHz`，`reg-shift=2`、`io-width=4` | 完全绕开 MT6985 时钟驱动（6.1 没有），保证 8250_of 能绑定 |
| CPU | 4×A510 + 3×A715 + 1×X3，vendor 私有 `performance-domains` | 只留 `reg`/`compatible`/`enable-method=psci` | 去掉 vendor 的 cpufreq/PD 依赖 |
| 时钟/电源域 | 大量 phandle | **全部剥离** + 启动参数 `clk_ignore_unused pd_ignore_unused` | 6.1 无 mt6985 clk/pd provider，保留会让设备永久 defer |
| 内存 | `0x40000000` 1 GB（原厂 DTB 里的值） | 原样保留 | 实际容量由 LK 按 SKU fixup；若引导后内存只有 1 GB 说明 LK 没 fixup 自建 DTB |
| 显示/触摸/充电/相机/音频/基带 | vendor 节点 | **未包含** | 主线 6.1 无驱动（见 PORT_PLAN.md Track B） |

## 怎么试

### 方案 1（最安全，不改分区）

```bash
# 1) 先只临时启动内核，验证能不能进内核并出日志
fastboot boot boot-6.1-k60u.img
```
`fastboot boot` 不改任何分区，断电重启即恢复原系统。

### 方案 2（需要换设备树才能生效）

```bash
fastboot flash boot     boot-6.1-k60u.img
fastboot flash vendor_boot vendor_boot-6.1-bringup.img
fastboot --disable-verity --disable-verification flash vbmeta vbmeta.img
fastboot reboot
```
> 想回滚：把原厂 `boot.img` / `vendor_boot.img` 再刷回去。

### 日志怎么拿

按可靠性排序：

1. **硬件 UART（最可靠）**：K60U 的调试串口需要拆机焊线，参数 **921600 8N1**，
   我已在启动参数里放了 `console=ttyS0,921600n1` + `earlycon=uart8250,mmio32,0x11001000,921600n1`。
2. **pstore/ramoops**：如果 6.1 panic 后你回到原系统，试
   `adb shell su -c 'cat /sys/fs/pstore/console-ramoops-0'`（需 root）。
3. **fastboot 阶段就停住**：说明 LK 拒绝镜像（AVB 未关或头部字段被校验），此时看
   `fastboot getvar all` 与屏幕提示。

## 预期结果（诚实版）

- **能进内核并打出 `Booting Linux on physical CPU 0x0` / `Linux version 6.1.0`** → Track A 达成，接下来补 eMMC/USB/PMIC；
- 大概率会停在半路（找不到根文件系统、或 earlycon 之后就没了），这属于正常，把日志发我，我按日志往里补节点；
- 屏幕不会亮（`simple-framebuffer` 的显存地址 LK 没有通过 DTB 暴露，需要另找途径）。

## 回滚清单

```bash
fastboot flash boot        <原厂 boot.img>
fastboot flash vendor_boot <原厂 vendor_boot.img>
fastboot flash vbmeta      <原厂 vbmeta.img>
```
