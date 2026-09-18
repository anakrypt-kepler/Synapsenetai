<script lang="ts">
  // Desktop send is always private: stealth dest + MLSAG-2 RingCT + 64-bit range proofs + Tor.
  // Memo is local only. No clearnet send path.
  import { onMount } from "svelte";
  import { sendNgt, getTransactions, rpcCall, privacyStatus } from "../../lib/rpc";
  import { nodeStatus } from "../../lib/store";
  import { generateQRSvg } from "../../lib/qr";
  import PixelStage from "../components/sprites/PixelStage.svelte";
  import PixelSprite from "../components/sprites/PixelSprite.svelte";
  import coinSprite from "../../assets/sprites/coin.svg";
  import snGlyph from "../../assets/sprites/sn-glyph.svg";

  let recipient = "";
  let amount = "";
  let memo = "";
  let sendError = "";
  let sendSuccess = "";
  let sending = false;
  let copied = false;
  let privacyInfo: {
    stealth_enabled: boolean;
    ring_enabled: boolean;
    confidential_enabled: boolean;
    ring_size?: number;
  } | null = null;
  let transactions: {
    type: string;
    amount: string;
    timestamp: string;
    status: string;
    to?: string;
    from?: string;
    txid?: string;
  }[] = [];
  let filter = "all";
  let walletAddress = "";
  let qrSvg = "";

  $: onTor = $nodeStatus.connection === "tor";
  $: privacyFailClosed =
    privacyInfo !== null &&
    (!privacyInfo.stealth_enabled || !privacyInfo.ring_enabled || !privacyInfo.confidential_enabled);
  $: sendLocked = !onTor || privacyFailClosed;

  onMount(async () => {
    await loadTransactions();
    await loadPrivacyStatus();
    try {
      const result = await rpcCall("wallet.info", "{}");
      const info = JSON.parse(result);
      walletAddress = info.address || "";
      if (walletAddress) qrSvg = generateQRSvg(walletAddress, 2);
    } catch {}
  });

  async function loadPrivacyStatus() {
    try {
      const raw = await privacyStatus();
      const parsed = JSON.parse(raw);
      privacyInfo = {
        stealth_enabled: !!parsed.stealth_enabled,
        ring_enabled: !!parsed.ring_enabled,
        confidential_enabled: !!parsed.confidential_enabled,
        ring_size: parsed.ring_size || 11,
      };
    } catch {
      privacyInfo = null;
    }
  }

  function formatTs(raw: any): string {
    if (!raw) return "-";
    if (typeof raw === "string" && raw.length > 4) return raw;
    if (typeof raw === "number") {
      const d = new Date(raw);
      return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")} ${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
    }
    return String(raw);
  }

  function emptyHistoryText(): string {
    if (filter === "sent") return "NO SENT TRANSFERS";
    if (filter === "received") return "NO RECEIVED TRANSFERS";
    if (filter === "rewards") return "NO MINING REWARDS";
    return "NO TRANSFERS YET";
  }

  function asUpperError(raw: any): string {
    const s = String(raw || "TX FAILED").trim();
    return (s || "TX FAILED").toUpperCase();
  }

  function validSnAddress(addr: string): boolean {
    const a = addr.trim();
    return a.startsWith("SN") && a.length >= 130 && /^SN[0-9a-fA-F]+$/.test(a);
  }

  async function loadTransactions() {
    try {
      const result = await getTransactions(filter);
      const parsed = JSON.parse(result);
      transactions = (parsed.transactions || []).map((tx: any) => ({
        type: tx.type || "unknown",
        amount: tx.amount || "0",
        timestamp: tx.timestamp || formatTs(tx.ts),
        status: tx.status || "pending",
        to: tx.to,
        from: tx.from,
        txid: tx.txid,
      }));
    } catch {
      transactions = [];
    }
  }

  async function handleSend() {
    sendError = "";
    sendSuccess = "";
    if (!onTor) {
      sendError = "TOR REQUIRED";
      return;
    }
    if (privacyFailClosed) {
      sendError = "FAIL CLOSED";
      return;
    }
    if (!recipient.trim() || !amount.trim()) {
      sendError = "RECIPIENT AND AMOUNT REQUIRED";
      return;
    }
    if (!validSnAddress(recipient)) {
      sendError = "USE THE RECIPIENT SN STEALTH ADDRESS FROM THEIR RECEIVE SCREEN";
      return;
    }
    const numAmt = parseFloat(amount);
    if (isNaN(numAmt) || numAmt <= 0) {
      sendError = "INVALID AMOUNT";
      return;
    }
    sending = true;
    try {
      const raw = await sendNgt(recipient, amount, memo || undefined);
      const resp = JSON.parse(raw);
      if (resp.error) {
        sendError = asUpperError(resp.error);
      } else {
        sendSuccess = `PRIVATE TX: ${resp.txid || "OK"}`;
        recipient = "";
        amount = "";
        memo = "";
        await loadTransactions();
      }
    } catch (e: any) {
      sendError = asUpperError(e?.message || "TX FAILED");
    }
    sending = false;
  }

  function setFilter(f: string) {
    filter = f;
    loadTransactions();
  }

  async function copyAddress() {
    if (!walletAddress) return;
    try {
      await navigator.clipboard.writeText(walletAddress);
      copied = true;
      setTimeout(() => (copied = false), 2000);
    } catch {}
  }
