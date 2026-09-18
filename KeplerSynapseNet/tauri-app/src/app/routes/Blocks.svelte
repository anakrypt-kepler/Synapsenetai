<script lang="ts">
  // Local PoE explorer. Height is this node's blocks.jsonl + naan_blocks.jsonl,
  // not a Bitcoin-style network sync and not hash mining.
  import { onMount, onDestroy } from "svelte";
  import { rpcCall } from "../../lib/rpc";
  import { nodeStatus, myWalletAddress } from "../../lib/store";
  import PixelStage from "../components/sprites/PixelStage.svelte";
  import PixelBricks from "../components/sprites/PixelBricks.svelte";

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

  interface BlockEvent {
    type: string;
    author: string;
    hash: string;
    ts: number;
  }

  interface BlockDetail {
    height: number;
    hash: string;
    prev_hash: string;
    timestamp: number;
    producer: string;
    difficulty: number;
    nonce: number;
    events: BlockEvent[];
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
  let myAddressLocal = "";
  $: myAddress = $myWalletAddress || myAddressLocal;
  // Prefer blocks.list height; fall back to status last_block (localChainHeight).
  $: heightShown = chainHeight > 0 ? chainHeight : ($nodeStatus.last_block || 0);

  onMount(async () => {
    await loadBlocks();
    try {
      const info = JSON.parse(await rpcCall("wallet.info", "{}"));
      myAddressLocal = info.address || "";
    } catch {}
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
      const parsed = JSON.parse(result);
      if (!parsed || parsed.error) return;
      const ev = parsed.events;
      const detail = parsed.events_detail;
      if (Array.isArray(ev)) parsed.events = ev;
      else if (Array.isArray(detail)) parsed.events = detail;
      else parsed.events = [];
      selectedBlock = parsed;
    } catch {}
  }

  function closeDetail() {
    selectedBlock = null;
  }

  function isMyBlock(producer: string): boolean {
    if (!myAddress || !producer) return false;
    return producer === myAddress || producer.startsWith(myAddress.slice(0, 12));
  }

  function shortHash(h: string): string {
    if (!h || h.length < 16) return h || "---";
    return h.slice(0, 8) + ".." + h.slice(-6);
  }

  function shortAddr(a: string): string {
    if (!a || a.length < 16) return a || "---";
    return a.slice(0, 10) + ".." + a.slice(-4);
  }

  // blocks.list timestamps are unix ms (nowMillis) or seconds on older/seed rows.
  function toMillis(ts: number): number {
    if (!ts || ts < 0) return 0;
    if (ts < 1e11) return ts * 1000;
    return ts;
  }

  function fmtTime(ts: number): string {
    const ms = toMillis(ts);
    if (!ms) return "---";
    const d = new Date(ms);
    if (isNaN(d.getTime()) || d.getFullYear() < 2000) return "---";
    const y = d.getFullYear();
    const mo = (d.getMonth() + 1).toString().padStart(2, "0");
    const da = d.getDate().toString().padStart(2, "0");
    const hh = d.getHours().toString().padStart(2, "0");
    const mm = d.getMinutes().toString().padStart(2, "0");
    const ss = d.getSeconds().toString().padStart(2, "0");
    return `${y}-${mo}-${da} ${hh}:${mm}:${ss}`;
  }

  function eventCount(block: BlockEntry): number {
    const e: unknown = block.events;
    if (typeof e === "number" && isFinite(e)) return e;
    if (Array.isArray(e)) return e.length;
    return 0;
  }

  function powUnused(difficulty: number, nonce: number): boolean {
    const d = difficulty ?? 0;
    const n = nonce ?? 0;
    return (d === 0 || d === 1) && n === 0;
  }

  function eventTypeLabel(t: string): string {
    const key = (t || "").toLowerCase();
    const map: Record<string, string> = {
      "knowledge": "KNOW", "transfer": "TX", "validation": "VOTE",
      "poe_entry": "POE", "poe_vote": "PVOTE", "genesis": "GEN",
      "model_register": "MODEL", "penalty": "SLASH", "identity_bind": "ID",
      "stealth_sent": "PRIV", "stealth": "PRIV", "ring_signed": "RING",
      "confidential": "CT", "code": "CODE", "poe_code": "CODE",
      "naan": "NAAN", "ide": "CODE"
    };
    return map[key] || (t || "---").toUpperCase();
  }
</script>

<div class="content-area">
  <div class="page-col">
  <PixelStage height={64} floor={false}>
    <PixelBricks count={heightShown} max={16} />
  </PixelStage>
  <div class="section-title">BLOCKS</div>
  <div class="grid-4">
    <div class="card">
      <div class="card-header">HEIGHT</div>
      <div class="card-value">{heightShown}</div>
      <div class="hint">Local PoE height. Not hash mining. Not waiting on Tor.</div>
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
        <div class="detail-row"><span class="dl">HASH</span><code class="dv mono">{selectedBlock.hash}</code></div>
        <div class="detail-row"><span class="dl">PREV</span><code class="dv mono">{selectedBlock.prev_hash}</code></div>
        <div class="detail-row">
          <span class="dl">PRODUCER</span>
          <code class="dv mono">{selectedBlock.producer}</code>
          {#if isMyBlock(selectedBlock.producer)}<span class="mine-badge">MINE</span>{/if}
        </div>
        <div class="detail-row"><span class="dl">TIME</span><span class="dv">{fmtTime(selectedBlock.timestamp)}</span></div>
        {#if powUnused(selectedBlock.difficulty, selectedBlock.nonce)}
          <div class="detail-row leftover">
            <span class="dl">POW LEFTOVERS</span>
            <span class="dv">difficulty={selectedBlock.difficulty ?? 1} nonce={selectedBlock.nonce ?? 0} — unused on this PoE chain</span>
          </div>
        {:else}
          <div class="detail-row leftover">
            <span class="dl">DIFFICULTY</span>
            <span class="dv">{selectedBlock.difficulty} (PoW leftover field)</span>
          </div>
          <div class="detail-row leftover">
            <span class="dl">NONCE</span>
            <span class="dv">{selectedBlock.nonce} (PoW leftover field)</span>
          </div>
        {/if}
      </div>
      <div class="section-title">EVENTS ({selectedBlock.events?.length || 0})</div>
      <div class="table-wrap">
        <table>
          <thead><tr><th>TYPE</th><th>AUTHOR</th><th>HASH</th><th>TIME</th></tr></thead>
          <tbody>
            {#each selectedBlock.events || [] as evt}
              <tr>
                <td><span class="tag evt-{evt.type}">{eventTypeLabel(evt.type)}</span></td>
                <td><code class="mono">{shortAddr(evt.author)}</code></td>
                <td><code class="mono">{shortHash(evt.hash)}</code></td>
                <td>{fmtTime(evt.ts)}</td>
              </tr>
            {:else}
              <tr><td colspan="4" class="empty-row">NO EVENTS</td></tr>
            {/each}
          </tbody>
        </table>
      </div>
    </div>

  {:else if tab === "chain"}
    <div class="card">
      <div class="card-header">RECENT BLOCKS — POE CHAIN, NOT POW</div>
    <div class="table-wrap">
      <table>
        <thead><tr><th>#</th><th>HASH</th><th>PRODUCER</th><th>EVENTS</th><th>TIME</th><th></th></tr></thead>
        <tbody>
          {#each blocks as block}
            <tr class:own-block={isMyBlock(block.producer)}>
              <td class="blk-h">{block.height}</td>
              <td><code class="mono">{shortHash(block.hash)}</code></td>
              <td>
                <code class="mono">{shortAddr(block.producer)}</code>
                {#if isMyBlock(block.producer)}<span class="mine-badge">MINE</span>{/if}
              </td>
              <td>{eventCount(block)}</td>
              <td>{fmtTime(block.timestamp)}</td>
              <td><button class="btn-link" on:click={() => viewBlock(block.height)}>VIEW</button></td>
            </tr>
          {:else}
            <tr>
              <td colspan="6" class="empty-row">
                <div>NO LOCAL POE BLOCKS YET</div>
                <div class="empty-hint">This node has a local chain. Height 0 means the jsonl files are empty, not that Tor/NAAN is down. Submit KNOW, IDE, or NAAN to write a block.</div>
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
    </div>

  {:else if tab === "producers"}
    <div class="card">
      <div class="card-header">POE PRODUCERS — NOT HASH MINERS</div>
    <div class="table-wrap">
      <table>
        <thead><tr><th>#</th><th>ADDRESS</th><th>BLOCKS</th><th>LAST TIME</th></tr></thead>
        <tbody>
          {#each producers as p, i}
            <tr class:own-producer={isMyBlock(p.address)}>
              <td>{i + 1}</td>
              <td>
                <code class="mono">{shortAddr(p.address)}</code>
                {#if isMyBlock(p.address)}<span class="mine-badge">MINE</span>{/if}
              </td>
              <td class="blk-count">{p.blocks}</td>
              <td>{fmtTime(p.last_block)}</td>
            </tr>
          {:else}
            <tr>
              <td colspan="4" class="empty-row">
                <div>NO PRODUCERS YET</div>
                <div class="empty-hint">Producers are wallets that wrote local PoE blocks, not hash miners.</div>
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
    </div>
  {/if}
  </div>
</div>

<style>
  /* Body/data stays readable mono; chrome is pixel via tokens. */
  .content-area {
    font-family: var(--font-mono);
    font-size: 12px;
    color: var(--text-primary);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
  }

  .mono {
    font-family: var(--font-mono, ui-monospace, "SF Mono", Menlo, Consolas, monospace);
  }

  button {
    font-family: var(--font);
    font-size: 10px;
    letter-spacing: 0;
    border-radius: 0;
    transition:
      border-color var(--dur) var(--ease),
      background-color var(--dur) var(--ease),
      color var(--dur) var(--ease);
  }

  button:hover:not(:disabled) {
    box-shadow: none;
  }

  button:focus-visible {
    outline: 2px solid var(--text-primary);
    outline-offset: 2px;
  }

  .card,
  .detail-panel,
  .table-wrap {
    border-radius: 0;
    background: none;
    border: none;
    border-top: 1px solid var(--border);
  }

  .card:hover {
    border-color: rgba(255, 255, 255, 0.18);
  }

  .card-header,
  .section-title {
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif);
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.08em;
    color: var(--text-faint);
  }

  .card-value {
    font-size: 22px;
    letter-spacing: -0.02em;
  }

  table {
    font-family: var(--font-mono);
    font-size: 12px;
  }

  th, td {
    font-size: 13px;
    padding: 10px 12px;
  }

  thead th {
    position: sticky;
    top: 0;
    z-index: 2;
    background: var(--surface-solid, #111);
    font-size: 11px;
    letter-spacing: 0.06em;
    color: var(--text-faint);
  }

  .table-wrap {
    overflow: auto;
    max-height: min(560px, 62vh);
  }

  .grid-4 {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
    gap: 10px;
    margin-bottom: 12px;
  }

  .hint {
    margin-top: 6px;
    font-size: 12px;
    color: var(--text-secondary);
    letter-spacing: 0.01em;
    line-height: 1.45;
  }

  .tab-row {
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
    margin-bottom: 12px;
  }

  .tbtn {
    font-size: 12px;
    padding: 7px 14px;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text-secondary);
    background: var(--surface);
    letter-spacing: 0.06em;
  }

  .tbtn:hover { color: var(--text-primary); border-color: rgba(255, 255, 255, 0.22); }

  .tbtn.active {
    color: #000;
    background: var(--text-primary);
    border-color: var(--text-primary);
  }

  .blk-h {
    font-weight: 650;
    color: var(--text-primary);
  }

  .blk-count {
    font-weight: 650;
    color: var(--ok);
  }

  .btn-link {
    font-size: 11px;
    color: var(--text-primary);
    background: none;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: 4px 10px;
    letter-spacing: 0.04em;
  }

  .btn-link:hover {
    border-color: var(--text-primary);
  }

  .detail-panel {
    padding: 14px;
  }

  .detail-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 15px;
    font-weight: 650;
    color: var(--text-primary);
    margin-bottom: 12px;
    letter-spacing: 0.04em;
  }

  .detail-grid {
    margin-bottom: 12px;
  }

  .detail-row {
    display: flex;
    gap: 12px;
    padding: 8px 0;
    border-bottom: 1px solid var(--border);
    font-size: 13px;
    align-items: baseline;
  }

  .dl {
    color: var(--text-secondary);
    font-weight: 650;
    min-width: 108px;
    letter-spacing: 0.06em;
    font-size: 11px;
  }

  .dv {
    color: var(--text-primary);
    word-break: break-all;
    font-size: 12px;
  }

  .leftover .dv,
  .leftover .dl {
    color: var(--text-faint);
    font-weight: 400;
  }

  .tag {
    font-family: var(--font);
    font-size: 8px;
    padding: 3px 7px;
    border: 1px solid var(--border);
    border-radius: 0;
    letter-spacing: 0;
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 28px 16px;
    font-size: 13px;
    letter-spacing: 0.02em;
  }

  .empty-hint {
    margin-top: 8px;
    font-size: 12px;
    color: var(--text-faint);
    letter-spacing: 0.01em;
    line-height: 1.5;
    max-width: 52em;
    margin-left: auto;
    margin-right: auto;
    text-transform: none;
  }

  .own-block {
    background: color-mix(in srgb, var(--text-primary) 8%, transparent);
    border-left: 2px solid var(--text-primary);
  }

  .own-producer {
    background: color-mix(in srgb, var(--text-primary) 8%, transparent);
  }

  .mine-badge {
    font-family: var(--font);
    font-size: 8px;
    font-weight: 400;
    padding: 3px 6px;
    border-radius: 0;
    background: var(--text-primary);
    color: #000;
    letter-spacing: 0;
    margin-left: 6px;
    display: inline-block;
    vertical-align: middle;
  }
</style>
