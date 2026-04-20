<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { rpcCall } from "../../lib/rpc";
  import { nodeStatus } from "../../lib/store";

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
  let rentError = "";
  let rentSuccess = "";
  let tab: "browse" | "my" | "share" = "browse";
  let pollHandle: ReturnType<typeof setInterval> | null = null;

  onMount(async () => {
    await loadData();
    pollHandle = setInterval(loadData, 10000);
  });

  onDestroy(() => { if (pollHandle) clearInterval(pollHandle); });

  async function loadData() {
    try {
      const result = await rpcCall("rental.list", "{}");
      const parsed = JSON.parse(result);
      listings = parsed.listings || [];
      myRentals = parsed.my_rentals || [];
      myGpuShared = parsed.sharing || false;
      myGpuPrice = parsed.share_price || "1.0";
    } catch {}
  }

  async function rentGpu(nodeId: string) {
    rentError = "";
    rentSuccess = "";
    try {
      await rpcCall("rental.rent", JSON.stringify({ node_id: nodeId }));
      rentSuccess = "RENTAL STARTED";
      await loadData();
    } catch (e: any) {
      rentError = e.message || "RENTAL FAILED";
    }
  }

  async function stopRental(rentalId: string) {
    try {
      await rpcCall("rental.stop", JSON.stringify({ rental_id: rentalId }));
      await loadData();
    } catch {}
  }

  async function toggleShare() {
    try {
      await rpcCall("rental.share", JSON.stringify({
        enabled: !myGpuShared,
        price_ngt_hr: myGpuPrice,
      }));
      myGpuShared = !myGpuShared;
    } catch {}
  }

  function shortId(id: string): string {
    if (id.length > 16) return id.slice(0, 8) + ".." + id.slice(-4);
    return id;
  }

  function fmtTime(ts: number): string {
    const d = new Date(ts);
    return `${d.getMonth()+1}/${d.getDate()} ${d.getHours().toString().padStart(2,"0")}:${d.getMinutes().toString().padStart(2,"0")}`;
  }
</script>

<div class="content-area">
  <div class="tab-row">
    <button class="tbtn" class:active={tab === "browse"} on:click={() => (tab = "browse")}>BROWSE</button>
    <button class="tbtn" class:active={tab === "my"} on:click={() => (tab = "my")}>MY RENTALS</button>
    <button class="tbtn" class:active={tab === "share"} on:click={() => (tab = "share")}>SHARE GPU</button>
  </div>

  {#if rentError}
    <div class="error-msg">{rentError}</div>
  {/if}
  {#if rentSuccess}
    <div class="success-msg">{rentSuccess}</div>
  {/if}

  {#if tab === "browse"}
    <div class="section-title">AVAILABLE GPU NODES</div>
    <table>
      <thead>
        <tr><th>NODE</th><th>GPU</th><th>VRAM</th><th>PRICE</th><th>PING</th><th>UP</th><th></th></tr>
      </thead>
      <tbody>
        {#each listings as gpu}
          <tr>
            <td><code>{shortId(gpu.node_id)}</code></td>
            <td>{gpu.gpu_name}</td>
            <td>{gpu.vram_mb}MB</td>
            <td>{gpu.price_ngt_hr} NGT/H</td>
            <td>{gpu.latency_ms}ms</td>
            <td>{gpu.uptime_pct}%</td>
            <td>
              {#if gpu.available}
                <button class="btn-primary rent-btn" on:click={() => rentGpu(gpu.node_id)}>[ RENT ]</button>
              {:else}
                <span class="tag">BUSY</span>
              {/if}
            </td>
          </tr>
        {:else}
          <tr><td colspan="7" class="empty-row">NO GPU NODES AVAILABLE</td></tr>
        {/each}
      </tbody>
    </table>

    <div class="info-box">
      <div class="info-title">DECENTRALIZED GPU RENTAL</div>
      <div class="info-line">Rent GPU compute from other nodes using NGT.</div>
      <div class="info-line">All connections are E2E encrypted via Tor hidden services.</div>
      <div class="info-line">Payment is streamed per-minute via state channels.</div>
      <div class="info-line">No KYC. No middleman. Pure P2P.</div>
    </div>

  {:else if tab === "my"}
    <div class="section-title">ACTIVE RENTALS</div>
    <table>
      <thead>
        <tr><th>ID</th><th>GPU</th><th>NODE</th><th>STARTED</th><th>PRICE</th><th>STATUS</th><th></th></tr>
      </thead>
      <tbody>
        {#each myRentals as rental}
          <tr>
            <td><code>{shortId(rental.rental_id)}</code></td>
            <td>{rental.gpu_name}</td>
            <td><code>{shortId(rental.node_id)}</code></td>
            <td>{fmtTime(rental.started)}</td>
            <td>{rental.price_ngt_hr} NGT/H</td>
            <td><span class="tag">{rental.status}</span></td>
            <td>
              {#if rental.status === "ACTIVE"}
                <button class="btn-secondary" on:click={() => stopRental(rental.rental_id)}>[ STOP ]</button>
              {/if}
            </td>
          </tr>
        {:else}
          <tr><td colspan="7" class="empty-row">NO ACTIVE RENTALS</td></tr>
        {/each}
      </tbody>
    </table>

  {:else if tab === "share"}
    <div class="section-title">SHARE YOUR GPU</div>
    <div class="card">
      <div class="card-header">GPU SHARING</div>
      <div class="share-status">
        STATUS: <span class:ok={myGpuShared} class:off={!myGpuShared}>{myGpuShared ? "ACTIVE" : "OFF"}</span>
      </div>
      <div class="form-group">
        <label>PRICE (NGT/HOUR)</label>
        <input type="text" bind:value={myGpuPrice} placeholder="1.0" />
      </div>
      <button class="btn-primary" on:click={toggleShare}>
        {myGpuShared ? "[ STOP SHARING ]" : "[ START SHARING ]"}
      </button>
    </div>
    <div class="info-box">
      <div class="info-title">EARN NGT WITH YOUR GPU</div>
      <div class="info-line">Share your GPU compute power and earn NGT tokens.</div>
      <div class="info-line">Other nodes can rent your GPU for AI inference, training, and rendering.</div>
      <div class="info-line">Your data and GPU access are protected by quantum-resistant encryption.</div>
      <div class="info-line">Payments are automatic via the SynapseNet state channel protocol.</div>
    </div>
  {/if}
</div>

<style>
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

  .rent-btn {
    font-size: 8px;
    padding: 3px 8px;
  }

  .share-status {
    font-size: 10px;
    color: var(--text-primary);
    margin-bottom: 10px;
  }

  .ok { color: var(--ok); }
  .off { color: var(--text-secondary); }

  .info-box {
    border: 1px solid var(--border);
    padding: 12px;
    margin-top: 12px;
  }

  .info-title {
    font-size: 10px;
    font-weight: 700;
    color: var(--text-primary);
    margin-bottom: 6px;
    letter-spacing: 1px;
  }

  .info-line {
    font-size: 8px;
    color: var(--text-secondary);
    line-height: 2;
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 16px;
  }
</style>
