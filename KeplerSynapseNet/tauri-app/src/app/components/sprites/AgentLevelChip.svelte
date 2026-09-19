<script lang="ts">
  // Dump nameplate / roster chip: dark CRT plate, amber frame, gold "Lv N".
  // A name-tag over the head — never a skill gate. pointer-events none.
  export let level: number = 1;
  export let frac: number | undefined = undefined;
  export let compact: boolean = false;
  export let name: string | undefined = undefined;

  $: lv = Math.max(1, Math.floor(Number.isFinite(level) ? level : 1));
  $: bar =
    typeof frac === "number" && Number.isFinite(frac)
      ? Math.max(0, Math.min(1, frac))
      : 0;
  $: label = (name || "").trim();
  $: fillPct = bar > 0 ? Math.max(1, Math.round(bar * 100)) : 0;
</script>

<span class="ag-lv" class:named={!!label} class:has-bar={bar > 0} class:compact aria-hidden="true">
  {#if label}
    <span class="ag-nm">{label}</span>
  {/if}
  <span class="ag-n">Lv {lv}</span>
  {#if bar > 0}
    <span class="ag-xp">
      <span class="ag-xp-fill" style="width:{fillPct}%"></span>
    </span>
  {/if}
</span>

<style>
  .ag-lv {
    position: relative;
    display: inline-flex;
    align-items: center;
    gap: 5px;
    max-width: 132px;
    padding: 2px 5px;
    background: rgba(6, 5, 4, 0.92);
    border: 1px solid #b9791c;
    border-radius: 0;
    color: #ffd34a;
    font-family: var(--font);
    font-size: 8px;
    line-height: 1.2;
    letter-spacing: 0;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    pointer-events: none;
    white-space: nowrap;
    box-sizing: border-box;
  }

  .ag-lv.has-bar {
    padding-bottom: 5px;
  }

  .ag-lv::before {
    content: "";
    position: absolute;
    left: 1px;
    right: 1px;
    top: 0;
    height: 1px;
    background: #ffd34a;
    opacity: 0.6;
    pointer-events: none;
  }

  .ag-lv.compact {
    max-width: 64px;
    gap: 3px;
    padding: 1px 4px 2px;
    font-size: 7px;
  }

  .ag-lv.compact .ag-nm {
    max-width: 36px;
  }

  .ag-n {
    color: #ffd34a;
    flex: 0 0 auto;
  }

  .ag-nm {
    color: #ffd34a;
    overflow: hidden;
    text-overflow: ellipsis;
    min-width: 0;
    max-width: 80px;
  }

  .ag-xp {
    position: absolute;
    left: 1px;
    right: 1px;
    bottom: 1px;
    height: 2px;
    background: #140c03;
    pointer-events: none;
  }

  .ag-xp-fill {
    display: block;
    height: 2px;
    min-width: 1px;
    background: #ffd34a;
  }
</style>
