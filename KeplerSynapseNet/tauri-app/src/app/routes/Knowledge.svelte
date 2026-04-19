<script lang="ts">
  import { onMount } from "svelte";
  import { submitKnowledge, searchKnowledge, rpcCall } from "../../lib/rpc";

  let title = "";
  let content = "";
  let citations = "";
  let submitError = "";
  let submitSuccess = "";

  let searchQuery = "";
  let searchResults: { title: string; content: string }[] = [];

  let mySubmissions: {
    title: string;
    status: string;
    ngt_earned: string;
  }[] = [];

  let poeStats = {
    epoch: 0,
    total_entries: 0,
    reward_pool: "0.00",
  };

  onMount(async () => {
    await loadMySubmissions();
    await loadPoeStats();
  });

  async function handleSubmit() {
    submitError = "";
    submitSuccess = "";
    if (!title.trim() || !content.trim()) {
      submitError = "Title and content are required.";
      return;
    }
    try {
      await submitKnowledge(title, content, citations);
      submitSuccess = "Knowledge entry submitted.";
      title = "";
      content = "";
      citations = "";
      await loadMySubmissions();
    } catch (e: any) {
      submitError = e.message || "Submission failed.";
    }
  }

  async function handleSearch() {
    if (!searchQuery.trim()) return;
    try {
      const result = await searchKnowledge(searchQuery);
      const parsed = JSON.parse(result);
      searchResults = parsed.results || [];
    } catch {
      searchResults = [];
    }
  }

  async function loadMySubmissions() {
    try {
      const result = await rpcCall("knowledge.my_submissions", "{}");
      const parsed = JSON.parse(result);
      mySubmissions = parsed.submissions || [];
    } catch {
      mySubmissions = [];
    }
  }

  async function loadPoeStats() {
    try {
      const result = await rpcCall("poe.stats", "{}");
      const parsed = JSON.parse(result);
      poeStats = {
        epoch: parsed.epoch || 0,
        total_entries: parsed.total_entries || 0,
        reward_pool: parsed.reward_pool || "0.00",
      };
    } catch {}
  }

  function handleSearchKeydown(e: KeyboardEvent) {
    if (e.key === "Enter") handleSearch();
  }
</script>

<div class="content-area">
  <div class="section-title">Submit Knowledge</div>
  <div class="card">
    <div class="form-group">
      <label>Title</label>
      <input type="text" bind:value={title} placeholder="Entry title" />
    </div>
    <div class="form-group">
      <label>Content</label>
      <textarea bind:value={content} rows="6" placeholder="Knowledge content"></textarea>
    </div>
    <div class="form-group">
      <label>Citations</label>
      <input type="text" bind:value={citations} placeholder="Sources, comma separated" />
    </div>
    {#if submitError}
      <div class="error-msg">{submitError}</div>
    {/if}
    {#if submitSuccess}
      <div class="success-msg">{submitSuccess}</div>
    {/if}
    <button class="btn-primary" on:click={handleSubmit}>Submit</button>
  </div>

  <div class="section-title">Browse Local Chain</div>
  <div class="search-row">
    <input type="text" bind:value={searchQuery} placeholder="Search knowledge entries" on:keydown={handleSearchKeydown} />
    <button class="btn-secondary" on:click={handleSearch}>Search</button>
  </div>
  {#each searchResults as result}
    <div class="card">
      <div class="card-header">{result.title}</div>
      <p class="result-content">{result.content}</p>
    </div>
  {:else}
    <p class="empty-text">No results</p>
  {/each}

  <div class="section-title">My Submissions</div>
  <table>
    <thead>
      <tr>
        <th>Title</th>
        <th>Status</th>
        <th>NGT Earned</th>
      </tr>
    </thead>
    <tbody>
      {#each mySubmissions as sub}
        <tr>
          <td>{sub.title}</td>
          <td><span class="tag">{sub.status}</span></td>
          <td>{sub.ngt_earned} NGT</td>
        </tr>
      {:else}
        <tr>
          <td colspan="3" class="empty-row">No submissions</td>
        </tr>
      {/each}
    </tbody>
  </table>

  <div class="section-title">PoE Stats</div>
  <div class="grid-3">
    <div class="card">
      <div class="card-header">Current Epoch</div>
      <div class="card-value">{poeStats.epoch}</div>
    </div>
    <div class="card">
      <div class="card-header">Total Entries</div>
      <div class="card-value">{poeStats.total_entries}</div>
    </div>
    <div class="card">
      <div class="card-header">Reward Pool</div>
      <div class="card-value">{poeStats.reward_pool} NGT</div>
    </div>
  </div>
</div>

<style>
  .search-row {
    display: flex;
    gap: 8px;
    margin-bottom: 12px;
  }

  .search-row input {
    flex: 1;
    color: var(--text-primary);
    border-color: var(--border);
    background: var(--bg);
  }

  .result-content {
    font-size: 12px;
    color: var(--text-secondary);
    margin-top: 6px;
    line-height: 1.6;
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

  .empty-text {
    font-size: 12px;
    color: var(--text-secondary);
    margin-bottom: 12px;
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 20px;
  }
</style>
