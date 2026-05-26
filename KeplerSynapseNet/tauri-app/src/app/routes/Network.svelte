<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { rpcCall } from "../../lib/rpc";

  interface PeerInfo {
    address: string;
    transport: string;
    latency_ms: number;
    connected_since: string;
    online?: boolean;
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

  $: connectionCount = peers.filter((p) => p.connected_since !== "LOCAL" && p.connected_since !== "local").length;
  $: isOnline = peers.some((p) => (p.connected_since === "SEED" || p.connected_since === "seed") && p.online);
</script>

<div class="content-area">
  <div class="section-title peers-header">
    <span>PEERS — {connectionCount} CONNECTED</span>
    <span class="net-status {isOnline ? 'online' : 'connecting'}">
      {isOnline ? "● ONLINE" : "○ CONNECTING"}
    </span>
  </div>
  <table>
    <thead><tr><th></th><th>ADDRESS</th><th>TYPE</th><th>PING</th><th>SINCE</th></tr></thead>
    <tbody>
      {#each peers as peer}
        <tr>
          <td class="dot-cell"><span class="peer-dot {peer.online ? 'online' : 'offline'}">{peer.online ? "●" : "○"}</span></td>
          <td><code>{peer.address}</code></td>
          <td>{peer.transport}</td>
          <td>{peer.latency_ms}ms</td>
          <td>{peer.connected_since}</td>
        </tr>
      {:else}
        <tr><td colspan="5" class="empty-row">NO PEERS</td></tr>
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
        <circle
          cx={50 + (i % 8) * 45}
          cy={34 + Math.floor(i / 8) * 40}
          r="5"
          fill={peer.online ? "#00c853" : "#444"}
        />
        <text
          x={50 + (i % 8) * 45}
          y={52 + Math.floor(i / 8) * 40}
          fill={peer.online ? "var(--text-secondary)" : "#555"}
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
  .peers-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .net-status {
    font-family: Silkscreen, monospace;
    font-size: 0.85em;
    letter-spacing: 1px;
  }

  .net-status.online {
    color: #00c853;
  }

  .net-status.connecting {
    color: var(--text-secondary);
  }

  .dot-cell {
    width: 14px;
    text-align: center;
    padding: 0 2px;
  }

  .peer-dot {
    font-size: 10px;
  }

  .peer-dot.online {
    color: #00c853;
  }

  .peer-dot.offline {
    color: #555;
  }

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
