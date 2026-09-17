<script lang="ts">
  // BLOCKS mark: a small pixel stack whose height maps to last_block (capped).
  // Pure presentational projection of a number; no engine calls.
  export let count: number = 0;
  export let max: number = 16;
  $: n = Math.max(0, Math.min(max, Math.floor(count || 0)));
  $: bricks = Array.from({ length: n });
</script>

<div class="brick-stack" aria-hidden="true">
  {#each bricks as _, i}
    <div class="brick" class:top={i === bricks.length - 1}></div>
  {:else}
    <div class="brick-empty"></div>
  {/each}
</div>

<style>
  .brick-stack {
    display: flex;
    flex-direction: column-reverse;
    align-items: center;
    justify-content: flex-start;
    gap: 1px;
    height: 100%;
    padding-bottom: 2px;
  }

  .brick {
    width: 30px;
    height: 5px;
    background: #e6e6ea;
    border-bottom: 1px solid #0a0a0a;
    image-rendering: pixelated;
  }

  /* Newest block flashes cyan briefly to read as "just stacked". */
  .brick.top {
    background: #00e5ff;
    animation: brick-place 1.4s ease-out infinite;
  }

  .brick-empty {
    width: 30px;
    height: 1px;
    background: rgba(255, 255, 255, 0.16);
  }

  @keyframes brick-place {
    0% { opacity: 0.4; }
    30% { opacity: 1; }
    100% { opacity: 1; background: #e6e6ea; }
  }
</style>
