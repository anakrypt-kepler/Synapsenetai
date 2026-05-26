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
  let lastBypass: { cve: string; protection: string; method: string; transport: string; ttfb_ms: number; http: number; bytes: number; ts: number } | null = null;
  let bypassCounters: Record<string, number> = {};
  let totalNgt = 0;

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
      agentStatus = (parsed.state || "off").toUpperCase();
      agentScore = {
        band: parsed.approval_rate > 80 ? "A" : parsed.approval_rate > 50 ? "B" : parsed.approval_rate > 0 ? "C" : "-",
        submissions: parsed.submissions || 0,
        approval_rate: (parsed.approval_rate != null ? Math.round(parsed.approval_rate) + "%" : "0%"),
      };
      currentTask = parsed.current_task || "";
      totalNgt = parsed.total_ngt || 0;
      lastBypass = parsed.last_bypass || null;
      bypassCounters = parsed.bypass_counters || {};
      submissionHistory = (parsed.history || []).map((h: any) => ({
        title: h.title, result: h.status, ngt_earned: h.ngt?.toFixed(2) || "0",
      }));
      observatory = parsed.observatory || [];
      agentLog = (parsed.log || []).map((e: any) => ({ ts: e.ts, msg: e.text || e.msg || "" }));
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

  $: bypassList = Object.entries(bypassCounters).sort((a, b) => b[1] - a[1]);
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

  <div class="grid-4">
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
    <div class="card">
      <div class="card-header">EARNED</div>
      <div class="card-value ngt-val">{totalNgt.toFixed(2)} NGT</div>
    </div>
  </div>

  <div class="card">
    <div class="card-header">CURRENT TASK</div>
    <div class="task-txt">{currentTask || "IDLE"}</div>
  </div>

  {#if lastBypass && lastBypass.cve}
    <div class="section-title">LAST BYPASS</div>
    <div class="card bypass-card">
      <div class="bypass-row"><span class="bp-label">CVE</span><span class="bp-val cve-id">{lastBypass.cve}</span></div>
      <div class="bypass-row"><span class="bp-label">PROTECTION</span><span class="bp-val">{lastBypass.protection}</span></div>
      <div class="bypass-row"><span class="bp-label">METHOD</span><span class="bp-val">{lastBypass.method}</span></div>
      <div class="bypass-row"><span class="bp-label">TRANSPORT</span><span class="bp-val">{lastBypass.transport}</span></div>
      <div class="bypass-row"><span class="bp-label">TTFB</span><span class="bp-val">{lastBypass.ttfb_ms}ms</span></div>
      <div class="bypass-row"><span class="bp-label">HTTP</span><span class="bp-val">{lastBypass.http}</span></div>
      <div class="bypass-row"><span class="bp-label">BYTES</span><span class="bp-val">{lastBypass.bytes}</span></div>
    </div>
  {/if}

  {#if bypassList.length > 0}
    <div class="section-title">BYPASS COUNTERS</div>
    <div class="card counter-grid">
      {#each bypassList as [cve, count]}
        <div class="counter-item">
          <span class="cve-id">{cve}</span>
          <span class="counter-val">{count}x</span>
        </div>
      {/each}
    </div>
  {/if}

  <div class="section-title">AGENT LOG</div>
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

  .grid-4 {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 8px;
    margin-bottom: 12px;
  }

  .ngt-val {
    color: #00c853;
    font-size: 10px;
  }

  .task-txt {
    font-size: 10px;
    color: var(--text-primary);
    margin-top: 4px;
    line-height: 1.6;
  }

  .bypass-card {
    font-size: 9px;
  }

  .bypass-row {
    display: flex;
    justify-content: space-between;
    padding: 2px 0;
    border-bottom: 1px solid var(--border);
  }

  .bypass-row:last-child {
    border-bottom: none;
  }

  .bp-label {
    color: var(--text-secondary);
    font-weight: 600;
  }

  .bp-val {
    color: var(--text-primary);
  }

  .cve-id {
    color: #f44;
    font-weight: 700;
  }

  .counter-grid {
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
  }

  .counter-item {
    display: flex;
    gap: 4px;
    align-items: center;
    font-size: 8px;
    padding: 2px 6px;
    border: 1px solid var(--border);
  }

  .counter-val {
    color: #00c853;
    font-weight: 700;
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
