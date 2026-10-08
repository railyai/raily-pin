# Vibration motor: DA7280 on software I2C

Source: the 3D printing workspace (`Raily keyring 3D/motor-options.md`), 2026-09-30. This is a short repo copy of its «Update 2026-09-30» section plus the key wiring facts from the earlier research.

## Why the motor changed

- The Seeed Grove Vibration Motor and its 20 cm Grove harness in port A0/D0 are replaced: the upright Grove plug lifted the cover by about 5 mm.
- The owner's rule is now 5 cm max of cable inside the case.

## Parts and wiring (no soldering)

- Motor: SparkFun Qwiic Haptic Driver DA7280 (ROB-17590). LRA, Ø10 mm; board 25.4 × 29.2 mm; I2C address 0x4A; runs on 3.3 V.
- Chain: Expansion Board Grove UART port D6/D7 → Grove cable 5 cm (Seeed 110990036) → Seeed Grove-Qwiic Hub 103020292 (foam tape on the display) → Qwiic cable 50 mm (SparkFun PRT-17260) → DA7280.
- Board: XIAO nRF52840 Sense Pre-Soldered (102010632), so there is no header soldering.
- The DA7280 has two right-angle Qwiic connectors, one on each long edge. Point the input connector toward USB (−x); the second one stays free.
- The Expansion Board's Grove I2C port (D4/D5, the main `Wire`) cannot be used in the case: its sockets face the switch wall, and the board sits 0.3 mm from that wall. The plug won't go in.

## Update 2026-09-30: I2C bus for the DA7280 with a XIAO Sense

- On the Sense, `Wire1` (TWIM1) is taken by the IMU, and the nRF52840 has only two TWIM blocks. So `Wire1.setPins(D6, D7)` would take the bus away from the IMU, which gestures need.
- Decision: D6/D7 run as **software (bit-banged) I2C**, e.g. SoftwareWire or SlowSoftI2CMaster, at about 100 kHz, with the DA7280 at 0x4A. Both hardware TWIMs stay as they are: `Wire` = OLED 0x3C + RTC 0x51, `Wire1` = IMU. Motor commands are rare and short, so software I2C is fast enough.
- The hub has pull-ups and a level shifter, and the DA7280 has its own pull-ups, so no internal pull-ups are needed.
- Pin order: on the Grove UART port the yellow wire is D7 and the white wire is D6; the Grove-to-Qwiic side expects yellow = SCL, white = SDA. So SDA = D6, SCL = D7. Check with a multimeter first; if it is reversed, swap the pins in code, not with an iron.
- Do not start `Serial1`: it uses the same D6/D7 pins.
- I2C addresses do not collide: OLED 0x3C, RTC 0x51, DA7280 0x4A.
