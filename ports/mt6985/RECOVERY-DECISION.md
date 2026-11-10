# UFS power-domain error recovery decision

Status: no runtime activation. This is a review record, not hardware recovery advice.

## Linux 6.1 contract verified from source

`drivers/base/power/domain.c` calls `_genpd_power_off()` at 685. On error it returns before assigning `GENPD_STATE_OFF` at 691. `genpd_power_on()` at 718 returns immediately for logical ON domains, bypassing the provider callback. Runtime suspend calls `genpd_power_off()` at 943 without propagating its result and returns zero at 946. Runtime resume at 981 calls the fast-path-capable power-on, then at 994/998 starts and resumes the consumer.

Therefore a provider returning an error after partial physical shutdown is not sufficient to protect a consumer. A permanent callback-local `faulted` flag does not fix a path which never invokes the callback. Never edit `genpd->status` directly or return false success to disguise this.

## Failure classes

| Phase | What is known | Current treatment | Remaining gap |
| --- | --- | --- | --- |
| Read-only admission | No candidate writes yet | Refuse inconsistent state | Concurrent firmware changes still possible |
| Bus-protection SET failure, before SRAM | Candidate touched bus protections only | Reverse clear including uncertain failed step; preserve original error | Clear may fail or other voters may still block traffic; no universal guarantee of old ON access |
| SRAM isolation or SRAM ACK failure | SRAM controls may already be altered | Stop and fault controller | No source-proven rollback from every intermediate SRAM state |
| RTFF SAVE or MTCMOS power loss | Retention/power may be partially changed | Stop and fault controller | Readback does not prove retention contents or recoverability |
| Power-on ACK failure | One power stage may be live while another is not | Stop and fault controller | No tested rollback/ownership policy |
| Power-on final bus CLEAR failure | Power/SRAM may be ON while some protections remain | Stop; preserve error and saved-state bookkeeping | Target is not usable just because both power ACK bits are ON |
| Final state readback failure | State or observation is uncertain | Stop and fault controller | Cannot infer completion or repair from an I/O error |

No phase after a partial write can be labeled fully recovered solely because the model's register image looks consistent. Failed writes may have none, some or all effects, and status/enable readback is not proof of bus accessibility.

## Rejected shortcuts

- Calling the ordinary ON sequence after every OFF error is not proven safe: the first ON sequence step assumes OFF/protected state, retention provenance may not exist, and firmware may own the operation.
- Marking a faulted domain logically OFF to force the next callback violates genpd bookkeeping and does not stop in-flight or other-domain access.
- Merely setting an always-on flag at runtime is not a repair for already-partial shutdown. A separately designed no-power-off bring-up policy could avoid this transition class, but does not implement power management and is not enabled here.
- System panic/reboot on failure is not recovery and is not introduced into a phone kernel without explicit risk review.

## What would permit reconsideration

A source-backed, hardware-tested restoration procedure which returns the domain to the exact prior usable state before callback error return, OR an integrated consumer isolation policy which covers all attach/start/resume and system sleep paths, including genpd logical-ON fast paths and other-domain consumers. Both need firmware ownership agreement and true external UFS quiescence. A register model alone cannot establish those conditions.

The candidate probe remains rejected before mapping or registration, with no boot-parameter bypass. The executable tests provide reproducible failure evidence; they do not approve runtime enablement or a flashable image.
