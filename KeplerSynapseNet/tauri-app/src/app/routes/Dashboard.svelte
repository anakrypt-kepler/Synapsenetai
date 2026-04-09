<script lang="ts">
  import { nodeStatus, activeTab } from "../../lib/store";

  function goSend() {
    activeTab.set("transfers");
  }

  function goKnowledge() {
    activeTab.set("knowledge");
  }

  function goIde() {
    activeTab.set("ide");
  }
</script>

<div class="content-area">
  <div class="dashboard-balance">
    <div class="card-header">NGT Balance</div>
    <div class="balance-value">{$nodeStatus.balance} <span class="balance-unit">NGT</span></div>
  </div>

  <div class="grid-2">
    <div class="card">
      <div class="card-header">NAAN Agent</div>
      <div class="card-value">{$nodeStatus.naan_state.toUpperCase()}</div>
    </div>
    <div class="card">
      <div class="card-header">Connected Peers</div>
      <div class="card-value">{$nodeStatus.peers}</div>
    </div>
  </div>

  <div class="grid-2">
    <div class="card">
      <div class="card-header">Connection</div>
      <div class="card-value">
        {#if $nodeStatus.connection === "tor"}
          <span class="status-dot green"></span> Tor
        {:else if $nodeStatus.connection === "clearnet"}
          <span class="status-dot yellow"></span> Clearnet
        {:else}
          <span class="status-dot red"></span> Disconnected
        {/if}
      </div>
    </div>
    <div class="card">
      <div class="card-header">Last Synced Block</div>
      <div class="card-value">#{$nodeStatus.last_block}</div>
    </div>
  </div>

  <div class="card">
    <div class="card-header">AI Model</div>
    <div class="card-value">
      {#if $nodeStatus.model_loaded}
        Loaded: {$nodeStatus.model_name}
      {:else}
        Not loaded
      {/if}
    </div>
  </div>

  <div class="section-title">Quick Actions</div>
  <div class="grid-3">
    <button class="btn-secondary" on:click={goSend}>Send NGT</button>
    <button class="btn-secondary" on:click={goKnowledge}>Submit Knowledge</button>
    <button class="btn-secondary" on:click={goIde}>Open IDE</button>
  </div>
</div>

<style>
  .dashboard-balance {
    text-align: center;
    padding: 32px 0;
    border: 1px solid var(--border);
    border-radius: 6px;
    margin-bottom: 12px;
    background: var(--surface);
    box-shadow: var(--shadow-sm);
  }

  .balance-value {
    font-size: 38px;
    font-weight: 700;
    color: var(--text-primary);
    letter-spacing: -0.5px;
  }

  .balance-unit {
    font-size: 16px;
    font-weight: 400;
    color: var(--text-secondary);
    margin-left: 4px;
  }
</style>
