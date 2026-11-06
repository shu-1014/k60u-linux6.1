#!/bin/bash
# 分析 MTK 厂商树 (corot-t-oss, Linux 5.15.78)，产出移植所需的事实数据
set -uo pipefail
V=${1:-vendor}
OUT=${2:-vendor_report.txt}
exec > >(tee "$OUT") 2>&1

echo "================ MTK 厂商树分析 ================"
echo "date: $(date -u)"
echo "tree: $V"
echo

echo "--- 内核版本 ---"
sed -n '1,6p' "$V/Makefile"
echo

echo "--- 顶层目录体积 ---"
du -sh "$V"/* 2>/dev/null | sort -rh | head -20
echo "总体积: $(du -sh "$V" 2>/dev/null | cut -f1)"
echo

echo "--- MTK 私有驱动目录体积 (top 25) ---"
find "$V/drivers" -maxdepth 3 -type d -name '*mediatek*' -o -maxdepth 3 -type d -name 'mtk*' 2>/dev/null | while read -r d; do
	printf "%s\t%s\n" "$(du -sm "$d" 2>/dev/null | cut -f1)" "$d"
done | sort -rn | head -25
echo

echo "--- 关键 MTK 子系统代码行数 (LOC, *.c/*.h) ---"
for d in \
	drivers/misc/mediatek \
	drivers/gpu/drm/mediatek \
	drivers/clk/mediatek \
	drivers/pinctrl/mediatek \
	drivers/regulator \
	drivers/power/supply \
	drivers/thermal/mediatek \
	drivers/soc/mediatek \
	drivers/iommu \
	drivers/mailbox \
	drivers/memory \
	drivers/mmc/host \
	drivers/i2c/busses \
	drivers/spi \
	drivers/usb \
	drivers/phy/mediatek \
	drivers/devfreq \
	drivers/interconnect \
	drivers/mfd \
	drivers/remoteproc \
	drivers/rpmsg \
	sound/soc/mediatek \
	sound/soc/codecs ; do
	if [ -d "$V/$d" ]; then
		n=$(find "$V/$d" -name '*.c' -o -name '*.h' 2>/dev/null | wc -l)
		l=$(find "$V/$d" \( -name '*.c' -o -name '*.h' \) -exec cat {} + 2>/dev/null | wc -l)
		printf "%-32s %6s files %10s lines\n" "$d" "$n" "$l"
	fi
done
echo

echo "--- MT6985 相关文件数量 ---"
printf "dts/dtsi  : %s\n" "$(find "$V/arch/arm64/boot/dts/mediatek" -name '*6985*' -o -name 'corot*' 2>/dev/null | wc -l)"
printf "c/h 命名  : %s\n" "$(find "$V" -name '*mt6985*' 2>/dev/null | wc -l)"
printf "源码内引用: %s 个文件\n" "$(grep -rl 'MT6985\|mt6985' "$V/drivers" "$V/sound" --include=*.c --include=*.h 2>/dev/null | wc -l)"
echo

echo "--- 厂商 defconfig / build config ---"
ls "$V/arch/arm64/configs" 2>/dev/null | grep -iE 'corot|6985|mtk|gki|debug' | head -20
ls "$V" | grep -E '^build\.config' | head -20
echo

echo "--- DTS Makefile 中 6985/corot 条目 ---"
grep -nE '6985|corot' "$V/arch/arm64/boot/dts/mediatek/Makefile" 2>/dev/null | head -20
echo

echo "--- vendor 对核心的私有改动迹象 (include/linux 下 MTK 头) ---"
ls "$V/include/linux" | grep -iE '^mtk|^mt_|mediatek' | head -30
echo "include/linux 下 MTK 头数量: $(ls "$V/include/linux" | grep -icE '^mtk|mediatek')"
echo

echo "--- 顶层 Kconfig.ext / Android.mk 存在性 ---"
ls -l "$V/Kconfig.ext" "$V/Android.mk" "$V/kenv.mk" 2>/dev/null
echo
echo "================ 分析结束 ================"
