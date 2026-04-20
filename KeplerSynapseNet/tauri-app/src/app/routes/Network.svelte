<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { rpcCall } from "../../lib/rpc";

  interface PeerInfo {
    address: string;
    transport: string;
    latency_ms: number;
    connected_since: string;
  }

  let peers: PeerInfo[] = [];
  let torStatus = { bootstrap: "0%", circuits: 0, bridge_status: "none" };
  let discovery = { dns_queries: 0, peer_exchange: 0 };
  let bandwidth = { inbound_kbps: 0, outbound_kbps: 0 };
  let pollHandle: ReturnType<typeof setInterval> | null = null;

  onMount(async () => {
    await loadNetworkInfo();
    pollHandle = setInterval(loadNetworkInfo, 5000);
  });

  onDestroy(() => { if (pollHandle) clearInterval(pollHandle); });

  async function loadNetworkInfo() {
    try {
      const result = await rpcCall("network.info", "{}");
      const parsed = JSON.parse(result);
      peers = parsed.peers || [];
      torStatus = parsed.tor || torStatus;
      discovery = parsed.discovery || discovery;
      bandwidth = parsed.bandwidth || bandwidth;
    } catch {}
  }
</script>

<div class="content-area">
  <div class="section-title">PEERS</div>
  <table>
    <thead><tr><th>ADDRESS</th><th>TYPE</th><th>PING</th><th>SINCE</th></tr></thead>
    <tbody>
      {#each peers as peer}
        <tr>
          <td><code>{peer.address}</code></td>
          <td>{peer.transport}</td>
          <td>{peer.latency_ms}ms</td>
          <td>{peer.connected_since}</td>
        </tr>
      {:else}
        <tr><td colspan="4" class="empty-row">NO PEERS</td></tr>
      {/each}
    </tbody>
  </table>

  <div class="section-title">TOR</div>
  <div class="grid-3">
    <div class="card">
      <div class="card-header">BOOTSTRAP</div>
      <div class="card-value">{torStatus.bootstrap}</div>
    </div>
    <div class="card">
      <div class="card-header">CIRCUITS</div>
      <div class="card-value">{torStatus.circuits}</div>
    </div>
    <div class="card">
      <div class="card-header">BRIDGES</div>
      <div class="card-value">{torStatus.bridge_status.toUpperCase()}</div>
    </div>
  </div>

  <div class="section-title">DISCOVERY</div>
  <div class="grid-2">
    <div class="card">
      <div class="card-header">DNS</div>
      <div class="card-value">{discovery.dns_queries}</div>
    </div>
    <div class="card">
      <div class="card-header">PEX</div>
      <div class="card-value">{discovery.peer_exchange}</div>
    </div>
  </div>

  <div class="section-title">BANDWIDTH</div>
  <div class="grid-2">
    <div class="card">
      <div class="card-header">IN</div>
      <div class="card-value">{bandwidth.inbound_kbps} KB/s</div>
    </div>
    <div class="card">
      <div class="card-header">OUT</div>
      <div class="card-value">{bandwidth.outbound_kbps} KB/s</div>
    </div>
  </div>

  <div class="section-title">PEER MAP</div>
  <div class="peer-map">
    <svg viewBox="0 0 400 160" width="100%" height="140">
      <rect width="400" height="160" fill="none" stroke="var(--border)" stroke-width="1" />
      {#each peers as peer, i}
        <rect
          x={46 + (i % 8) * 45}
          y={30 + Math.floor(i / 8) * 40}
          width="8" height="8"
          fill="var(--text-primary)"
        />
        <text
          x={50 + (i % 8) * 45}
          y={50 + Math.floor(i / 8) * 40}
          fill="var(--text-secondary)"
          font-size="6"
          font-family="Silkscreen, monospace"
          text-anchor="middle"
        >
          {peer.address.slice(0, 6)}
        </text>
      {/each}
      {#if peers.length === 0}
        <text x="200" y="85" fill="var(--text-secondary)" font-size="10" font-family="Silkscreen, monospace" text-anchor="middle">
          NO PEERS
        </text>
      {/if}
    </svg>
  </div>
</div>

<style>
  .peer-map {
    border: 1px solid var(--border);
    padding: 8px;
    margin-top: 4px;
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 16px;
  }
</style>
