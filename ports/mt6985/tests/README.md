# MT6985 UFS 候选函数用户态执行模型

此测试**实际编译并调用候选驱动的 C 函数**，不是 Python 重写电源算法，也不运行 probe。不需要硬件，不远端执行，不启用候选驱动。

## Linux / CI

仅需 Python 3.9+ 和 GCC（无 pip 依赖），从仓库根目录执行：

```sh
python3 project/gh/ports/mt6985/tests/run_power_model.py
```

如果 CI 的工作目录已经是 `project/gh`：

```sh
python3 ports/mt6985/tests/run_power_model.py
```

脚本默认在本 tests 目录下建立临时构建目录，编译运行后自动清理。任意断言失败、提取边界变化、编译失败或程序非零退出都会使 Python 非零退出。成功输出 9 组 `PASS` 和最终总结果。

保留生成文件或使用其他编译器：

```sh
python3 ports/mt6985/tests/run_power_model.py --build-dir ports/mt6985/tests/.build
CC=gcc python3 ports/mt6985/tests/run_power_model.py
python3 ports/mt6985/tests/run_power_model.py --cc 'gcc -fsanitize=address,undefined'
```

Windows 不要求本地编译；可只生成 C，之后交由 Linux GCC 编译运行：

```sh
python3 ports/mt6985/tests/run_power_model.py --generate-only --build-dir ports/mt6985/tests/.build
gcc -std=gnu11 -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-const-variable -O0 -g ports/mt6985/tests/.build/power_model.c -o ports/mt6985/tests/.build/power_model
ports/mt6985/tests/.build/power_model
```

`--build-dir` 限定为本 tests 目录的子目录。默认候选路径相对脚本定位，不依赖调用者工作目录；`--source PATH` 可指定候选副本，并输出整份候选的 SHA-256 供审查确认。

## 抽取边界与执行证据

生成器读取 `../linux-6.1/drivers/soc/mediatek/mt6985-ufs-power-experimental.c` 全文，从 `#define UFS_RST_B` 开始原样抽取，到 `static int ufs_power_on(` 前结束。保留宏、数组、结构、静态 gate 定义以及 `ufs_update` 到 `ufs_transition` 全部八个函数，**不替换函数体、不删除分支、不修改 gate**。提取器验证函数名和顺序，遇到边界变化直接失败。SHA-256 为源文件身份记录，不是固定版本锁。

生成前还运行 `check_source_contract.py`：以独立硬编码的 vendor bus 表检查 IFR/VLP/AO 地址、位号与顺序，避免候选表和模型同源自证；检查不可绕过的 compile-only probe gate 位于映射/注册之前。此静态检查不执行 probe。

`power_model.c.in` 在一个标记处接入上述原文，提供缺失的内核类型和 mock 原语；断言直接调用提取出的 `ufs_transition()`，后者执行真实候选状态检查、bus/SRAM/ON/RTFF 序列与故障路径。fixture 初始化仅配置 `ufs_power`、两个域和初始寄存器，不复制 transition 算法；初始 PHY OFF fixture 只用于验证恢复 provenance 拒绝。

编译使用 GNU C11（候选 `void *` 地址运算及 poll statement expression），`-O0`，`-Wall -Wextra -Werror`。仅关闭原候选有符号循环比较警告和未调用 probe 遗留的静态 gate 常量 unused 警告，不掩盖其他编译错误。

## 断言范围

