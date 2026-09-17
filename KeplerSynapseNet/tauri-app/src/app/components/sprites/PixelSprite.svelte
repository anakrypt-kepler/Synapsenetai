<script lang="ts">
  // Presentational sprite slot. `src` is a plain .svg URL (import it in the route)
  // so Kepler can replace the art 1:1 without touching Svelte. Animation is a
  // pure-CSS projection of live state chosen by the route (e.g. spin when mining).
  // No JS loop, no canvas. Reduced motion is frozen globally in global.css.
  export let src: string;
  export let anim:
    | "idle"
    | "spin"
    | "rotate"
    | "walk"
    | "run"
    | "pulse"
    | "blink"
    | "slide"
    | "none" = "idle";
  export let size: number = 16;
  // Horizontal travel for the "slide" coin (px). walk/run use the whole stage.
  export let travel: number = 40;
  export let label: string = "";
</script>

<span
  class="psprite {anim}"
  style="--sz:{size}px; --travel:{travel}px"
  role={label ? "img" : undefined}
  aria-label={label || undefined}
  aria-hidden={label ? undefined : "true"}
>
  <img class="sprite-img" {src} alt="" width={size} height={size} draggable="false" />
</span>

<style>
  .psprite {
    display: inline-flex;
    width: var(--sz);
    height: var(--sz);
    line-height: 0;
  }

  .sprite-img {
    width: 100%;
    height: 100%;
    display: block;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    transform-origin: center;
  }

  /* Floor travellers sit on the stage floor and cross it left-right. */
  .psprite.walk,
  .psprite.run {
    position: absolute;
    bottom: var(--floor, 10px);
    left: 4%;
  }

  .idle .sprite-img {
    animation: sp-idle 2.6s ease-in-out infinite;
  }

  .spin .sprite-img {
    animation: sp-spin 1.1s linear infinite;
  }

  .rotate .sprite-img {
    animation: sp-rotate 2.8s steps(8) infinite;
  }

  .pulse .sprite-img {
    animation: sp-pulse 2.2s ease-in-out infinite;
  }

  .blink .sprite-img {
    animation: sp-blink 2.8s ease-in-out infinite;
  }

  .slide .sprite-img {
    animation: sp-slide 1.9s ease-in infinite;
  }

  /* Parent carries left + facing flip; child carries the vertical bob.
     Two elements keep the two transforms from clobbering each other. */
  .walk .sprite-img {
    animation: sp-bob 0.5s steps(2) infinite;
  }

  .run .sprite-img {
    animation: sp-bob 0.28s steps(2) infinite;
  }

  .psprite.walk {
    animation: sp-walk 3.6s linear infinite;
  }

  .psprite.run {
    animation: sp-run 2s linear infinite;
  }

  @keyframes sp-idle {
    0%, 100% { transform: translateY(0); }
    50% { transform: translateY(-1px); }
  }

  /* Pixel coin flip: squash on X, no 3D. */
  @keyframes sp-spin {
    0%, 100% { transform: scaleX(1); }
    50% { transform: scaleX(0.1); }
  }

  @keyframes sp-rotate {
    to { transform: rotate(360deg); }
  }

  @keyframes sp-pulse {
    0%, 100% { transform: scale(1); opacity: 1; }
    50% { transform: scale(1.16); opacity: 0.7; }
  }

  @keyframes sp-blink {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.28; }
  }

  /* Coin slides toward the SN glyph, then resets. */
  @keyframes sp-slide {
    0% { transform: translateX(0); opacity: 1; }
    70% { opacity: 1; }
    100% { transform: translateX(var(--travel)); opacity: 0; }
  }

  @keyframes sp-bob {
    0% { transform: translateY(0); }
    50% { transform: translateY(-1px); }
    100% { transform: translateY(0); }
  }

  /* Walk across the floor, flip to face travel direction at each end. */
  @keyframes sp-walk {
    0% { left: 4%; transform: scaleX(1); }
    48% { left: 78%; transform: scaleX(1); }
    50% { left: 78%; transform: scaleX(-1); }
    98% { left: 4%; transform: scaleX(-1); }
    100% { left: 4%; transform: scaleX(1); }
  }

  @keyframes sp-run {
    0% { left: 2%; transform: scaleX(1); }
    48% { left: 84%; transform: scaleX(1); }
    50% { left: 84%; transform: scaleX(-1); }
    98% { left: 2%; transform: scaleX(-1); }
    100% { left: 2%; transform: scaleX(1); }
  }
</style>
