// Orbit sky behind the harvest map. Starfield wraps. The only body is a
// cropped black hole (horizon + photon ring + inclined accretion disk).
// Never blit a full-sky ImageData of unused zeros — WebKitGTK paints those
// as 16x16 opaque tiles.

const SEED = 0x57A2BE7;
const BASE = "#010103";
const SPD = { neb: 1.2, dust: 3, mid: 8, near: 15 };

const TINTS: Array<[number, string]> = [
  [0.34, "180,200,230"],
  [0.54, "205,218,242"],
  [0.70, "255,210,140"],
  [0.84, "0,229,255"],
  [0.93, "196,168,255"],
  [1.01, "150,235,222"],
];

function mulberry32(seed: number): () => number {
  let a = seed | 0;
  return () => {
    a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function pickTint(r: number): string {
  for (const t of TINTS) if (r < t[0]) return t[1];
  return TINTS[0][1];
}

function puff(
  c: CanvasRenderingContext2D,
  w: number,
  x: number,
  y: number,
  r: number,
  rgb: string,
  a: number,
) {
  for (const xo of [x - w, x, x + w]) {
    if (xo + r < 0 || xo - r > w) continue;
    const g = c.createRadialGradient(xo, y, 0, xo, y, r);
    g.addColorStop(0, `rgba(${rgb},${a})`);
    g.addColorStop(1, "rgba(0,0,0,0)");
    c.fillStyle = g;
    c.fillRect(xo - r, y - r, r * 2, r * 2);
  }
}

function hash2(ix: number, iy: number, seed: number): number {
  let k = Math.imul(ix + 0x1F123BB5, 0x27D4EB2D) ^ Math.imul(iy + 0x68E31DA4, 0x165667B1) ^ seed;
  k = Math.imul(k ^ (k >>> 15), 0x2C1B3C6D);
  return ((k ^ (k >>> 12)) >>> 0) / 4294967296;
}

function valueNoise(x: number, y: number, seed: number): number {
  const ix = Math.floor(x);
  const iy = Math.floor(y);
  const tx = x - ix;
  const ty = y - iy;
  const sx = tx * tx * (3 - 2 * tx);
  const sy = ty * ty * (3 - 2 * ty);
  const a = hash2(ix, iy, seed);
  const b = hash2(ix + 1, iy, seed);
  const c = hash2(ix, iy + 1, seed);
  const d = hash2(ix + 1, iy + 1, seed);
  const top = a + (b - a) * sx;
  const bot = c + (d - c) * sx;
  return top + (bot - top) * sy;
}

function fbm(x: number, y: number, seed: number): number {
  return (
    valueNoise(x, y, seed) * 0.40 +
    valueNoise(x * 2.1, y * 2.1, seed + 11) * 0.24 +
    valueNoise(x * 4.3, y * 4.3, seed + 23) * 0.15 +
    valueNoise(x * 8.7, y * 8.7, seed + 41) * 0.09 +
    valueNoise(x * 17.1, y * 17.1, seed + 67) * 0.07 +
    valueNoise(x * 33.5, y * 33.5, seed + 89) * 0.05
  );
}

type RGB = [number, number, number];

function mix(a: RGB, b: RGB, t: number): RGB {
  const k = Math.max(0, Math.min(1, t));
  return [
    a[0] + (b[0] - a[0]) * k,
    a[1] + (b[1] - a[1]) * k,
    a[2] + (b[2] - a[2]) * k,
  ];
}

function quant(c: RGB, dither: number): RGB {
  const step = 6;
  const d = dither * 0.5;
  return [
    Math.max(0, Math.min(255, Math.round((c[0] + d) / step) * step)),
    Math.max(0, Math.min(255, Math.round((c[1] + d) / step) * step)),
    Math.max(0, Math.min(255, Math.round((c[2] + d) / step) * step)),
  ];
}

function smoothstep(a: number, b: number, t: number): number {
  const x = Math.max(0, Math.min(1, (t - a) / (b - a)));
  return x * x * (3 - 2 * x);
}

type Blit = { cv: HTMLCanvasElement; x: number; y: number };

function makeSoftCanvas(size: number): { cv: HTMLCanvasElement; c: CanvasRenderingContext2D; img: ImageData; D: Uint8ClampedArray } | null {
  const cv = document.createElement("canvas");
  cv.width = size;
  cv.height = size;
  const c = cv.getContext("2d", { alpha: true, willReadFrequently: true });
  if (!c) return null;
  c.clearRect(0, 0, size, size);
  const img = c.createImageData(size, size);
  const D = img.data;
  // Ghost fill so no 16x16 GPU tile is fully transparent black.
  for (let i = 0; i < D.length; i += 4) {
    D[i] = 8;
    D[i + 1] = 10;
    D[i + 2] = 16;
    D[i + 3] = 2;
  }
  return { cv, c, img, D };
}

// Face-on ring. paintVoid squashes Y and rotates it to look like an inclined disk.
function bakeDisk(radius: number, seed: number): HTMLCanvasElement {
  const pad = 2;
  const size = Math.max(8, Math.ceil(radius * 2 + pad * 2));
  const baked = makeSoftCanvas(size);
  const cv = document.createElement("canvas");
  cv.width = size;
  cv.height = size;
  if (!baked) return cv;
  const { c, img, D } = baked;
  const cx = size / 2;
  const cy = size / 2;
  const inner = radius * 0.36;
  const outer = radius;

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const dx = x + 0.5 - cx;
      const dy = y + 0.5 - cy;
      const r = Math.hypot(dx, dy);
      if (r < inner || r > outer) continue;
      const p = (y * size + x) * 4;
      const u = (r - inner) / (outer - inner);
      const ang = Math.atan2(dy, dx);
      const turb = fbm(Math.cos(ang) * 2.4 + u * 3.1, Math.sin(ang) * 2.4, seed);
      const lanes = 0.55 + 0.45 * Math.sin(u * 18 + turb * 6);
      const doppler = 0.42 + 0.58 * (0.5 + 0.5 * Math.cos(ang));
      const heat = Math.pow(1 - u, 1.15) * lanes;
      const innerHot: RGB = [255, 210, 140];
      const mid: RGB = [220, 92, 28];
      const outerCool: RGB = [70, 18, 12];
      let col = mix(innerHot, mid, smoothstep(0.0, 0.38, u));
      col = mix(col, outerCool, smoothstep(0.38, 1, u));
      col = mix(col, [255, 236, 190], heat * doppler * 0.35);
      col = quant(col, (hash2(x, y, seed) - 0.5) * 8);
      const a = Math.round(210 * doppler * (0.55 + 0.45 * heat) * (1 - smoothstep(0.88, 1, u)));
      D[p] = col[0];
      D[p + 1] = col[1];
      D[p + 2] = col[2];
      D[p + 3] = Math.max(3, Math.min(230, a));
    }
  }
  c.putImageData(img, 0, 0);
  return baked.cv;
}

