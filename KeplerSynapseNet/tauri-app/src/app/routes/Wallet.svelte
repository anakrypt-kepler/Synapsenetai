<script lang="ts">
  import { nodeStatus } from "../../lib/store";
  import { rpcCall } from "../../lib/rpc";
  import { generateQRSvg } from "../../lib/qr";

  let walletAddress = "";
  let seedVisible = false;
  let seedPhrase = "";
  let passwordInput = "";
  let passwordRequired = false;
  let copied = false;
  let qrSvg = "";

  async function loadWalletInfo() {
    try {
      const result = await rpcCall("wallet.info", "{}");
      const info = JSON.parse(result);
      walletAddress = info.address || "";
      if (walletAddress) qrSvg = generateQRSvg(walletAddress, 3);
    } catch {}
  }

  loadWalletInfo();

  function copyAddress() {
    navigator.clipboard.writeText(walletAddress);
    copied = true;
    setTimeout(() => (copied = false), 2000);
  }

  async function showSeed() {
    passwordRequired = true;
  }

  async function confirmShowSeed() {
    try {
      const result = await rpcCall("wallet.seed", JSON.stringify({ password: passwordInput }));
      const parsed = JSON.parse(result);
      seedPhrase = parsed.seed || "";
      seedVisible = true;
      passwordRequired = false;
      passwordInput = "";
    } catch {
      seedPhrase = "";
    }
  }

  function hideSeed() {
    seedVisible = false;
    seedPhrase = "";
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
      <code class="address-text">{walletAddress || "..."}</code>
      <button class="btn-secondary" on:click={copyAddress}>
        {copied ? "OK" : "[ COPY ]"}
      </button>
    </div>
  </div>

  <div class="card">
    <div class="card-header">QR CODE</div>
    <div class="qr-container">
      {#if qrSvg}
        {@html qrSvg}
      {:else}
        <div class="qr-empty">NO ADDRESS</div>
      {/if}
    </div>
  </div>

  <div class="section-title">SEED PHRASE</div>
  {#if !seedVisible && !passwordRequired}
    <button class="btn-secondary" on:click={showSeed}>[ SHOW SEED ]</button>
  {/if}
  {#if passwordRequired}
    <div class="form-group">
      <label>ENTER PASSWORD</label>
      <input type="password" bind:value={passwordInput} placeholder="..." />
    </div>
    <button class="btn-primary" on:click={confirmShowSeed}>[ CONFIRM ]</button>
  {/if}
  {#if seedVisible}
    <div class="seed-display">{seedPhrase}</div>
    <button class="btn-secondary" on:click={hideSeed}>[ HIDE ]</button>
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

  .seed-display {
    padding: 12px;
    border: 1px solid var(--warn);
    background: var(--warn-muted);
    font-size: 10px;
    line-height: 2;
    word-spacing: 4px;
    color: var(--text-primary);
    margin-bottom: 8px;
  }
</style>
