<script lang="ts">
  // Presentational only. A small black stage with a 1px pixel floor line.
  // Sprites rest on the floor and animate via CSS. Reduced motion is honored
  // globally in styles/global.css (animations freeze on the first frame).
  export let height: number = 56;
  export let floor: boolean = true;
  // Extra bottom room so a walker's feet sit on the floor line, not under it.
  export let floorGap: number = 10;
</script>

<div class="pixel-stage" style="--stage-h:{height}px; --floor:{floorGap}px" aria-hidden="true">
  {#if floor}<div class="stage-floor"></div>{/if}
  <slot />
</div>

<style>
  .pixel-stage {
    position: relative;
    width: 100%;
    height: var(--stage-h);
    background: #000000;
    overflow: hidden;
    display: flex;
    align-items: flex-end;
    justify-content: center;
    image-rendering: pixelated;
    /* Only chrome above; the stage itself is just void + a floor. */
    border-radius: 0;
  }

  .stage-floor {
    position: absolute;
    left: 0;
    right: 0;
    bottom: var(--floor);
    height: 1px;
    background: rgba(255, 255, 255, 0.16);
    pointer-events: none;
  }
</style>
