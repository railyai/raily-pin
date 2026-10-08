"""Builds assembly-guide.html. Run from the art/ folder (figures are read from the working directory):

    cd hardware/raily-pin-public/guide/art && python3 ../cad/build_full.py ../assembly-guide.html
"""
import os, re, sys
proto = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'proto.html')).read()
css = re.search(r'<style>(.*?)</style>', proto, re.S).group(1)
css += """
@font-face { font-family: 'Inter Guide'; src: url('fonts/Inter-700.woff2') format('woff2'); font-weight: 700; }
.step .art.short { height: 70mm; }
.step .art.tall { height: 175mm; }
.step .art.mid { height: 120mm; }
.row3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 6mm; margin-top: 5mm; }
.row3 .fig { height: 82mm; display: flex; align-items: flex-end; justify-content: center; }
.row3.tall .fig { height: 118mm; }
.row3 p .n, .row2 p .n { display: block; color: #86868b; font-size: 9pt; font-weight: 400; margin: 0 0 1mm; }
.row2.done { grid-template-columns: 1.35fr 1fr; }
.row3.done3 { grid-template-columns: 0.8fr 1.4fr 0.8fr; }
.row3 p, .row2 p { margin: 3mm 0 0; font-size: 10.5pt; line-height: 1.35; }
.row2 { display: grid; grid-template-columns: 1fr 1fr; gap: 8mm; margin-top: 5mm; }
.row2 .fig { height: 112mm; display: flex; align-items: flex-end; justify-content: center; }
.row2.short .fig { height: 92mm; }
.row2.pins-a .fig { height: 86mm; } .row2.pins-b .fig { height: 84mm; } .row2.pins-c .fig { height: 60mm; }
.spools { width: 100%; border-collapse: collapse; margin: 6mm 0 3mm; font-size: 10pt; }
.spools th, .spools td { text-align: left; padding: 1.6mm 2mm; border-bottom: 0.3mm solid #e5e5ea; }
.spools th { font-weight: 600; color: #6e6e73; font-size: 8.8pt; }
.spools .sw { display: inline-block; width: 3.2mm; height: 3.2mm; border-radius: 50%; margin-right: 2mm; vertical-align: -0.4mm; border: 0.2mm solid #c7c7cc; }
.bul { margin: 0; padding-left: 4.5mm; font-size: 9.8pt; line-height: 1.4; color: #3c3c40; } .bul li { margin-bottom: 1.4mm; }
.item span { text-align: center; }
.step .art.wide { width: 100%; height: 76mm; }
.step .art.glance { width: 100%; height: 125mm; margin-top: 6mm; }
.phrase { background: #f5f5f7; border-radius: 4mm; padding: 7mm 8mm; margin: 6mm 0; }
.phrase q { quotes: none; font-size: 17pt; letter-spacing: -0.02em; line-height: 1.3; }
.phrase a { color: #3d5afe; text-decoration: none; }
.note { font-size: 10pt; color: #6e6e73; margin: 1.5mm 0 0; }
.pending-old { height: 70mm; border: 0.4mm dashed #c7c7cc; border-radius: 4mm; display: grid; place-items: center; color: #86868b; font-size: 10pt; }
.info { font-size: 8.4pt; line-height: 1.45; columns: 2; column-gap: 8mm; }
.info h3 { font-size: 9.5pt; margin: 0 0 1.2mm; break-after: avoid; }
.info p, .info ul { margin: 0 0 3.2mm; } .info ul { padding-left: 4mm; } .info li { margin-bottom: 0.8mm; }
.info section { break-inside: avoid; }
.item img[src$='.svg'] { object-fit: contain; }
"""
pages = re.findall(r'<section class="page.*?</section>', proto, re.S)   # cover, box
foot = lambda n: f'<div class="foot"><span>Raily Keyring</span><span>{n}</span></div>'
def page(n, title, body): return f'<section class="page"><h2>{title}</h2>{body}{foot(n)}</section>'
def step(num, art, caption, note='', cls=''):
    n = f'<p class="note">{note}</p>' if note else ''
    return f'<div class="step"><div class="art {cls}">{art}</div><p><span class="n">{num}</span>{caption}</p>{n}</div>'
