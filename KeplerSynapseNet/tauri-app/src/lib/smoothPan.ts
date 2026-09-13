// Time-based inertial pan. Per-frame friction feels 3× faster at 180Hz.

const SKIP = "textarea, input, select, option, [data-no-smooth]";
const REF_MS = 1000 / 60;
const FRICTION = 0.94;
const STOP = 0.08;

type Axis = {
  pos: number;
  vel: number;
};

type Pan = {
  y: Axis;
  x: Axis;
  raf: number;
  last: number;
};

const pans = new WeakMap<HTMLElement, Pan>();

function prefersReduced(): boolean {
  return typeof matchMedia !== "undefined" &&
    matchMedia("(prefers-reduced-motion: reduce)").matches;
}

function isSkip(el: Element): boolean {
  if (el.closest(SKIP)) return true;
  const cls = (el as HTMLElement).className;
  if (typeof cls === "string" && cls.includes("monaco")) return true;
  return false;
}

function canScroll(el: HTMLElement, overflow: string, size: number, client: number): boolean {
  return (overflow === "auto" || overflow === "scroll" || overflow === "overlay") &&
    size > client + 1;
}

function roomY(el: HTMLElement, dy: number): boolean {
  if (dy === 0) return false;
  const max = el.scrollHeight - el.clientHeight;
  if (max <= 1) return false;
  return dy > 0 ? el.scrollTop < max - 0.5 : el.scrollTop > 0.5;
}

function roomX(el: HTMLElement, dx: number): boolean {
  if (dx === 0) return false;
  const max = el.scrollWidth - el.clientWidth;
  if (max <= 1) return false;
  return dx > 0 ? el.scrollLeft < max - 0.5 : el.scrollLeft > 0.5;
}

function scrollParent(start: EventTarget | null, dy: number, dx: number): HTMLElement | null {
  let el: Element | null = start instanceof Element ? start : null;
  while (el && el !== document.documentElement) {
    if (isSkip(el)) return null;
    if (el instanceof HTMLElement) {
      const st = getComputedStyle(el);
      const yOk = canScroll(el, st.overflowY, el.scrollHeight, el.clientHeight) && roomY(el, dy);
      const xOk = canScroll(el, st.overflowX, el.scrollWidth, el.clientWidth) && roomX(el, dx);
      if (yOk || xOk) return el;
    }
    el = el.parentElement;
  }
  return null;
}

function ensure(el: HTMLElement): Pan {
  let p = pans.get(el);
  if (!p) {
    p = {
      y: { pos: el.scrollTop, vel: 0 },
      x: { pos: el.scrollLeft, vel: 0 },
      raf: 0,
      last: 0,
    };
    pans.set(el, p);
  }
  return p;
}

function step(el: HTMLElement, p: Pan, now: number) {
  if (!p.last) p.last = now;
  const dt = Math.min(32, Math.max(1, now - p.last));
  p.last = now;
  const n = dt / REF_MS;
  const decay = Math.pow(FRICTION, n);

  const maxY = Math.max(0, el.scrollHeight - el.clientHeight);
  const maxX = Math.max(0, el.scrollWidth - el.clientWidth);
  p.y.pos += p.y.vel * n;
  p.x.pos += p.x.vel * n;
  p.y.vel *= decay;
  p.x.vel *= decay;
  if (p.y.pos < 0) { p.y.pos = 0; p.y.vel *= 0.35; }
  if (p.y.pos > maxY) { p.y.pos = maxY; p.y.vel *= 0.35; }
  if (p.x.pos < 0) { p.x.pos = 0; p.x.vel *= 0.35; }
  if (p.x.pos > maxX) { p.x.pos = maxX; p.x.vel *= 0.35; }
  el.scrollTop = p.y.pos;
  el.scrollLeft = p.x.pos;
  if (Math.abs(p.y.vel) > STOP || Math.abs(p.x.vel) > STOP) {
    p.raf = requestAnimationFrame((t) => step(el, p, t));
  } else {
    p.y.vel = 0;
    p.x.vel = 0;
    p.raf = 0;
    p.last = 0;
  }
}

function onWheel(e: WheelEvent) {
  if (e.ctrlKey || e.defaultPrevented) return;
  const dy = e.deltaY;
  const dx = e.deltaX;
  if (dy === 0 && dx === 0) return;
  const el = scrollParent(e.target, dy, dx);
  if (!el) return;
  e.preventDefault();
  const p = ensure(el);
  if (!p.raf) {
    p.y.pos = el.scrollTop;
    p.x.pos = el.scrollLeft;
    p.last = 0;
  }
  const scale = e.deltaMode === 1 ? 8 : e.deltaMode === 2 ? el.clientHeight * 0.35 : 1;
  const impulse = 0.12;
  const cap = 10;
  p.y.vel += Math.sign(dy) * Math.min(Math.abs(dy * scale) * impulse, cap);
  p.x.vel += Math.sign(dx) * Math.min(Math.abs(dx * scale) * impulse, cap);
  p.y.vel = Math.max(-18, Math.min(18, p.y.vel));
  p.x.vel = Math.max(-18, Math.min(18, p.x.vel));
  if (!p.raf) p.raf = requestAnimationFrame((t) => step(el, p, t));
}

export function startSmoothPan(): () => void {
  // Native WebKit overflow on .content-area. Custom wheel felt stiff.
  return () => {};
}
