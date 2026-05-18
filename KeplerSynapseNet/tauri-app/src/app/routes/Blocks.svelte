<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { rpcCall } from "../../lib/rpc";
  import { nodeStatus } from "../../lib/store";

  interface BlockEntry {
    height: number;
    hash: string;
    prev_hash: string;
    timestamp: number;
    producer: string;
    events: number;
    difficulty: number;
    nonce: number;
    size: number;
  }

  interface BlockDetail {
    height: number;
    hash: string;
    prev_hash: string;
    timestamp: number;
    producer: string;
    difficulty: number;
    nonce: number;
    events: { type: string; author: string; hash: string; ts: number }[];
  }

  interface ProducerStat {
    address: string;
    blocks: number;
    last_block: number;
  }

  let blocks: BlockEntry[] = [];
  let chainHeight = 0;
  let totalEvents = 0;
  let avgEventsPerBlock = 0;
  let selectedBlock: BlockDetail | null = null;
  let producers: ProducerStat[] = [];
  let tab: "chain" | "producers" = "chain";
  let pollHandle: ReturnType<typeof setInterval> | null = null;

  onMount(async () => {
    await loadBlocks();
    pollHandle = setInterval(loadBlocks, 5000);
  });

  onDestroy(() => { if (pollHandle) clearInterval(pollHandle); });

  async function loadBlocks() {
    try {
      const result = await rpcCall("blocks.list", "{}");
      const parsed = JSON.parse(result);
      blocks = parsed.blocks || [];
      chainHeight = parsed.height || 0;
      totalEvents = parsed.total_events || 0;
      avgEventsPerBlock = parsed.avg_events_per_block || 0;
      producers = parsed.producers || [];
    } catch {}
  }

  async function viewBlock(height: number) {
    try {
      const result = await rpcCall("blocks.get", JSON.stringify({ height }));
      selectedBlock = JSON.parse(result);
    } catch {}
  }

  function closeDetail() {
    selectedBlock = null;
  }

  function shortHash(h: string): string {
    if (!h || h.length < 16) return h || "---";
    return h.slice(0, 8) + ".." + h.slice(-6);
  }

  function shortAddr(a: string): string {
    if (!a || a.length < 16) return a || "---";
    return a.slice(0, 10) + ".." + a.slice(-4);
  }

  function fmtTime(ts: number): string {
    if (!ts) return "---";
    const d = new Date(ts);
    return `${d.getMonth()+1}/${d.getDate()} ${d.getHours().toString().padStart(2,"0")}:${d.getMinutes().toString().padStart(2,"0")}:${d.getSeconds().toString().padStart(2,"0")}`;
  }

  function eventTypeLabel(t: string): string {
    const map: Record<string, string> = {
      "knowledge": "KNOW", "transfer": "TX", "validation": "VOTE",
      "poe_entry": "POE", "poe_vote": "PVOTE", "genesis": "GEN",
      "model_register": "MODEL", "penalty": "SLASH", "identity_bind": "ID"
    };
    return map[t] || t.toUpperCase();
  }
</script>

