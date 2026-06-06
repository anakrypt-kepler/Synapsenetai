<script lang="ts">
  import { onMount } from "svelte";
  import { nodeStatus } from "../../lib/store";
  import { sendNgt, getTransactions, rpcCall, privacyStealthSend, privacyStatus } from "../../lib/rpc";
  import { generateQRSvg } from "../../lib/qr";

  let recipient = "";
  let amount = "";
  let memo = "";
  let sendError = "";
  let sendSuccess = "";
  let sending = false;
  let privacyMode = false;
  let privacyInfo: { stealth_enabled: boolean; ring_enabled: boolean; confidential_enabled: boolean } | null = null;
  let transactions: { type: string; amount: string; timestamp: string; status: string; to?: string; from?: string; txid?: string }[] = [];
  let filter = "all";
  let walletAddress = "";
  let qrSvg = "";

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
      return `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,"0")}-${String(d.getDate()).padStart(2,"0")} ${String(d.getHours()).padStart(2,"0")}:${String(d.getMinutes()).padStart(2,"0")}`;
    }
    return String(raw);
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
    } catch { transactions = []; }
  }

  async function handleSend() {
    sendError = "";
    sendSuccess = "";
    if (!recipient.trim() || !amount.trim()) {
      sendError = "RECIPIENT AND AMOUNT REQUIRED";
      return;
    }
    const numAmt = parseFloat(amount);
    if (isNaN(numAmt) || numAmt <= 0) {
      sendError = "INVALID AMOUNT";
      return;
    }
    sending = true;
    try {
      const raw = privacyMode
        ? await privacyStealthSend(recipient, amount, memo || undefined)
        : await sendNgt(recipient, amount, memo || undefined);
      const resp = JSON.parse(raw);
      if (resp.error) {
        sendError = resp.error.toUpperCase();
      } else {
        sendSuccess = `TX CONFIRMED: ${resp.txid || "OK"}`;
        recipient = "";
        amount = "";
        memo = "";
        await loadTransactions();
      }
    } catch (e: any) {
      sendError = e.message || "TX FAILED";
    }
    sending = false;
  }

  function setFilter(f: string) {
    filter = f;
    loadTransactions();
  }
</script>

<div class="content-area">
  <div class="section-title">SEND NGT</div>
  <div class="section-title">PRIVACY MODE</div>
  <div class="privacy-bar">
    <button class="fbtn" class:active={!privacyMode} on:click={() => privacyMode = false}>STANDARD</button>
    <button class="fbtn" class:active={privacyMode} on:click={() => privacyMode = true}>ANONYMOUS</button>
    {#if privacyInfo}
      <span class="privacy-badge" class:enabled={privacyInfo.stealth_enabled}>STEALTH</span>
      <span class="privacy-badge" class:enabled={privacyInfo.ring_enabled}>RING</span>
      <span class="privacy-badge" class:enabled={privacyInfo.confidential_enabled}>CT</span>
    {/if}
  </div>
  <div class="card">
    <div class="form-group">
      <label>RECIPIENT</label>
      <input type="text" bind:value={recipient} placeholder="NGT address" />
    </div>
    <div class="form-group">
      <label>AMOUNT</label>
      <input type="text" bind:value={amount} placeholder="0.00" />
    </div>
    <div class="form-group">
      <label>MEMO</label>
      <input type="text" bind:value={memo} placeholder="optional" />
    </div>
    {#if sendError}
      <div class="error-msg">{sendError}</div>
    {/if}
    {#if sendSuccess}
      <div class="success-msg">{sendSuccess}</div>
    {/if}
    <button class="btn-primary" on:click={handleSend} disabled={sending}>
      {sending ? "[ SENDING... ]" : "[ SEND ]"}
    </button>
  </div>

  <div class="section-title">RECEIVE</div>
  <div class="card">
    <div class="card-header">YOUR ADDRESS</div>
    <code class="addr">{walletAddress || "..."}</code>
    <div class="qr-small">
      {#if qrSvg}
        {@html qrSvg}
      {/if}
    </div>
  </div>

  <div class="section-title">HISTORY</div>
  <div class="filter-row">
    <button class="fbtn" class:active={filter === "all"} on:click={() => setFilter("all")}>ALL</button>
    <button class="fbtn" class:active={filter === "sent"} on:click={() => setFilter("sent")}>SENT</button>
    <button class="fbtn" class:active={filter === "received"} on:click={() => setFilter("received")}>RECV</button>
    <button class="fbtn" class:active={filter === "rewards"} on:click={() => setFilter("rewards")}>MINE</button>
  </div>
  <table>
    <thead><tr><th>TYPE</th><th>AMOUNT</th><th>DETAIL</th><th>TIME</th><th>STATUS</th></tr></thead>
    <tbody>
      {#each transactions as tx}
        <tr>
          <td><span class="tag">{tx.type}</span></td>
          <td>{tx.amount} NGT</td>
          <td class="detail-cell">{tx.to ? `→ ${tx.to.slice(0,12)}…` : tx.txid ? tx.txid : "-"}</td>
          <td>{tx.timestamp}</td>
          <td><span class="status-{tx.status}">{tx.status}</span></td>
        </tr>
      {:else}
        <tr><td colspan="5" class="empty-row">NO TRANSACTIONS</td></tr>
      {/each}
    </tbody>
  </table>
</div>

<style>
  .filter-row {
    display: flex;
    gap: 2px;
    margin-bottom: 8px;
  }

  .fbtn {
    font-size: 8px;
    padding: 4px 10px;
    border: 1px solid var(--border);
    color: var(--text-secondary);
    background: none;
  }

  .fbtn:hover { color: var(--text-primary); border-color: var(--text-primary); }

  .fbtn.active {
    color: #000;
    background: var(--text-primary);
    border-color: var(--text-primary);
  }

  .addr {
    font-size: 8px;
    word-break: break-all;
    color: var(--text-primary);
    display: block;
    margin-top: 4px;
    line-height: 1.6;
  }

  .qr-small {
    display: flex;
    justify-content: center;
    margin-top: 8px;
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 16px;
  }

  .detail-cell {
    font-size: 7px;
    color: var(--text-secondary);
    max-width: 100px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .privacy-bar {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-bottom: 8px;
    flex-wrap: wrap;
  }

  .privacy-badge {
    font-size: 7px;
    padding: 3px 6px;
    border: 1px solid var(--border);
    color: var(--text-secondary);
    letter-spacing: 1px;
  }

  .privacy-badge.enabled {
    color: #00c853;
    border-color: #00c853;
  }

  .status-confirmed { color: #00c853; }
  .status-pending { color: #ffc107; }
  .status-failed { color: #f44; }

  button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
</style>
