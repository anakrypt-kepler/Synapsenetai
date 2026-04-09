<script lang="ts">
  import { nodeStatus } from "../../lib/store";
  import { rpcCall } from "../../lib/rpc";
  import { qrToSvg } from "../../lib/qr";

  let walletAddress = "";
  let seedVisible = false;
  let seedPhrase = "";
  let passwordInput = "";
  let passwordRequired = false;
  let copied = false;
  let walletError = "";

  $: qrSvgHtml = walletAddress ? qrToSvg(walletAddress, 120, "var(--text-primary)") : "";

  async function loadWalletInfo() {
    try {
      const result = await rpcCall("wallet.info", "{}");
      const info = JSON.parse(result);
      walletAddress = info.address || "";
    } catch (e: any) {
      walletError = e.message || "Failed to load wallet info";
      console.error("wallet.info failed:", e);
    }
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
      const result = await rpcCall(
        "wallet.seed",
        JSON.stringify({ password: passwordInput })
      );
      const parsed = JSON.parse(result);
      seedPhrase = parsed.seed || "";
      seedVisible = true;
      passwordRequired = false;
      passwordInput = "";
    } catch (e: any) {
      seedPhrase = "";
      console.error("Failed to reveal seed phrase:", e);
    }
  }

  function hideSeed() {
    seedVisible = false;
    seedPhrase = "";
  }

  async function exportWallet() {
    try {
      await rpcCall("wallet.export", "{}");
    } catch (e: any) {
      walletError = e.message || "Export failed";
      console.error("wallet.export failed:", e);
    }
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
    } catch (e: any) {
      walletError = e.message || "Import failed";
      console.error("wallet.import failed:", e);
    }
  }
</script>

<div class="content-area">
  <div class="card">
    <div class="card-header">NGT Balance</div>
    <div class="card-value">{$nodeStatus.balance} NGT</div>
  </div>

  <div class="card">
    <div class="card-header">Wallet Address</div>
    <div class="address-row">
      <code class="address-text">{walletAddress || "Loading..."}</code>
      <button class="btn-secondary" on:click={copyAddress}>
        {copied ? "Copied" : "Copy"}
      </button>
    </div>
  </div>

  <div class="card">
    <div class="card-header">QR Code</div>
    <div class="qr-placeholder" aria-label="QR code for wallet address">
      {#if qrSvgHtml}
        {@html qrSvgHtml}
      {:else}
        <span class="qr-empty">No address available</span>
      {/if}
    </div>
  </div>

  <div class="section-title">Seed Phrase</div>
  {#if !seedVisible && !passwordRequired}
    <button class="btn-secondary" on:click={showSeed}>Show Seed Phrase</button>
  {/if}
  {#if passwordRequired}
    <div class="form-group">
      <label for="wallet-seed-password">Enter password to reveal seed phrase</label>
      <input id="wallet-seed-password" type="password" bind:value={passwordInput} placeholder="Password" />
    </div>
    <button class="btn-primary" on:click={confirmShowSeed}>Confirm</button>
  {/if}
  {#if seedVisible}
    <div class="seed-display">{seedPhrase}</div>
    <button class="btn-secondary" on:click={hideSeed}>Hide</button>
  {/if}

  <div class="section-title">Wallet Management</div>
  <div class="grid-2">
    <button class="btn-secondary" on:click={exportWallet}>Export Wallet</button>
    <button class="btn-secondary" on:click={importWallet}>Import Wallet</button>
  </div>
</div>

<style>
  .address-row {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-top: 8px;
  }

  .address-text {
    font-size: 11px;
    color: var(--text-primary);
    word-break: break-all;
    flex: 1;
    line-height: 1.5;
  }

  .qr-placeholder {
    display: flex;
    justify-content: center;
    padding: 20px;
  }

  .qr-empty {
    font-size: 11px;
    color: var(--text-secondary);
  }

  .seed-display {
    padding: 14px;
    border: 1px solid var(--status-yellow);
    border-radius: 4px;
    background: var(--status-yellow-muted);
    font-size: 13px;
    line-height: 1.8;
    word-spacing: 4px;
    color: var(--text-primary);
    margin-bottom: 8px;
  }
</style>
