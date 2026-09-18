// Dump XP ladder (xp.js): cumulative XP to REACH level n = LEVEL_K * n * (n-1).
// Levels DESCRIBE; they never gate capability.
//
// harvestLevel does NOT mint dump user-feedback XP (memory.feedback / Commander
// verdicts). It projects a local Kepler harvest tally onto that same curve so
// the name-tag chip can render: xp = submissions * LEVEL_K + floor(ngt) * 5.
// Start at Lv 1. agentId is accepted for callers; it does not change the math.

export const LEVEL_K = 25;

function whole(v: unknown, fallback = 0): number {
  const n = typeof v === "number" ? v : Number(v);
  if (!Number.isFinite(n)) return fallback;
  return Math.max(0, Math.floor(n));
}

// Cumulative XP required to stand on `level` (L1 = 0, L2 = 50, L3 = 150, …).
export function xpForLevel(level: number): number {
  const n = Math.max(1, Math.floor(Number.isFinite(level) ? level : 1));
  return LEVEL_K * n * (n - 1);
}

// Largest n with LEVEL_K * n * (n-1) <= xp
// → n = floor((1 + sqrt(1 + 4*xp/LEVEL_K)) / 2). Never below 1.
export function levelOf(xp: number): number {
  if (!Number.isFinite(xp)) return 1;
  const x = Math.max(0, Math.floor(xp));
  return Math.max(1, Math.floor((1 + Math.sqrt(1 + (4 * x) / LEVEL_K)) / 2));
}

export function xpToNext(xp: number): number {
  const x = whole(xp, 0);
  const next = xpForLevel(levelOf(x) + 1);
  return Math.max(0, next - x);
}

export function frac(xp: number): number {
  const x = whole(xp, 0);
  const level = levelOf(x);
  const base = xpForLevel(level);
  const span = Math.max(1, xpForLevel(level + 1) - base);
  return Math.max(0, Math.min(1, (x - base) / span));
}

export type HarvestLevelInput = {
  submissions?: number;
  ngt?: number;
  agentId?: string;
};

export type HarvestLevel = {
  xp: number;
  level: number;
  frac: number;
  toNext: number;
};

export function harvestLevel(input: HarvestLevelInput = {}): HarvestLevel {
  const submissions = whole(input.submissions, 0);
  const ngt = whole(input.ngt, 0);
  const xp = submissions * LEVEL_K + ngt * 5;
  return {
    xp,
    level: levelOf(xp),
    frac: frac(xp),
    toNext: xpToNext(xp),
  };
}
