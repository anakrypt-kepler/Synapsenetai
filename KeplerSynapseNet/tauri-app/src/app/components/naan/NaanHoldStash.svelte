<script lang="ts">
  // Compact HOLD locker strip under the map. Crates track focused harvest runs.

  import { onDestroy } from "svelte";

  export let name: string = "";
  export let submissions: number = 0;
  export let level: number | null = null;

  const SLOT_CAP = 8;
  let flashSlot = -1;
  let flashTimer: ReturnType<typeof setTimeout> | null = null;

  $: packed = Math.max(0, Math.floor(Number(submissions) || 0));
  $: overflow = Math.max(0, packed - SLOT_CAP);
  $: filledSlots = Math.min(packed, SLOT_CAP);

  function hideBroken(ev: Event) {
    const el = ev.currentTarget;
    if (el instanceof HTMLImageElement) el.style.display = "none";
  }

  function tapSlot(i: number) {
    if (i >= filledSlots) return;
    flashSlot = i;
    if (flashTimer) clearTimeout(flashTimer);
    flashTimer = setTimeout(() => {
      flashSlot = -1;
      flashTimer = null;
    }, 160);
  }

  onDestroy(() => {
    if (flashTimer) clearTimeout(flashTimer);
  });
</script>

<section class="hold" aria-label="HOLD stash">
  <div class="hold-meta">
    <div class="hold-title">HOLD</div>
    <div class="hold-sub">STASH</div>
    {#if name}
      <div class="hold-name">{name}</div>
    {/if}
    {#if level != null}
      <div class="hold-lv">Lv {level}</div>
    {/if}
  </div>
  <div class="hold-row">
    {#each Array(SLOT_CAP) as _, i}
      {@const filled = i < filledSlots}
      <button
        type="button"
        class="slot"
        class:filled
        class:flash={flashSlot === i}
        aria-label={overflow > 0 && i === SLOT_CAP - 1
          ? `HOLD slot ${i + 1}, plus ${overflow} more`
          : filled
            ? `HOLD crate ${i + 1}`
            : `Empty HOLD locker ${i + 1}`}
        on:click={() => tapSlot(i)}
      >
        {#if filled}
          <img
            src="/station/props/crate.png"
            alt=""
            draggable="false"
            on:error={hideBroken}
          />
        {:else}
          <img
            src="/station/props/industrial_locker.png"
            alt=""
            draggable="false"
            on:error={hideBroken}
          />
        {/if}
        {#if overflow > 0 && i === SLOT_CAP - 1}
          <span class="plus">+{overflow}</span>
        {/if}
      </button>
    {/each}
  </div>
</section>

<style>
  .hold {
    display: flex;
    align-items: center;
    gap: 10px;
    margin: 6px 0 10px;
    padding: 4px 8px;
    max-height: 52px;
    background: #050301;
    border: 1px solid rgba(0, 229, 255, 0.22);
    overflow: hidden;
  }

  .hold-meta {
    flex: 0 0 auto;
    min-width: 0;
    max-width: 92px;
  }

  .hold-title {
    font-family: var(--font);
    font-size: 9px;
    color: var(--cy, #00e5ff);
    line-height: 1.2;
  }

  .hold-sub {
    font-family: var(--font);
    font-size: 7px;
    color: var(--text-faint);
    margin-top: 2px;
  }

  .hold-name,
  .hold-lv {
    font-family: var(--font);
    font-size: 7px;
    color: var(--text-secondary);
    margin-top: 2px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .hold-lv {
    color: var(--ph-bright, #b8fbff);
  }

  .hold-row {
    display: flex;
    gap: 4px;
    flex: 1;
    min-width: 0;
  }

  .slot {
    position: relative;
    flex: 1 1 0;
    max-width: 44px;
    height: 40px;
    padding: 2px;
    margin: 0;
    background: #030506;
    border: 1px solid rgba(0, 229, 255, 0.16);
    border-radius: 0;
    color: inherit;
    cursor: default;
    font: inherit;
  }

  .slot.filled {
    cursor: pointer;
    border-color: rgba(0, 229, 255, 0.32);
  }

  .slot.flash {
    border-color: var(--cy, #00e5ff);
    animation: hold-flash 0.16s linear;
  }

  .slot img {
    display: block;
    width: 100%;
    height: 100%;
    object-fit: contain;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    pointer-events: none;
  }

  .plus {
    position: absolute;
    right: 1px;
    bottom: 1px;
    font-family: var(--font);
    font-size: 7px;
    line-height: 1;
    color: var(--gold, #ffd34a);
    background: #050301;
    border: 1px solid rgba(255, 211, 74, 0.45);
    padding: 1px 2px;
  }

  @keyframes hold-flash {
    0% { background: rgba(0, 229, 255, 0.28); }
    100% { background: #030506; }
  }

  @media (prefers-reduced-motion: reduce) {
    .slot.flash {
      animation: none;
      background: rgba(0, 229, 255, 0.16);
    }
  }
</style>
