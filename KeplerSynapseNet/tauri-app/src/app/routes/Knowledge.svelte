<script lang="ts">
  // KNOW tab: one-screen PoE knowledge submit. Status stays pending and NGT is 0 until finalize/votes.
  // knowledge.submit writes knowledge.jsonl and appends a local PoE block. Still pending — not paid.
  import { onMount } from "svelte";
  import { submitKnowledge, searchKnowledge, rpcCall } from "../../lib/rpc";

  type KnowledgeHit = { title: string; snippet: string; author: string };
  type KnowledgeSub = { title: string; kind?: string; status: string; ngt_earned: string };

  let submitTitle = "";
  let submitContent = "";
  let submitCitations = "";
  let submitError = "";
  let submitSuccess = "";
  let submitting = false;
  let searchQuery = "";
  let searchTried = false;
  let searchResults: KnowledgeHit[] = [];
  let mySubmissions: KnowledgeSub[] = [];
  let poeStats = {
    current_epoch: 0,
    total_entries: 0,
    reward_pool: "0",
    chain_height: 0,
    next_epoch_in: "0h 0m",
  };

  onMount(async () => {
    await loadMySubmissions();
    await loadPoeStats();
  });

  function pendingBanner(raw: string): { error?: string; ok: string } {
    // Always pending / not paid. Show id/hash when the RPC JSON actually returns them.
    const pending = "SUBMITTED PENDING — NOT PAID YET";
    try {
      const parsed = JSON.parse(raw);
      if (parsed && parsed.error) {
        return { error: String(parsed.error).toUpperCase() };
      }
      const id = parsed?.id || parsed?.submitId || "";
      const hash = parsed?.hash || "";
      let ok = pending;
      if (id) ok += `  ID ${id}`;
      if (hash) ok += `  HASH ${hash}`;
      return { ok };
    } catch {
      return { ok: pending };
    }
  }

  async function handleSubmit() {
    submitError = "";
    submitSuccess = "";
    if (!submitTitle.trim() || !submitContent.trim()) {
      submitError = "TITLE AND CONTENT REQUIRED";
      return;
    }
    submitting = true;
    try {
      const raw = await submitKnowledge(submitTitle.trim(), submitContent, submitCitations);
      const outcome = pendingBanner(raw);
      if (outcome.error) {
        submitError = outcome.error;
      } else {
        submitSuccess = outcome.ok;
        submitTitle = "";
        submitContent = "";
        submitCitations = "";
        await loadMySubmissions();
        await loadPoeStats();
      }
    } catch (e: any) {
      submitError = e?.message || "SUBMISSION FAILED";
    }
    submitting = false;
  }

  async function handleSearch() {
    if (!searchQuery.trim()) return;
    searchTried = true;
    try {
      const result = await searchKnowledge(searchQuery.trim());
      const parsed = JSON.parse(result);
      const rows = parsed.results || [];
      // Hits are substring matches. Do not show a fake rank score.
      searchResults = rows.map((r: { title?: string; snippet?: string; author?: string }) => ({
        title: r.title || "untitled",
        snippet: r.snippet || "",
        author: r.author || "",
      }));
    } catch {
      searchResults = [];
    }
  }

  async function loadMySubmissions() {
    try {
      const result = await rpcCall("knowledge.my_submissions", "{}");
      const parsed = JSON.parse(result);
      const all: KnowledgeSub[] = parsed.submissions || [];
      mySubmissions = all.filter((s) => (s.kind || "knowledge") === "knowledge");
    } catch {
      mySubmissions = [];
    }
  }

  async function loadPoeStats() {
    try {
      const result = await rpcCall("poe.stats", "{}");
      poeStats = { ...poeStats, ...JSON.parse(result) };
    } catch {}
  }
</script>

