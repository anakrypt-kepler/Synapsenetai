<script lang="ts">
  import { nodeStatus } from "../../lib/store";
  import { rpcCall } from "../../lib/rpc";
  import { generateQRSvg } from "../../lib/qr";

  let walletAddress = "";
  let seedVisible = false;
  let seedPhrase = "";
  let copied = false;
  let qrSvg = "";
  let loading = true;

  async function loadWalletInfo() {
    try {
      const result = await rpcCall("wallet.info", "{}");
      const info = JSON.parse(result);
      walletAddress = info.address || "";
      if (walletAddress) qrSvg = generateQRSvg(walletAddress, 3);
    } catch {}
    loading = false;
  }

  loadWalletInfo();

  function copyAddress() {
    if (!walletAddress) return;
    navigator.clipboard.writeText(walletAddress);
    copied = true;
    setTimeout(() => (copied = false), 2000);
  }

  async function toggleSeed() {
    if (seedVisible) {
      seedVisible = false;
      seedPhrase = "";
      return;
    }
    try {
      const result = await rpcCall("wallet.seed", "{}");
      const parsed = JSON.parse(result);
      seedPhrase = parsed.seed || "";
      seedVisible = true;
    } catch {
      seedPhrase = "";
    }
  }

  async function exportWallet() {
    try { await rpcCall("wallet.export", "{}"); } catch {}
  }

  async function importWallet() {
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({
        filters: [{ name: "Wallet", extensions: ["dat"] }],
        multiple: false,
      });
      if (selected) {
        const path = typeof selected === "string" ? selected : selected.path;
        await rpcCall("wallet.import", JSON.stringify({ path }));
        await loadWalletInfo();
      }
    } catch {}
  }
</script>

<div class="content-area">
  <div class="card">
    <div class="card-header">BALANCE</div>
    <div class="card-value">{$nodeStatus.balance} NGT</div>
  </div>

  <div class="card">
    <div class="card-header">ADDRESS</div>
    <div class="address-row">
      <code class="address-text">{walletAddress || (loading ? "LOADING..." : "NO WALLET")}</code>
      {#if walletAddress}
        <button class="btn-secondary" on:click={copyAddress}>
          {copied ? "COPIED" : "[ COPY ]"}
        </button>
      {/if}
    </div>
  </div>

  <div class="card">
    <div class="card-header">QR CODE</div>
    <div class="qr-container">
      {#if qrSvg}
        {@html qrSvg}
      {:else}
        <div class="qr-empty">{loading ? "LOADING..." : "NO ADDRESS"}</div>
      {/if}
    </div>
  </div>

  <div class="section-title">SEED PHRASE</div>
  <button class="btn-secondary seed-toggle" on:click={toggleSeed}>
    {#if seedVisible}
      <span class="eye">&#x1F441;</span> [ HIDE SEED ]
    {:else}
      <span class="eye-closed">&#x2014;</span> [ SHOW SEED ]
    {/if}
  </button>
  {#if seedVisible && seedPhrase}
    <div class="seed-display">{seedPhrase}</div>
  {/if}

  <div class="section-title">MANAGE</div>
  <div class="grid-2">
    <button class="btn-secondary" on:click={exportWallet}>[ EXPORT ]</button>
    <button class="btn-secondary" on:click={importWallet}>[ IMPORT ]</button>
  </div>
</div>

<style>
  .address-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-top: 6px;
  }

  .address-text {
    font-size: 8px;
    color: var(--text-primary);
    word-break: break-all;
    flex: 1;
    line-height: 1.6;
  }

  .qr-container {
    display: flex;
    justify-content: center;
    padding: 16px;
  }

  .qr-empty {
    font-size: 8px;
    color: var(--text-secondary);
  }

  .seed-toggle {
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .eye, .eye-closed {
    font-size: 12px;
  }

  .seed-display {
    padding: 12px;
    border: 1px solid var(--warn);
    background: var(--warn-muted);
    font-size: 10px;
    line-height: 2;
    word-spacing: 4px;
    color: var(--text-primary);
    margin-top: 8px;
    margin-bottom: 8px;
  }
</style>
