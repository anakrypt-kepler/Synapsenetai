<script lang="ts">
  // RENT MVP: local share flag only. rental.list has no live marketplace.
  // rental.rent is a stub (fake id, no my_rentals). Do not invent GPU rows.
  import { onMount, onDestroy } from "svelte";
  import { rpcCall } from "../../lib/rpc";
  import { nodeStatus } from "../../lib/store";
  import PixelStage from "../components/sprites/PixelStage.svelte";
  import PixelSprite from "../components/sprites/PixelSprite.svelte";
  import gpuSprite from "../../assets/sprites/gpu.svg";

  interface GpuListing {
    node_id: string;
    gpu_name: string;
    vram_mb: number;
    price_ngt_hr: string;
    available: boolean;
    latency_ms: number;
    uptime_pct: number;
  }

  interface MyRental {
    rental_id: string;
    gpu_name: string;
    node_id: string;
    started: number;
    price_ngt_hr: string;
    status: string;
  }

  let listings: GpuListing[] = [];
  let myRentals: MyRental[] = [];
  let myGpuShared = false;
  let myGpuPrice = "1.0";
  let shareError = "";
  let shareOk = "";
  let listError = "";
  let pollHandle: ReturnType<typeof setInterval> | null = null;

  // Engine getStatus has no GPU field today. Never use model_name (that is the LLM).
  function gpuFromStatus(status: Record<string, unknown>): string {
    const keys = ["gpu_name", "gpu_device", "gpu", "gpu_model"];
    for (const k of keys) {
      const v = status[k];
      if (typeof v === "string" && v.trim()) return v.trim();
    }
    return "THIS MACHINE";
  }

  $: gpuLabel = gpuFromStatus($nodeStatus as unknown as Record<string, unknown>);

  onMount(async () => {
    await loadData();
    pollHandle = setInterval(loadData, 10000);
  });

  onDestroy(() => { if (pollHandle) clearInterval(pollHandle); });

  async function loadData() {
    listError = "";
    try {
      const result = await rpcCall("rental.list", "{}");
      const parsed = JSON.parse(result);
      if (parsed.error) {
        listError = String(parsed.error).toUpperCase();
        listings = [];
        myRentals = [];
        return;
      }
      // Engine rows only. This UI never injects ghost GPUs or a self-listing after share.
      listings = Array.isArray(parsed.listings) ? parsed.listings : [];
      myRentals = Array.isArray(parsed.my_rentals) ? parsed.my_rentals : [];
      myGpuShared = !!parsed.sharing;
      if (parsed.share_price) myGpuPrice = String(parsed.share_price);
    } catch (e: any) {
      listError = e.message || "LIST FAILED";
      listings = [];
      myRentals = [];
    }
  }

  async function setShare(enabled: boolean) {
    shareError = "";
    shareOk = "";
    try {
      const raw = await rpcCall("rental.share", JSON.stringify({
        enabled,
        price_ngt_hr: myGpuPrice,
      }));
      const parsed = JSON.parse(raw);
      if (parsed.error) {
        shareError = String(parsed.error).toUpperCase();
      } else {
        shareOk = enabled
          ? "OK — LOCAL FLAG ON. NOT ADVERTISED."
          : "OK — LOCAL FLAG OFF.";
      }
      await loadData();
    } catch (e: any) {
      shareError = e.message || "SHARE FAILED";
    }
  }

  function shortId(id: string): string {
    if (!id) return "—";
    if (id.length > 16) return id.slice(0, 8) + ".." + id.slice(-4);
    return id;
  }

  function fmtTime(ts: number): string {
    if (!ts) return "—";
    const d = new Date(ts);
    return `${d.getMonth() + 1}/${d.getDate()} ${d.getHours().toString().padStart(2, "0")}:${d.getMinutes().toString().padStart(2, "0")}`;
  }
</script>