<div class="content-area">
  <div class="grid-4">
    <div class="card">
      <div class="card-header">HEIGHT</div>
      <div class="card-value">{$nodeStatus.last_block || chainHeight}</div>
    </div>
    <div class="card">
      <div class="card-header">TOTAL EVENTS</div>
      <div class="card-value">{totalEvents}</div>
    </div>
    <div class="card">
      <div class="card-header">AVG EVT/BLK</div>
      <div class="card-value">{avgEventsPerBlock}</div>
    </div>
    <div class="card">
      <div class="card-header">PRODUCERS</div>
      <div class="card-value">{producers.length}</div>
    </div>
  </div>

  <div class="tab-row">
    <button class="tbtn" class:active={tab === "chain"} on:click={() => { tab = "chain"; selectedBlock = null; }}>CHAIN</button>
    <button class="tbtn" class:active={tab === "producers"} on:click={() => { tab = "producers"; selectedBlock = null; }}>PRODUCERS</button>
  </div>

  {#if selectedBlock}
    <div class="detail-panel">
      <div class="detail-header">
        <span>BLOCK #{selectedBlock.height}</span>
        <button class="btn-secondary" on:click={closeDetail}>[ CLOSE ]</button>
      </div>
      <div class="detail-grid">
        <div class="detail-row"><span class="dl">HASH</span><code class="dv">{selectedBlock.hash}</code></div>
        <div class="detail-row"><span class="dl">PREV</span><code class="dv">{selectedBlock.prev_hash}</code></div>
        <div class="detail-row"><span class="dl">PRODUCER</span><code class="dv">{selectedBlock.producer}</code></div>
        <div class="detail-row"><span class="dl">TIME</span><span class="dv">{fmtTime(selectedBlock.timestamp)}</span></div>
        <div class="detail-row"><span class="dl">DIFFICULTY</span><span class="dv">{selectedBlock.difficulty}</span></div>
        <div class="detail-row"><span class="dl">NONCE</span><span class="dv">{selectedBlock.nonce}</span></div>
      </div>
      <div class="section-title">EVENTS ({selectedBlock.events?.length || 0})</div>
      <table>
        <thead><tr><th>TYPE</th><th>AUTHOR</th><th>HASH</th><th>TIME</th></tr></thead>
        <tbody>
          {#each selectedBlock.events || [] as evt}
            <tr>
              <td><span class="tag evt-{evt.type}">{eventTypeLabel(evt.type)}</span></td>
              <td><code>{shortAddr(evt.author)}</code></td>
              <td><code>{shortHash(evt.hash)}</code></td>
              <td>{fmtTime(evt.ts)}</td>
            </tr>
          {:else}
            <tr><td colspan="4" class="empty-row">NO EVENTS</td></tr>
          {/each}
        </tbody>
      </table>
    </div>

  {:else if tab === "chain"}
    <div class="section-title">RECENT BLOCKS</div>
    <table>
      <thead><tr><th>#</th><th>HASH</th><th>PRODUCER</th><th>EVENTS</th><th>TIME</th><th></th></tr></thead>
      <tbody>
        {#each blocks as block}
          <tr>
            <td class="blk-h">{block.height}</td>
            <td><code>{shortHash(block.hash)}</code></td>
            <td><code>{shortAddr(block.producer)}</code></td>
            <td>{block.events}</td>
            <td>{fmtTime(block.timestamp)}</td>
            <td><button class="btn-link" on:click={() => viewBlock(block.height)}>VIEW</button></td>
          </tr>
        {:else}
          <tr><td colspan="6" class="empty-row">NO BLOCKS YET</td></tr>
        {/each}
      </tbody>
    </table>

  {:else if tab === "producers"}
    <div class="section-title">BLOCK PRODUCERS</div>
    <table>
      <thead><tr><th>#</th><th>ADDRESS</th><th>BLOCKS</th><th>LAST BLOCK</th></tr></thead>
      <tbody>
        {#each producers as p, i}
          <tr>
            <td>{i + 1}</td>
            <td><code>{shortAddr(p.address)}</code></td>
            <td class="blk-count">{p.blocks}</td>
            <td>{fmtTime(p.last_block)}</td>
          </tr>
        {:else}
          <tr><td colspan="4" class="empty-row">NO PRODUCERS</td></tr>
        {/each}
      </tbody>
    </table>
  {/if}
</div>

<style>
  .grid-4 {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 8px;
    margin-bottom: 12px;
  }

  .tab-row {
    display: flex;
    gap: 2px;
    margin-bottom: 12px;
  }

  .tbtn {
    font-size: 8px;
    padding: 6px 14px;
    border: 1px solid var(--border);
    color: var(--text-secondary);
    background: none;
    letter-spacing: 1px;
  }

  .tbtn:hover { color: var(--text-primary); border-color: var(--text-primary); }

  .tbtn.active {
    color: #000;
    background: var(--text-primary);
    border-color: var(--text-primary);
  }

  .blk-h {
    font-weight: 700;
    color: var(--text-primary);
  }

  .blk-count {
    font-weight: 700;
    color: var(--ok);
  }

  .btn-link {
    font-size: 8px;
    color: var(--text-primary);
    background: none;
    border: 1px solid var(--border);
    padding: 2px 8px;
    letter-spacing: 1px;
  }

  .btn-link:hover {
    border-color: var(--text-primary);
  }

  .detail-panel {
    border: 1px solid var(--border);
    padding: 12px;
  }

  .detail-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 12px;
    font-weight: 700;
    color: var(--text-primary);
    margin-bottom: 10px;
    letter-spacing: 1px;
  }

  .detail-grid {
    margin-bottom: 12px;
  }

  .detail-row {
    display: flex;
    gap: 12px;
    padding: 4px 0;
    border-bottom: 1px solid var(--border);
    font-size: 8px;
  }

  .dl {
    color: var(--text-secondary);
    font-weight: 700;
    min-width: 80px;
    letter-spacing: 1px;
  }

  .dv {
    color: var(--text-primary);
    word-break: break-all;
  }

  .tag {
    font-size: 8px;
    padding: 1px 6px;
    border: 1px solid var(--border);
    letter-spacing: 0.5px;
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 16px;
  }
</style>
