<script lang="ts">
  // 2D visual picker for station skins. Tiles are the actual PNGs, not labels.
  import type { StationAgent, StationTile } from "../../lib/stationSkins";

  export let title: string = "";
  export let items: Array<StationAgent | StationTile> = [];
  export let value: string = "";
  export let kind: "agent" | "tile" = "tile";

  function srcOf(item: StationAgent | StationTile): string {
    return item.src;
  }

  function pick(id: string) {
    value = id;
  }
</script>

<div class="skin-block">
  {#if title}
    <div class="skin-title">{title}</div>
  {/if}
  <div class="skin-grid" class:agents={kind === "agent"} class:tiles={kind === "tile"} role="listbox" aria-label={title || "skins"}>
    {#each items as item}
      <button
        type="button"
        class="skin-cell"
        class:on={item.id === value}
        role="option"
        aria-selected={item.id === value}
        title={item.label}
        on:click={() => pick(item.id)}
      >
        <span class="skin-art" class:pixel={kind === "agent"}>
          <img src={srcOf(item)} alt="" draggable="false" />
        </span>
        <span class="skin-name">{item.label}</span>
      </button>
    {/each}
  </div>
</div>

<style>
  .skin-block {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .skin-title {
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.06em;
    color: var(--text-secondary, #a1a1a6);
  }

  .skin-grid {
    display: grid;
    gap: 8px;
    max-height: 280px;
    overflow: auto;
    padding: 2px;
  }

  .skin-grid.agents {
    grid-template-columns: repeat(auto-fill, minmax(88px, 1fr));
  }

  .skin-grid.tiles {
    grid-template-columns: repeat(auto-fill, minmax(104px, 1fr));
  }

  .skin-cell {
    display: flex;
    flex-direction: column;
    align-items: stretch;
    gap: 6px;
    padding: 6px;
    margin: 0;
    border: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    border-radius: var(--radius-sm, 10px);
    background: rgba(0, 0, 0, 0.35);
    color: var(--text-secondary, #a1a1a6);
    cursor: pointer;
    text-align: left;
  }

  .skin-cell.on {
    border-color: var(--cy, #00e5ff);
    color: var(--text-primary, #f5f5f7);
    background: rgba(0, 229, 255, 0.08);
  }

  .skin-art {
    display: block;
    width: 100%;
    height: 64px;
    overflow: hidden;
    border-radius: 6px;
    background: #111;
  }

  .skin-art img {
    width: 100%;
    height: 100%;
    object-fit: cover;
    image-rendering: auto;
    display: block;
  }

  .skin-art.pixel img {
    object-fit: contain;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    background: #0a0a0a;
  }

  .skin-name {
    font-size: 10px;
    line-height: 1.25;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
</style>
