// Orbit sky behind the harvest map. Dense colored starfield plus pinned
// planet discs (Earth-like left, ice moon right). Bodies do not wrap.
// Stars still wrap. No accretion disc.

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

type PlanetKind = "terra" | "moon";

function smoothstep(a: number, b: number, t: number): number {
  const x = Math.max(0, Math.min(1, (t - a) / (b - a)));
  return x * x * (3 - 2 * x);
}

function fbmCloud(x: number, y: number, seed: number): number {
  return (
    valueNoise(x * 1.3, y * 1.3, seed) * 0.38 +
    valueNoise(x * 2.8, y * 2.8, seed + 13) * 0.26 +
    valueNoise(x * 5.9, y * 5.9, seed + 31) * 0.18 +
    valueNoise(x * 12.1, y * 12.1, seed + 53) * 0.10 +
    valueNoise(x * 24.0, y * 24.0, seed + 71) * 0.08
  );
}

type PlanetBlit = { cv: HTMLCanvasElement; x: number; y: number };

// Crop to the disc. A full-sky ImageData upload leaves 16x16 GPU tiles in empty
// sky on WebKitGTK when the unused pixels stay 0,0,0,0.
function bakePlanet(
  cx: number,
  cy: number,
  radius: number,
  kind: PlanetKind,
  seed: number,
): PlanetBlit {
  const atmosThick = kind === "terra" ? 0.12 : 0.04;
  const pad = Math.max(4, Math.ceil(radius * (1 + atmosThick) + 2));
  const size = pad * 2;
  const originX = Math.round(cx) - pad;
  const originY = Math.round(cy) - pad;
  const cv = document.createElement("canvas");
  cv.width = size;
  cv.height = size;
  const c = cv.getContext("2d", { alpha: true });
  if (!c) return { cv, x: originX, y: originY };
  c.clearRect(0, 0, size, size);
  const img = c.createImageData(size, size);
  const D = img.data;
  const lx = 0.58;
  const ly = -0.32;
  const lz = 0.74;
  const llen = Math.hypot(lx, ly, lz);
  const Lx = lx / llen;
  const Ly = ly / llen;
  const Lz = lz / llen;
  const pxCx = cx - originX;
  const pxCy = cy - originY;

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const nx = (x - pxCx) / radius;
      const ny = (y - pxCy) / radius;
      const rr = nx * nx + ny * ny;
      if (rr > (1 + atmosThick) * (1 + atmosThick)) continue;
      const p = (y * size + x) * 4;
      const dither = (hash2(x, y, seed) - 0.5) * 10;

      if (rr > 1) {
        // atmosphere rim glow
        const dist = Math.sqrt(rr) - 1;
        const fall = 1 - dist / atmosThick;
        if (fall <= 0) continue;
        const curve = fall * fall;
        if (kind === "terra") {
          const rimR = Math.round(mix([50, 130, 220], [90, 180, 255], curve)[0]);
          const rimG = Math.round(mix([50, 130, 220], [90, 180, 255], curve)[1]);
          const rimB = Math.round(mix([50, 130, 220], [90, 180, 255], curve)[2]);
          D[p] = rimR;
          D[p + 1] = rimG;
          D[p + 2] = rimB;
          D[p + 3] = Math.round(140 * curve);
        } else {
          D[p] = 140;
          D[p + 1] = 135;
          D[p + 2] = 128;
          D[p + 3] = Math.round(40 * curve);
        }
        continue;
      }

      const nz = Math.sqrt(Math.max(0, 1 - rr));
      const ndl = Math.max(0, nx * Lx + ny * Ly + nz * Lz);
      const limb = Math.pow(nz, 0.38);
      const lon = Math.atan2(nx, nz);
      const lat = Math.asin(Math.max(-1, Math.min(1, ny)));
      const u = (lon / Math.PI + 1) * 2.8;
      const v = (lat / Math.PI + 0.5) * 3.6;
      const n1 = fbm(u, v, seed);
      const n2 = fbm(u * 1.7 + 4, v * 1.7, seed + 7);

      let col: RGB;
      if (kind === "terra") {
        const iceLat = Math.abs(lat);
        const ice = iceLat > 1.05 || (iceLat > 0.78 && n1 > 0.38);
        const coastLine = smoothstep(0.44, 0.52, n1);
        const land = n1 > 0.48;
        if (ice) {
          col = mix([178, 198, 222], [238, 244, 252], n2 * 0.6 + n1 * 0.4);
        } else if (land) {
          const elev = smoothstep(0.48, 0.72, n1);
          const moisture = n2;
          const desert: RGB = mix([148, 128, 78], [178, 158, 98], n1);
          const forest: RGB = mix([28, 82, 38], [68, 118, 48], n1);
          const highland: RGB = mix([108, 98, 78], [148, 138, 108], n1);
          const base = moisture > 0.52 ? mix(forest, highland, elev) : mix(desert, highland, elev);
          col = mix(base, [218, 210, 195], elev * 0.25);
        } else {
          const depth = smoothstep(0.48, 0.2, n1);
          const shallow: RGB = [18, 78, 138];
          const deep: RGB = [6, 28, 72];
          col = mix(shallow, deep, depth);
          const shore = smoothstep(0.44, 0.48, n1);
          col = mix(col, [42, 128, 168], shore * 0.3);
        }
        // Clouds: separate noise, semi-transparent whites
        const cn = fbmCloud(u + 2.5, v + 1.2, seed + 200);
        const cloudAlpha = smoothstep(0.42, 0.68, cn);
        if (cloudAlpha > 0 && !ice) {
          const cloudBright: RGB = [235, 240, 248];
          const cloudShadow: RGB = [195, 205, 218];
          const cloudCol = mix(cloudShadow, cloudBright, ndl * 0.7 + 0.3);
          col = mix(col, cloudCol, cloudAlpha * 0.65);
        }
        // Day/night with softer terminator
        const terminator = smoothstep(-0.08, 0.22, ndl);
        const night: RGB = mix([3, 6, 14], [12, 20, 38], n1 * 0.3);
        const day = mix(col, [255, 242, 210], ndl * 0.08);
        col = mix(night, day, terminator);
        // Limb darkening + blue atmosphere at edges
        if (limb < 0.35) col = mix(col, [60, 140, 210], (0.35 - limb) * 1.2);
        col = mix(col, [0, 0, 0] as RGB, (1 - limb) * 0.15);
      } else {
        // Moon: more cratered, varied regolith
        const craterN = fbm(u * 2.2, v * 2.2, seed + 100);
        const crater = smoothstep(0.56, 0.64, craterN) * 0.25;
        const dust: RGB = mix([52, 48, 52], [138, 128, 112], n1);
        const highlight = mix(dust, [188, 178, 162], n1 * 0.2);
        const lit = mix(highlight, [200, 192, 178], ndl * 0.5);
        const terminator = smoothstep(-0.04, 0.18, ndl);
        col = mix([10, 8, 12], lit, terminator);
        col = mix(col, [5, 4, 6] as RGB, crater);
        if (limb < 0.2) col = mix(col, [150, 140, 128], (0.2 - limb) * 0.6);
        col = mix(col, [0, 0, 0] as RGB, (1 - limb) * 0.1);
      }

      col = quant(col, dither);
      D[p] = col[0];
      D[p + 1] = col[1];
      D[p + 2] = col[2];
      D[p + 3] = 255;
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
  terra: PlanetBlit;
  moon: PlanetBlit;
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

  const terra = bakePlanet(w * 0.16, h * 0.44, Math.max(48, h * 0.28), "terra", 0x51A7);
  const moon = bakePlanet(w * 0.86, h * 0.20, Math.max(18, h * 0.085), "moon", 0xC0DE);

  sky = { w, h, neb, dust, spark, mid, near, terra, moon };
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
  ctx.globalAlpha = 0.9 + 0.1 * Math.sin(now / 7000);
  blitWrap(ctx, sky.neb, t * SPD.neb, sway, w, h);
  ctx.globalAlpha = 0.92 + 0.08 * Math.sin(now / 4100);
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
  const bx = reduceMotion ? 0 : Math.round(3 * Math.sin((t / 18) * Math.PI * 2));
  const by = reduceMotion ? 0 : Math.round(2 * Math.sin((t / 23) * Math.PI * 2));
  ctx.drawImage(sky.terra.cv, sky.terra.x + bx, sky.terra.y + by);
  ctx.drawImage(sky.moon.cv, sky.moon.x - bx, sky.moon.y + by);
}
