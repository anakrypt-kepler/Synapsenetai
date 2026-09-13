<script lang="ts">
  // Home dashboard: NGT balance, Tor/peers, shortcuts into other tabs.
  import { nodeStatus, activeTab } from "../../lib/store";

  $: isTor = $nodeStatus.connection === "tor";
  $: hasOnion = !!$nodeStatus.onion;
  $: onionShort = hasOnion ? $nodeStatus.onion.slice(0, 8) : "";
  $: chainEmpty = !$nodeStatus.last_block;

  let copyMsg = "";

  function goSend() { activeTab.set("transfers"); }
  function goKnowledge() { activeTab.set("knowledge"); }
  function goIde() { activeTab.set("ide"); }
  function goWallet() { activeTab.set("wallet"); }
  function goNaan() { activeTab.set("naan"); }
  function goRental() { activeTab.set("rental"); }
  function goNet() { activeTab.set("network"); }
  function goSet() { activeTab.set("settings"); }

  async function copyOnion() {
    copyMsg = "";
    if (!$nodeStatus.onion) return;
    try {
      await navigator.clipboard.writeText($nodeStatus.onion + ":8333");
      copyMsg = "COPIED";
    } catch {
      copyMsg = "COPY FAILED";
    }
  }
</script>

<div class="content-area">
  <div class="balance-block card">
    <div class="card-header">BALANCE</div>
    <div class="balance-value">{$nodeStatus.balance}<span class="balance-unit">NGT</span></div>
  </div>

  <div class="main-grid">
    <div class="card">
      <div class="card-header">CONNECTION</div>
      <div class="card-value wrap">
        {#if isTor}
          <span class="conn-dot tor-pulse"></span>TOR
        {:else}
          <span class="conn-dot"></span>OFF
        {/if}
      </div>
      {#if $nodeStatus.tor_bootstrap}
        <div class="hint">BOOTSTRAP {$nodeStatus.tor_bootstrap}{#if $nodeStatus.tor_circuits} · {$nodeStatus.tor_circuits} CIRCUITS{/if}</div>
      {/if}
    </div>

    <div class="card">
      <div class="card-header">PEERS · 1 = YOU ON TOR</div>
      {#if hasOnion}
        <div class="card-value wrap">
          {$nodeStatus.peers}
          {#if $nodeStatus.peers >= 1}
            <span class="you-badge">YOU</span>
          {/if}
        </div>
        <div class="onion-row">
          <div class="hs-line mono" title={$nodeStatus.onion}>HS:{onionShort}…</div>
          <button class="btn-secondary" type="button" on:click={copyOnion}>[ COPY ]</button>
        </div>
        {#if copyMsg}<div class="ok-line">{copyMsg}</div>{/if}
      {:else}
        <div class="card-value wrap">
          <span class="ks-spinner"></span>BOOTSTRAPPING TOR
        </div>
        {#if $nodeStatus.tor_bootstrap}
          <div class="hint">{$nodeStatus.tor_bootstrap}</div>
        {/if}
      {/if}
      <div class="hint">This node counts as 1 when the onion is published. Remote mesh is NET.</div>
    </div>
  </div>

  <div class="main-grid">
    <div class="card">
      <div class="card-header">NAAN AGENT</div>
      <div class="card-value wrap">{$nodeStatus.naan_state.toUpperCase()}</div>
    </div>
    <div class="card">
      <div class="card-header">CHAIN · POE HEIGHT</div>
      {#if chainEmpty}
        <div class="card-value wrap">LOCAL · NO BLOCKS YET</div>
        <div class="hint">This node has a local chain. Height 0 is empty, not a missing network.</div>
      {:else}
        <div class="card-value wrap">#{$nodeStatus.last_block}</div>
        <div class="hint">PoE height. Not proof of work.</div>
      {/if}
    </div>
  </div>

  <div class="card">
    <div class="card-header">AI MODEL</div>
    {#if $nodeStatus.model_loaded}
      <div class="card-value wrap">{$nodeStatus.model_name || "LOADED"}</div>
      <div class="hint">Local inference only. The LLM is not consensus and does not mine.</div>
    {:else}
      <div class="model-none">
        <div class="card-value wrap">NONE</div>
        <button class="btn-secondary" type="button" on:click={goSet}>[ SET ]</button>
      </div>
      <div class="hint">Load a GGUF in SET. The LLM is not consensus and does not mine.</div>
    {/if}
  </div>

  <div class="section-title">EARN NGT</div>
  <div class="grid-3 earn-grid">
    <button class="earn-card" type="button" on:click={goKnowledge}>
      <div class="card-header">KNOW</div>
      <div class="earn-copy">Submit knowledge. Votes, then finalize. Acceptance pays RingCT to stealth. Author stays public.</div>
    </button>
    <button class="earn-card" type="button" on:click={goIde}>
      <div class="card-header">IDE</div>
      <div class="earn-copy">Submit code the same path. Not hash mining. Pay is the PoE acceptance reward.</div>
    </button>
    <button class="earn-card" type="button" on:click={goNaan}>
      <div class="card-header">NAAN</div>
      <div class="earn-copy">Harvests over Tor and files drafts. Does not mint NGT. Mesh mint is only after PoE votes and finalize.</div>
    </button>
  </div>

  <div class="section-title">ACTIONS</div>
  <div class="actions">
    <button class="btn-secondary" type="button" on:click={goSend}>[ SEND ]</button>
    <button class="btn-secondary" type="button" on:click={goWallet}>[ WALLET ]</button>
    <button class="btn-secondary" type="button" on:click={goKnowledge}>[ KNOW ]</button>
    <button class="btn-secondary" type="button" on:click={goIde}>[ IDE ]</button>
    <button class="btn-secondary" type="button" on:click={goNaan}>[ NAAN ]</button>
    <button class="btn-secondary" type="button" on:click={goRental}>[ RENT ]</button>
    <button class="btn-secondary" type="button" on:click={goNet}>[ NET ]</button>
  </div>
</div>

<style>
  /* Visual reset: antialiased system UI for this tab. */
  .content-area {
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif);
    font-size: 13px;
    color: var(--text-primary);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    text-rendering: optimizeLegibility;
    image-rendering: auto;
  }

  .content-area :global(.ks-spinner) {
    display: inline-block;
    width: 14px;
    height: 14px;
    margin-right: 8px;
    border: 2px solid var(--border);
    border-top-color: var(--text-primary);
    border-radius: 50%;
    animation: ks-spin 0.7s linear infinite;
    vertical-align: -2px;
  }

  @keyframes ks-spin {
    to { transform: rotate(360deg); }
  }

  .mono {
    font-family: var(--font-mono, ui-monospace, "SF Mono", Menlo, Consolas, monospace);
  }

  button {
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif);
    font-size: 12px;
    letter-spacing: 0.04em;
    border-radius: var(--radius-sm);
    transition:
      transform var(--dur) var(--ease),
      box-shadow var(--dur) var(--ease),
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
  .balance-block {
    border-radius: var(--radius);
    background: var(--surface);
    backdrop-filter: blur(var(--blur));
    -webkit-backdrop-filter: blur(var(--blur));
    border: 1px solid var(--border);
    transition:
      transform var(--dur) var(--ease),
      box-shadow var(--dur) var(--ease),
      border-color var(--dur) var(--ease);
  }

  .card:hover,
  .balance-block:hover {
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

  .balance-block {
    text-align: center;
    padding: 28px 16px;
    margin-bottom: 10px;
  }

  .balance-value {
    font-size: 32px;
    font-weight: 650;
    letter-spacing: -0.03em;
    color: var(--text-primary);
    overflow-wrap: anywhere;
  }

  .balance-unit {
    font-size: 13px;
    font-weight: 500;
    color: var(--text-secondary);
    margin-left: 8px;
    letter-spacing: 0.06em;
  }

  .main-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 10px;
  }

  .main-grid .card {
    min-width: 0;
  }

  .card-header {
    overflow-wrap: anywhere;
    white-space: normal;
  }

  .card-value.wrap {
    font-size: 15px;
    line-height: 1.4;
    overflow-wrap: anywhere;
    word-break: break-word;
    white-space: normal;
  }

  .conn-dot {
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--err);
    margin-right: 8px;
    vertical-align: middle;
  }

  @keyframes tor-pulse {
    0%, 100% {
      opacity: 1;
      box-shadow: 0 0 0 0 rgba(245, 245, 247, 0.42);
    }
    50% {
      opacity: 0.72;
      box-shadow: 0 0 0 6px rgba(245, 245, 247, 0);
    }
  }

  .conn-dot.tor-pulse {
    background: var(--text-primary);
    animation: tor-pulse 2.2s var(--ease, cubic-bezier(0.22, 1, 0.36, 1)) infinite;
  }

  .you-badge {
    font-size: 11px;
    font-weight: 650;
    padding: 2px 8px;
    border-radius: 999px;
    background: var(--ok);
    color: #000;
    letter-spacing: 0.04em;
    margin-left: 8px;
    display: inline-block;
    vertical-align: middle;
  }

  .onion-row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    align-items: center;
    margin-top: 8px;
  }

  .hs-line {
    font-size: 12px;
    letter-spacing: 0.02em;
    color: var(--text-primary);
    overflow-wrap: anywhere;
    min-width: 0;
    flex: 1 1 auto;
  }

  .hint {
    margin-top: 8px;
    font-size: 12px;
    letter-spacing: 0.01em;
    color: var(--text-secondary);
    line-height: 1.5;
    text-transform: none;
    overflow-wrap: anywhere;
    white-space: normal;
  }

  .ok-line {
    margin-top: 6px;
    font-size: 12px;
    color: var(--ok);
    letter-spacing: 0.04em;
  }

  .model-none {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    align-items: center;
  }

  .earn-grid {
    margin-bottom: 8px;
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 10px;
  }

  .earn-card {
    text-align: left;
    padding: 14px;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--surface);
    backdrop-filter: blur(var(--blur));
    -webkit-backdrop-filter: blur(var(--blur));
    color: inherit;
    width: 100%;
    min-width: 0;
    transition:
      transform var(--dur) var(--ease),
      box-shadow var(--dur) var(--ease),
      border-color var(--dur) var(--ease),
      background-color var(--dur) var(--ease);
  }

  .earn-card:hover {
    border-color: rgba(255, 255, 255, 0.2);
    background: var(--accent-muted, rgba(255, 255, 255, 0.06));
  }

  .earn-copy {
    font-size: 12px;
    color: var(--text-secondary);
    letter-spacing: 0.01em;
    line-height: 1.5;
    font-weight: 400;
    text-transform: none;
    overflow-wrap: anywhere;
    white-space: normal;
  }

  .actions {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
  }

  .actions button {
    flex: 1 1 140px;
  }
</style>
