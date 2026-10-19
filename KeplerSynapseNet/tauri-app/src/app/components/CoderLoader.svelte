<script lang="ts">
  // Pixel coder at the desk. SMIL lives in the SVG, so it has to be inline
  // (an img tag will not play it). Reduced motion freezes the screen.
  import { onMount } from "svelte";
  import coderSvg from "../../assets/sprites/coder-loader.svg?raw";

  export let width = 420;
  export let label = "Mining code";

  let host: HTMLDivElement;

  onMount(() => {
    if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
      host.querySelector("svg")?.pauseAnimations();
    }
  });
</script>

<div
  class="coder-loader"
  bind:this={host}
  style="width:{width}px"
  role="img"
  aria-label={label}
>
  {@html coderSvg}
</div>

<style>
  .coder-loader {
    max-width: 100%;
    margin: 0 auto;
    line-height: 0;
  }

  .coder-loader :global(svg) {
    display: block;
    width: 100%;
    height: auto;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
  }
</style>
