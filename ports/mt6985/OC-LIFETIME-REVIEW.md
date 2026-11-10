# MT6363 OC delayed-work lifetime review

The candidate remains runtime-disabled. This document distinguishes source ownership reasoning from tested kernel behavior.

## IRQ core reference

Pristine 6.1 kernel/irq/manage.c __free_irq removes the action, shuts down the last IRQ action and synchronizes before freeing it. kernel/irq/chip.c irq_shutdown sets the started descriptor depth to1; irq_startup sets it to0. Do not call enable_irq after free_irq to balance a driver's temporary disable: that can enable an unowned interrupt. A shared IRQ would require a separate analysis; this candidate must not request shared OC IRQs.

## Required ordering

Per-instance storage is allocated before regulator registration. Initialize every delayed_work before an OF parse callback can run or any IRQ can be requested. Mutable static tables must not carry work/IRQ/backup state across reprobes or multiple devices.

Before teardown, mark all entries stopping with synchronization shared with the worker's check-and-enable operation. A worker that began before stopping must finish its enable before the stop marker commits. Once marked, no worker may enable an IRQ. Then free/synchronize every requested IRQ, ensuring the handler can no longer queue more work; cancel_delayed_work_sync on all initialized works before devm regulator devices and instance storage disappear. Never hold the worker mutex while free_irq or cancel_delayed_work_sync waits for callbacks using it.

A handler already in progress when stopping begins may queue work, but free_irq synchronization must precede cancellation, and that worker must observe stopping and skip enable. Do not hold private lifetime locks over regulator notifier callbacks. Runtime callback/unbind reentrancy, notifier-triggered removal and actual nested IRQ synchronization remain untested.

## Error admission

NULL parent regmap is an error. EPROBE_DEFER from optional IRQ lookup must be preserved, and unexpected negative IRQ errors must not silently masquerade as an absent resource. Registration and request failures must unwind already active entries. Missing optional resources are allowed only according to the exact platform API's missing-resource errno.

Per-regulator operation backup must return its first failing regmap read rather than combining unrelated negative errno values with bitwise OR. No successful source cleanup or compilation validates protected writes, PMIF completion or real hardware OC behavior.
