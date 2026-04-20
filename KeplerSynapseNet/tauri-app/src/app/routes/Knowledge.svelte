<script lang="ts">
  import { onMount } from "svelte";
  import { submitKnowledge, searchKnowledge, rpcCall } from "../../lib/rpc";

  let submitTitle = "";
  let submitContent = "";
  let submitCitations = "";
  let submitError = "";
  let submitSuccess = "";
  let searchQuery = "";
  let searchResults: { title: string; snippet: string; score: number; author: string }[] = [];
  let mySubmissions: { title: string; status: string; ngt_earned: string }[] = [];
  let poeStats = { current_epoch: 0, total_entries: 0, reward_pool: "0", next_epoch_in: "0h 0m" };

  onMount(async () => {
    await loadMySubmissions();
    await loadPoeStats();
  });

  async function handleSubmit() {
    submitError = "";
    submitSuccess = "";
    if (!submitTitle.trim() || !submitContent.trim()) {
      submitError = "TITLE AND CONTENT REQUIRED";
      return;
    }
    try {
      await submitKnowledge(submitTitle, submitContent, submitCitations || undefined);
      submitSuccess = "SUBMITTED FOR REVIEW";
      submitTitle = "";
      submitContent = "";
      submitCitations = "";
      await loadMySubmissions();
    } catch (e: any) {
      submitError = e.message || "SUBMISSION FAILED";
    }
  }

  async function handleSearch() {
    if (!searchQuery.trim()) return;
    try {
      const result = await searchKnowledge(searchQuery);
      const parsed = JSON.parse(result);
      searchResults = parsed.results || [];
    } catch { searchResults = []; }
  }

  async function loadMySubmissions() {
    try {
      const result = await rpcCall("knowledge.my_submissions", "{}");
      const parsed = JSON.parse(result);
      mySubmissions = parsed.submissions || [];
    } catch {}
  }

  async function loadPoeStats() {
    try {
      const result = await rpcCall("poe.stats", "{}");
      const parsed = JSON.parse(result);
      poeStats = { ...poeStats, ...parsed };
    } catch {}
  }
</script>

<div class="content-area">
  <div class="section-title">SUBMIT KNOWLEDGE</div>
  <div class="card">
    <div class="form-group">
      <label>TITLE</label>
      <input type="text" bind:value={submitTitle} placeholder="Entry title" />
    </div>
    <div class="form-group">
      <label>CONTENT</label>
      <textarea bind:value={submitContent} rows="6" placeholder="Knowledge content"></textarea>
    </div>
    <div class="form-group">
      <label>CITATIONS</label>
      <input type="text" bind:value={submitCitations} placeholder="URLs, comma separated" />
    </div>
    {#if submitError}<div class="error-msg">{submitError}</div>{/if}
    {#if submitSuccess}<div class="success-msg">{submitSuccess}</div>{/if}
    <button class="btn-primary" on:click={handleSubmit}>[ SUBMIT ]</button>
  </div>

  <div class="section-title">SEARCH</div>
  <div class="card">
    <div class="search-row">
      <input type="text" bind:value={searchQuery} placeholder="Search..." />
      <button class="btn-primary" on:click={handleSearch}>[ GO ]</button>
    </div>
    {#each searchResults as result}
      <div class="result-item">
        <div class="result-title">{result.title}</div>
        <div class="result-snip">{result.snippet}</div>
        <div class="result-meta">SCORE:{result.score.toFixed(2)} AUTHOR:{result.author}</div>
      </div>
    {/each}
  </div>

  <div class="section-title">MY SUBMISSIONS</div>
  <table>
    <thead><tr><th>TITLE</th><th>STATUS</th><th>NGT</th></tr></thead>
    <tbody>
      {#each mySubmissions as sub}
        <tr>
          <td>{sub.title}</td>
          <td><span class="tag">{sub.status}</span></td>
          <td>{sub.ngt_earned}</td>
        </tr>
      {:else}
        <tr><td colspan="3" class="empty-row">NONE</td></tr>
      {/each}
    </tbody>
  </table>

  <div class="section-title">PROOF-OF-EXISTENCE</div>
  <div class="grid-2">
    <div class="card">
      <div class="card-header">EPOCH</div>
      <div class="card-value">{poeStats.current_epoch}</div>
    </div>
    <div class="card">
      <div class="card-header">ENTRIES</div>
      <div class="card-value">{poeStats.total_entries}</div>
    </div>
  </div>
  <div class="grid-2">
    <div class="card">
      <div class="card-header">REWARD POOL</div>
      <div class="card-value">{poeStats.reward_pool} NGT</div>
    </div>
    <div class="card">
      <div class="card-header">NEXT EPOCH</div>
      <div class="card-value">{poeStats.next_epoch_in}</div>
    </div>
  </div>
</div>

<style>
  .search-row {
    display: flex;
    gap: 4px;
    margin-bottom: 10px;
  }

  .search-row input { flex: 1; }

  .result-item {
    padding: 8px 0;
    border-bottom: 1px solid var(--border);
  }

  .result-title {
    font-size: 10px;
    font-weight: 700;
    color: var(--text-primary);
    margin-bottom: 2px;
  }

  .result-snip {
    font-size: 8px;
    color: var(--text-secondary);
    line-height: 1.6;
    margin-bottom: 2px;
  }

  .result-meta {
    font-size: 8px;
    color: var(--text-faint);
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 16px;
  }
</style>
