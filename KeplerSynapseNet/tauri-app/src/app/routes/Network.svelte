<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { nodeStatus } from "../../lib/store";
  import { rpcCall } from "../../lib/rpc";

  let peers: { address: string; transport: string; latency: number }[] = [];
  let torStatus = {
    bootstrap: "",
    circuits: 0,
    bridge_status: "",
  };
  let discovery = {
    dns_queries: 0,
    peer_exchange: 0,
  };
  let bandwidth = {
    inbound: 0,
    outbound: 0,
  };

  let peerMapNodes: { x: number; y: number }[] = [];
  let pollHandle: ReturnType<typeof setInterval> | null = null;

  onMount(async () => {
    await loadNetworkData();
    pollHandle = setInterval(loadNetworkData, 3000);
  });

  onDestroy(() => {
    if (pollHandle) clearInterval(pollHandle);
  });

  async function loadNetworkData() {
    try {
      const result = await rpcCall("network.info", "{}");
      const parsed = JSON.parse(result);
      peers = parsed.peers || [];
      torStatus = parsed.tor || torStatus;
      discovery = parsed.discovery || discovery;
      bandwidth = parsed.bandwidth || bandwidth;
      generatePeerMap();
    } catch {}
  }

  function generatePeerMap() {
    const rng = (seed: number) => {
      let x = Math.sin(seed) * 10000;
      return x - Math.floor(x);
    };
    peerMapNodes = peers.map((_, i) => ({
      x: rng(i * 7 + 3) * 280 + 10,
      y: rng(i * 13 + 7) * 130 + 10,
    }));
  }
</script>

<div class="content-area">
  <div class="section-title">Connected Peers</div>
  <table>
    <thead>
      <tr>
        <th>Address</th>
        <th>Transport</th>
        <th>Latency (ms)</th>
      </tr>
    </thead>
    <tbody>
      {#each peers as peer}
        <tr>
          <td><code>{peer.address}</code></td>
          <td><span class="tag">{peer.transport}</span></td>
          <td>{peer.latency}</td>
        </tr>
      {:else}
        <tr>
          <td colspan="3" class="empty-row">No peers connected</td>
        </tr>
      {/each}
    </tbody>
  </table>

  <div class="grid-2">
    <div class="card">
      <div class="card-header">Tor Status</div>
      <div class="tor-info">
        <div class="info-row">
          <span class="info-label">Bootstrap</span>
          <span class="info-value">{torStatus.bootstrap || "N/A"}</span>
        </div>
        <div class="info-row">
          <span class="info-label">Circuits</span>
          <span class="info-value">{torStatus.circuits}</span>
        </div>
        <div class="info-row">
          <span class="info-label">Bridges</span>
          <span class="info-value">{torStatus.bridge_status || "N/A"}</span>
        </div>
      </div>
    </div>
    <div class="card">
      <div class="card-header">Discovery</div>
      <div class="tor-info">
        <div class="info-row">
          <span class="info-label">DNS Queries</span>
          <span class="info-value">{discovery.dns_queries}</span>
        </div>
        <div class="info-row">
          <span class="info-label">Peer Exchange</span>
          <span class="info-value">{discovery.peer_exchange}</span>
        </div>
      </div>
    </div>
  </div>

  <div class="grid-2">
    <div class="card">
      <div class="card-header">Inbound</div>
      <div class="card-value">{bandwidth.inbound.toFixed(1)} KB/s</div>
    </div>
    <div class="card">
      <div class="card-header">Outbound</div>
      <div class="card-value">{bandwidth.outbound.toFixed(1)} KB/s</div>
    </div>
  </div>

  <div class="section-title">Peer Map</div>
  <div class="peer-map-container">
    <svg class="peer-map" viewBox="0 0 300 150">
      {#each peerMapNodes as node}
        <circle cx={node.x} cy={node.y} r="3" fill="var(--text-secondary)" />
      {/each}
      {#if peerMapNodes.length === 0}
        <text x="150" y="75" text-anchor="middle" fill="var(--text-secondary)" font-size="10">No peers</text>
      {/if}
    </svg>
  </div>
</div>

<style>
  .tor-info {
    display: flex;
    flex-direction: column;
    gap: 8px;
    margin-top: 8px;
  }

  .info-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .info-label {
    font-size: 11px;
    color: var(--text-secondary);
  }

  .info-value {
    font-size: 12px;
    color: var(--text-primary);
  }

  .peer-map-container {
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 8px;
    background: var(--surface);
    box-shadow: var(--shadow-sm);
  }

  .peer-map {
    width: 100%;
    height: 150px;
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 20px;
  }
</style>
