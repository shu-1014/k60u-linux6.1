# PMIF callback userspace model

`run_pmif_model.py` extracts the real definitions from
`../linux-6.1/drivers/spmi/spmi-mt6985-experimental.c`, starting at
`MT6985_PMIF_DELAY_US` and ending at the explicit `RCS IRQ implementation begins` marker, before the IRQ helpers. The extracted
fragment includes the macros, enums, register/clock arrays, controller-private
struct, and the exact `mt6985_pmif_wait`, `mt6985_pmif_read_cmd`,
`mt6985_pmif_write_cmd`, and `mt6985_pmif_cmd` function bodies. It is inserted
unchanged into `pmif_model.c.in`; the callbacks are actually executed as C, not
reimplemented as Python or a separate transport algorithm. The runner rejects
unexpected function/declaration boundaries and prints the candidate SHA-256.
No probe, remove, platform registration or real hardware is executed.

## Run on Linux

From this tests directory, with Python 3 and GCC installed:

```sh
python3 run_pmif_model.py
python3 run_pmif_model.py --cc 'gcc -fsanitize=address,undefined -fno-omit-frame-pointer'
python3 run_pmif_model.py --build-dir .pmif-model-build
python3 run_pmif_model.py --generate-only --build-dir .pmif-model-generated
```

`CC` supplies the default compiler command when `--cc` is omitted. Compiler
commands can include flags, so sanitizer compilation and linking use the same
flags. Assertions are the test oracle: do not pass `-DNDEBUG`. Sanitizer runtime
availability is the caller's responsibility. Any compile or executable failure
propagates as a failing runner exit; success reports eight groups.

Without `--build-dir`, generated C and the binary are kept in a temporary tests
subdirectory and removed on exit. An explicit build directory must resolve to a
**strict descendant** of this tests directory, including when symlinks are
involved. `--generate-only` requires that directory and does not compile/run.
Generated artifacts are not candidate or integration changes. `--source PATH`
can select another candidate for a deliberate regression/mutation test; its
printed path and hash identify exactly what was executed.

## Eight groups and independent oracles

1. Read opcodes from all accepted ranges use independent **literal command
   words**, checking type, SID, bytecount and address together. Lengths 1–4
   produce low-byte-first output from literal RDATA `0xe7c35a91`; canaries
   before and after the requested output catch out-of-bounds writes. The trace
   requires IDLE, ACC, bounded busy reads, WFVLDCLR, RDATA, then VLD_CLR.
2. Writes cover lengths 1–4, accepted register/extended ranges and representative
   opcode `0x80`. Independent literal words check command fields and WDATA
   packing (`0x91`, `0x5a91`, `0xc35a91`, `0xe7c35a91`). The trace ends at ACC
   with the model still busy: return zero is **submission only, not completion,
   ACK, NACK status or PMIC success**. The candidate accepts every opcode
   `>= 0x80`; the representative test does not claim stricter opcode validation.
   In particular `0x80` with length 4/address `0x1234` tests the callback's
   permissive encoding behavior, **not a protocol-valid SPMI ZERO_WRITE**.
   The source does not additionally restrict length/address per opcode; these
   vectors are not SPMI protocol-conformance validation.
3. Invalid lengths 0/5, SID 16, invalid read/write opcode boundaries and NULL
   buffers return `-EINVAL` without MMIO or changed read output.
4. Offline and already-failed instances return `-EIO`, with balanced lock
   operations and no MMIO or output changes.
5. Initial IDLE timeouts exercise both read and write callbacks. They set
   `failed`, return `-ETIMEDOUT`, release the lock, never submit anything, and
   block subsequent read/write MMIO.
6. Read WFVLDCLR timeout occurs after ACC, poisons the instance, releases the
   lock, leaves output/canaries untouched, and does not read RDATA, clear valid,
   retry, or allow subsequent callbacks to access MMIO.
7. Non-data RESET through WAKEUP (`0x10..0x13`) for SIDs 0–15 return
   `-EOPNOTSUPP`; invalid bounds return `-EINVAL`. Neither path uses MMIO.
8. A submitted write remains busy. The next transfer must wait for IDLE and
   timeout before changing WDATA/ACC, rather than overwrite the active command.

The expected command words, payload words, register offsets, and FSM encodings
in the shim/oracles are literal values, not computed from extracted candidate
command macros. A changed candidate field encoding therefore cannot silently
change the expectations with it.

## Shim guarantees and limits

Every mocked `readl`/`writel` records operation, offset and value, and asserts the
active controller's raw lock is held. Lock acquisition rejects recursive or
already-held locks; each tested callback checks release/acquisition balance.
The FSM supports IDLE, busy and WFVLDCLR, and asserts legal WDATA/ACC/VLD_CLR
accesses. This verifies callback lock *coverage and release paths in serial
execution*. It does **not** simulate concurrency or prove mutual exclusion under
an actual kernel scheduler, IRQ delivery, SMP, preemption or memory ordering.

`readl_poll_timeout_atomic` is replaced by a bounded deterministic virtual-time
loop: delay 10 / timeout 10000 gives at most 1001 status reads per poll. It
executes the real wait function and condition but does not reproduce Linux
`iopoll.h` wall-clock accounting, its final-read timing, IRQ-off latency, or
hardware response latency. No sleeping or unbounded polling is used.

The FSM is an explicit test assumption, not independently verified MT6985
silicon behavior. Passing cannot establish offsets/provenance, clock/reset
sequencing, firmware ownership, arbitration, ACK/error semantics, write drain,
SPMI-P routing, PMIC child interrupt delivery, lifecycle correctness or safe
removal. It neither opens the runtime approval gate nor changes DT, Kconfig,
Makefiles, CI/workflow configuration, candidate source, boot images or devices.
This model requires no network, flashing, target MMIO or kernel module load.
