# Integrating the Teensy 4.1 core into SONGBEAM (plan and limitations, 2026-11-08)

Goal: replace the Teensy 4.1 breakout module (U8 in MChat_1) with the Teensy's own circuitry on the SongBeam board, in a separate KiCad schematic, so the board no longer depends on the PJRC module.

Status: **no schematic file has been generated.** Reason below.

## What I could not do (limitations)
1. **I have not seen the Teensy 4.1 schematic.** PJRC publishes it only as an image (`https://www.pjrc.com/teensy/schematic41.png`). This session's network blocks that host (fetch refused as not on the allowlist), and I did not work around it. Nothing below is copied from it.
2. **A BGA196 part has hundreds of pin-to-net connections.** From memory I would get some wrong, and a wrong net on a BGA fails silently: ERC passes, the board is fabricated, and it does not boot. I cannot verify against hardware, and I cannot run KiCad ERC here.
3. **No i.MX RT1062 symbol/footprint verified.** I do not know whether your KiCad libraries have the MIMXRT1062 in BGA196 (12 mm, 0.8 mm pitch). A hand-made symbol would need checking pin by pin against the NXP datasheet.
4. **Bootloader chip.** Teensy loads through a separate pre-programmed MKL02Z32 chip that PJRC sells ($6.25 on their bootloader-chip page). It is proprietary firmware; you buy it, you cannot reproduce it. Teensy Loader / Teensyduino compatibility depends on it. Only Winbond "M" flash parts (e.g. W25Q64JVXGIM, $1.20 from PJRC) are listed as working.
5. **Licence and trademark.** I found no licence terms for PJRC's schematic (the page only shows the image, and mentions GPL3 only for a third-party USB host library). Treat copying it into an open-source project as unconfirmed until you check with PJRC. The "Teensy" name is theirs.
6. **No layout feedback.** `MChat_1.kicad_pcb` is empty. A BGA fan-out, crystal placement, DCDC/SNVS decoupling and impedance-controlled USB are layout problems I cannot verify from a schematic.
7. **PJRC's own position:** they recommend following the Teensy 4.0/4.1 schematic and say they cannot give direct technical help with custom PCBs.

## What PJRC documents (from their bootloader-chip page)
- Parts they sell for DIY: IMXRT1062 MCU (BGA196, $14.00), W25Q64JVXGIM flash, MKL02Z32 bootloader chip, DP83825 Ethernet PHY.
- Power: SNVS regulator first; DCDC_PSWITCH needs a clean low-to-high transition with no RC delay.
- 24 MHz crystal required; 32.768 kHz optional; leave ball N3 unconnected.
- Standard PCB process works for the BGA.
- Teensy 4.1 itself: single 3.3 V regulator (TLV75533P in newer revisions), 250 mA recommended external load, VIN and VUSB tied together.

## Suggested scope for SongBeam (smaller than the full Teensy 4.1)
You probably do not need: Ethernet (PHY + magjack), PSRAM pads, most of the 55 I/O breakout. Needed, based on MChat_1:
- RT1062 power domains, decoupling, SNVS/coin-cell RTC
- 24 MHz crystal (+ optional 32.768 kHz)
- QSPI flash + MKL02 bootloader chip + program button
- USB device (and the VIN/VUSB isolation issue)
- SD card (SDIO) for recording
- SAI I2S RX pins for the 4 mic data lines (pin muxing must be checked first, because only some Teensy pins map to RX data)
- UART (GPS), I2C (sensors), a few GPIO (latch, GPS_ON, SENSOR_ON, LEDs)

## Recommended process
1. **You supply the real reference.** Send me `schematic41.png` (download it from the PJRC Teensy 4.1 page) or a PDF; I can read an attached image. Also the NXP IMXRT1060 datasheet and NXP hardware development guide (AN12245) for the ball map.
2. Decide the licence question with PJRC before reusing anything.
3. I build the schematic in **stages as a separate file** (e.g. `Teensy41_Core.kicad_sch`): power/SNVS first, then clock, flash/bootloader, USB, SD, I/O. Each stage gets a pin-by-pin table (ball → net → source page) so you can check it.
4. Run KiCad ERC yourself and compare each ball against the NXP datasheet before ordering anything.
5. Prototype risk: order one assembled test board before committing the mic/GPS design to it. Keep a Teensy 4.1 socket footprint as a fallback.

## Cheaper-risk alternative
Keep the Teensy 4.1 as a socketed module, but fix the integration problems instead: dedicated LDO for MIC_3V3, VIN/VUSB isolation, correct I2S RX pins. That removes most of the audit risk without a BGA design.