def img(src, alt):
    if src.endswith('.svg'):
        return re.sub(r'<svg ', '<svg style="width:100%;height:100%" ', open(src).read(), count=1)   # inline, so labels use Inter
    return f'<img src="{src}" alt="{alt}">'
PICK = {'laptop with the USB-C cable': 'step-flash'}
pend = lambda what: f'<img src="{PICK[what]}.jpg" alt="{what}">'

two = lambda a, b: f'<div style="display:grid;grid-template-columns:1.4fr 1fr;gap:4mm;height:100%;align-items:center">{a}{b}</div>'
out = pages[:]
out[0] = out[0].replace('src="cover.jpg" alt="The finished Raily Keyring"', 'src="kc-cover-v1.png" alt="The finished Raily Keyring, V1"')
# box page (2026-09-30, DA7280 edition): eight electronic parts, no soldering, then the case and its hardware
_tiles = [('xiao-sense.jpg', 'XIAO nRF52840 Sense, pre‑soldered'), ('board-tile.svg', 'Expansion Board'), ('da7280.jpg', 'Haptic motor DA7280'),
          ('qwiic-hub.jpg', 'Grove-Qwiic hub'), ('grove-cable-5cm.jpg', 'Grove cable, 5 cm'), ('qwiic-cable.jpg', 'Qwiic cable, 50 mm'),
          ('battery.jpg', 'LiPo battery 602030'), ('usb.jpg', 'USB-C data cable'), ('kcb-closed-tile.png', 'Printed case'),
          ('ring.jpg', 'Split ring, 25 mm'), ('screws-cs.svg', 'M2 × 16 countersunk screws and nuts, 4 each'), ('foam-tape.svg', 'Double-sided foam tape')]
_grid = '<div class="grid">' + ''.join(f'<div class="item"><img src="{f}" alt=""><span>{t}</span></div>' for f, t in _tiles) + '</div>'
out[1], _n = re.subn(r'<div class="grid">.*?</div>(?=<div class="foot">)', _grid + '<p class="note" style="margin-top:4mm">No soldering: the XIAO comes with its pins, and every cable plugs in. Parts not to scale.</p>', out[1], flags=re.S)
assert _n == 1
glance = page(0, "What you're building", '<div class="step"><div class="art glance">' + img('overview.svg', 'The keyring parts connected on the bench, each one named')
              + '</div><p>Everything connected on the bench, before it goes into the case.</p>'
              + '<p class="note">The pages that follow build it in this order: board, motor, firmware, app, battery, case.</p></div>')
out.insert(2, glance)
col = lambda num, art, cap: f'<div><div class="fig">{art}</div><p><span class="n">{num}</span>{cap}</p></div>'
col2 = lambda art, cap: f'<div><div class="fig">{art}</div><p>{cap}</p></div>'
out.append(page(4, 'Onto the board',
    '<div class="row2 pins-b">' + col(1, img('hdr-module.svg', ''), 'The XIAO Sense comes with its pins already soldered. There is no soldering in this build.')
    + col(2, img('hdr-seat.svg', ''), 'Push it into the two inner rows of sockets, all the way down. USB-C at the edge without the screen.') + '</div>'
    + '<p class="note" style="margin-top:6mm">Before the cover goes on, the spring pins under the USB-C end lift the XIAO slightly. That is normal.</p>'))
out.append(page(4, 'Motor and power', step(3, img('motor-chain.svg', 'Grove cable from the UART port to the hub, Qwiic cable from the hub to the motor'), 'Plug the 5 cm Grove cable into the port marked UART and into the hub. Then the Qwiic cable from the hub into the motor.', 'Grove and Qwiic plugs fit one way only. On the bench the hub and the motor lie loose; in the case the hub sits on the screen and the motor on its shelf (see «Into the case»).', 'wide')
               + step(4, img('usb-nobatt.svg', 'USB-C into the XIAO, battery unplugged'), 'Connect USB-C to your computer. No battery yet.', 'Use a cable that carries data. The battery waits until page 8.', 'short')))