// Horizon, photon ring, and a little gravitational glow. Cropped blit.
function bakeHole(cx: number, cy: number, rs: number): Blit {
  const glow = rs * 1.85;
  const pad = Math.max(4, Math.ceil(glow + 2));
  const size = pad * 2;
  const originX = Math.round(cx) - pad;
  const originY = Math.round(cy) - pad;
  const baked = makeSoftCanvas(size);
  if (!baked) {
    const cv = document.createElement("canvas");
    cv.width = size;
    cv.height = size;
    return { cv, x: originX, y: originY };
  }
  const { cv, c, img, D } = baked;
  const px = cx - originX;
  const py = cy - originY;

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const dx = x + 0.5 - px;
      const dy = y + 0.5 - py;
      const r = Math.hypot(dx, dy) / rs;
      if (r > 1.85) continue;
      const p = (y * size + x) * 4;
      const dither = (hash2(x, y, 0xB10C) - 0.5) * 6;

      if (r < 1.0) {
        // Event horizon — readable silhouette, not a soft blob.
        D[p] = 1;
        D[p + 1] = 1;
        D[p + 2] = 2;
        D[p + 3] = 255;
        continue;
      }

      const ring = 1 - Math.abs(r - 1.22) / 0.055;
      if (ring > 0) {
        const glowCol = mix([255, 232, 176], [255, 252, 236], ring);
        const col = quant(glowCol, dither);
        D[p] = col[0];
        D[p + 1] = col[1];
        D[p + 2] = col[2];
        D[p + 3] = Math.round(235 * ring * ring);
        continue;
      }

      // Lensed light: thin blue-white falloff, kept dim so labels stay readable.
      const lens = 1 - (r - 1.02) / 0.78;
      if (lens > 0) {
        const k = lens * lens * 0.85;
        const n = fbm(dx * 0.08, dy * 0.08, 0x51A7);
        const col = mix([18, 24, 48], [70, 110, 170], n * 0.35);
        D[p] = col[0];
        D[p + 1] = col[1];
        D[p + 2] = col[2];
        D[p + 3] = Math.round(58 * k);
      }
    }
  }
  c.putImageData(img, 0, 0);
  return { cv, x: originX, y: originY };
}

