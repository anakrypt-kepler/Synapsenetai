<script lang="ts">
  // Wallet tab: stealth receive (SN+hex), RingCT balance from nodeStatus,
  // seed reveal with confirm. Seed stays in libsynapsed, never in the Svelte store.
  import { onMount, onDestroy } from "svelte";
  import { nodeStatus, myWalletAddress } from "../../lib/store";
  import { rpcCall } from "../../lib/rpc";
  import { generateQRSvg } from "../../lib/qr";

  let walletAddress = "";
  let qrSvg = "";
  let loading = true;
  let copied = false;
  let busy = false;

  let infoError = "";
  let bannerError = "";
  let bannerOk = "";

  let seedStep: "hidden" | "confirm" | "visible" = "hidden";
  let seedPhrase = "";
  let seedError = "";

  let exportPath = "";
  let restoreDraft = "";
  let restoreError = "";
  let alive = true;

  function wipeSeedUi() {
    seedPhrase = seedPhrase ? " ".repeat(seedPhrase.length) : "";
    seedPhrase = "";
    seedStep = "hidden";
    seedError = "";
  }

  function wipeRestoreDraft() {
    restoreDraft = restoreDraft ? " ".repeat(restoreDraft.length) : "";
    restoreDraft = "";
  }

  function rpcJson(raw: string): { ok: boolean; data: Record<string, any>; error: string } {
    try {
      const data = JSON.parse(raw);
      if (data && typeof data.error === "string" && data.error) {
        return { ok: false, data, error: data.error };
      }
      return { ok: true, data: data || {}, error: "" };
    } catch {
      return { ok: false, data: {}, error: "BAD RESPONSE" };
    }
  }

  function applyAddress(addr: string) {
    walletAddress = addr || "";
    if (walletAddress) {
      myWalletAddress.set(walletAddress);
      qrSvg = generateQRSvg(walletAddress, 3);
    } else {
      qrSvg = "";
    }
  }

  async function loadWalletInfo() {
    try {
      const result = await rpcCall("wallet.info", "{}");
      if (!alive) return;
      const parsed = rpcJson(result);
      if (!parsed.ok) {
        if (parsed.error === "not initialized") {
          infoError = "";
          return;
        }
        infoError = parsed.error.toUpperCase();
        if (!walletAddress) applyAddress("");
        loading = false;
        return;
      }
      infoError = "";
      applyAddress(typeof parsed.data.address === "string" ? parsed.data.address : "");
      loading = false;
    } catch (e: any) {
      if (!alive) return;
      infoError = (e && e.message) ? String(e.message) : "WALLET INFO FAILED";
      if (!walletAddress) applyAddress("");
      loading = false;
    }
  }

  onMount(() => {
    let lastBalance = "";
    const unsub = nodeStatus.subscribe((s) => {
      const balanceChanged = s.balance !== lastBalance;
      lastBalance = s.balance;
      if (balanceChanged || loading) void loadWalletInfo();
    });
    return unsub;
  });

  onDestroy(() => {
    alive = false;
    wipeSeedUi();
    wipeRestoreDraft();
  });

  function addressPlaceholder(): string {
    if (loading) return "LOADING...";
    return "NO WALLET";
  }

  async function copyAddress() {
    if (!walletAddress) return;
    try {
      await navigator.clipboard.writeText(walletAddress);
      copied = true;
      bannerError = "";
      setTimeout(() => (copied = false), 2000);
    } catch {
      bannerError = "COPY FAILED";
    }
  }

  function requestShowSeed() {
    seedError = "";
    if (seedStep === "visible") {
      wipeSeedUi();
      return;
    }
    if (seedStep === "hidden") {
      seedStep = "confirm";
    }
  }

  function cancelShowSeed() {
    wipeSeedUi();
  }

  async function confirmShowSeed() {
    seedError = "";
    try {
      const result = await rpcCall("wallet.seed", "{}");
      const parsed = rpcJson(result);
      if (!parsed.ok) {
        seedError = parsed.error.toUpperCase();
        seedStep = "confirm";
        return;
      }
      const seed = typeof parsed.data.seed === "string" ? parsed.data.seed : "";
      if (!seed.trim()) {
        seedPhrase = "";
        seedStep = "confirm";
        seedError = "NO SEED IN ENGINE";
        return;
      }
      seedPhrase = seed;
      seedStep = "visible";
    } catch (e: any) {
      seedError = (e && e.message) ? String(e.message) : "SEED FAILED";
      seedStep = "confirm";
    }
  }

  async function exportWallet() {
    bannerError = "";
    bannerOk = "";
    exportPath = "";
    busy = true;
    try {
      const result = await rpcCall("wallet.export", "{}");
      const parsed = rpcJson(result);
      if (!parsed.ok) {
        bannerError = parsed.error.toUpperCase();
        return;
      }
      const path = typeof parsed.data.path === "string" ? parsed.data.path : "";
      exportPath = path;
      bannerOk = path ? `EXPORTED TO ${path}` : "EXPORT OK (NO PATH RETURNED)";
    } catch (e: any) {
      bannerError = (e && e.message) ? String(e.message) : "EXPORT FAILED";
    } finally {
      busy = false;
    }
  }

  async function importWallet() {
    bannerError = "";
    bannerOk = "";
    busy = true;
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({
        title: "Import wallet",
        filters: [{ name: "Wallet key", extensions: ["key", "dat"] }],
        multiple: false,
      });
      if (!selected) return;
      const path = typeof selected === "string" ? selected : (selected as { path?: string }).path;
      if (!path) {
        bannerError = "NO FILE SELECTED";
        return;
      }
      const result = await rpcCall("wallet.import", JSON.stringify({ path }));
      const parsed = rpcJson(result);
      if (!parsed.ok) {
        bannerError = parsed.error.toUpperCase();
        return;
      }
      wipeSeedUi();
      await loadWalletInfo();
      bannerOk = "WALLET IMPORTED";
    } catch (e: any) {
      bannerError = (e && e.message) ? String(e.message) : "IMPORT FAILED";
    } finally {
      busy = false;
    }
  }

  function restoreWordList(raw: string): string[] {
    return raw.trim().split(/\s+/).filter((w) => w.length > 0);
  }

  function validateRestoreWords(raw: string): string {
    const words = restoreWordList(raw);
    if (words.length !== 24) return "NEED EXACTLY 24 WORDS";
    for (const w of words) {
      if (!/^[a-zA-Z]+$/.test(w)) return "WORDS MUST BE LETTERS ONLY";
    }
    return "";
  }

  async function restoreWallet() {
    restoreError = "";
    bannerError = "";
    bannerOk = "";
    const check = validateRestoreWords(restoreDraft);
    if (check) {
      restoreError = check;
      return;
    }
    const words = restoreWordList(restoreDraft).map((w) => w.toLowerCase());
    const seed = words.join(" ");
    busy = true;
    try {
      const result = await rpcCall("wallet.restore", JSON.stringify({ seed }));
      const parsed = rpcJson(result);
      if (!parsed.ok) {
        restoreError = parsed.error.toUpperCase();
        return;
      }
      wipeSeedUi();
      wipeRestoreDraft();
      await loadWalletInfo();
      bannerOk = "WALLET RESTORED";
    } catch (e: any) {
      restoreError = (e && e.message) ? String(e.message) : "RESTORE FAILED";
    } finally {
      busy = false;
    }
  }
