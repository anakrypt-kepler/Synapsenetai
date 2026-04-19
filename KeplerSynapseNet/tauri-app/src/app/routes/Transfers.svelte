<script lang="ts">
  import { onMount } from "svelte";
  import { nodeStatus } from "../../lib/store";
  import { sendNgt, getTransactions, rpcCall } from "../../lib/rpc";

  let recipient = "";
  let amount = "";
  let memo = "";
  let sendError = "";
  let sendSuccess = "";

  let transactions: {
    type: string;
    amount: string;
    timestamp: string;
    status: string;
  }[] = [];
  let filter = "all";
  let walletAddress = "";

  onMount(async () => {
    await loadTransactions();
    try {
      const result = await rpcCall("wallet.info", "{}");
      const info = JSON.parse(result);
      walletAddress = info.address || "";
    } catch {}
  });

  async function loadTransactions() {
    try {
      const result = await getTransactions(filter);
      const parsed = JSON.parse(result);
      transactions = parsed.transactions || [];
    } catch {
      transactions = [];
    }
  }

  async function handleSend() {
    sendError = "";
    sendSuccess = "";
    if (!recipient.trim() || !amount.trim()) {
      sendError = "Recipient and amount are required.";
      return;
    }
    try {
      await sendNgt(recipient, amount, memo || undefined);
      sendSuccess = "Transaction submitted.";
      recipient = "";
      amount = "";
      memo = "";
      await loadTransactions();
    } catch (e: any) {
      sendError = e.message || "Transaction failed.";
    }
  }

  function setFilter(f: string) {
    filter = f;
    loadTransactions();
  }
</script>

<div class="content-area">
  <div class="section-title">Send NGT</div>
  <div class="card">
    <div class="form-group">
      <label>Recipient Address</label>
      <input type="text" bind:value={recipient} placeholder="NGT address" />
    </div>
    <div class="form-group">
      <label>Amount</label>
      <input type="text" bind:value={amount} placeholder="0.00" />
    </div>
    <div class="form-group">
      <label>Memo (optional)</label>
      <input type="text" bind:value={memo} placeholder="Optional memo" />
    </div>
    {#if sendError}
      <div class="error-msg">{sendError}</div>
    {/if}
    {#if sendSuccess}
      <div class="success-msg">{sendSuccess}</div>
    {/if}
    <button class="btn-primary" on:click={handleSend}>Send</button>
  </div>

  <div class="section-title">Receive</div>
  <div class="card">
    <div class="card-header">Your Address</div>
    <code class="address-display">{walletAddress || "Loading..."}</code>
  </div>

  <div class="section-title">Transaction History</div>
  <div class="filter-row">
    <button class="filter-btn" class:active={filter === "all"} on:click={() => setFilter("all")}>All</button>
    <button class="filter-btn" class:active={filter === "sent"} on:click={() => setFilter("sent")}>Sent</button>
    <button class="filter-btn" class:active={filter === "received"} on:click={() => setFilter("received")}>Received</button>
    <button class="filter-btn" class:active={filter === "rewards"} on:click={() => setFilter("rewards")}>Rewards</button>
  </div>
  <table>
    <thead>
      <tr>
        <th>Type</th>
        <th>Amount</th>
        <th>Timestamp</th>
        <th>Status</th>
      </tr>
    </thead>
    <tbody>
      {#each transactions as tx}
        <tr>
          <td>
            <span class="tag">{tx.type}</span>
          </td>
          <td>{tx.amount} NGT</td>
          <td>{tx.timestamp}</td>
          <td>{tx.status}</td>
        </tr>
      {:else}
        <tr>
          <td colspan="4" class="empty-row">No transactions</td>
        </tr>
      {/each}
    </tbody>
  </table>
</div>

<style>
  .filter-row {
    display: flex;
    gap: 4px;
    margin-bottom: 12px;
  }

  .filter-btn {
    font-size: 11px;
    padding: 4px 12px;
    border: 1px solid var(--border);
    border-radius: 4px;
    color: var(--text-secondary);
    background: none;
    transition: color 0.15s ease, border-color 0.15s ease, background 0.15s ease;
  }

  .filter-btn:hover {
    color: var(--text-primary);
    background: var(--surface-alt);
  }

  .filter-btn.active {
    color: var(--accent);
    border-color: var(--accent);
    background: var(--accent-muted);
  }

  .error-msg {
    font-size: 12px;
    color: var(--status-red);
    margin-bottom: 8px;
    padding: 6px 10px;
    border-radius: 4px;
    background: var(--status-red-muted);
    border: 1px solid var(--status-red);
  }

  .success-msg {
    font-size: 12px;
    color: var(--status-green);
    margin-bottom: 8px;
    padding: 6px 10px;
    border-radius: 4px;
    background: var(--status-green-muted);
    border: 1px solid var(--status-green);
  }

  .address-display {
    font-size: 11px;
    word-break: break-all;
    color: var(--text-primary);
    display: block;
    margin-top: 6px;
    line-height: 1.6;
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 20px;
  }
</style>