// Far side of the disk, warped over the top and bottom of the hole.
function bakeWarp(cx: number, cy: number, rs: number, seed: number): Blit {
  const pad = Math.max(6, Math.ceil(rs * 2.05));
  const size = pad * 2;
  const originX = Math.round(cx) - pad;
  const originY = Math.round(cy) - pad;
  const baked = makeSoftCanvas(size);
  if (!baked) {
    const cv = document.createElement("canvas");
    cv.width = size;
    cv.height = size;
    return { cv, x: originX, y: originY };
  }
  const { cv, c, img, D } = baked;
  const px = cx - originX;
  const py = cy - originY;

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const dx = x + 0.5 - px;
      const dy = y + 0.5 - py;
      const r = Math.hypot(dx, dy) / rs;
      if (r < 1.04 || r > 1.72) continue;
      const ang = Math.atan2(dy, dx);
      // Poles of the inclined disk: the far side wraps over the hole.
      const pole = Math.pow(Math.abs(Math.sin(ang)), 1.65);
      const band = 1 - Math.abs(r - 1.34) / 0.26;
      if (band <= 0) continue;
      const heat = band * band * (0.18 + 0.82 * pole);
      if (heat < 0.08) continue;
      const p = (y * size + x) * 4;
      const turb = fbm(Math.cos(ang) * 2.2, r * 3.1, seed);
      const innerHot: RGB = [255, 224, 168];
      const mid: RGB = [232, 96, 32];
      const col = quant(mix(mid, innerHot, heat * (0.55 + 0.45 * turb)), (hash2(x, y, seed) - 0.5) * 8);
      D[p] = col[0];
      D[p + 1] = col[1];
      D[p + 2] = col[2];
      D[p + 3] = Math.max(4, Math.min(210, Math.round(200 * heat)));
    }
  }
  c.putImageData(img, 0, 0);
  return { cv, x: originX, y: originY };
}

type Star = { x: number; y: number; s: number; tint: string; tw: number; glint?: boolean; rate?: number };

type Sky = {
  w: number;
  h: number;
  neb: HTMLCanvasElement;
  dust: HTMLCanvasElement;
  spark: Star[];
  mid: Star[];
  near: Star[];
  hole: Blit;
  warp: Blit;
  disk: HTMLCanvasElement;
  hx: number;
  hy: number;
  diskR: number;
};

let sky: Sky | null = null;

