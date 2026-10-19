<script lang="ts">
  // Pixel miner. SMIL lives in the SVG, so it has to be inline (an img tag
  // will not play it). Reduced motion freezes the pick on the first frame.
  import { onMount } from "svelte";
  import minerSvg from "../../assets/sprites/miner-loader.svg?raw";

  export let width = 224;
  export let label = "Loading";

  let host: HTMLDivElement;

  onMount(() => {
    if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
      host.querySelector("svg")?.pauseAnimations();
    }
  });
</script>

<div
  class="miner-loader"
  bind:this={host}
  style="width:{width}px"
  role="status"
  aria-label={label}
>
  {@html minerSvg}
</div>

<style>
  .miner-loader {
    max-width: 100%;
    margin: 0 auto;
    line-height: 0;
  }

  .miner-loader :global(svg) {
    display: block;
    width: 100%;
    height: auto;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
  }
</style>
