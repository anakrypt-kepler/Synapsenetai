<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { naanControl, naanConfigUpdate, rpcCall } from "../../lib/rpc";

  let agentStatus = "OFF";
  let agentScore = { band: "-", submissions: 0, approval_rate: "0%" };
  let currentTask = "";
  let draftQueue: { title: string; preview: string }[] = [];
  let submissionHistory: { title: string; result: string; ngt_earned: string }[] = [];
  let observatory: { agent: string; task: string; status: string }[] = [];
  let agentLog: { ts: number; msg: string }[] = [];

  let topicPreferences = "";
  let researchSources = "both";
  let tickInterval = 60;
  let budgetLimit = "100";

  let pollHandle: ReturnType<typeof setInterval> | null = null;

  onMount(async () => {
    await loadAgentState();
    pollHandle = setInterval(loadAgentState, 3000);
  });

  onDestroy(() => { if (pollHandle) clearInterval(pollHandle); });

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
      agentLog = parsed.log || agentLog;
      if (parsed.config) {
        topicPreferences = parsed.config.topics || topicPreferences;
        researchSources = parsed.config.sources || researchSources;
        tickInterval = parsed.config.tick_interval || tickInterval;
        budgetLimit = parsed.config.budget_limit || budgetLimit;
      }
    } catch {}
  }

  async function startAgent() {
    try { await naanControl("start"); await loadAgentState(); } catch {}
  }

  async function stopAgent() {
    try { await naanControl("stop"); await loadAgentState(); } catch {}
  }

  async function saveConfig() {
    try {
      await naanConfigUpdate(JSON.stringify({
        topics: topicPreferences, sources: researchSources,
        tick_interval: tickInterval, budget_limit: budgetLimit,
      }));
    } catch {}
  }

  function fmtTime(ts: number): string {
    const d = new Date(ts);
    return `${d.getHours().toString().padStart(2,"0")}:${d.getMinutes().toString().padStart(2,"0")}:${d.getSeconds().toString().padStart(2,"0")}`;
  }
</script>

<div class="content-area">
  <div class="grid-2">
    <div class="card">
      <div class="card-header">STATUS</div>
      <div class="card-value status-lbl" class:active={agentStatus === "ACTIVE"} class:cooldown={agentStatus === "COOLDOWN"} class:quarantine={agentStatus === "QUARANTINE"}>
        {agentStatus}
      </div>
    </div>
    <div class="card">
      <div class="card-header">CONTROL</div>
      <div style="margin-top:6px">
        {#if agentStatus === "ACTIVE"}
          <button class="btn-secondary" on:click={stopAgent}>[ STOP ]</button>
        {:else}
          <button class="btn-primary" on:click={startAgent}>[ START ]</button>
        {/if}
      </div>
    </div>
  </div>

  <div class="grid-3">
    <div class="card">
      <div class="card-header">BAND</div>
      <div class="card-value">{agentScore.band}</div>
    </div>
    <div class="card">
      <div class="card-header">SUBS</div>
      <div class="card-value">{agentScore.submissions}</div>
    </div>
    <div class="card">
      <div class="card-header">RATE</div>
      <div class="card-value">{agentScore.approval_rate}</div>
    </div>
  </div>

  <div class="card">
    <div class="card-header">CURRENT TASK</div>
    <div class="task-txt">{currentTask || "IDLE"}</div>
  </div>

  <div class="section-title">AGENT CHAT (READ-ONLY)</div>
  <div class="chat-box">
    {#each agentLog as entry}
      <div class="chat-line">
        <span class="chat-ts">[{fmtTime(entry.ts)}]</span>
        <span class="chat-msg">{entry.msg}</span>
      </div>
    {:else}
      <div class="chat-line empty">NO AGENT ACTIVITY</div>
    {/each}
  </div>

  <div class="section-title">OBSERVATORY</div>
  <table>
    <thead><tr><th>AGENT</th><th>TASK</th><th>STATUS</th></tr></thead>
    <tbody>
      {#each observatory as agent}
        <tr>
          <td><code>{agent.agent}</code></td>
          <td>{agent.task}</td>
          <td><span class="tag">{agent.status}</span></td>
        </tr>
      {:else}
        <tr><td colspan="3" class="empty-row">NO AGENTS</td></tr>
      {/each}
    </tbody>
  </table>

  <div class="section-title">CONFIG</div>
  <div class="card">
    <div class="form-group">
      <label>TOPICS</label>
      <input type="text" bind:value={topicPreferences} placeholder="AI, crypto, systems" />
    </div>
    <div class="form-group">
      <label>SOURCES</label>
      <div class="src-row">
        <button class="fbtn" class:active={researchSources === "tor"} on:click={() => (researchSources = "tor")}>TOR</button>
        <button class="fbtn" class:active={researchSources === "clearnet"} on:click={() => (researchSources = "clearnet")}>NET</button>
        <button class="fbtn" class:active={researchSources === "both"} on:click={() => (researchSources = "both")}>BOTH</button>
      </div>
    </div>
    <div class="form-group">
      <label>TICK: {tickInterval}s</label>
      <input type="range" min="10" max="600" bind:value={tickInterval} />
    </div>
    <div class="form-group">
      <label>BUDGET (NGT/EPOCH)</label>
      <input type="text" bind:value={budgetLimit} />
    </div>
    <button class="btn-primary" on:click={saveConfig}>[ SAVE ]</button>
  </div>

  <div class="section-title">HISTORY</div>
  <table>
    <thead><tr><th>TITLE</th><th>RESULT</th><th>NGT</th></tr></thead>
    <tbody>
      {#each submissionHistory as entry}
        <tr>
          <td>{entry.title}</td>
          <td><span class="tag">{entry.result}</span></td>
          <td>{entry.ngt_earned}</td>
        </tr>
      {:else}
        <tr><td colspan="3" class="empty-row">NONE</td></tr>
      {/each}
    </tbody>
  </table>
</div>

<style>
  .status-lbl { font-weight: 700; }
  .status-lbl.active { color: var(--ok); }
  .status-lbl.cooldown { color: var(--warn); }
  .status-lbl.quarantine { color: var(--err); }

  .task-txt {
    font-size: 10px;
    color: var(--text-primary);
    margin-top: 4px;
    line-height: 1.6;
  }

  .chat-box {
    border: 1px solid var(--border);
    padding: 8px;
    max-height: 200px;
    overflow-y: auto;
    font-size: 8px;
    line-height: 1.8;
    user-select: text;
    pointer-events: auto;
  }

  .chat-line {
    display: flex;
    gap: 6px;
  }

  .chat-line.empty {
    color: var(--text-secondary);
  }

  .chat-ts {
    color: var(--text-faint);
    flex-shrink: 0;
  }

  .chat-msg {
    color: var(--text-primary);
  }

  .src-row {
    display: flex;
    gap: 2px;
    margin-top: 4px;
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

  input[type="range"] {
    width: 100%;
    padding: 0;
    border: none;
    background: none;
    accent-color: var(--text-primary);
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 16px;
  }
</style>
