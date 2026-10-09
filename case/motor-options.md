# Vibration motor: DA7280 on software I2C

Source: the 3D printing workspace (`Raily keyring 3D/motor-options.md`), 2026-09-30. This is a short repo copy of its «Update 2026-09-30» section plus the key wiring facts from the earlier research.

## Why the motor changed

- The Seeed Grove Vibration Motor and its 20 cm Grove harness in port A0/D0 are replaced: the upright Grove plug lifted the cover by about 5 mm.
- The owner's rule was then 5 cm max of cable inside the case. On 2026-10-09 the owner lifted it for the motor cable: the 100 mm Grove-to-Qwiic cable fits in the case (see below).

## Parts and wiring (no soldering)

- Motor: SparkFun Qwiic Haptic Driver DA7280 (ROB-17590). LRA, Ø10 mm; board 25.4 × 29.2 mm; I2C address 0x4A; runs on 3.3 V.
- Chain (owner, 2026-10-09): Expansion Board Grove UART port D6/D7 → one Grove-to-Qwiic cable, 100 mm (Adafruit 4528) → DA7280. One cable with a plug at each end; nothing else.
  - In the case: the Grove end into the UART port, the Qwiic end into the DA7280's J1. The spare length lies in a loose loop over the board (the owner's fit test, 2026-10-02: over the screen, not down toward the XIAO). Nothing is stuck to the display.
  - 2026-09-30 to 2026-10-08 the chain was Grove cable 5 cm (Seeed 110990036) → Seeed Grove-Qwiic Hub 103020292 (foam tape on the display) → Qwiic cable 50 mm (SparkFun PRT-17260). The hub, both short cables and the foam tape are out of the kit.
- Board: XIAO nRF52840 Sense Pre-Soldered (102010632), so there is no header soldering.
- The DA7280 has two right-angle Qwiic connectors, one on each long edge. Point the input connector toward USB (−x); the second one stays free.
- The Expansion Board's Grove I2C port (D4/D5, the main `Wire`) cannot be used in the case: its sockets face the switch wall, and the board sits 0.3 mm from that wall. The plug won't go in.

## Update 2026-09-30: I2C bus for the DA7280 with a XIAO Sense

- On the Sense, `Wire1` (TWIM1) is taken by the IMU, and the nRF52840 has only two TWIM blocks. So `Wire1.setPins(D6, D7)` would take the bus away from the IMU, which gestures need.
- Decision: D6/D7 run as **software (bit-banged) I2C**, e.g. SoftwareWire or SlowSoftI2CMaster, at about 100 kHz, with the DA7280 at 0x4A. Both hardware TWIMs stay as they are: `Wire` = OLED 0x3C + RTC 0x51, `Wire1` = IMU. Motor commands are rare and short, so software I2C is fast enough.
- The DA7280 has its own I2C pull-ups, so no internal pull-ups are needed. Both sides run at 3.3 V, so no level shifter is needed. The firmware is the same as with the hub.
- Pin order (checked against the sources, 2026-10-09): SCL = D7, SDA = D6.
  - The Grove UART port carries D7 on pin 1 (yellow) and D6 on pin 2. A Grove UART plug is labelled from the base board: pin 1 = RX, pin 2 = TX ([Seeed Grove System](https://wiki.seeedstudio.com/Grove_System/)). The XIAO nRF52840 core puts RX on D7 and TX on D6 (`PIN_SERIAL1_RX 7`, `PIN_SERIAL1_TX 6` in [variant.h](https://github.com/Seeed-Studio/Adafruit_nRF52_Arduino/blob/master/variants/Seeed_XIAO_nRF52840_Sense/variant.h)). Zephyr's [shield page](https://docs.zephyrproject.org/latest/boards/shields/seeed_xiao_expansion_board/doc/) for the board agrees: pin 6 = Grove UART TX, pin 7 = Grove UART RX.
  - The Adafruit 4528 is a Grove I2C cable: Grove pin 1 = SCL, pin 2 = SDA. Its wires: yellow = SCL, **blue** = SDA, red = V+, black = GND ([Adafruit 4528](https://www.adafruit.com/product/4528)). It has no white wire: on this cable D6 / SDA is the blue one.
  - So yellow = D7 = SCL and blue = D6 = SDA. The firmware uses `RAILY_HAPTIC_SDA_PIN D6`, `RAILY_HAPTIC_SCL_PIN D7`. Its boot probe also tries the swapped order once and says which order answered.
- Do not start `Serial1`: it uses the same D6/D7 pins.
- I2C addresses do not collide: OLED 0x3C, RTC 0x51, DA7280 0x4A.