function rebuild(w: number, h: number) {
  const rnd = mulberry32(SEED ^ (w * 73856093) ^ (h * 19349663));
  const area = w * h;
  const neb = document.createElement("canvas");
  neb.width = w;
  neb.height = h;
  const nc = neb.getContext("2d");
  const dust = document.createElement("canvas");
  dust.width = w;
  dust.height = h;
  const dc = dust.getContext("2d");
  if (!nc || !dc) {
    sky = null;
    return;
  }

  const bandY = h * 0.38;
  const bandAmp = h * 0.06;
  const bandPh = rnd() * Math.PI * 2;
  const bandHalf = h * 0.12;
  const bandAt = (x: number) => bandY + bandAmp * Math.sin((x / w) * Math.PI * 2 + bandPh);

  const gas: Array<[string, number]> = [
    ["28,90,140", 0.14],
    ["40,70,160", 0.10],
    ["120,50,30", 0.08],
    ["20,110,130", 0.11],
    ["80,40,120", 0.07],
  ];
  for (let i = 0; i < 7; i++) {
    const [rgb, a] = gas[i % gas.length];
    puff(
      nc,
      w,
      rnd() * w,
      h * (0.18 + rnd() * 0.64),
      Math.max(w, h) * (0.14 + rnd() * 0.18),
      rgb,
      a,
    );
  }
  nc.globalCompositeOperation = "lighter";
  for (let i = 0; i < 26; i++) {
    const bx = (i / 26) * w + (rnd() - 0.5) * w * 0.03;
    puff(nc, w, bx, bandAt(bx) + (rnd() - 0.5) * bandHalf * 0.8, bandHalf * (1.1 + 0.7 * rnd()), "28,90,140", 0.02);
  }
  nc.globalCompositeOperation = "source-over";

  const dustN = Math.min(14000, Math.round(area / 620));
  for (let i = 0; i < dustN; i++) {
    const x = rnd() * w;
    const y = rnd() < 0.35
      ? bandAt(x) + (rnd() + rnd() - 1) * bandHalf
      : rnd() * h;
    dc.fillStyle = `rgba(${pickTint(rnd())},${(0.28 + 0.55 * rnd()).toFixed(3)})`;
    dc.fillRect(x, ((y % h) + h) % h, rnd() < 0.82 ? 1 : 2, 1);
  }

  const spark: Star[] = [];
  for (let i = 0, n = Math.min(900, Math.round(area / 2400)); i < n; i++) {
    spark.push({
      x: rnd(),
      y: rnd(),
      s: 1,
      tint: pickTint(rnd()),
      tw: rnd() * 10,
      rate: 500 + rnd() * 1200,
    });
  }
  const mid: Star[] = [];
  const near: Star[] = [];
  const midN = Math.min(620, Math.round(area / 5200));
  const nearN = Math.min(320, Math.round(area / 11000));
  for (let i = 0; i < midN; i++) {
    mid.push({ x: rnd(), y: rnd(), s: rnd() < 0.8 ? 1 : 2, tint: pickTint(rnd()), tw: rnd() * 10 });
  }
  for (let i = 0; i < nearN; i++) {
    near.push({
      x: rnd(),
      y: rnd(),
      s: rnd() < 0.55 ? 1 : 2,
      tint: pickTint(rnd()),
      tw: rnd() * 10,
      glint: rnd() < 0.16,
    });
  }

  const hx = w * 0.22;
  const hy = h * 0.42;
  const rs = Math.max(32, h * 0.12);
  const diskR = Math.max(72, rs * 2.85);
  const hole = bakeHole(hx, hy, rs);
  const warp = bakeWarp(hx, hy, rs, 0xD15C);
  const disk = bakeDisk(diskR, 0xA11CE);

  sky = { w, h, neb, dust, spark, mid, near, hole, warp, disk, hx, hy, diskR };
}

function wrapX(x: number, w: number): number {
  return ((x % w) + w) % w;
}

