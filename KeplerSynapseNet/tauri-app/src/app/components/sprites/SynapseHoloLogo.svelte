<script lang="ts">
  // Pixel SYNAPSENET word. Phosphor is NAAN cyan.
  // Scramble copies dump asciifx decode; reduced-motion skips it.
  import { onDestroy, onMount } from "svelte";
  import { HOLO_GLYPHS, SYNAPSENET_ASCII } from "../../../lib/synapseHolo";

  export let compact: boolean = false;

  const reduceMotion =
    typeof matchMedia !== "undefined" &&
    matchMedia("(prefers-reduced-motion: reduce)").matches;

  let shown = SYNAPSENET_ASCII;
  let scrambling = false;
  let raf = 0;
  let guard = 0;

  function gl(): string {
    return HOLO_GLYPHS[(Math.random() * HOLO_GLYPHS.length) | 0];
  }

  function scramble() {
    const final = SYNAPSENET_ASCII;
    if (reduceMotion) {
      shown = final;
      scrambling = false;
      return;
    }
    const chars = Array.from(final);
    const dur = 620;
    scrambling = true;
    const start = performance.now();
    const finish = () => {
      if (!scrambling) return;
      scrambling = false;
      shown = final;
      if (raf) cancelAnimationFrame(raf);
      if (guard) clearTimeout(guard);
    };
    guard = window.setTimeout(finish, dur + 240);
    const tick = (t0: number) => {
      if (!scrambling) return;
      const t = Math.min(1, (t0 - start) / dur);
      let out = "";
      for (let i = 0; i < chars.length; i++) {
        const ch = chars[i];
        if (ch === " " || ch === "\n") {
          out += ch;
          continue;
        }
        const thresh = (i / Math.max(1, chars.length)) * 0.82;
        out += t >= thresh ? ch : gl();
      }
      shown = out;
      if (t >= 1) finish();
      else raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
  }

  onMount(() => scramble());
  onDestroy(() => {
    scrambling = false;
    if (raf) cancelAnimationFrame(raf);
    if (guard) clearTimeout(guard);
  });
</script>

<div class="holo" class:compact aria-label="SynapseNet">
  <div class="logo-word" class:afx={scrambling} aria-hidden="true">{shown}</div>
</div>

<style>
  .holo {
    --ph: var(--cy, #00e5ff);
    --ph-bright: #b8fbff;
    --ph-dim: #148a99;
    --ph-glow: rgba(0, 229, 255, 0.5);
    --ph-glow2: rgba(0, 229, 255, 0.14);
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 8px;
    margin: 0 0 14px;
    pointer-events: none;
  }

  .logo-word {
    font-family: var(--font);
    font-size: 14px;
    line-height: 1;
    letter-spacing: 6px;
    color: var(--ph);
    text-shadow: 0 0 12px var(--ph-glow), 0 0 2px var(--ph-glow);
    animation: sp-breathe 4.2s ease-in-out infinite;
    white-space: pre;
  }

  .logo-word.afx {
    text-shadow: 0 0 8px var(--ph-glow), 0 0 2px var(--ph-bright);
  }

  .compact .logo-word {
    font-size: 11px;
    letter-spacing: 4px;
  }

  @keyframes sp-breathe {
    0%,
    100% {
      text-shadow: 0 0 12px var(--ph-glow), 0 0 2px var(--ph-glow);
    }
    50% {
      text-shadow: 0 0 22px var(--ph-glow), 0 0 6px var(--ph-bright);
    }
  }

  @media (prefers-reduced-motion: reduce) {
    .logo-word {
      animation: none;
    }
  }
</style>