</script>

<div class="content-area">
  <div class="card">
    <div class="card-header">WALLET NGT (PRIVATE)</div>
    <div class="card-value">{$nodeStatus.balance} NGT</div>
    <div class="hint">RINGCT PRIVATE WALLET · NOT A REWARD POOL</div>
  </div>

  {#if bannerError}
    <div class="error-msg">{bannerError}</div>
  {/if}
  {#if bannerOk}
    <div class="success-msg">{bannerOk}</div>
  {/if}

  <div class="card">
    <div class="card-header">STEALTH ADDRESS (SN)</div>
    <div class="address-row">
      <code class="address-text mono">
        {#if loading}<span class="ks-spinner"></span>{/if}{walletAddress || addressPlaceholder()}
      </code>
      {#if walletAddress}
        <button class="btn-secondary" on:click={copyAddress}>
          {copied ? "COPIED" : "[ COPY ]"}
        </button>
      {/if}
    </div>
    {#if infoError}
      <div class="error-msg info-err">{infoError}</div>
    {/if}
  </div>

  <div class="card">
    <div class="card-header">QR CODE</div>
    <div class="qr-container">
      {#if qrSvg}
        {@html qrSvg}
      {:else}
        <div class="qr-empty">
          {#if loading}
            <span class="ks-spinner"></span> LOADING...
          {:else}
            NO ADDRESS
          {/if}
        </div>
      {/if}
    </div>
  </div>

  <div class="section-title">SEED PHRASE</div>
  <p class="hint">Anyone with this seed can spend your NGT. Hide clears it from this screen.</p>
  {#if seedStep === "hidden"}
    <button class="btn-secondary seed-toggle" on:click={requestShowSeed} disabled={busy}>
      [ SHOW SEED ]
    </button>
  {:else if seedStep === "confirm"}
    <div class="confirm-box">
      <p class="warn-text">I understand this seed can empty the wallet. Reveal it only on a private screen.</p>
      <div class="grid-2">
        <button class="btn-primary" on:click={confirmShowSeed} disabled={busy}>[ I UNDERSTAND ]</button>
        <button class="btn-secondary" on:click={cancelShowSeed} disabled={busy}>[ CANCEL ]</button>
      </div>
    </div>
  {:else}
    <button class="btn-secondary seed-toggle" on:click={requestShowSeed}>
      [ HIDE SEED ]
    </button>
    {#if seedPhrase}
      <div class="seed-display">{seedPhrase}</div>
    {/if}
  {/if}
  {#if seedError}
    <div class="error-msg">{seedError}</div>
  {/if}

  <div class="section-title">RESTORE 24 WORDS</div>
  <div class="form-group">
    <label>BIP39 SEED (STAYS IN THIS TAB)</label>
    <textarea
      rows="3"
      spellcheck="false"
      autocomplete="off"
      autocapitalize="off"
      placeholder="24 words"
      bind:value={restoreDraft}
      disabled={busy}
    ></textarea>
  </div>
  {#if restoreError}
    <div class="error-msg">{restoreError}</div>
  {/if}
  <button class="btn-secondary" on:click={restoreWallet} disabled={busy}>[ RESTORE ]</button>

  <div class="section-title">MANAGE</div>
  <div class="grid-2">
    <button class="btn-secondary" on:click={exportWallet} disabled={busy}>[ EXPORT ]</button>
    <button class="btn-secondary" on:click={importWallet} disabled={busy}>[ IMPORT ]</button>
  </div>
  {#if exportPath}
    <div class="path-box">
      <div class="card-header">EXPORT PATH</div>
      <code class="address-text mono">{exportPath}</code>
    </div>
  {/if}
  <p class="hint">Export writes wallet_export.key under the node data dir. Import accepts .key or .dat.</p>
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

  button,
  input,
  textarea {
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif);
    font-size: 13px;
    letter-spacing: 0.02em;
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

  button:focus-visible,
  input:focus-visible,
  textarea:focus-visible {
    outline: 2px solid var(--text-primary);
    outline-offset: 2px;
  }

  textarea {
    background: var(--surface-solid, #111);
    border: 1px solid var(--border);
    color: var(--text-primary);
    line-height: 1.55;
    resize: vertical;
  }

  .card,
  .confirm-box,
  .path-box {
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
  .path-box:hover {
    border-color: rgba(255, 255, 255, 0.18);
    box-shadow: 0 10px 28px rgba(0, 0, 0, 0.28);
  }

  .card-header,
  .section-title,
  .form-group label {
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif);
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.08em;
    color: var(--text-faint);
  }

  .card-value {
    font-size: 22px;
    letter-spacing: -0.02em;
  }

  .error-msg,
  .success-msg {
    font-size: 13px;
    border-radius: var(--radius-sm);
    letter-spacing: 0.02em;
  }

  .address-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-top: 6px;
  }

  .address-text {
    font-size: 12px;
    color: var(--text-primary);
    word-break: break-all;
    flex: 1;
    line-height: 1.55;
    user-select: text;
    -webkit-user-select: text;
  }

  .qr-container {
    display: flex;
    justify-content: center;
    padding: 16px;
  }

  .qr-container :global(svg) {
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    border-radius: var(--radius-sm);
  }

  .qr-empty {
    display: flex;
    align-items: center;
    justify-content: center;
    min-height: 72px;
    font-size: 13px;
    color: var(--text-secondary);
    letter-spacing: 0.04em;
  }

  .seed-toggle {
    display: inline-block;
  }

  /* Seed must stay selectable, high-contrast, and larger than brick UI. */
  .seed-display {
    padding: 14px 16px;
    border: 1px solid var(--warn);
    border-radius: var(--radius);
    background: var(--warn-muted);
    font-family: var(--font-mono, ui-monospace, "SF Mono", Menlo, Consolas, monospace);
    font-size: 15px;
    line-height: 1.7;
    word-spacing: 0.35em;
    color: var(--text-primary);
    margin-top: 8px;
    margin-bottom: 8px;
    user-select: text;
    -webkit-user-select: text;
    cursor: text;
    white-space: pre-wrap;
    overflow-wrap: anywhere;
  }

  .hint {
    font-size: 12px;
    letter-spacing: 0.01em;
    color: var(--text-secondary);
    margin-top: 6px;
    margin-bottom: 8px;
    line-height: 1.5;
  }

  .warn-text {
    font-size: 13px;
    color: var(--warn);
    margin-bottom: 10px;
    line-height: 1.5;
  }

  .confirm-box {
    padding: 14px;
    border: 1px solid var(--warn);
    margin-bottom: 8px;
  }

  .path-box {
    margin-top: 8px;
    padding: 14px;
  }

  .info-err {
    margin-top: 8px;
    margin-bottom: 0;
  }
</style>