out.append(page(5, 'Flash', '<div class="phrase"><p class="note">Open Claude Code, Codex or Cursor and paste:</p><q>Flash my Raily Pin using <a href="https://github.com/railyai/raily-pin">https://github.com/railyai/raily-pin</a></q></div>'
               + step(5, pend('laptop with the USB-C cable'), 'Your AI assistant installs the firmware and checks it.', 'It explains each step and asks before it writes to the board. No assistant? See «Flash by hand» at the back.', 'short')))
out.append(page(6, 'Make it yours', step(6, two(img('app-phone-only.svg', 'The Raily Device app on an iPhone'), img('kcb-closed-plain.svg', 'The keyring')), 'Install the Raily Device app and sign in.', 'Download link: devices.railyai.com/build.', 'short')
               + step(7, img('button-d1.svg', 'The user button on the board edge'), 'Press the button on the board edge. The app binds the keyring to you.', 'Then tap Pair in the app and confirm on the iPhone: without it, presses do not reach the phone. Test on USB: each press shows up once. The motor starts working with the firmware your agent installs.', 'short')))
out.append(page(7, 'Battery', step(8, two(img('check-meter.svg', 'Multimeter reading +3.9 V'), img('check-plug.svg', 'Red probe on contact A')), 'Check + with a multimeter first.', 'Red probe on contact A reads +3.9 V: A is plus. A must meet the + printed by the socket. Reads −3.9 V? The red probe is on minus: move it to the other contact and check for +3.9 V.', 'short')
               + step(9, two(img('battery-jst.svg', 'Battery plug with the red wire on the pin printed +'), img('switch-off.svg', 'Slide switch at OFF')), 'Switch OFF, plug in the battery, switch ON.', 'Red wire to +. Push the plug by its body, never by the wires.', 'short')))
out.append(page(8, 'First charge', step(10, img('charge-pouch.svg', 'Board and battery in a LiPo-safe pouch, USB-C into the XIAO'), 'Watch the first charge for 30 minutes, outside the case.', 'Board and battery in a LiPo-safe bag, the USB-C cable in the XIAO. No smell, no swelling, no heat.', 'mid')))
col = lambda num, art, cap: f'<div><div class="fig">{art}</div><p><span class="n">{num}</span>{cap}</p></div>'
col2 = lambda art, cap: f'<div><div class="fig">{art}</div><p>{cap}</p></div>'
out.append(page(9, 'Print the case',
    '<p class="note" style="margin-top:0">Skip this page if your kit came with a printed case. Four plates: tray (loop and side button included); shelf; the cover plate: face (LED skin included), frame and a dowel rack in three sizes, S, M and L; and the dowel pusher. The pusher (Ø12 × 40 mm, one cup Ø3.4 × 1.5) is an optional helper for pressing the dowels into the face. It is Pastel Periwinkle, like the cover plate: about 2.7 g, about 35 min. Print it once and reuse it for every cover.</p>'
    + '<div class="row2 short">' + col2(img('kcb-colour-v1-iso.svg', 'V1'), '<b>V1.</b> Cotton White body, loop and button. Pastel Periwinkle front.')
    + col2(img('kcb-colour-v5-iso.svg', 'V5'), '<b>V5.</b> Cotton White body. Pastel Periwinkle front, loop and button.') + '</div>'
    + '<table class="spools"><tr><th>Polymaker Panchroma Matte PLA, 1.75 mm</th><th>SKU</th><th>Per case</th></tr>'
    + '<tr><td><span class="sw" style="background:#F4EFEB"></span>Matte Cotton White</td><td>CA04016</td><td>about 21 g, with supports</td></tr>'
    + '<tr><td><span class="sw" style="background:#ADB4E6"></span>Matte Pastel Periwinkle</td><td>CA04036</td><td>about 20 g, dowels and pusher included</td></tr></table>'
    + '<ul class="bul"><li>V1: every plate is one colour: tray and shelf Cotton White, the cover plate Pastel Periwinkle. No colour changes.</li>'
    + '<li>V5: the tray changes colour at the loop and the button, so it needs a two-colour printer.</li></ul>'))
