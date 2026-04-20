<script lang="ts">
  import { onMount } from "svelte";
  import { nodeStatus } from "../../lib/store";
  import { sendNgt, getTransactions, rpcCall } from "../../lib/rpc";
  import { generateQRSvg } from "../../lib/qr";

  let recipient = "";
  let amount = "";
  let memo = "";
  let sendError = "";
  let sendSuccess = "";
  let transactions: { type: string; amount: string; timestamp: string; status: string }[] = [];
  let filter = "all";
  let walletAddress = "";
  let qrSvg = "";

  onMount(async () => {
    await loadTransactions();
    try {
      const result = await rpcCall("wallet.info", "{}");
      const info = JSON.parse(result);
      walletAddress = info.address || "";
      if (walletAddress) qrSvg = generateQRSvg(walletAddress, 2);
    } catch {}
  });

  async function loadTransactions() {
    try {
      const result = await getTransactions(filter);
      const parsed = JSON.parse(result);
      transactions = parsed.transactions || [];
    } catch { transactions = []; }
  }

  async function handleSend() {
    sendError = "";
    sendSuccess = "";
    if (!recipient.trim() || !amount.trim()) {
      sendError = "RECIPIENT AND AMOUNT REQUIRED";
      return;
    }
    try {
      await sendNgt(recipient, amount, memo || undefined);
      sendSuccess = "TX SUBMITTED";
      recipient = "";
      amount = "";
      memo = "";
      await loadTransactions();
    } catch (e: any) {
      sendError = e.message || "TX FAILED";
    }
  }

  function setFilter(f: string) {
    filter = f;
    loadTransactions();
  }
</script>

<div class="content-area">
  <div class="section-title">SEND NGT</div>
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
    <button class="btn-primary" on:click={handleSend}>[ SEND ]</button>
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
    <thead><tr><th>TYPE</th><th>AMOUNT</th><th>TIME</th><th>STATUS</th></tr></thead>
    <tbody>
      {#each transactions as tx}
        <tr>
          <td><span class="tag">{tx.type}</span></td>
          <td>{tx.amount} NGT</td>
          <td>{tx.timestamp}</td>
          <td>{tx.status}</td>
        </tr>
      {:else}
        <tr><td colspan="4" class="empty-row">NO TRANSACTIONS</td></tr>
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
</style>
