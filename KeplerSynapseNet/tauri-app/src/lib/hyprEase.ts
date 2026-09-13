// Hyprland + end-4 Quickshell curves (hyprland/general.conf, Appearance.qml).
// Hyprland time units are ~100ms; workspaces = 7 → 700ms, menu_decel.

export function cubicBezier(x1: number, y1: number, x2: number, y2: number) {
  return (t: number): number => {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    let x = t;
    for (let i = 0; i < 8; i++) {
      const cx = 3 * x1;
      const bx = 3 * (x2 - x1) - cx;
      const ax = 1 - cx - bx;
      const cur = ((ax * x + bx) * x + cx) * x;
      const dx = cur - t;
      if (Math.abs(dx) < 1e-6) break;
      const d = (3 * ax * x + 2 * bx) * x + cx;
      if (Math.abs(d) < 1e-6) break;
      x -= dx / d;
    }
    const cy = 3 * y1;
    const by = 3 * (y2 - y1) - cy;
    const ay = 1 - cy - by;
    return ((ay * x + by) * x + cy) * x;
  };
}

export const easeEmphasizedDecel = cubicBezier(0.05, 0.7, 0.1, 1);
export const easeMenuDecel = cubicBezier(0.1, 1, 0, 1);
export const easeSpatial = cubicBezier(0.38, 1.21, 0.22, 1);

// WebKit on 180Hz cannot afford Hyprland workspace (7×100ms, full-width, two copies).
// Same curve, compositor-cheap distance — ~50 frames at 180Hz.
export const DUR_TAB_MS = 260;
export const TAB_SLIDE_PX = 64;
export const DUR_ENTER_MS = 180;
export const DUR_MOVE_MS = 280;
