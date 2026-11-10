# PHY PM design checkpoint — implementation intentionally not enabled

## Rejected shortcut

Enabling provider runtime PM before generic PHY creation and letting generic PHY core own all PM references is not a demonstrated equivalent to the vendor host's explicit PHY-provider lifecycle.

Mainline host takes two PHY holds during initialization: the base hold in variant init and a clock hold via setup_clocks. During suspend, the base hold is dropped in the variant callback before HCI-off, while the final clock hold is dropped later after refclk-off. Generic PHY runtime put is asynchronous. During resume, platform clock setup can trigger PHY PM before variant HCI-on. The vendor host instead brackets the whole core runtime suspend/resume with provider put/get. Thus copying only firmware callbacks into generic PHY PM can change ordering, not just reference ownership.

No PHY firmware power path is enabled until ordering and ownership are implemented and reviewed together. Do not change the original MMIO resource size or invent PHY clocks to work around this.

## Preparatory host error propagation

The host slice currently adds checks for failed phy_power_on/off, avoids marking the base PHY hold as powered on when the PHY operation fails, rolls back VA09 enable on PHY-on failure, restores clock/refclk intent on clock-hold power-off failure, and propagates errors from setup_clocks/initialization.

This is necessary groundwork, not a complete transactional PM implementation. Regulator-disable rollback failure, firmware SMC return semantics, unbind/error-path cleanup and core callers' handling of callback errors must still be reviewed. The slice remains separate from the boot-image workflow.

## Evidence sources

- Pinned vendor PHY/host: MiCode/Xiaomi_Kernel_OpenSource commit cb2ab59a621b17aafa4a3f349c84d9f18adf7aaf.
- Linux 6.1 drivers/phy/phy-core.c: phy_power_on/off and phy_create PM registration.
- Linux 6.1 drivers/ufs/host/ufs-mediatek.c: base hold in init, clock hold in pwr_ctrl, variant suspend/resume.

No current OS4 firmware power-order contract or on-device boot test has been obtained.
