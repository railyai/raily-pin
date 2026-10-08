# CAD step figures, v0.5: QA checklist

| Figure | Check | Result |
|---|---|---|
| motor-a0.svg | Cable in the A0/D0 shroud: the front-edge right-angle Grove nearest the XIAO (x 143.5–153.5 in the Seeed STEP, opening at the board edge). Motor module on the other end, 4 wires, a Grove plug at each end. | PASS |
| usb-nobatt.svg | USB-C plug in the XIAO receptacle, which overhangs the board edge (x 117.96 < board edge 119.5), away from the OLED. No battery in the scene. | PASS |
| button-d1.svg | Ring on the edge tact switch between UART and the JST: «Button(D1)» on Blueprint 02. | PASS |
| battery-jst.svg | JST-PH 2-pin, opening toward the right board edge (the opening face is +x in the STEP). + on the pin toward the front corner, − toward the GND/5V header, per the Blueprint 02 silkscreen (the CAD has no silkscreen); side by side in `jst-compare.png`. Switch OFF is in the caption only; the knob position isn't identifiable in the model. | PASS (polarity: lead to verify) |

Pins 7 + 7 in the inner rows on every scene. One camera per scene: (-1, 1.1, 1) for the front scenes, (1, 1.1, 1) for the JST.

## Round 2 (lead QA on 8b28a0be2)
- **battery-jst.svg:**
  - «+» and «−» sit as small labels in free space beside the socket, each on a short leader to its own pin window. + is the front, board-corner pin; − is the back one. No leader crosses any geometry.
  - The plug is a JST-PH 2-pin housing (two contact windows, latch ramp, rear wire exits).
  - The red wire exits on the + pin, the black on −.
- **switch-off.svg (new):**
  - Slide switch on the back edge next to the XIAO (the 88.2 mm³ solid in the STEP), in object weight.
  - Per the Blueprint 02 photo, «ON» is printed at the end toward the XIAO and «OFF» at the end toward the I2C Grove. The knob in the CAD sits at the OFF end.
  - Blue arrow toward OFF.
- **Layout:** motor + USB share page 4; the switch sits beside the JST figure.
- **Fonts:** all board figures are inline SVG, so labels render in Inter. render.mjs checks every inline label (minimum 9.1 pt now).

## button-d1 (page 6), lead fix 2026-09-27

- Crop to the board. The motor module is out of frame. The Grove cable is cut where it crosses the right edge (scenes3.py: `stub`), so nothing on a long cable reads as a button.
- The D1 ring stays. The label `button` sits on a leader in the empty top-right corner and passes the render.mjs clipping gate.
- Caption: «Press the button on the board edge.» D1 is side-actuated on the board edge. How the case presses it is listed under «Not final yet» (case design). The case pages do not draw a press-the-centre gesture.
- Page 4 note: «On the bench the motor hangs on its cable. In the case it sits against the tray (page 9).»

## overview (page 3, «What you're building»), owner request 2026-09-27

- One large CAD scene with everything connected on the bench: the board with the seated XIAO, the motor module on its Grove cable in A0/D0, and a LiPo pouch (30 × 24 × 5, modelled) on two wires in the JST socket, with the plug seated (gap 0).
- Seven callouts on blue leaders, each dot on the real part:
  - USB-C · charging: the receptacle mouth at x 119.5.
  - Light: the XIAO RGB LED, the 4-pad 2.6 mm³ part next to the USB-C at (121.4, 7.4, 6.6).
  - Power switch: the 88.2 mm³ slide switch on the back edge.
  - XIAO · the brain.
  - Button: D1 at the board edge.
  - Battery.
  - Vibration motor.
- The render gate passes: labels are ≥ 9 pt and none are clipped. The crop is the geometry box plus room for the callouts (scenes3.py `overview`).
- The footer numbers are renumbered in order by build_full.py. Cross-references moved to page 8 (battery) and page 10 (case tray).

### overview, lead v2 (2026-09-27)

- Callouts: USB-C · charge and data; Light (XIAO RGB LED next to the USB-C, not the OLED); Power switch; XIAO · the brain; OLED screen; Button · side press; Battery socket; Battery; Motor · A0/D0.
- The OLED screen is fused into the board body in Seeed's STEP: its top face is at y −0.6, x 141.9–166.6, z −8.0–5.8. The dot sits at its centre.
- The Battery socket dot is on the JST socket top (172.7, 3.8, 11.0), with the plug seated.
- All labels are vector text at 4.0 units (≥ 9 pt on the page). There is no text inside raster art (the fill PNG has no text).