<div class="content-area">
  <div class="section-title">SUBMIT KNOWLEDGE</div>
  <div class="card">
    <div class="path-hint">submit → local record pending → NGT on finalize</div>
    <div class="form-group">
      <label for="know-title">TITLE</label>
      <input id="know-title" type="text" bind:value={submitTitle} placeholder="Required" />
    </div>
    <div class="form-group">
      <label for="know-content">CONTENT</label>
      <textarea id="know-content" bind:value={submitContent} rows="6" placeholder="Required"></textarea>
    </div>
    <div class="form-group">
      <label for="know-citations">CITATIONS (OPTIONAL)</label>
      <input
        id="know-citations"
        type="text"
        bind:value={submitCitations}
        placeholder="URLs or IDs, comma separated"
      />
    </div>
    {#if submitError}<div class="error-msg">{submitError}</div>{/if}
    {#if submitSuccess}<div class="success-msg">{submitSuccess}</div>{/if}
    <button class="btn-primary" type="button" on:click={handleSubmit} disabled={submitting}>
      {#if submitting}
        <span class="ks-busy"><span class="ks-spinner"></span> SUBMITTING</span>
      {:else}
        SUBMIT
      {/if}
    </button>
  </div>

  <div class="section-title">SEARCH</div>
  <div class="card">
    <div class="form-group">
      <label for="know-search">QUERY</label>
      <div class="search-row">
        <input id="know-search" type="text" bind:value={searchQuery} placeholder="Search local knowledge" />
        <button class="btn-primary" type="button" on:click={handleSearch}>GO</button>
      </div>
    </div>
    {#each searchResults as result}
      <div class="result-item">
        <div class="result-title">{result.title}</div>
        {#if result.snippet}<div class="result-snip">{result.snippet}</div>{/if}
        <div class="result-meta">
          MATCH{#if result.author} AUTHOR:{result.author}{/if}
        </div>
      </div>
    {:else}
      {#if searchTried}
        <div class="empty-search">No matches</div>
      {/if}
    {/each}
  </div>

  <div class="section-title">MY SUBMISSIONS</div>
  <div class="table-wrap">
    <table>
      <thead>
        <tr>
          <th>TITLE</th>
          <th>STATUS</th>
          <th>NGT</th>
        </tr>
      </thead>
      <tbody>
        {#each [...mySubmissions].reverse() as sub}
          <tr>
            <td>{sub.title}</td>
            <td><span class="tag">{sub.status}</span></td>
            <td>{sub.ngt_earned}</td>
          </tr>
        {:else}
          <tr><td colspan="3" class="empty-row">No submissions</td></tr>
        {/each}
      </tbody>
    </table>
  </div>

  <div class="section-title">POE (PROOF OF EMERGENCE)</div>
  <div class="grid-3">
    <div class="card">
      <div class="card-header">EPOCH</div>
      <div class="card-value">{poeStats.current_epoch}</div>
    </div>
    <div class="card">
      <div class="card-header">ENTRIES</div>
      <div class="card-value">{poeStats.total_entries}</div>
    </div>
    <div class="card">
      <div class="card-header">CHAIN</div>
      <div class="card-value">{poeStats.chain_height}</div>
    </div>
  </div>
  <div class="grid-2 stats-row">
    <div class="card">
      <div class="card-header">WALLET</div>
      <div class="card-value">{poeStats.reward_pool} NGT</div>
    </div>
    <div class="card">
      <div class="card-header">NEXT EPOCH</div>
      <div class="card-value">{poeStats.next_epoch_in}</div>
    </div>
  </div>
</div>

<style>
  .content-area {
    font-family: var(--font);
    font-size: 13px;
    line-height: 1.45;
    color: var(--text-primary);
    background: transparent;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    image-rendering: auto;
    padding-bottom: 84px;
  }

  .section-title {
    font-family: var(--font);
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.08em;
    color: var(--text-secondary);
    margin-bottom: 10px;
    margin-top: 20px;
  }

  .section-title:first-child {
    margin-top: 0;
  }

  .card {
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 16px;
    margin-bottom: 10px;
    transition: border-color var(--dur) var(--ease), box-shadow var(--dur) var(--ease);
  }

  .card:hover {
    border-color: rgba(255, 255, 255, 0.18);
  }

  .card-header {
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.06em;
    color: var(--text-secondary);
    margin-bottom: 6px;
  }

  .card-value {
    font-size: 15px;
    font-weight: 600;
    color: var(--text-primary);
  }

  .form-group label {
    font-family: var(--font);
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.06em;
    color: var(--text-secondary);
  }

  .content-area :global(input),
  .content-area :global(textarea),
  .content-area :global(button) {
    font-family: var(--font);
    font-size: 13px;
    border-radius: var(--radius-sm);
    image-rendering: auto;
    -webkit-font-smoothing: antialiased;
    transition: background var(--dur) var(--ease), border-color var(--dur) var(--ease), color var(--dur) var(--ease);
  }

  .content-area :global(input),
  .content-area :global(textarea) {
    background: rgba(0, 0, 0, 0.45);
    border: 1px solid var(--border);
    color: var(--text-primary);
    padding: 8px 12px;
  }

  .content-area :global(input:focus),
  .content-area :global(textarea:focus) {
    border-color: rgba(255, 255, 255, 0.32);
    outline: none;
  }

  .btn-primary {
    font-weight: 600;
    letter-spacing: 0.04em;
    padding: 8px 16px;
  }

  .error-msg,
  .success-msg {
    font-family: var(--font);
    font-size: 12px;
    border-radius: var(--radius-sm);
  }

  .path-hint {
    font-size: 12px;
    color: var(--text-secondary);
    letter-spacing: 0.01em;
    line-height: 1.5;
    margin-bottom: 12px;
  }

  .search-row {
    display: flex;
    gap: 8px;
  }

  .search-row input { flex: 1; }

  .result-item {
    padding: 12px 0;
    border-bottom: 1px solid var(--border);
  }

  .result-item:last-child {
    border-bottom: none;
    padding-bottom: 0;
  }

  .result-title {
    font-size: 14px;
    font-weight: 600;
    color: var(--text-primary);
    margin-bottom: 4px;
  }

  .result-snip {
    font-size: 13px;
    color: var(--text-secondary);
    line-height: 1.5;
    margin-bottom: 4px;
  }

  .result-meta {
    font-size: 11px;
    color: var(--text-faint);
  }

  .empty-search {
    text-align: center;
    color: var(--text-secondary);
    font-size: 13px;
    padding: 16px 0 4px;
  }

  .table-wrap {
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
    border: 1px solid var(--border);
    border-radius: var(--radius);
    overflow: auto;
    margin-bottom: 10px;
  }

  .table-wrap table {
    font-family: var(--font);
    font-size: 13px;
    margin: 0;
  }

  .table-wrap th {
    font-family: var(--font);
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.06em;
    color: var(--text-secondary);
    padding: 10px 12px;
  }

  .table-wrap td {
    font-size: 13px;
    padding: 10px 12px;
  }

  .tag {
    font-family: var(--font);
    font-size: 11px;
    border-radius: var(--radius-full, 999px);
    padding: 2px 8px;
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 20px;
    font-size: 13px;
  }

  .stats-row {
    margin-top: 8px;
  }

  .ks-busy {
    display: inline-flex;
    align-items: center;
    gap: 8px;
  }

  .ks-busy .ks-spinner {
    width: 16px;
    height: 16px;
  }
</style>
