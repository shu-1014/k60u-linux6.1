# RCS init/exit serial API model

`run_rcs_lifetime_model.py` reads the real candidate at
`../linux-6.1/drivers/spmi/spmi-mt6985-experimental.c`, extracts the verbatim
slice beginning `static void mt6985_pmif_rcs_exit(` and ending immediately
before `static int mt6985_pmif_experimental_probe(`, and inserts that slice
into `rcs_lifetime.c.in`. The init/exit functions are not reimplemented.
The template mocks device/platform-device/domain objects, API calls,
READ/WRITE_ONCE, IRQF_ONESHOT, and a never-executed thread-handler symbol.
The EINT register offsets and byte masks are independent literal tables.

## Run

From this directory:

```sh
python3 run_rcs_lifetime_model.py
python3 run_rcs_lifetime_model.py --cc 'clang -fsanitize=address,undefined -fno-omit-frame-pointer'
python3 run_rcs_lifetime_model.py --generate-only --build-dir .rcs-lifetime-generated
python3 run_rcs_lifetime_model.py --cc gcc --build-dir .rcs-lifetime-build
```

Like `run_rcs_model.py`, `--cc` accepts a shell-split compiler command with
optional flags, compilation uses GNU C11 and warning-as-error flags, and
default temporary artifacts live below tests and are automatically removed.
An explicit `--build-dir` must resolve to a **strict descendant of tests**
(not tests itself, an ancestor, or a symlink resolving outside it).
`--generate-only` requires `--build-dir` and performs no compilation/execution.
Assertions must remain enabled; do not pass `-DNDEBUG`.

## Six groups

1. `of_irq_get_byname`: preserve `-EPROBE_DEFER` and other negative values;
   zero becomes `-EINVAL`; no domain allocation, MMIO clear, or IRQ request.
2. NULL domain: `-ENOMEM`, without MMIO clear or request.
3. Failed IRQ request: preserve error; dispose/remove domain without freeing
   an IRQ that was never successfully requested.
4. Successful init: exactly 16 writes, checking each literal offset and byte
   mask, all before the request; check NULL primary handler, thread symbol,
   exact IRQF_ONESHOT flag, IRQ number, name and cookie.
5. Exit after success: free_irq return marker precedes every lookup/disposal
   and domain removal. Sparse SIDs 0, 3, 9, 15 use non-consecutive virqs;
   enable bits are false before lookup and all false at removal.
6. Repeated exits cause no additional free/dispose/remove. A partial
   domain-only fixture is cleaned without free_irq; empty cleanup is a no-op.

The mock records an ordered API-event trace and asserts failed/successful
paths independently of the extracted implementation. The free_irq mock sets
an explicit synchronous-return marker; subsequent mapping APIs require that
marker when a parent IRQ was freed. This is only a serial ordering oracle.
There are **no actual threads**, actual IRQ synchronization, kernel lifecycle
or concurrency claims. READ/WRITE_ONCE are ordinary userspace accesses here.
This does not prove kernel race freedom, safe hardware quiescence, pending
interrupt preservation/replay, controller child removal, suspend/wakeup,
real IRQ-domain behavior, or hardware correctness. No network, flashing or
candidate/workflow modification is performed by this runner.
