# Executable register-model test policy

The planned test harness compiles the candidate's actual C sequence helpers with user-space MMIO/regmap shims. It does not run a separate Python reimplementation of the driver as evidence of C correctness.

Mandatory distinctions:

- MMIO ACK follows request bits in the model, unless fault injection suppresses a selected acknowledgment. That is a test assumption, not measured MT6985 behavior.
- Regmap write-one-set/write-one-clear semantics and status behavior are modeled. Other firmware voters, real arbitration and timing are not simulated completely.
- A successful callback and correct event order under the model do not validate firmware ownership, voltages, retention contents or physical power sequencing.
- Faulted stops subsequent direct candidate callbacks only. The model must not call that consumer isolation: Linux genpd may bypass callbacks on logical-ON state after partial failure.
- Runtime probe must remain unconditionally disabled with no parameter bypass. No test result authorizes shipping DTS integration or flashing.
- Tests should exercise extracted non-optimized helpers even if the compile-only object's unreachable callbacks are optimized away due to the immutable false probe gate.

Future deployment requires a separate design for reliable power-off failure recovery or consumer isolation covering the core's fast paths, plus hardware validation with a known recovery route.