function stepTwinkle(ph: number): number {
  if (ph > 0.45) return 1;
  if (ph > -0.35) return 0.55;
  return 0.15;
}

function blitWrap(ctx: CanvasRenderingContext2D, img: HTMLCanvasElement, ox: number, oy: number, w: number, h: number) {
  const x = wrapX(ox, w);
  ctx.drawImage(img, x - w, oy, w, h);
  ctx.drawImage(img, x, oy, w, h);
}

export function paintVoid(
  ctx: CanvasRenderingContext2D,
  w: number,
  h: number,
  now: number,
  reduceMotion: boolean,
): void {
  if (!sky || sky.w !== w || sky.h !== h) rebuild(w, h);
  if (!sky) {
    ctx.fillStyle = BASE;
    ctx.fillRect(0, 0, w, h);
    return;
  }
  ctx.imageSmoothingEnabled = false;
  ctx.fillStyle = BASE;
  ctx.fillRect(0, 0, w, h);
  const t = reduceMotion ? 0 : now / 1000;
  const sway = reduceMotion ? 0 : Math.round(5 * Math.sin((t / 41) * Math.PI * 2));
  ctx.globalAlpha = 0.94;
  blitWrap(ctx, sky.neb, t * SPD.neb, sway, w, h);
  ctx.globalAlpha = 0.9;
  blitWrap(ctx, sky.dust, t * SPD.dust, 0, w, h);
  ctx.globalAlpha = 1;
  if (!reduceMotion) {
    for (const s of sky.spark) {
      const lv = stepTwinkle(Math.sin(now / (s.rate || 900) + s.tw));
      if (lv < 0.2) continue;
      ctx.fillStyle = `rgba(${s.tint},${(lv * 0.6).toFixed(3)})`;
      ctx.fillRect(wrapX(s.x * w + t * SPD.dust, w), s.y * h, 1, 1);
    }
    for (const s of sky.mid) {
      const tw = stepTwinkle(Math.sin(now / (900 + s.tw * 300) + s.tw)) * 0.8;
      ctx.fillStyle = `rgba(${s.tint},${tw.toFixed(3)})`;
      ctx.fillRect(wrapX(s.x * w + t * SPD.mid, w), s.y * h, s.s, s.s);
    }
    for (const s of sky.near) {
      const tw = stepTwinkle(Math.sin(now / (900 + s.tw * 300) + s.tw));
      const x = wrapX(s.x * w + t * SPD.near, w);
      const y = s.y * h;
      ctx.fillStyle = `rgba(${s.tint},${tw.toFixed(3)})`;
      ctx.fillRect(x, y, s.s, s.s);
      if (s.glint && tw > 0.9) {
        ctx.fillStyle = `rgba(${s.tint},${(tw * 0.3).toFixed(3)})`;
        ctx.fillRect(x - s.s * 2, y + (s.s >> 1), s.s * 5, 1);
        ctx.fillRect(x + (s.s >> 1), y - s.s * 2, 1, s.s * 5);
      }
    }
  }

  const bx = reduceMotion ? 0 : Math.round(2 * Math.sin((t / 22) * Math.PI * 2));
  const by = reduceMotion ? 0 : Math.round(1 * Math.sin((t / 29) * Math.PI * 2));
  const hx = sky.hx + bx;
  const hy = sky.hy + by;
  const ang = reduceMotion ? 0.42 : 0.42 + t * 0.09;
  ctx.save();
  ctx.imageSmoothingEnabled = false;
  ctx.translate(hx, hy);
  ctx.scale(1, 0.32);
  ctx.rotate(ang);
  ctx.drawImage(sky.disk, -sky.diskR - 2, -sky.diskR - 2);
  ctx.restore();
  ctx.drawImage(sky.hole.cv, sky.hole.x + bx, sky.hole.y + by);
  ctx.drawImage(sky.warp.cv, sky.warp.x + bx, sky.warp.y + by);
}
