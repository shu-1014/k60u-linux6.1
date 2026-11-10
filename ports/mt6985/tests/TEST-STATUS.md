# Platform validation scope

This directory contains two distinct validation layers:

1. Independent source contracts check vendor-derived offsets/clock identifiers and immutable runtime rejection. These are static checks.
2. User-space C fixtures compile verbatim extracted candidate helpers and execute them against modeled registers. These are not Linux core or hardware integration tests.

Power model: exercise UFS host/PHY register sequencing and failure propagation; it does not solve genpd logical-ON fast-path consumer isolation.

PMIF model: exercise read/write command encoding, byte packing, input rejection, lock pairing and timeout poisoning; it does not prove write completion, real arbitration, clock initialization, IRQ-off latency, or RCS SID delivery.

RCS handler model: execute the actual threaded handler and check literal byte clears, dispatch filters and missing mappings. RCS lifetime model: execute actual init/exit against mocked IRQ/domain APIs with fault injection. The free_irq mock checks ordering, not real kernel synchronization, concurrent unbind, hardware pending replay or regulator delayed-work safety.

All platform candidates remain excluded from the boot-image build. The PMIF, MFD, regulator and power-domain probes reject runtime use before resources are mapped or published. Passing tests do not authorize removing these gates or flashing an image.