<div class="content-area rent-page">
  <div class="page-col">
  <PixelStage height={60}>
    <PixelSprite src={gpuSprite} anim="park" size={26} label="gpu" />
  </PixelStage>
  <div class="info-box">
    <div class="info-title">LOCAL SHARE FLAG ONLY</div>
    <div class="info-line">SHARE GPU writes a local sharing flag and price. Other nodes cannot see it.</div>
    <div class="info-line">BROWSE stays empty until nodes advertise over Tor (not implemented).</div>
    <div class="info-line">This desktop does not invent listings. No marketplace. No state channels. No automatic payments.</div>
  </div>

  {#if listError}
    <div class="error-msg">{listError}</div>
  {/if}

  <div class="card">
    <div class="card-header">BROWSE</div>
  {#if listings.length === 0}
    <div class="empty-block">
      <div class="empty-head">NO REMOTE GPUs</div>
      <div class="hint">This desktop does not invent listings. Advertise over Tor is not implemented.</div>
    </div>
  {:else}
    <div class="hint warn-hint">Engine returned rows from disk. They are not a live marketplace. RENT is disabled until the engine persists rentals, advertises over Tor, and takes NGT.</div>
    <div class="table-wrap">
      <table>
        <thead>
          <tr><th>NODE</th><th>GPU</th><th>VRAM</th><th>PRICE</th><th>PING</th><th>UP</th><th></th></tr>
        </thead>
        <tbody>
          {#each listings as gpu}
            <tr>
              <td><code>{shortId(gpu.node_id)}</code></td>
              <td>{gpu.gpu_name || "—"}</td>
              <td>{gpu.vram_mb ? gpu.vram_mb + "MB" : "—"}</td>
              <td>{gpu.price_ngt_hr || "—"} NGT/H</td>
              <td>{gpu.latency_ms != null ? gpu.latency_ms + "ms" : "—"}</td>
              <td>{gpu.uptime_pct != null ? gpu.uptime_pct + "%" : "—"}</td>
              <td>
                <button class="btn-primary rent-btn" type="button" disabled title="rental.rent is a stub">[ RENT ]</button>
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
    <div class="stub-line">RENT DISABLED — NOT A REAL RENTAL. ENGINE STUB.</div>
  {/if}
  </div>

  <div class="card">
    <div class="card-header">MY RENTALS</div>
  {#if myRentals.length === 0}
    <div class="empty-block">
      <div class="empty-head">NO RENTALS</div>
      <div class="hint">rental.rent returns a fake id and does not persist my_rentals. Nothing to show.</div>
    </div>
  {:else}
    <div class="table-wrap">
      <table>
        <thead>
          <tr><th>ID</th><th>GPU</th><th>NODE</th><th>STARTED</th><th>PRICE</th><th>STATUS</th><th></th></tr>
        </thead>
        <tbody>
          {#each myRentals as rental}
            <tr>
              <td><code>{shortId(rental.rental_id)}</code></td>
              <td>{rental.gpu_name || "—"}</td>
              <td><code>{shortId(rental.node_id)}</code></td>
              <td>{fmtTime(rental.started)}</td>
              <td>{rental.price_ngt_hr || "—"} NGT/H</td>
              <td><span class="tag">{rental.status || "—"}</span></td>
              <td>
                <button class="btn-secondary" type="button" disabled title="rental.stop is a stub">[ STOP ]</button>
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
    <div class="stub-line">STOP DISABLED — ENGINE DOES NOT PERSIST RENTALS.</div>
  {/if}
  </div>

  <div class="section-title">SHARE GPU</div>
  <div class="card">
    <div class="card-header">THIS NODE — LOCAL FLAG</div>
    <div class="gpu-name">{gpuLabel}</div>
    <div class="share-status">
      STATUS: <span class:ok={myGpuShared} class:off={!myGpuShared}>{myGpuShared ? "FLAG ON" : "OFF"}</span>
    </div>
    <div class="form-group">
      <label>PRICE (NGT/HOUR) — STORED LOCALLY</label>
      <input type="text" bind:value={myGpuPrice} placeholder="1.0" />
    </div>
    {#if shareError}<div class="error-msg">{shareError}</div>{/if}
    {#if shareOk}<div class="success-msg">{shareOk}</div>{/if}
    <div class="share-actions">
      <button class="btn-primary" type="button" on:click={() => setShare(!myGpuShared)}>
        {myGpuShared ? "[ STOP SHARING ]" : "[ START SHARING ]"}
      </button>
      {#if myGpuShared}
        <button class="btn-secondary" type="button" on:click={() => setShare(true)}>[ SAVE PRICE ]</button>
      {/if}
    </div>
    <div class="hint">Persists via rental.share. Does not add a browse row. Nobody else can rent this until advertise-over-Tor exists.</div>
  </div>

  <div class="info-box">
    <div class="info-title">ENGINE NEEDS (NOT FAKED HERE)</div>
    <div class="info-line">1. Persist my_rentals when a rent succeeds. rental.rent must not be a disposable fake id.</div>
    <div class="info-line">2. Advertise the share flag over Tor so other nodes can fill rental.list.</div>
    <div class="info-line">3. Require an NGT payment before a rental is real. No free stub rent.</div>
  </div>
  </div>
</div>

<style>
  /* Flat black. Forms and cards only. Share price storage is unchanged. */
  .rent-page {
    font-family: var(--font-mono);
    font-size: 12px;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    padding-bottom: 72px;
  }

  .rent-page :global(.section-title) {
    font-family: var(--font);
    font-size: 9px;
    font-weight: 400;
    letter-spacing: 0;
  }

  .rent-page :global(.card) {
    border-radius: 0;
    background: none;
    backdrop-filter: none;
    -webkit-backdrop-filter: none;
  }

  .rent-page :global(.card-header) {
    font-family: var(--font);
    font-size: 9px;
    letter-spacing: 0;
  }

  .rent-page :global(button) {
    font-family: var(--font);
    font-size: 10px;
    font-weight: 400;
    letter-spacing: 0;
    border-radius: 0;
    -webkit-font-smoothing: antialiased;
    transition:
      background var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .rent-page :global(input) {
    font-family: var(--font-mono);
    font-size: 12px;
    letter-spacing: 0;
    border-radius: 0;
    -webkit-font-smoothing: antialiased;
  }

  .rent-page :global(.form-group label) {
    font-family: var(--font);
    font-size: 9px;
    letter-spacing: 0;
  }

  .rent-page :global(table) {
    font-size: 13px;
  }

  .rent-page :global(th) {
    font-family: var(--font);
    font-size: 9px;
    letter-spacing: 0;
  }

  .rent-page :global(code) {
    font-family: var(--font-mono, ui-monospace, "SF Mono", SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace);
    font-size: 12px;
  }

  .rent-page :global(.tag) {
    border-radius: var(--radius-sm, 10px);
    font-size: 11px;
    padding: 2px 8px;
  }

  .rent-page :global(.error-msg),
  .rent-page :global(.success-msg) {
    border-radius: var(--radius-sm, 10px);
    font-size: 13px;
  }

  .rent-btn {
    font-size: 12px;
    padding: 6px 12px;
  }

  .share-status {
    font-size: 13px;
    color: var(--text-primary);
    margin-bottom: 10px;
  }

  .ok { color: var(--ok); }
  .off { color: var(--text-secondary); }

  .gpu-name {
    font-size: 16px;
    font-weight: 600;
    color: var(--text-primary);
    margin-bottom: 8px;
    letter-spacing: 0;
  }

  .share-actions {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 8px;
  }

  .info-box {
    border: none;
    border-top: 1px solid var(--border);
    border-radius: 0;
    padding: 14px 2px;
    margin-top: 12px;
    background: none;
  }

  .info-title {
    font-family: var(--font);
    font-size: 9px;
    font-weight: 400;
    color: var(--text-primary);
    margin-bottom: 8px;
    letter-spacing: 0;
    line-height: 1.6;
  }

  .info-line {
    font-size: 13px;
    color: var(--text-secondary);
    line-height: 1.55;
  }

  .empty-block {
    border: none;
    border-top: 1px solid var(--border);
    border-radius: 0;
    padding: 20px 2px;
    text-align: center;
    background: none;
  }

  .table-wrap {
    border: none;
    border-top: 1px solid var(--border);
    border-radius: 0;
    overflow: hidden;
    background: none;
  }

  .empty-head {
    font-size: 13px;
    font-weight: 600;
    color: var(--text-primary);
    letter-spacing: 0;
    margin-bottom: 6px;
  }

  .hint {
    font-size: 12px;
    color: var(--text-secondary);
    line-height: 1.5;
  }

  .warn-hint {
    margin-bottom: 8px;
  }

  .stub-line {
    font-size: 12px;
    color: var(--err);
    margin-top: 8px;
    letter-spacing: 0;
  }
</style>