# case B (DA7280), 2026-10-03: cover 18.2-B (owner-verified assembly): dowels into the face first, then the frame; dowel rack S / M / L,
# no test joint, no part codes; figures from case/guide_render_b2.py via case_b2_figs.py
out.append(page(10, 'The cover',
    '<p class="note" style="margin-top:0">The dowels come on one rack in three sizes: S 3.00, M 3.05 and L 3.10 mm, ten of each. Start with S. A dowel that feels loose: swap it for the next size up, M, then L. Dowels always go into the face first: pushed into the frame first, they crack it. Holes 2 and 6 only locate the face: their dowels may sit loose until the frame is on. That is normal; do not swap sizes there.</p>'
    + '<div class="row3 tall">' + col(11, '<div style="display:flex;flex-direction:column;gap:3mm;width:100%;height:100%"><div style="height:24%">' + img('kcb-rack.svg', 'The dowel rack, rows S, M and L') + '</div><div style="flex:1;min-height:0">' + img('kcb-face-dowels.svg', 'Dowels into the face') + '</div></div>', 'Face down, its back up: twist a dowel off the rack and press it into each hole 1–9 until it stops. Your thumb is enough; the pusher can help. Keep the spare dowels.')
    + col(12, img('kcb-frame-on.svg', 'Frame onto the dowels'), 'Frame on, its pillars up, loop end to loop end: its holes over the nine dowels. Press with your palm over each dowel until it sits flat, with no gap at the loop end.')
    + col(13, img('kcb-nuts.svg', 'Nuts into the pillars'), 'Leave the cover face down. Slide an M2 nut into the side slot of each of the four pillars until it lines up with the hole: check with a screw tip.') + '</div>'))
out.append(page(11, 'Into the case', '<p class="note" style="margin-top:0">Before you start: switch OFF, then unplug the battery, the Grove and the Qwiic cables.</p>'
    + '<div class="row3">' + col(14, img('kcb-step-6a.svg', 'Battery into its bay'), 'Battery into its bay, the lead up through the notch toward the board. Never clamp, tape or glue it: it must stay replaceable.')
    + col(15, img('kcb-step-6b.svg', 'Shelf over the battery'), 'Shelf onto its ledges over the battery, its arrow toward the loop.')
    + col(16, img('kcb-step-6c.svg', 'Motor board on the shelf'), 'Motor board onto its seat on the shelf, the round motor up. The ringed socket takes the Qwiic cable.') + '</div>'))
out.append(page(12, 'Board, hub and cover', '<div class="row3 tall">' + col(17, img('kcb-step-7.svg', 'Board'), 'Board onto the four standoffs, USB-C in the end window. Plug in the battery lead, switch still OFF.')
    + col(18, img('kcb-step-8.svg', 'Hub and cables'), 'Hub onto the board’s screen with a square of foam tape. Grove cable from the UART port into the hub, Qwiic cable from the hub into the motor board.')
    + col(19, img('kcb-step-9.svg', 'Cover onto the tray'), 'Turn the cover face up and set it on the tray: USB-C end first, then press the loop end down until the rim sits flush.') + '</div>'))
out.append(page(13, 'Screws and done', '<div class="row3 tall done3">' + col(20, img('kcb-step-10.svg', 'Screws from the back'), 'Turn it over: four M2 × 16 countersunk screws from the back into the pillar nuts. Snug, not tight.')
    + col2(img('kcb-closed.svg', 'The closed keyring'), 'Switch ON with a pen tip through the slot on the other long side. Press the side button to use it. The status light shines through the face near the USB-C end.')
    + col(21, img('kcb-hanging.svg', 'On the split ring'), 'Thread the split ring through the loop.') + '</div>'))
