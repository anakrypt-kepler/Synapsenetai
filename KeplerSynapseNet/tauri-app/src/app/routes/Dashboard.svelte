<script lang="ts">
  import { nodeStatus, activeTab } from "../../lib/store";

  $: isTor = $nodeStatus.connection === "tor";

  function goSend() { activeTab.set("transfers"); }
  function goKnowledge() { activeTab.set("knowledge"); }
  function goIde() { activeTab.set("ide"); }
  function goWallet() { activeTab.set("wallet"); }
  function goNaan() { activeTab.set("naan"); }
  function goRental() { activeTab.set("rental"); }
</script>

<div class="content-area">
  <div class="balance-block">
    <div class="card-header">BALANCE</div>
    <div class="balance-value">{$nodeStatus.balance}<span class="balance-unit">NGT</span></div>
  </div>

  <div class="grid-2">
    <div class="card">
      <div class="card-header">CONNECTION</div>
      <div class="card-value">
        {#if isTor}
          <span class="conn-dot tor-blink"></span>TOR
        {:else if $nodeStatus.connection === "clearnet"}
          <span class="conn-dot on"></span>NET
        {:else}
          <span class="conn-dot"></span>OFF
        {/if}
      </div>
    </div>
    <div class="card">
      <div class="card-header">PEERS</div>
      <div class="card-value">{$nodeStatus.peers}</div>
    </div>
  </div>

  <div class="grid-2">
    <div class="card">
      <div class="card-header">NAAN AGENT</div>
      <div class="card-value">{$nodeStatus.naan_state.toUpperCase()}</div>
    </div>
    <div class="card">
      <div class="card-header">BLOCK</div>
      <div class="card-value">#{$nodeStatus.last_block}</div>
    </div>
  </div>

  <div class="card">
    <div class="card-header">AI MODEL</div>
    <div class="card-value">
      {#if $nodeStatus.model_loaded}
        {$nodeStatus.model_name}
      {:else}
        NONE
      {/if}
    </div>
  </div>

  <div class="section-title">ACTIONS</div>
  <div class="grid-3">
    <button class="btn-secondary" on:click={goSend}>[ SEND ]</button>
    <button class="btn-secondary" on:click={goWallet}>[ WALLET ]</button>
    <button class="btn-secondary" on:click={goKnowledge}>[ KNOW ]</button>
  </div>
  <div class="grid-3">
    <button class="btn-secondary" on:click={goIde}>[ IDE ]</button>
    <button class="btn-secondary" on:click={goNaan}>[ NAAN ]</button>
    <button class="btn-secondary" on:click={goRental}>[ RENT ]</button>
  </div>
</div>

<style>
  .balance-block {
    text-align: center;
    padding: 24px 0;
    border: 1px solid var(--border);
    margin-bottom: 8px;
  }

  .balance-value {
    font-size: 28px;
    font-weight: 700;
    color: var(--text-primary);
  }

  .balance-unit {
    font-size: 10px;
    font-weight: 400;
    color: var(--text-secondary);
    margin-left: 6px;
  }

  .conn-dot {
    display: inline-block;
    width: 6px;
    height: 6px;
    background: var(--err);
    margin-right: 6px;
  }

  .conn-dot.on {
    background: var(--text-primary);
  }

  @keyframes tor-blink {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.15; }
  }

  .conn-dot.tor-blink {
    background: var(--text-primary);
    animation: tor-blink 1.5s step-end infinite;
  }
</style>
