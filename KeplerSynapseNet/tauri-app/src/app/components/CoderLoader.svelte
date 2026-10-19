<script lang="ts">
  // Pixel coder at the desk. It plays the whole time the IDE tab is open,
  // not only during a load. SMIL has to be inline, and a webview will not
  // start a timeline that was injected as HTML, so this kicks it on mount.
  import { onMount } from "svelte";
  import coderSvg from "../../assets/sprites/coder-loader.svg?raw";

  export let width = 420;
  export let label = "Mining code";

  let host: HTMLDivElement;

  function startDesk(svg: SVGSVGElement) {
    svg.setCurrentTime(0);
    svg.unpauseAnimations();
    svg.querySelectorAll("animate, animateTransform").forEach((node) => {
      const anim = node as SVGAnimateElement;
      if (typeof anim.beginElement === "function") anim.beginElement();
    });
  }

  onMount(() => {
    const svg = host.querySelector("svg");
    if (!(svg instanceof SVGSVGElement)) return;
    startDesk(svg);
    // The first kick can land before the injected timeline exists.
    const again = requestAnimationFrame(() => startDesk(svg));
    return () => cancelAnimationFrame(again);
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