</script>

<div class="content-area">
  <div class="page-col">
  <PixelStage height={60}>
    <div class="send-track">
      <PixelSprite src={coinSprite} anim="slide" size={18} travel={44} />
      <PixelSprite src={snGlyph} anim="idle" size={20} label="destination" />
    </div>
  </PixelStage>
  <div class="section-title">SEND NGT</div>
  <div class="privacy-bar">
    <span class="mode-label">DESKTOP SEND IS ALWAYS PRIVATE</span>
    {#if privacyInfo}
      <span class="privacy-badge" class:enabled={privacyInfo.stealth_enabled}>STEALTH</span>
      <span class="privacy-badge" class:enabled={privacyInfo.ring_enabled}>RING {privacyInfo.ring_size || 11}</span>
      <span class="privacy-badge" class:enabled={privacyInfo.confidential_enabled}>RINGCT</span>
    {/if}
    <span class="privacy-badge" class:enabled={onTor}>TOR</span>
    <span class="privacy-note">Stealth dest + MLSAG-2 RingCT + 64-bit range proofs. Not Monero. No bulletproofs. Mining rewards are RingCT coinbase. Memo is local only, never on chain.</span>
  </div>

  {#if !onTor}
    <div class="error-msg">TOR REQUIRED</div>
  {/if}
  {#if privacyFailClosed}
    <div class="error-msg">FAIL CLOSED</div>
  {/if}

  <div class="card">
    <div class="form-group">
      <label for="send-recipient">RECIPIENT STEALTH ADDRESS</label>
      <input
        id="send-recipient"
        class="mono"
        type="text"
        bind:value={recipient}
        placeholder="SN + 128 hex (from their Receive screen)"
        autocomplete="off"
        spellcheck="false"
      />
    </div>
    <div class="form-group">
      <div class="amount-head">
        <label for="send-amount">AMOUNT</label>
        <span class="bal-hint">WALLET {$nodeStatus.balance} NGT</span>
      </div>
      <input id="send-amount" type="text" bind:value={amount} placeholder="0.00" inputmode="decimal" />
    </div>
    <div class="form-group">
      <label for="send-memo">MEMO (LOCAL ONLY)</label>
      <input id="send-memo" type="text" bind:value={memo} placeholder="never published on the tx" />
    </div>
    {#if sendError}
      <div class="error-msg">{sendError}</div>
    {/if}
    {#if sendSuccess}
      <div class="success-msg">{sendSuccess}</div>
    {/if}
    <button class="btn-primary" type="button" on:click={handleSend} disabled={sending || sendLocked}>
      {#if sending}<span class="ks-spinner"></span>{/if}{sending ? "[ SENDING... ]" : "[ SEND PRIVATE ]"}
    </button>
  </div>

  <div class="section-title">RECEIVE</div>
  <div class="card">
    <div class="card-header">YOUR STEALTH ADDRESS</div>
    <code class="addr mono">{walletAddress || "NO ADDRESS"}</code>
    <button class="fbtn copy-btn" type="button" on:click={copyAddress} disabled={!walletAddress}>
      {copied ? "COPIED" : "COPY"}
    </button>
    <div class="qr-small">
      {#if qrSvg}
        {@html qrSvg}
      {/if}
    </div>
  </div>

  <div class="card">
    <div class="card-header">HISTORY</div>
  <div class="filter-row">
    <button class="fbtn" type="button" class:active={filter === "all"} on:click={() => setFilter("all")}>ALL</button>
    <button class="fbtn" type="button" class:active={filter === "sent"} on:click={() => setFilter("sent")}>SENT</button>
    <button class="fbtn" type="button" class:active={filter === "received"} on:click={() => setFilter("received")}>RECEIVED</button>
    <button class="fbtn" type="button" class:active={filter === "rewards"} on:click={() => setFilter("rewards")}>REWARDS</button>
  </div>
  <div class="table-wrap">
    <table>
      <thead><tr><th>TYPE</th><th>AMOUNT</th><th>DETAIL</th><th>TIME</th><th>STATUS</th></tr></thead>
      <tbody>
        {#each transactions as tx}
          <tr>
            <td><span class="tag">{tx.type}</span></td>
            <td>{tx.amount} NGT</td>
            <td class="detail-cell mono">{tx.txid ? tx.txid.slice(0, 16) : "-"}</td>
            <td>{tx.timestamp}</td>
            <td><span class="status-{tx.status}">{tx.status}</span></td>
          </tr>
        {:else}
          <tr><td colspan="5" class="empty-row">{emptyHistoryText()}</td></tr>
        {/each}
      </tbody>
    </table>
  </div>
  </div>
  </div>
</div>

<style>
  /* Body/data stays readable mono; chrome is pixel via tokens. */
  .content-area {
    font-family: var(--font-mono);
    font-size: 12px;
    color: var(--text-primary);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
  }

  .send-track {
    display: flex;
    align-items: center;
    gap: 26px;
    padding-bottom: 12px;
  }

  /* Dark pixel spinner: it sits on the white btn-primary while sending. */
  .content-area :global(.ks-spinner) {
    display: inline-block;
    width: 12px;
    height: 12px;
    margin-right: 8px;
    border: 2px solid rgba(0, 0, 0, 0.3);
    border-radius: 0;
    background: linear-gradient(#000, #000) left top / 40% 40% no-repeat;
    image-rendering: pixelated;
    animation: ks-spin 0.7s steps(8) infinite;
    vertical-align: -1px;
  }

  @keyframes ks-spin {
    to { transform: rotate(360deg); }
  }

  .mono {
    font-family: var(--font-mono, ui-monospace, "SF Mono", Menlo, Consolas, monospace);
  }

  button {
    font-family: var(--font);
    font-size: 10px;
    letter-spacing: 0;
    border-radius: 0;
    transition:
      border-color var(--dur) var(--ease),
      background-color var(--dur) var(--ease),
      color var(--dur) var(--ease);
  }

  input {
    font-family: var(--font-mono);
    font-size: 12px;
    letter-spacing: 0;
    border-radius: 0;
  }

  button:hover:not(:disabled) {
    box-shadow: none;
  }

  button:focus-visible,
  input:focus-visible {
    outline: 2px solid var(--text-primary);
    outline-offset: 2px;
  }

  input {
    background: var(--surface-solid, #111);
    border: 1px solid var(--border);
    color: var(--text-primary);
  }

  .card,
  .privacy-bar,
  .table-wrap {
    border-radius: 0;
    background: none;
    border: none;
    border-top: 1px solid var(--border);
  }

  .card:hover {
    border-color: rgba(255, 255, 255, 0.18);
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

  .error-msg,
  .success-msg {
    font-size: 13px;
    border-radius: var(--radius-sm);
    letter-spacing: 0.02em;
  }

  .tag {
    font-family: var(--font);
    font-size: 8px;
    border-radius: 0;
    padding: 3px 7px;
    letter-spacing: 0;
  }

  table {
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif);
    font-size: 13px;
  }

  th, td {
    font-size: 13px;
    padding: 10px 12px;
  }

  thead th {
    position: sticky;
    top: 0;
    z-index: 2;
    background: var(--surface-solid, #111);
    font-size: 11px;
    letter-spacing: 0.06em;
    color: var(--text-faint);
  }

  .table-wrap {
    overflow: auto;
    max-height: min(520px, 60vh);
  }

  .filter-row {
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
    margin-bottom: 10px;
  }

  .fbtn {
    font-size: 12px;
    padding: 6px 12px;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text-secondary);
    background: var(--surface);
    letter-spacing: 0.04em;
  }

  .fbtn:hover { color: var(--text-primary); border-color: rgba(255, 255, 255, 0.22); }

  .fbtn.active {
    color: #000;
    background: var(--text-primary);
    border-color: var(--text-primary);
  }

  .copy-btn { margin-top: 8px; }

  .addr {
    font-size: 12px;
    word-break: break-all;
    color: var(--text-primary);
    display: block;
    margin-top: 4px;
    line-height: 1.55;
    user-select: text;
    -webkit-user-select: text;
  }

  .qr-small {
    display: flex;
    justify-content: center;
    margin-top: 10px;
  }

  .qr-small :global(svg) {
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    border-radius: var(--radius-sm);
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 28px 16px;
    font-size: 13px;
    letter-spacing: 0.02em;
  }

  .detail-cell {
    font-size: 12px;
    color: var(--text-secondary);
    max-width: 140px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .privacy-bar {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 10px;
    flex-wrap: wrap;
    padding: 12px 14px;
  }

  .mode-label {
    font-size: 12px;
    letter-spacing: 0.06em;
    font-weight: 600;
    color: var(--ok, #00c853);
  }

  .privacy-note {
    flex-basis: 100%;
    font-size: 12px;
    letter-spacing: 0.01em;
    color: var(--text-secondary);
    line-height: 1.5;
  }

  .privacy-badge {
    font-family: var(--font);
    font-size: 8px;
    padding: 4px 7px;
    border: 1px solid var(--border);
    border-radius: 0;
    color: var(--text-secondary);
    letter-spacing: 0;
  }

  .privacy-badge.enabled {
    color: var(--ok, #00c853);
    border-color: var(--ok, #00c853);
  }

  .amount-head {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 8px;
    margin-bottom: 4px;
  }

  .amount-head label {
    margin-bottom: 0;
  }

  .bal-hint {
    font-size: 12px;
    letter-spacing: 0.02em;
    color: var(--text-secondary);
    white-space: nowrap;
  }

  .status-confirmed { color: var(--ok, #00c853); }
  .status-pending { color: var(--warn, #ffc107); }
  .status-broadcast { color: var(--warn, #ffc107); }
  .status-failed { color: var(--err, #f44); }

  button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
</style>