1. **host 与 PHY OFF/ON**：两个域完整往返，最终 `ufs_state`、PHY `rtff_saved`、无 fault；同态请求不写。精确检查全部 MMIO 写序列及 bus set 正序 / clear 逆序；包括 SRAM CLKISO → ISOINT_B → PDN、PHY save/restore 脉冲、主/次 ON 顺序、最后才解除 bus。
2. **RTFF provenance 与共享故障**：PHY 初始 OFF 无保存来源时 `-EUCLEAN` 且零写；残留 SAVE 位拒绝；该失败后另一域直接 transition 返回 `-EIO` 且不写。
3. **ready 与混合状态**：未 ready 时两个域均 `-EAGAIN`、无读写 bus / MMIO 写且不 fault；混合 ACK/控制状态拒绝并共享 fault。
4. **电源 ACK/SRAM 超时**：分别冻结 ACK、ACK_2ND、SRAM_ACK，覆盖 host OFF 与 ON 六种超时，必须返回 `-ETIMEDOUT`、fault、不可伪成功；另一域后续回调不得写；SRAM off 失败时不得继续关闭主电源。
5. **bus 故障逆序 rollback**：host 三个 set stage 逐一注入写错误，错误写先产生设备效果再报错，rollback 必须包括失败 stage 并严格逆序，SRAM/电源零写；rollback 自身报错仍继续其他清理，并记录独立错误。另设 `failed_write_no_effect`，令第 3 次 set 与第 5 次写（stage 2 rollback clear）均无效果而报错：六次 bus 写仍完成，SPM 保持 ON、stage 2 enable/status 仍置位，其余 stage 清除，`ufs_state == -EUCLEAN` 且共享 fault，另一域直接回调零新增写。此例明确表明 rollback 不能保证恢复。
6. **bus ACK 与读取错误**：set ACK 不到达必须超时并 rollback；准入 regmap read 失败不得写；set 后 enable read 失败必须 rollback。
7. **enable 重试上限**：前十次 enable 不生效时第十一次成功；十一遍都不生效时 `-EIO` 并 rollback。
8. **BUS_PROT_IGN**：clear 后 status ACK 故意保持为 1，enable 为 0；host/PHY ON 均成功，写序列正确，并检查从第一次 clear 写开始到最终 ON 验证结束从未读取 protection status（ON 开始前的 OFF 准入仍需读取 status）。

9. **ON clear 失败**：host 三个逆序 clear stage、PHY 一个 stage 逐一注入 `-EIO`；均先成功 OFF（建立 PHY 保存来源），再清空轨迹。默认错误写先生效再报错，必须停止后续 clear、不做电源 rollback、保持 shared fault，PHY `rtff_saved` 不清除，另一域直接回调 `-EIO` 且零新增写。另以 `suppress_clear_enable` 让 host/PHY 第一次 clear 的 enable 位不清（写返回成功），单次 readback 后应 `-EIO`，不重试、不继续 clear。所有这些 clear 期间 status 故意保持置位，且从首次 clear 起不得读取 status；预期只是模型中已尝试/未尝试 stage 的 enable 位变化，**不声称硬件真正恢复**。

## 模型假设与非证明范围（父审查必读）

- mock `readl/writel` 仅覆盖两个 SPM 寄存器。ON / ON_2ND / SRAM_PDN 控制位正常时立即驱动相应只读 ACK；指定 ACK 可保持原值以模拟超时。不会证明真实硬件延时、时钟、电压、内存排序或寄存器副作用。
- regmap 的 set/clear 修改 enable，正常时 set 自动置 status；clear 可模拟其他 voter 保持 status。默认写错误先产生完整设备效果再返回错误；`failed_write_no_effect` 则令注入的错误写完全无效果。`suppress_clear_enable` 仅阻止 clear 修改 enable，status 独立由 `hold_clear_status` 控制；这些开关均由 reset 清零。这些模式用于验证不确定失败与 rollback 的局限，不穷举所有部分写组合。记录所有 bus 读写和 MMIO 写，测试按轨迹断言而不是只检查最终状态；`write_count()` 只计 BUS/MMIO 写，排除 READ_BUS 与 DELAY。
- poll 宏最多执行四次真实 mock read，成功条件使用候选原表达式；持续不匹配返回 `-ETIMEDOUT`，regmap 读取错误原样返回。没有模拟一秒墙钟时间或精确轮询周期。
- `udelay` 只记录 DELAY 事件而不等待；断言 ON 主 ON 写后请求 50us、ON SRAM ISOINT_B 写后请求 1us、OFF SRAM ISOINT_B 写后请求 1us，且事件处于正确写事务之间。只验证请求值与调用位置，不验证真实时间间隔。mutex shim 检查单线程成对锁/解锁与无重入，不模拟并发。genpd 只有函数需要的名称字段，`dev_err` 只计数，不验证日志文字。
- **没有执行 Linux genpd core、probe、DT 注册、consumer resume、provider 生命周期或真实锁竞争。** fault 测试仅证明下一次直接进入候选回调被拒绝，不能证明 genpd 在逻辑 ON 的部分失败后一定再次调用回调，不能证明消费者隔离已解决。
- 本模型不是硬件验证，不构成删除 runtime review gate 的理由；候选 compile-only/probe 拒绝政策保持不变。没有改变候选驱动、workflow 或任何 tests 目录外文件。
