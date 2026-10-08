"""Provenance of the Gemini line-art figures (part tiles, flashing figure, solder inset). Needs gem.py (this folder) and the
reference renders in refs/*.png (not committed). Run from this folder: python3 gemini-jobs.py [job ...]. Exits non-zero if any job fails."""
import subprocess, os, sys
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); R = lambda n: os.path.join(HERE, 'refs', n + '.png')
STYLE = ("Style: Apple quick-start guide illustration, identical to the line style of the LAST reference image: precise isometric line art, thin even dark-grey strokes, "
         "very light grey flat fills, pure white background, no shadows, no gradients, no colour, no text of any kind, no letters, no numbers, no logos, "
         "screens are plain blank rectangles. Keep every object's shape exactly as in its reference image; do not invent parts. ")
CASE = ("The keyring case is PORTRAIT (taller than wide): a translucent faceted front cover with one round hole in the middle, and a black open rear tray "
        "with a closed keyring loop centred on its TOP short edge and a small USB-C slot centred on its BOTTOM short edge. ")
J = {
 'laptop': ("An open laptop on a desk, its screen a blank rectangle with an abstract chat window drawn as grey lines only; a USB-A to USB-C cable (image 1) runs from the laptop's side port out of the right edge of the frame. Nothing else.", ['usb', 'style']),
 'phone': ("A hand holding an iPhone showing a blank app screen with one large round button shape; next to the phone, lying on the table, the finished portrait keyring (image 1 front cover on image 2 rear tray). Curved signal waves between the keyring and the phone.", ['lid', 'rear', 'style']),
 'multimeter': ("A digital multimeter with two probes, red and black drawn as line art; the probe tips touch the two metal contacts of a small white 2-pin JST connector plug at the end of the battery's two leads (image 1). The meter display is a blank rectangle. The battery lies flat, nothing else in frame.", ['battery', 'style']),
 'charge': ("A small fire-proof LiPo-safe charging bag, open, with a small flat lithium pouch battery (image 1) inside on its leads; a USB-C cable goes into the bag; a simple round kitchen timer stands next to the bag.", ['battery', 'style']),
 'stack': ("Exploded assembly view, parts floating in one vertical column, bottom to top: the black rear tray (image 1, portrait, loop centred on top edge) at the bottom with small hex nuts in its corner pockets and a small square vibration motor board lying against the tray floor at one side; above it a thin rectangular foam pad; above it the flat pouch battery (image 3); above it a rectangular circuit board seen only as a flat simple plate; above it a small round button cap (image 4); above it the translucent faceted front cover (image 2, portrait, round centre hole); above that, small screws pointing down. Thin vertical dashed alignment lines. " + CASE, ['rear', 'lid', 'battery', 'button', 'style']),
 'screws': ("The closed portrait keyring case lying face down (rear tray visible, image 1), with a small Phillips screwdriver (PH0) turning one small pan-head screw into a corner hole of the rear tray. " + CASE, ['rear', 'screws', 'style']),
 'ring': ("The finished portrait keyring (translucent faceted front cover from image 1 on the black rear tray from image 2, round button cap in the centre hole) standing upright; a stainless split ring (image 3) is being threaded through the loop centred on the top edge of the rear tray; two house keys hang on the ring. " + CASE, ['lid', 'rear', 'ring', 'style']),
 'multimeter2': ("Close-up: a handheld digital multimeter lies on the table, its display a blank rectangle. Its red probe tip touches the LEFT metal contact and its black probe tip touches the RIGHT metal contact inside the open front of a small white 2-pin JST plug. The plug hangs from the two thin leads (one red, one black) of the flat lithium pouch battery (image 1), which lies beside it. The two probe tips clearly touch the plug's two contacts. No circuit board anywhere.", ['battery', 'style']),
 'stack2': ("Exploded assembly view of the keyring, parts floating in one vertical column above each other, from bottom to top: 1) the black rear tray (image 1) lying flat, its keyring loop on one short end, with four small hex nuts sitting in corner pockets and a small square vibration-motor board with a coin motor lying flat on the tray floor at the end opposite the loop; 2) a thin flat rectangular foam pad; 3) the flat pouch battery (image 3) with its two leads; 4) a flat rectangular circuit board drawn as a simple plate with a few small blocks; 5) a small round button plunger (image 4); 6) the translucent faceted front cover (image 2) with its round centre hole; 7) four small screws pointing down. Thin vertical dashed alignment lines between the parts.", ['rear', 'lid', 'battery', 'button', 'style']),
 'screws2': ("The CLOSED keyring lying face down: the black rear tray (image 1) is on top, fully closed onto the translucent faceted front cover below it, loop at one short end. A small Phillips screwdriver drives one small pan-head screw into a screw hole near a corner of the tray's closed back. The other corner holes already hold screw heads. " + CASE, ['rear', 'lid', 'screws', 'style']),
}
def free(path):
    """An unused file name: full-job-k.png, then full-job-k-2.png, -3 ... (never overwrites a candidate)."""
    base, n = path[:-4], 2
    while os.path.exists(path):
        path = f'{base}-{n}.png'; n += 1
    return path
def run(t):
    job, k = t; prompt, refs = J[job]
    os.makedirs(os.path.join(HERE, 'out'), exist_ok=True)
    out = free(os.path.join(HERE, 'out', f'full-{job}-{k}.png'))
    r = subprocess.run(['python3', os.path.join(HERE, 'gem.py'), 'gemini-3-pro-image-preview', out, prompt + ' ' + STYLE] + [R(x) for x in refs], capture_output=True, text=True)
    ok = r.returncode == 0 and os.path.exists(out)
    return ok, f'{job}-{k}: ' + (r.stdout.strip() or r.stderr.strip()[-200:])
tasks = [(j, k) for j in (sys.argv[1:] or J) for k in (1, 2, 3)]
failed = 0
with ThreadPoolExecutor(4) as ex:
    for ok, line in ex.map(run, tasks):
        print(line if ok else 'FAILED ' + line, flush=True); failed += not ok
sys.exit(1 if failed else 0)
