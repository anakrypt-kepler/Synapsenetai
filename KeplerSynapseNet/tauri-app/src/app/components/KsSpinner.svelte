<script lang="ts">
  // Gemini-style scalloped ring. Keep geometry in sync with .ks-spinner in global.css.
  export let size: number = 48;

  const LOBES = 10;
  const INNER_R = 25.5;
  const AMP = 7.2;
  const OUTER_R = 47.2;
  const STEPS = LOBES * 24;
  const CX = 50;
  const CY = 50;

  function ringPath(): string {
    const top = (CY - OUTER_R).toFixed(1);
    const bot = (CY + OUTER_R).toFixed(1);
    const outer =
      `M50 ${top}` +
      `A${OUTER_R} ${OUTER_R} 0 1 1 50 ${bot}` +
      `A${OUTER_R} ${OUTER_R} 0 1 1 50 ${top}Z`;
    const pts: string[] = [];
    for (let i = 0; i <= STEPS; i++) {
      const theta = (i / STEPS) * Math.PI * 2 - Math.PI / 2;
      const r = INNER_R + AMP * Math.sin(LOBES * theta);
      const x = (CX + r * Math.cos(theta)).toFixed(2);
      const y = (CY + r * Math.sin(theta)).toFixed(2);
      pts.push(`${i === 0 ? "M" : "L"}${x} ${y}`);
    }
    return `${outer}${pts.join("")}Z`;
  }

  const d = ringPath();
</script>

<span
  class="ks-spinner-el"
  style="width:{size}px;height:{size}px"
  role="status"
  aria-label="Loading"
>
  <svg viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
    <path fill="#e8e8ed" fill-rule="evenodd" d={d} />
  </svg>
</span>

<style>
  .ks-spinner-el {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    animation: ks-spin 1.15s linear infinite;
  }

  .ks-spinner-el svg {
    width: 100%;
    height: 100%;
    display: block;
  }

  @keyframes ks-spin {
    to { transform: rotate(360deg); }
  }

  @media (prefers-reduced-motion: reduce) {
    .ks-spinner-el {
      animation: none;
    }
  }
</style>
