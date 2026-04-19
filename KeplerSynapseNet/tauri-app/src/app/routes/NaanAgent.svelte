<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { naanControl, naanConfigUpdate, rpcCall } from "../../lib/rpc";

  let agentStatus = "OFF";
  let agentScore = { band: "-", submissions: 0, approval_rate: "0%" };
  let currentTask = "";
  let draftQueue: { title: string; preview: string }[] = [];
  let submissionHistory: { title: string; result: string; ngt_earned: string }[] = [];

  let topicPreferences = "";
  let researchSources = "both";
  let tickInterval = 60;
  let budgetLimit = "100";

  let observatory: { agent: string; task: string; status: string }[] = [];

  let pollHandle: ReturnType<typeof setInterval> | null = null;

  onMount(async () => {
    await loadAgentState();
    pollHandle = setInterval(loadAgentState, 3000);
  });

  onDestroy(() => {
    if (pollHandle) clearInterval(pollHandle);
  });

  async function loadAgentState() {
    try {
      const result = await rpcCall("naan.status", "{}");
      const parsed = JSON.parse(result);
      agentStatus = parsed.status || "OFF";
      agentScore = parsed.score || agentScore;
      currentTask = parsed.current_task || "";
      draftQueue = parsed.draft_queue || [];
      submissionHistory = parsed.history || [];
      observatory = parsed.observatory || [];

      if (parsed.config) {
        topicPreferences = parsed.config.topics || topicPreferences;
        researchSources = parsed.config.sources || researchSources;
        tickInterval = parsed.config.tick_interval || tickInterval;
        budgetLimit = parsed.config.budget_limit || budgetLimit;
      }
    } catch {}
  }

  async function startAgent() {
    try {
      await naanControl("start");
      await loadAgentState();
    } catch {}
  }

  async function stopAgent() {
    try {
      await naanControl("stop");
      await loadAgentState();
    } catch {}
  }

  async function saveConfig() {
    try {
      await naanConfigUpdate(
        JSON.stringify({
          topics: topicPreferences,
          sources: researchSources,
          tick_interval: tickInterval,
          budget_limit: budgetLimit,
        })
      );
    } catch {}
  }
</script>

<div class="content-area">
  <div class="grid-2">
    <div class="card">
      <div class="card-header">Status</div>
      <div class="card-value status-label" class:active={agentStatus === "ACTIVE"} class:cooldown={agentStatus === "COOLDOWN"} class:quarantine={agentStatus === "QUARANTINE"}>
        {agentStatus}
      </div>
    </div>
    <div class="card">
      <div class="card-header">Control</div>
      <div class="control-buttons">
        {#if agentStatus === "ACTIVE"}
          <button class="btn-secondary" on:click={stopAgent}>Stop</button>
        {:else}
          <button class="btn-primary" on:click={startAgent}>Start</button>
        {/if}
      </div>
    </div>
  </div>

  <div class="grid-3">
    <div class="card">
      <div class="card-header">Band</div>
      <div class="card-value">{agentScore.band}</div>
    </div>
    <div class="card">
      <div class="card-header">Submissions</div>
      <div class="card-value">{agentScore.submissions}</div>
    </div>
    <div class="card">
      <div class="card-header">Approval Rate</div>
      <div class="card-value">{agentScore.approval_rate}</div>
    </div>
  </div>

  <div class="card">
    <div class="card-header">Current Task</div>
    <div class="task-display">{currentTask || "Idle"}</div>
  </div>

  <div class="section-title">Draft Queue</div>
  {#each draftQueue as draft}
    <div class="card">
      <div class="card-header">{draft.title}</div>
      <p class="draft-preview">{draft.preview}</p>
    </div>
  {:else}
    <p class="empty-text">No pending drafts</p>
  {/each}

  <div class="section-title">Submission History</div>
  <table>
    <thead>
      <tr>
        <th>Title</th>
        <th>Result</th>
        <th>NGT Earned</th>
      </tr>
    </thead>
    <tbody>
      {#each submissionHistory as entry}
        <tr>
          <td>{entry.title}</td>
          <td><span class="tag">{entry.result}</span></td>
          <td>{entry.ngt_earned} NGT</td>
        </tr>
      {:else}
        <tr>
          <td colspan="3" class="empty-row">No submissions yet</td>
        </tr>
      {/each}
    </tbody>
  </table>

  <div class="section-title">Configuration</div>
  <div class="card">
    <div class="form-group">
      <label>Topic Preferences</label>
      <input type="text" bind:value={topicPreferences} placeholder="e.g. AI, cryptography, distributed systems" />
    </div>
    <div class="form-group">
      <label>Research Sources</label>
      <div class="source-toggle">
        <button class="filter-btn" class:active={researchSources === "clearnet"} on:click={() => (researchSources = "clearnet")}>Clearnet</button>
        <button class="filter-btn" class:active={researchSources === "tor"} on:click={() => (researchSources = "tor")}>Tor</button>
        <button class="filter-btn" class:active={researchSources === "both"} on:click={() => (researchSources = "both")}>Both</button>
      </div>
    </div>
    <div class="form-group">
      <label>Tick Interval (seconds): {tickInterval}</label>
      <input type="range" min="10" max="600" bind:value={tickInterval} />
    </div>
    <div class="form-group">
      <label>Budget Limit (NGT per epoch)</label>
      <input type="text" bind:value={budgetLimit} />
    </div>
    <button class="btn-primary" on:click={saveConfig}>Save Config</button>
  </div>

  <div class="section-title">Observatory</div>
  <table>
    <thead>
      <tr>
        <th>Agent</th>
        <th>Task</th>
        <th>Status</th>
      </tr>
    </thead>
    <tbody>
      {#each observatory as agent}
        <tr>
          <td><code>{agent.agent}</code></td>
          <td>{agent.task}</td>
          <td><span class="tag">{agent.status}</span></td>
        </tr>
      {:else}
        <tr>
          <td colspan="3" class="empty-row">No agents visible</td>
        </tr>
      {/each}
    </tbody>
  </table>
</div>

<style>
  .status-label {
    font-weight: 700;
    font-size: 18px;
  }

  .status-label.active {
    color: var(--status-green);
  }

  .status-label.cooldown {
    color: var(--status-yellow);
  }

  .status-label.quarantine {
    color: var(--status-red);
  }

  .control-buttons {
    margin-top: 6px;
  }

  .task-display {
    font-size: 12px;
    color: var(--text-primary);
    margin-top: 6px;
    line-height: 1.6;
  }

  .draft-preview {
    font-size: 12px;
    color: var(--text-secondary);
    margin-top: 4px;
    line-height: 1.55;
  }

  .source-toggle {
    display: flex;
    gap: 4px;
    margin-top: 4px;
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

  input[type="range"] {
    width: 100%;
    padding: 0;
    border: none;
    background: none;
    accent-color: var(--accent);
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
