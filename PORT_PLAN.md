# MT6985 / K60U 到 Linux 6.1 的适配评估与路线图

> 本文所有数字来自 CI 对官方厂商树的**实测**（workflow `Port MT6985 vendor tree`，产物 `vendor_report.txt`）。

## 一、事实：厂商树有多大

官方源：`MiCode/Xiaomi_Kernel_OpenSource` 分支 **`corot-t-oss`**，内核 **5.15.78**，解压后 **1.4 GB**。

| 目录 | 文件数 | 代码行数 |
|---|---:|---:|
| `drivers/misc/mediatek`（MTK 私有主干） | 2,302 | **1,052,917** |
| `drivers/gpu/drm/mediatek`（显示） | 270 | 236,984 |
| `drivers/clk/mediatek` | 179 | 57,657 |
| `drivers/pinctrl/mediatek` | 54 | 56,383 |
| `drivers/iommu` | 71 | 65,138 |
| `drivers/memory`（EMI/DRAM） | 59 | 43,044 |
| `drivers/soc/mediatek` | 43 | 27,307 |
| `drivers/mailbox`（CMDQ） | 32 | 16,453 |
| `drivers/thermal/mediatek` | 25 | 14,834 |
| `sound/soc/mediatek` | 200 | 148,145 |
| `sound/soc/codecs` | 627 | 612,576 |
| `drivers/media/platform/mtk-{aie,isp,vpu,vcu,jpeg,mdp,vcodec}`（相机/编解码） | — | 约 69+21+… MB |
| **MT6985 直接相关** | 29 个 dts/dtsi、112 个文件名含 mt6985、186 个源文件引用 | — |

补充事实：
- 该分支**没有** `k6985*_defconfig`（24 个 defconfig 里只有 k6853/k6873 等旧平台），`arch/arm64/boot/dts/mediatek/Makefile` 里也**没有** mt6985/corot 条目 → 设备构建胶水在 ODM/`kernel_device_modules` 侧，不在这个 drop 里。
- 主线 6.1 **完全没有 MT6985 支持**（该 SoC 进主线是 6.11 之后的事）：clk/pinctrl/regulator/thermal/drm/iommu/spmi 的 mt6985 驱动都必须移植。

结论：把整棵厂商树直接前移到 6.1，要面对 **100 万行以上** 驱动代码的 API 差异（5.15→6.1 的 drm、clk、iommu、pm、thermal、usb、i2c、mmc 子系统全部有破坏性变更），实测通常产生数千到上万处编译错误。这是 MTK 自己一个 Android 大版本的迭代量级，**不是一次会话能收敛的**。

## 二、两条技术路线

### Track A：主线 6.1 便携启动（可达成，优先做）

目标：**用主线 6.1 内核把 K60U 引导起来**，拿到 UART 日志、可用 USB/adb、可读 eMMC，屏幕用 `simple-framebuffer`（bootloader 已点亮的显存）显示控制台。

依赖的主线已有驱动（6.1 都具备）：
- `8250_of`（MTK UART，compatible `mediatek,mt6577-uart`）
- `mtk-sd`（eMMC）、`mtu3`/`xhci-mtk`（USB）、`mtk-pmic-wrap` + `spmi-mtk`（PMIC 通路）
- `rtc-mt6397`、`mtk_wdt`、`simple-framebuffer`、`arm,psci`、`arm,gic-v3`、`arm,armv8-timer`

需要做的关键工作（难点在设备树，不在内核）：
1. 以真机还原的 `mt6985-corot-k60u.dts` 为底，裁出 **bringup DTS**：只保留 `memory`/`reserved-memory`/`psci`/`gic`/`timer`/`uart*`/`mmc*`/`usb`/`pwrap`/`spmi`/`rtc`/`wdt`/`simple-framebuffer`。
2. **剥离厂商依赖**：目标节点的 `clocks`/`clock-names`/`power-domains`/`mediatek,*` 私有属性必须去掉（否则 6.1 上没有对应 clk/pd provider，设备会永久 defer）。
3. 把 compatible 改成主线认识的（如 `mediatek,mt6577-uart`、`mediatek,mt8183-mmc`）。
4. 内核命令行加 `clk_ignore_unused pd_ignore_unused`（bootloader 已把时钟/电源打开，不驱动也不断电）。
5. 用已编好的 6.1 `Image` + bringup DTB 打包 boot.img 刷机，抓 dmesg。

交付物：可引导的 6.1 + 串口日志 + 「哪些设备起来了」的实测清单。**这是能真正推进的部分。**

### Track B：厂商驱动整体前移（长期）

按子系统推进，每个子系统单独可验证：

| 里程碑 | 内容 | 预估 |
|---|---|---|
| M1 | clk + pinctrl + regulator + pwrap/pmic（mt6363/mt6368） | 2–4 周 |
| M2 | iommu + CMDQ(mailbox) + mmsys/disp 显示通路 | 3–6 周 |
| M3 | 触摸 / 充电(mtk_charger) / typec / USB | 2–3 周 |
| M4 | 音频（sound/soc/mediatek + codec） | 3–5 周 |
| M5 | 相机（mtk-isp/vcu/vpu/jpeg） | 8 周+ |
| M6 | 基带/RF（md1img、connsys，闭源固件+内核胶水） | 视固件而定 |

> 更聪明的 Track B 变体：找一棵**同代 MTK 6.1 厂商树**当骨架（如 mt6989/mt6991 的 Android 14 机型），它已经把 `drivers/misc/mediatek` 的框架适配到 6.1，只需把 mt6985 的 SoC 特定部分（clk/pinctrl/dtsi/regulator 数据表）从 5.15 搬过去。已核查 21 个小米 `-u-oss`/`-t-oss` 分支：`corot-t-oss`=5.15.78、`sheng-u-oss`=5.15.78，其余 6.1 分支（`warm-u-oss` 6.1.68、`bsp-rothko-u-oss`/`chenfeng-u-oss`/`malachite-u-oss`/`peridot-u-oss`/`bsp-degas-u-oss` 6.1.57、`aurora-u-oss`/`bsp-manet-u-oss`/`shennong-u-oss` 6.1.25）**都不含 `drivers/misc/mediatek`**（是骁龙机型的 GKI 树），所以这条路要多找 OPPO/vivo/MTK 的公开 drop。

## 三、已完成 / 进行中

已完成：
- Linux 6.1 `Image`（本地 Cygwin + CI Linux 双通道验证）
- K60U 完整设备树（从 `vendor_boot.img` + `dtbo.img` 还原并合并，17,032 行，编译通过）
- `boot-6.1-k60u.img`（ANDROID! v4）
- 厂商树实测分析（本文数据）

进行中：
- Track A 的 bringup DTS 裁剪与刷机验证材料

## 四、为什么不能承诺「一次做完并完美运行」

1. MT6985 在主线 6.1 零支持，所有 SoC 驱动都要移植；
2. 厂商树 100 万行私有驱动，5.15→6.1 的子系统 API 破坏性变更多；
3. 真机验证需要你在设备上刷机抓日志（我无法物理接触设备）；
4. 相机/基带涉及闭源固件与签名，可能永远无法在自编内核上完全工作。

**能保证的**：内核能编、设备树是真的、boot.img 能刷、Track A 目标（引导 + UART/USB/eMMC + 控制台显示）是可达成的；Track B 按里程碑推进，每个里程碑都能给出实测清单。
