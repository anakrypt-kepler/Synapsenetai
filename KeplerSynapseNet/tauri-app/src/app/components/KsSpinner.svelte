<script lang="ts">
  // Pixel square spinner. Name/props kept so existing imports still work.
  // A bright pixel block steps around a square frame (Minecraft-style loader).
  // Matches .ks-spinner in global.css. Reduced motion freezes it.
  export let size: number = 48;
  $: border = Math.max(2, Math.round(size / 16));
</script>

<span
  class="ks-spinner-el"
  style="width:{size}px;height:{size}px;border-width:{border}px"
  role="status"
  aria-label="Loading"
></span>

<style>
  .ks-spinner-el {
    display: inline-block;
    flex-shrink: 0;
    vertical-align: middle;
    box-sizing: border-box;
    border-style: solid;
    border-color: rgba(255, 255, 255, 0.22);
    border-radius: 0;
    background: linear-gradient(#e8e8ed, #e8e8ed) left top / 38% 38% no-repeat;
    image-rendering: pixelated;
    animation: ks-spin 0.8s steps(8) infinite;
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
