<script lang="ts" context="module">
  export type RosterRow = {
    id: string;
    name: string;
    portrait: string;
    level: number;
    working: boolean;
    harvest: boolean;
  };
</script>

<script lang="ts">
  // Dump #crew rail: portrait, name, Lv, WORKING/IDLE. Only the harvest
  // body can show WORKING — extra walkers do not mint a second loop.

  export let rows: RosterRow[] = [];
  export let focusedId: string = "primary";

  $: working = rows.filter((r) => r.working).length;
  $: idle = Math.max(0, rows.length - working);
</script>

<div class="crew-wrap">
  <div class="crew-head">CREW</div>
  <ul id="crew" class="crew">
    {#each rows as row (row.id)}
      <li>
        <button
          type="button"
          class="crew-row"
          class:selected={row.id === focusedId}
          class:working={row.working}
          aria-pressed={row.id === focusedId}
          on:click={() => (focusedId = row.id)}
        >
          <img class="crew-portrait" src={row.portrait} alt="" draggable="false" />
          <span class="dot" class:on={row.working}></span>
          <div class="crew-main">
            <div class="crew-name">
              {row.name}
              <span class="crew-room">Lv {row.level}</span>
            </div>
            <div class="crew-status">{row.working ? "WORKING" : "IDLE"}</div>
            <div class="crew-prog" aria-hidden="true"><div></div></div>
          </div>
        </button>
      </li>
    {:else}
      <li class="crew-empty">NO AGENTS ON STATION</li>
    {/each}
  </ul>
  <div class="crew-sum">
    <span class="pos">▮ {working} WORKING</span>
    <span class="dim">▯ {idle} IDLE</span>
  </div>
</div>

<style>
  .crew-wrap {
    min-width: 0;
  }

  .crew-head {
    font-family: var(--font);
    font-size: 9px;
    color: var(--text-secondary);
    margin-bottom: 8px;
  }

  .crew {
    list-style: none;
    margin: 0;
    padding: 0;
    max-height: 280px;
    overflow-y: auto;
  }

  .crew-row {
    display: flex;
    gap: 8px;
    width: 100%;
    text-align: left;
    padding: 6px 4px;
    border: 1px solid transparent;
    border-bottom: 1px solid var(--border);
    background: none;
    color: inherit;
    cursor: pointer;
    align-items: flex-start;
    font: inherit;
  }

  .crew-row:hover {
    border-color: var(--cy, #00e5ff);
    background: rgba(0, 229, 255, 0.06);
  }

  .crew-row.selected {
    border-left: 2px solid var(--cy, #00e5ff);
    background: rgba(0, 229, 255, 0.08);
  }

  .crew-portrait {
    width: 28px;
    height: 32px;
    object-fit: contain;
    object-position: bottom center;
    image-rendering: pixelated;
    flex: 0 0 28px;
  }

  .dot {
    width: 7px;
    height: 7px;
    background: var(--text-faint);
    margin-top: 8px;
    flex-shrink: 0;
  }

  .dot.on {
    background: var(--ok);
    box-shadow: 0 0 7px var(--ok);
    animation: pulse 2.4s infinite;
  }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.4; }
  }

  .crew-main {
    min-width: 0;
    flex: 1;
  }

  .crew-name {
    font-family: var(--font);
    font-size: 8px;
    color: var(--cy, #00e5ff);
    display: flex;
    align-items: baseline;
    gap: 8px;
  }

  .crew-room {
    font-size: 7px;
    color: var(--text-faint);
    margin-left: auto;
  }

  .crew-status {
    font-family: var(--font);
    font-size: 7px;
    color: var(--text-secondary);
    margin-top: 4px;
  }

  .crew-prog {
    height: 0;
    background: rgba(0, 229, 255, 0.12);
    margin-top: 0;
    overflow: hidden;
  }

  .crew-row.working .crew-prog {
    height: 3px;
    margin-top: 4px;
  }

  .crew-prog > div {
    height: 100%;
    width: 40%;
    background: var(--cy, #00e5ff);
    box-shadow: 0 0 5px var(--ph-glow, rgba(0, 229, 255, 0.5));
    animation: bar-sweep 1.1s linear infinite;
  }

  @keyframes bar-sweep {
    0% { transform: translateX(-120%); }
    100% { transform: translateX(280%); }
  }

  .crew-sum {
    display: flex;
    justify-content: space-between;
    gap: 6px;
    border-top: 1px solid var(--border);
    padding: 6px 4px;
    font-family: var(--font);
    font-size: 7px;
    color: var(--text-faint);
  }

  .pos {
    color: var(--ok);
  }

  .crew-empty {
    font-family: var(--font);
    font-size: 8px;
    color: var(--text-secondary);
    padding: 10px 4px;
  }

  @media (prefers-reduced-motion: reduce) {
    .dot.on,
    .crew-prog > div {
      animation: none;
    }
  }
</style>
