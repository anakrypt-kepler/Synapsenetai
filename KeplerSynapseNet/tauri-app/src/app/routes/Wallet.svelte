<script lang="ts">
  import { nodeStatus } from "../../lib/store";
  import { rpcCall } from "../../lib/rpc";

  let walletAddress = "";
  let seedVisible = false;
  let seedPhrase = "";
  let passwordInput = "";
  let passwordRequired = false;
  let copied = false;

  async function loadWalletInfo() {
    try {
      const result = await rpcCall("wallet.info", "{}");
      const info = JSON.parse(result);
      walletAddress = info.address || "";
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
      const result = await rpcCall(
        "wallet.seed",
        JSON.stringify({ password: passwordInput })
      );
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
    try {
      await rpcCall("wallet.export", "{}");
    } catch {}
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
    <div class="qr-placeholder">
      <svg viewBox="0 0 100 100" width="120" height="120">
        <rect x="10" y="10" width="20" height="20" fill="var(--text-primary)" />
        <rect x="70" y="10" width="20" height="20" fill="var(--text-primary)" />
        <rect x="10" y="70" width="20" height="20" fill="var(--text-primary)" />
        <rect x="40" y="40" width="20" height="20" fill="var(--text-primary)" />
        <rect x="35" y="10" width="5" height="5" fill="var(--text-primary)" />
        <rect x="45" y="15" width="5" height="5" fill="var(--text-primary)" />
        <rect x="55" y="10" width="5" height="5" fill="var(--text-primary)" />
        <rect x="35" y="55" width="5" height="5" fill="var(--text-primary)" />
        <rect x="60" y="45" width="5" height="5" fill="var(--text-primary)" />
        <rect x="15" y="45" width="5" height="5" fill="var(--text-primary)" />
      </svg>
    </div>
  </div>

  <div class="section-title">Seed Phrase</div>
  {#if !seedVisible && !passwordRequired}
    <button class="btn-secondary" on:click={showSeed}>Show Seed Phrase</button>
  {/if}
  {#if passwordRequired}
    <div class="form-group">
      <label>Enter password to reveal seed phrase</label>
      <input type="password" bind:value={passwordInput} placeholder="Password" />
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
