# Experimental UFS power wiring — NOT a deployable DTS

Do not apply the following to the shipped DT. The experimental driver intentionally uses a different compatible and does not bind the original `mediatek,mt6985-scpsys` node. Driver code must remain default-disabled. Only domains 2 and 3 are in scope.

## Read-only mapping from the original merged tree

- SPM resource: 0x1c001000, size 0x1000 (original power-controller node).
- Original `ifr_bus` phandle 0x64: syscon at 0x1002c000, size 0x1000. This is NOT the clock-gate infracfg_ao at 0x10001000.
- Original `vlpcfg` phandle 0x6b: syscon at 0x1c00c000, size 0x1000.
- Original `ufscfg_ao_bus` phandle 0x4e: syscon at 0x112b8000, size 0x1000, shared with UFS gate/reset provider.
- Original host uses domain 2; original PHY uses domain 3. Other original consumers must not be redirected to an incomplete provider.

The candidate's explicit phandle property names are `mediatek,infracfg`, `mediatek,vlpcfg`, `mediatek,ufscfg-ao`. The first must point to the original IFR BUS map, despite its generic property name. This naming is experimental, not an upstream binding claim.

## Why no overlay is delivered

The existing scpsys node owns the SPM region and exports other domains. Adding another owner at the same MMIO range is unsafe. Replacing it with a two-domain implementation would leave other devices without their power domains. The PHY firmware power-control SMC may also manipulate the same state; ownership and ordering have not been verified. An integration DTS therefore needs a separate review after full provider dependency closure and firmware PM ownership are established.

No auto-modification of original DT, active partition or firmware is performed. Object compilation and register-model tests cannot establish on-device ownership or physical power sequencing correctness.
