#!/usr/bin/env node
// Renders assembly-guide.html to an A4 PDF and a PNG per page, and fails when
// a page overflows, content comes within 6 mm of the footer, a page misses its
// footer number, a figure label is under 9 pt or outside its figure, or an image
// (including rasters nested in SVG figures) or an Inter face does not load.
//
//   node guide/render.mjs            # writes raily-keyring-assembly-guide.pdf and pages/page-NN.png
//
// Needs Playwright (`cd guide && npm i --no-save playwright && npx playwright install chromium`)
// or PLAYWRIGHT_FROM=<folder with node_modules/playwright>; PLAYWRIGHT_CHANNEL=chrome uses installed Chrome.
// The board drawings come from cad/ (see README); the part tiles are in art/.
import { createRequire } from 'node:module';
import { mkdirSync, readdirSync, statSync, unlinkSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
function loadPlaywright() {
  for (const base of [process.env.PLAYWRIGHT_FROM, here, resolve(here, '../../../frontend-visualization')].filter(Boolean)) {
    try { return createRequire(join(base, 'package.json'))('playwright'); } catch { /* next */ }
  }
  console.error('Playwright not found. See the header of this file.'); process.exit(2);
}
const { chromium } = loadPlaywright();
const browser = await chromium.launch(process.env.PLAYWRIGHT_CHANNEL ? { channel: process.env.PLAYWRIGHT_CHANNEL } : {});
const page = await browser.newPage({ deviceScaleFactor: 2 });
await page.goto(pathToFileURL(join(here, 'assembly-guide.html')).href);
await page.evaluate(() => document.fonts.ready);
await page.waitForLoadState('networkidle', { timeout: 30000 });

const check = () => page.evaluate(async () => {
  const mm = 96 / 25.4;
  const pages = [...document.querySelectorAll('.page')];
  // every page except the cover carries a footer whose number is its position
  const nofoot = pages.map((pg, i) => {
    if (pg.classList.contains('cover')) return null;
    const n = pg.querySelector('.foot span:last-child');
    return n && n.textContent.trim() === String(i + 1) ? null : i + 1;
  }).filter(Boolean);
  const crowded = pages.map((pg, i) => {
    const f = pg.querySelector('.foot');
    const over = pg.scrollHeight > pg.clientHeight + 1;
    if (!f) return over ? i + 1 : null;
    const lim = f.getBoundingClientRect().top - 6 * mm;
    const hit = [...pg.querySelectorAll('*')].some((e) => !e.closest('.foot') && e.getBoundingClientRect().height > 0 && e.getBoundingClientRect().bottom > lim + 0.5);
    return over || hit ? i + 1 : null;
  }).filter(Boolean);
  const imgs = [...document.images];
  const broken = imgs.filter((i) => !i.naturalWidth).map((i) => i.getAttribute('src'));
  // rasters nested in inline SVG figures (<image href>): document.images does not list them, so load each one
  const nested = [...document.querySelectorAll('svg image')].map((im) => im.getAttribute('href') || im.getAttributeNS('http://www.w3.org/1999/xlink', 'href'));
  const loads = await Promise.all(nested.map((h) => new Promise((ok) => {
    const t = new Image(); t.onload = () => ok(t.naturalWidth ? null : h); t.onerror = () => ok(h); t.src = h;
  })));
  broken.push(...loads.filter(Boolean).map((h) => (h.startsWith('data:') ? 'an embedded SVG raster' : h)));
  // Labels inside inline SVG figures (the SVGs loaded as <img> carry their own labels; checked at build time).
  const fonts = ['400', '700'].every((w) => [...document.fonts].some((f) => f.family.includes('Inter Guide') && f.weight === w && f.status === 'loaded'));
  return { n: pages.length, crowded, broken, fonts, nofoot };
});
const s1 = await check();
mkdirSync(join(here, 'pages'), { recursive: true });
// drop the previews of an earlier, longer render so pages/ matches this PDF
for (const f of readdirSync(join(here, 'pages'))) if (/^page-\d+\.png$/.test(f)) unlinkSync(join(here, 'pages', f));
for (let i = 0; i < s1.n; i++) await page.locator('.page').nth(i).screenshot({ path: join(here, 'pages', `page-${String(i + 1).padStart(2, '0')}.png`) });
await page.emulateMedia({ media: 'print' });
const s2 = await check();
const pdf = join(here, 'raily-keyring-assembly-guide.pdf');
await page.pdf({ path: pdf, format: 'A4', printBackground: true, preferCSSPageSize: true });

// Figure labels: every <text> in an inline SVG figure must be >= 9 pt at print size and inside its viewBox.
const small = await page.evaluate(() => [...document.querySelectorAll('.page svg')].flatMap((svg) => {
  const vb = svg.viewBox.baseVal; if (!vb || !vb.width) return [];
  const r = svg.getBoundingClientRect(); const k = Math.min(r.width / vb.width, r.height / vb.height);
  return [...svg.querySelectorAll('text')].map((t) => {
    const b = t.getBBox(); const pt = Number(t.getAttribute('font-size')) * k * 0.75;
    const out = b.x < vb.x - 0.5 || b.y < vb.y - 0.5 || b.x + b.width > vb.x + vb.width + 0.5 || b.y + b.height > vb.y + vb.height + 0.5;
    return { t: t.textContent, pt: +pt.toFixed(1), out, label: svg.getAttribute('aria-label') || '' };
  }).filter((x) => x.pt < 9 || x.out).map((x) => `${x.label.slice(0, 40)}: "${x.t}" ${x.out ? 'clipped' : x.pt + ' pt'}`);
}));
await browser.close();

console.log(`${s1.n} pages, ${(statSync(pdf).size / 1024).toFixed(0)} KB`);
const problems = [];
const crowded = [...new Set([...s1.crowded, ...s2.crowded])];
if (crowded.length) problems.push(`pages overflowing or within 6 mm of the footer: ${crowded.join(', ')}`);
if (s1.broken.length) problems.push(`missing images: ${s1.broken.join(', ')}`);
if (!s1.fonts) problems.push('Inter 400 or 700 did not load');
if (s1.nofoot.length) problems.push(`pages without the right footer number: ${s1.nofoot.join(', ')}`);
problems.push(...small.map((x) => `figure label ${x}`));
if (problems.length) { console.error(problems.join('\n')); process.exit(1); }
