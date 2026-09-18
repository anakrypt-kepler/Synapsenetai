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
    valueNoise(x, y, seed) * 0.5 +
    valueNoise(x * 2.1, y * 2.1, seed + 11) * 0.28 +
    valueNoise(x * 4.3, y * 4.3, seed + 23) * 0.14 +
    valueNoise(x * 8.7, y * 8.7, seed + 41) * 0.08
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
  const step = 14;
  return [
    Math.max(0, Math.min(255, Math.round((c[0] + dither) / step) * step)),
    Math.max(0, Math.min(255, Math.round((c[1] + dither) / step) * step)),
    Math.max(0, Math.min(255, Math.round((c[2] + dither) / step) * step)),
  ];
}

type PlanetKind = "terra" | "moon";

function bakePlanet(
  w: number,
  h: number,
  cx: number,
  cy: number,
  radius: number,
  kind: PlanetKind,
  seed: number,
): HTMLCanvasElement {
  const cv = document.createElement("canvas");
  cv.width = w;
  cv.height = h;
  const c = cv.getContext("2d");
  if (!c) return cv;
  const img = c.createImageData(w, h);
  const D = img.data;
  const lx = 0.58;
  const ly = -0.32;
  const lz = 0.74;
  const llen = Math.hypot(lx, ly, lz);
  const Lx = lx / llen;
  const Ly = ly / llen;
  const Lz = lz / llen;
  const x0 = Math.max(0, Math.floor(cx - radius - 4));
  const y0 = Math.max(0, Math.floor(cy - radius - 4));
  const x1 = Math.min(w - 1, Math.ceil(cx + radius + 4));
  const y1 = Math.min(h - 1, Math.ceil(cy + radius + 4));

  for (let y = y0; y <= y1; y++) {
    for (let x = x0; x <= x1; x++) {
      const nx = (x - cx) / radius;
      const ny = (y - cy) / radius;
      const rr = nx * nx + ny * ny;
      if (rr > 1.08) continue;
      const p = (y * w + x) * 4;
      const dither = (hash2(x, y, seed) - 0.5) * 10;

      if (rr > 1) {
        // atmosphere limb only for terra
        if (kind !== "terra") continue;
        const fall = 1 - (Math.sqrt(rr) - 1) / 0.08;
        if (fall <= 0) continue;
        const a = Math.round(110 * fall * fall);
        D[p] = 70;
        D[p + 1] = 150;
        D[p + 2] = 220;
        D[p + 3] = a;
        continue;
      }

      const nz = Math.sqrt(Math.max(0, 1 - rr));
      const ndl = Math.max(0, nx * Lx + ny * Ly + nz * Lz);
      const limb = Math.pow(nz, 0.45);
      const lon = Math.atan2(nx, nz);
      const lat = Math.asin(Math.max(-1, Math.min(1, ny)));
      const u = (lon / Math.PI + 1) * 2.4;
      const v = (lat / Math.PI + 0.5) * 3.2;
      const n1 = fbm(u, v, seed);
      const n2 = fbm(u * 1.7 + 4, v * 1.7, seed + 7);

      let col: RGB;
      if (kind === "terra") {
        const ice = Math.abs(lat) > 1.05 || (Math.abs(lat) > 0.82 && n1 > 0.42);
        const land = n1 > 0.52;
        const cloud = n2 > 0.72;
        if (ice) col = mix([168, 188, 210], [228, 236, 244], n2);
        else if (land) {
          const dry = n2 > 0.55;
          col = dry
            ? mix([92, 78, 48], [128, 108, 58], n1)
            : mix([36, 92, 48], [78, 122, 52], n1);
        } else {
          col = mix([10, 42, 92], [28, 98, 148], n1);
        }
        if (cloud && !ice) col = mix(col, [220, 230, 238], 0.55 + n2 * 0.2);
        const night: RGB = mix([4, 8, 18], [18, 28, 48], n1 * 0.4);
        const day = mix(col, [255, 236, 200], ndl * 0.12);
        col = mix(night, day, 0.12 + ndl * 0.88);
        if (limb < 0.28) col = mix(col, [80, 160, 220], (0.28 - limb) * 1.4);
      } else {
        const crater = n1 > 0.62 ? 0.18 : 0;
        const dust = mix([42, 40, 44], [118, 110, 98], n1);
        const lit = mix(dust, [176, 166, 150], ndl);
        col = mix([16, 14, 18], lit, 0.16 + ndl * 0.84);
        col = mix(col, [8, 8, 10], crater);
        if (limb < 0.22) col = mix(col, [160, 150, 138], (0.22 - limb) * 0.8);
      }

      col = quant(col, dither);
      D[p] = col[0];
      D[p + 1] = col[1];
      D[p + 2] = col[2];
      D[p + 3] = 255;
    }
  }
  c.putImageData(img, 0, 0);
  return cv;
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
  terra: HTMLCanvasElement;
  moon: HTMLCanvasElement;
  terraX: number;
  terraY: number;
  moonX: number;
  moonY: number;
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

  const terraX = w * 0.16;
  const terraY = h * 0.44;
  const moonX = w * 0.86;
  const moonY = h * 0.20;
  const terra = bakePlanet(w, h, terraX, terraY, Math.max(48, h * 0.28), "terra", 0x51A7);
  const moon = bakePlanet(w, h, moonX, moonY, Math.max(18, h * 0.085), "moon", 0xC0DE);

  sky = { w, h, neb, dust, spark, mid, near, terra, moon, terraX, terraY, moonX, moonY };
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
  ctx.drawImage(sky.terra, bx, by);
  ctx.drawImage(sky.moon, -bx, by);
}
