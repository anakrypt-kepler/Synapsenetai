<script lang="ts">
  // Dump ag-hero wells: portrait + RUNS/LEVEL/KUDOS + NOW/DONE.

  export let name: string = "";
  export let portrait: string = "";
  export let harvest: boolean = true;
  export let statusKind: "down" | "working" | "on" = "on";
  export let statusText: string = "ONLINE";
  export let model: string = "";
  export let runs: number | null = null;
  export let level: number | null = null;
  export let kudos: number | null = null;
  export let nowLine: string = "";
  export let purpose: string = "";
  export let done: { title: string; result: string }[] = [];
  export let logTail: { ts: string; msg: string }[] = [];

  function cell(v: number | null): string {
    return v == null ? "—" : String(v);
  }
</script>

<section class="ag-hero" aria-label="Agent dossier">
  <div class="ag-portrait-wrap">
    <div class="ag-portrait-well">
      <span class="ag-ptick a"></span>
      <span class="ag-ptick b"></span>
      <span class="ag-ptick c"></span>
      <span class="ag-ptick d"></span>
      <span class="ag-psweep" aria-hidden="true"></span>
      {#if portrait}
        <img src={portrait} alt="" width="100" height="108" draggable="false" />
      {/if}
    </div>
  </div>
  <div class="ag-info">
    <div class="ag-name">{name || "NAAN"}</div>
    <div class="ag-role-line">
      <span
        class="ag-sdot"
        class:on={statusKind === "on"}
        class:working={statusKind === "working"}
        class:down={statusKind === "down"}
      ></span>
      {statusText}
    </div>
    <div class="ag-tags">
      <span class="tag">NAAN</span>
      {#if harvest}
        <span class="tag model">{model || "HARVEST"}</span>
      {:else}
        <span class="tag">CREW</span>
      {/if}
    </div>
    <div class="stat-grid">
      <div class="stat-cell">
        <div class="stat-val">{cell(runs)}</div>
        <div class="stat-lbl">RUNS</div>
      </div>
      <div class="stat-cell">
        <div class="stat-val">{cell(level)}</div>
        <div class="stat-lbl">LEVEL</div>
      </div>
      <div class="stat-cell">
        <div class="stat-val pos">{cell(kudos)}</div>
        <div class="stat-lbl">KUDOS</div>
      </div>
    </div>
  </div>
</section>

<div class="ag-mission">
  <div class="ag-mission-lbl">PURPOSE</div>
  <div class="ag-mission-text">
    {purpose || "No topics set. Write them in CONFIG before START."}
  </div>
</div>

<div class="ag-now">
  <div class="ag-mission-lbl">NOW</div>
  <div class="ag-mission-text">{nowLine || "IDLE"}</div>
</div>

{#if !harvest}
  <p class="honest">Extra NAAN bodies walk their own wing. They do not run a second harvest loop.</p>
{/if}

<div class="ag-done">
  <div class="ag-mission-lbl">DONE</div>
  {#if harvest && (done.length || logTail.length)}
    {#each done as row}
      <div class="done-row">
        <span class="done-title">{row.title}</span>
        <span class="tag">{row.result}</span>
      </div>
    {/each}
    {#each logTail as entry}
      <div class="done-log">
        <span class="chat-ts">[{entry.ts}]</span>
        {entry.msg}
      </div>
    {/each}
  {:else if harvest}
    <div class="ag-mission-cta">No harvest work yet.</div>
  {:else}
    <div class="ag-mission-cta">No harvest ledger on this body.</div>
  {/if}
</div>

<style>
  .ag-hero {
    display: flex;
    gap: 14px;
    align-items: flex-start;
    padding-bottom: 10px;
    margin-bottom: 8px;
    border-bottom: 1px solid var(--border);
  }

  .ag-portrait-wrap {
    flex: 0 0 100px;
  }

  .ag-portrait-well {
    position: relative;
    width: 100px;
    height: 108px;
    overflow: hidden;
    background: linear-gradient(180deg, #0a1214, #030506);
    border: 1px solid rgba(0, 229, 255, 0.22);
  }

  .ag-portrait-well img {
    position: relative;
    z-index: 1;
    width: 100%;
    height: 100%;
    object-fit: contain;
    object-position: bottom center;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    display: block;
  }

  .ag-ptick {
    position: absolute;
    width: 8px;
    height: 8px;
    border: 1px solid var(--cy, #00e5ff);
    opacity: 0.65;
    z-index: 3;
    pointer-events: none;
  }
  .ag-ptick.a { top: 3px; left: 3px; border-right: 0; border-bottom: 0; }
  .ag-ptick.b { top: 3px; right: 3px; border-left: 0; border-bottom: 0; }
  .ag-ptick.c { bottom: 3px; left: 3px; border-right: 0; border-top: 0; }
  .ag-ptick.d { bottom: 3px; right: 3px; border-left: 0; border-top: 0; }

  .ag-psweep {
    position: absolute;
    left: 0;
    right: 0;
    height: 20px;
    z-index: 2;
    pointer-events: none;
    background: linear-gradient(180deg, transparent, rgba(0, 229, 255, 0.16), transparent);
    animation: ag-psweep 2.8s linear infinite;
  }

  @keyframes ag-psweep {
    0% { top: -20px; }
    100% { top: 112px; }
  }

  .ag-info {
    flex: 1;
    min-width: 0;
  }

  .ag-name {
    font-family: var(--font);
    font-size: 11px;
    color: var(--cy, #00e5ff);
    line-height: 1.4;
  }

  .ag-role-line {
    display: flex;
    align-items: center;
    gap: 7px;
    font-family: var(--font);
    font-size: 8px;
    color: var(--cy, #00e5ff);
    margin: 6px 0;
  }

  .ag-sdot {
    width: 7px;
    height: 7px;
    background: var(--text-faint);
    flex: none;
  }
  .ag-sdot.on {
    background: var(--ok);
    box-shadow: 0 0 5px rgba(48, 209, 88, 0.55);
  }
  .ag-sdot.working {
    background: var(--gold, #ffd34a);
    box-shadow: 0 0 7px rgba(255, 211, 74, 0.7);
    animation: sdot-pulse 1.1s infinite;
  }
  .ag-sdot.down {
    background: var(--err);
    box-shadow: 0 0 6px rgba(255, 69, 58, 0.75);
    animation: sdot-pulse 2.2s infinite;
  }

  @keyframes sdot-pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.3; }
  }

  .ag-tags {
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
    margin: 0 0 8px;
  }

  .tag {
    font-family: var(--font);
    font-size: 7px;
    color: var(--text-secondary);
    border: 1px solid var(--border);
    padding: 3px 6px;
  }

  .tag.model {
    color: var(--cy, #00e5ff);
    border-color: rgba(0, 229, 255, 0.35);
  }

  .stat-grid {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 6px;
    margin: 9px 0 0;
  }

  .stat-cell {
    background: #050301;
    border: 1px solid rgba(0, 229, 255, 0.16);
    padding: 8px 4px;
    text-align: center;
  }

  .stat-val {
    font-family: var(--font);
    font-size: 12px;
    color: var(--ph-bright, #b8fbff);
    line-height: 1;
    text-shadow: 0 0 12px var(--ph-glow2, rgba(0, 229, 255, 0.14));
  }

  .stat-val.pos {
    color: var(--gold, #ffd34a);
  }

  .stat-lbl {
    font-family: var(--font);
    font-size: 7px;
    color: var(--text-faint);
    margin-top: 6px;
  }

  .ag-mission,
  .ag-now,
  .ag-done {
    border-left: 2px solid var(--cy, #00e5ff);
    padding: 6px 10px;
    margin: 0 0 8px;
    background: rgba(0, 0, 0, 0.22);
  }

  .ag-mission-lbl {
    font-family: var(--font);
    font-size: 7px;
    color: var(--text-faint);
    margin-bottom: 4px;
  }

  .ag-mission-text,
  .ag-mission-cta,
  .done-row,
  .done-log,
  .honest {
    font-family: var(--font-mono);
    font-size: 12px;
    color: var(--text-primary);
    line-height: 1.5;
  }

  .ag-mission-cta {
    color: var(--text-secondary);
    font-style: italic;
  }

  .honest {
    color: var(--text-secondary);
    margin: 0 0 8px;
  }

  .done-row {
    display: flex;
    justify-content: space-between;
    gap: 8px;
    padding: 2px 0;
  }

  .done-title {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .done-log {
    color: var(--ok);
  }

  .chat-ts {
    color: var(--text-faint);
    margin-right: 6px;
  }

  @media (prefers-reduced-motion: reduce) {
    .ag-psweep,
    .ag-sdot.working,
    .ag-sdot.down {
      animation: none;
    }
  }
</style>