info1 = """
<section><h3>Battery polarity</h3><p>Sellers wire JST-PH plugs differently, so the red wire is not proof. Set the multimeter to V DC and touch the two plug contacts. A positive reading means the red probe is on plus. That contact must line up with the + printed by the board socket. If it does not, stop and do not plug it in. A negative reading means the probes are swapped.</p></section>
<section><h3>Charging</h3><p>Buy the 602030 cell only from a seller that provides its UN38.3 test summary. Charge through USB-C from a CE-marked 5 V charger, never unattended, in a bed or in a pocket. Regular charging only after the charge current is confirmed for your board revision and the cell's datasheet; until then, charge only while you watch. Hot, swollen or smelly? Unplug it and let it cool somewhere safe.</p></section>
<section><h3>Never</h3><ul><li>A battery on the Expansion Board and on the XIAO's battery pads at the same time.</li><li>A swollen or damaged cell.</li><li>A clamped, screwed-down, taped or glued cell.</li><li>A first charge inside the case.</li><li>Soldering onto the battery.</li></ul></section>
<section><h3>Using it</h3><p>Not a toy; keep it from children. Not waterproof. Not an emergency or medical device. At the end of its life, take it to an electrical waste or battery collection point.</p></section>
<section><h3>Print settings</h3><p>Polymaker Panchroma Matte PLA, 0.4 mm nozzle; sliced for a Bambu Lab A1 mini. Tray: bottom on the bed, 0.16 mm layers, grid support under the key loop only, about 1 h. Shelf: fence on the bed with a 5 mm brim, 0.16 mm, grid support under the plate and the ledge, about 20 min. Cover plate, no supports, about 1 h 55 min: the face on its flat back at 0.08 mm with the top ironed; the frame parting face down and the dowels standing on their rack, both at 0.16 mm. Pusher: standing upright, cup end up, 0.16 mm, no supports, about 35 min. Case 49.6 × 89.5 mm, 24.0 mm thick over the front (20.3 at the rim), 97.8 mm long with the loop.</p></section>
"""
info2 = """
<section><h3>Flash by hand</h3><p>Download the firmware and the start-up program update for your board from download.railyai.com/pins (links in the README at github.com/railyai/raily-pin). Check each file’s SHA-256 against releases.json in that repository before you copy it. Press reset twice quickly; a drive appears: XIAO-SENSE on the Sense board, XIAO-BOOT on the plain one. If the first line of INFO_UF2.TXT does not start with «UF2 Bootloader 0.9.2-OTAFIX2.3», copy the update, wait for the drive to return and check again; keep the cable in. Then copy the firmware (.uf2). Full steps: the README at github.com/railyai/raily-pin.</p></section>
<section><h3>Help</h3><p>Questions and build photos: GitHub Discussions in railyai/raily-pin. Firmware bugs: Issues in the same repository. Your account: Help in the app. Never post your device ID (it starts with rp1-).</p></section>
<section><h3>Licences</h3><p>This guide and the shell models: CC BY-NC-SA 4.0. Firmware and flashing tools: Apache License 2.0. Board drawings derived from Seeed Studio 3D models (CC BY-SA 4.0). © 2026 Raily LLC. «Raily» and the Raily logo are trademarks of Raily LLC and are not covered by these licences.</p></section>

"""
out.append(page(11, 'Important information', f'<div class="info">{info1}</div>'))
out.append(page(12, 'Important information', f'<div class="info">{info2}</div>'))
pages_html = ''.join(out)
_n = iter(range(2, 100))
pages_html = re.sub(r'(<div class="foot"><span>Raily Keyring</span><span>)\d+', lambda m: m.group(1) + str(next(_n)), pages_html)
pages_html = re.sub(r'<img src="([a-z0-9-]+\.svg)" alt="[^"]*">', lambda m: img(m.group(1), ''), pages_html)
html = f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Raily Keyring Assembly Guide</title><style>{css}</style></head><body>' + pages_html + '</body></html>'
html = html.replace('<section class="page cover">', '<section class="page cover" data-page="1">')
if len(sys.argv) < 2:
    open('guide.html', 'w').write(html)
else:
    h = re.sub(r'src="([a-z0-9-]+\.(?:jpg|svg|png))"', r'src="art/\1"', html)
    h = re.sub(r'href="([a-z0-9-]+-fill\.png)"', r'href="art/\1"', h)
    open(sys.argv[1], 'w').write(h); print(len(out), 'pages')
