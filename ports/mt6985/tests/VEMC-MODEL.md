# VEMC setter userspace model

## Run

From the mt6985 port directory:

```sh
python3 tests/run_vemc_model.py
python3 tests/run_vemc_model.py --cc 'clang -fsanitize=address,undefined'
python3 tests/run_vemc_model.py --generate-only --build-dir tests/.vemc-generated
python3 tests/run_vemc_model.py --build-dir tests/.vemc-build
```

`--cc` accepts a compiler command and flags via `shlex.split`, without a shell.
Compilation adds GNU C11, warnings-as-errors, debug information and `-O0`.
`--generate-only` requires `--build-dir`. The resolved build directory must be
strictly below `tests/`; `tests/` itself and paths outside it are rejected.
Without an explicit directory, the runner uses and cleans a temporary directory
under `tests/`. Compiler or executable failure propagates as runner failure.

## What is tested

The runner reads [the candidate regulator source](../linux-6.1/drivers/regulator/mt6363-regulator.c)
and extracts its original text from `static int mt6363_vemc_set_voltage_sel(`
up to, but not including, `static int mt6363_vemc_get_voltage_sel(`. It does not
reimplement the setter. The unlock definition is copied from that source, and
the key/trap register definitions from
[the regulator header](../linux-6.1/include/linux/regulator/mt6363-regulator.h).
These are inserted into [the C template](vemc_model.c.in).
The independent oracle instead uses literal `0x9c9c`, key address `0x39e`, trap
address `0x36`, masks `0x0f`/`0xf0`, and `sel`/`sel << 4`. Thus changing a copied
key definition does not silently change the expected key.

Eight executable groups cover:

1. Trap 0, every selector 0–15, low-nibble selection and preservation of the other nibble.
2. Trap 1, every selector 0–15, high-nibble selection and preservation of the other nibble.
3. Invalid range and mask selectors: `sel=16`, `UINT_MAX`, mask rejection with
   `n_voltages=16, mask=7, sel=8`, and the distinct range case
   `n_voltages=10, mask=15, sel=10`; also a zero-length range. All require zero
   bus events and zero error logs.
4. Illegal traps 2 and `UINT_MAX`: no update, relock attempt, and original
   `-EINVAL` returned even if either relock byte fails.
5. Unlock byte 1 or 2 failure, each combined with successful relock or either
   relock byte failure; the unlock error wins over a relock error.
6. Trap-read failure, including either relock byte failure; no update occurs.
7. Update failure for both traps, including either relock byte failure.
8. Successful operation followed by relock byte 1 or 2 failure: return the
   relock error and issue exactly one mocked `dev_err`.

Normal descriptors use `n_voltages=16, vsel_mask=15`. The descriptor's VSEL
register is a synthetic `0x1234`: the test checks that the setter forwards that
descriptor address, not the physical VEMC register address. Exact ordered traces check
bulk attempts, each byte submission and failure, trap reads, update address,
mask and value, and error logging arguments. Every unlock attempt, including a
first-byte failure and a partially completed second-byte failure, must be
followed by a relock bulk attempt. No later operation may run after an earlier
failure except that cleanup attempt. A successful fake relock zeros both fake
key bytes; a byte-two relock failure still zeros the first fake byte.

## Limits: this is not hardware validation

The fake `regmap_bulk_write` deliberately splits the two-byte bulk request into
two ordered single-byte submissions, stopping at the first error. It can leave
a changed first byte when the second fails. This represents the relevant
`use_single_write` control-flow shape; it is **not the actual Linux regmap
implementation** and is not a test of regmap itself. The mock's immediate
state changes and error codes are test conventions, not PMIC behavior.

The fake read/update and `dev_err` likewise check setter behavior, not PMIF,
SPMI, cache, locking, timing, concurrency, transport error propagation, actual
voltage, or physical key state. In particular the PMIF write return represents
submission, not proof of device completion. A relock attempt or a successful
mock write does **not** establish a hardware relock. No candidate source or
workflow is modified by this model; generation writes only into its chosen
strict `tests/` descendant.
