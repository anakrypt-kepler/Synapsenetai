<script lang="ts">
  // NAAN harvests drafts over Tor. It does not mint NGT. Pay is PoE finalize.
  import { onMount, onDestroy, afterUpdate } from "svelte";
  import { naanControl, naanConfigUpdate, rpcCall } from "../../lib/rpc";

  let agentStatus = "OFF";
  let agentScore = { band: "-", submissions: 0, approval_rate: "0%" };
  let currentTask = "";
  let submissionHistory: { title: string; result: string; ngt_earned: string }[] = [];
  // Only filled when naan.status actually returns observatory rows. Never invent agents.
  let observatory: { agent: string; task: string; status: string }[] = [];
  let agentLog: { ts: number; msg: string }[] = [];
  let lastBypass: { cve: string; protection: string; method: string; transport: string; ttfb_ms: number; http: number; bytes: number; ts: number } | null = null;
  let bypassCounters: Record<string, number> = {};
  let totalNgt = 0;
  let modelName = "";
  let modelLoaded = false;
  let inferenceReady = false;

  let topicPreferences = "";
  let researchSources = "tor";
  let tickInterval = 60;
  let budgetLimit = "100";
  let configDirty = false;

  let actionError = "";
  let pollError = "";
  let configMsg = "";
  let busy = false;

  let pollHandle: ReturnType<typeof setInterval> | null = null;
  let logBox: HTMLDivElement | null = null;
  let prevLogLen = 0;

  onMount(async () => {
    await loadAgentState();
    pollHandle = setInterval(loadAgentState, 3000);
  });

  onDestroy(() => { if (pollHandle) clearInterval(pollHandle); });

  afterUpdate(() => {
    if (!logBox || agentLog.length === prevLogLen) return;
    prevLogLen = agentLog.length;
    logBox.scrollTop = logBox.scrollHeight;
  });

  function fmtErr(e: unknown, fallback: string): string {
    const s = e instanceof Error ? e.message : String(e || "");
    const t = s.trim();
    return t ? t.toUpperCase() : fallback;
  }

  function readRpcJson(raw: string): Record<string, any> {
    const parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
      throw new Error("BAD RPC PAYLOAD");
    }
    return parsed;
  }

  function normalizeSources(_s: unknown): "tor" {
    return "tor";
  }

  function fmtNgt(v: unknown): string {
    const n = typeof v === "number" ? v : Number(v);
    return Number.isFinite(n) ? n.toFixed(2) : "0.00";
  }

  function markDirty() {
    configDirty = true;
    configMsg = "";
  }

  function applyConfigFromStatus(cfg: Record<string, any>) {
    if (typeof cfg.topics === "string") topicPreferences = cfg.topics;
    researchSources = normalizeSources(cfg.sources);
    const tick = Number(cfg.tick_interval);
    if (Number.isFinite(tick) && tick >= 10) tickInterval = tick;
    if (cfg.budget_limit != null && cfg.budget_limit !== "") {
      budgetLimit = String(cfg.budget_limit);
    }
  }

  async function loadAgentState() {
    try {
      const result = await rpcCall("naan.status", "{}");
      const parsed = readRpcJson(result);
      if (parsed.error) {
        pollError = String(parsed.error).toUpperCase();
        return;
      }
      pollError = "";
      agentStatus = String(parsed.state || "off").toUpperCase();
      const rate = Number(parsed.approval_rate);
      agentScore = {
        band: rate > 80 ? "A" : rate > 50 ? "B" : rate > 0 ? "C" : "-",
        submissions: Number(parsed.submissions) || 0,
        approval_rate: Number.isFinite(rate) ? Math.round(rate) + "%" : "0%",
      };
      currentTask = parsed.current_task || "";
      totalNgt = Number(parsed.total_ngt) || 0;
      modelLoaded = parsed.model_loaded === true;
      inferenceReady = parsed.inference === true;
      modelName = typeof parsed.model_name === "string" ? parsed.model_name : "";
      lastBypass = parsed.last_bypass && typeof parsed.last_bypass === "object"
        ? parsed.last_bypass
        : null;
      bypassCounters = parsed.bypass_counters && typeof parsed.bypass_counters === "object"
        ? parsed.bypass_counters
        : {};
      submissionHistory = Array.isArray(parsed.history)
        ? parsed.history.map((h: any) => ({
            title: h?.title || "",
            result: h?.status || h?.result || "",
            ngt_earned: fmtNgt(h?.ngt ?? h?.ngt_earned),
          }))
        : [];
      // Engine naanStatus() does not emit observatory. Do not fake a mesh of agents.
      observatory = Array.isArray(parsed.observatory)
        ? parsed.observatory
            .map((row: any) => ({
              agent: String(row?.agent || row?.agentId || ""),
              task: String(row?.task || row?.current_task || ""),
              status: String(row?.status || row?.state || ""),
            }))
            .filter((row: { agent: string; task: string; status: string }) =>
              row.agent || row.task || row.status)
        : [];
      agentLog = Array.isArray(parsed.log)
        ? parsed.log.map((e: any) => ({ ts: Number(e?.ts) || 0, msg: e?.text || e?.msg || "" }))
        : [];
      if (!configDirty && parsed.config && typeof parsed.config === "object") {
        applyConfigFromStatus(parsed.config);
      }
    } catch (e) {
      pollError = fmtErr(e, "NAAN.STATUS FAILED");
    }
  }

  async function persistConfig(): Promise<boolean> {
    const sources = normalizeSources(researchSources);
    const tick = Number(tickInterval);
    const raw = await naanConfigUpdate(JSON.stringify({
      topics: topicPreferences,
      sources,
      tick_interval: Number.isFinite(tick) ? Math.min(600, Math.max(10, tick)) : 60,
      budget_limit: String(budgetLimit),
    }));
    const parsed = readRpcJson(raw);
    if (parsed.error) {
      throw new Error(String(parsed.error));
    }
    configDirty = false;
    return true;
  }

  async function startAgent() {
    if (busy) return;
    actionError = "";
    configMsg = "";
    if (!topicPreferences.trim()) {
      actionError = "TOPICS REQUIRED BEFORE START";
      return;
    }
    busy = true;
    try {
      await persistConfig();
      const raw = await naanControl("start");
      const parsed = readRpcJson(raw);
      if (parsed.error) {
        actionError = String(parsed.error).toUpperCase();
        return;
      }
      await loadAgentState();
    } catch (e) {
      actionError = fmtErr(e, "START FAILED");
    } finally {
      busy = false;
    }
  }

  async function stopAgent() {
    if (busy) return;
    actionError = "";
    configMsg = "";
    busy = true;
    agentStatus = "STOPPING";
    try {
      const raw = await naanControl("stop");
      const parsed = readRpcJson(raw);
      if (parsed.error) {
        actionError = String(parsed.error).toUpperCase();
        return;
      }
      agentStatus = String(parsed.state || "stopping").toUpperCase();
      await loadAgentState();
    } catch (e) {
      actionError = fmtErr(e, "STOP FAILED");
    } finally {
      busy = false;
    }
  }

  async function saveConfig() {
    if (busy) return;
    actionError = "";
    configMsg = "";
    busy = true;
    try {
      await persistConfig();
      configMsg = "SAVED";
      await loadAgentState();
    } catch (e) {
      actionError = fmtErr(e, "CONFIG SAVE FAILED");
    } finally {
      busy = false;
    }
  }

  function fmtTime(ts: number): string {
    const d = new Date(ts);
    return `${d.getHours().toString().padStart(2, "0")}:${d.getMinutes().toString().padStart(2, "0")}:${d.getSeconds().toString().padStart(2, "0")}`;
  }

  $: bypassList = Object.entries(bypassCounters).sort((a, b) => b[1] - a[1]);
  $: statusWarn = agentStatus === "COOLDOWN" || agentStatus === "BUDGET_EXHAUSTED";
  $: stopping = agentStatus === "STOPPING";
</script>

<div class="content-area">
  {#if pollError}
    <div class="error-msg">{pollError}</div>
  {/if}
  {#if actionError}
    <div class="error-msg">{actionError}</div>
  {/if}

  <div class="grid-2">
    <div class="card">
      <div class="card-header">STATUS</div>
      <div
        class="card-value status-lbl"
        class:active={agentStatus === "ACTIVE"}
        class:cooldown={statusWarn}
        class:quarantine={agentStatus === "QUARANTINE"}
      >
        {agentStatus}
      </div>
    </div>
    <div class="card">
      <div class="card-header">CONTROL</div>
      <div class="ctrl-actions">
        {#if agentStatus === "ACTIVE" || stopping}
          <button class="btn-secondary" type="button" disabled={busy || stopping} on:click={stopAgent}>
            {#if stopping || busy}
              <span class="ks-busy"><span class="ks-spinner"></span> STOPPING</span>
            {:else}
              STOP
            {/if}
          </button>
        {:else}
          <button class="btn-primary" type="button" disabled={busy} on:click={startAgent}>
            {#if busy}
              <span class="ks-busy"><span class="ks-spinner"></span> START</span>
            {:else}
              START
            {/if}
          </button>
        {/if}
        {#if inferenceReady}
          <p class="ok-line">LLM {modelName || "GGUF"} is in RAM. Harvest, hard captchas, and IDE chat share it.</p>
        {:else if modelLoaded}
          <p class="warn-text">GGUF {modelName || "on disk"} is registered. START loads it for hard captchas and chat.</p>
        {:else}
          <p class="warn-text">No GGUF yet. Harvest still runs. Load a model in SET for hard captchas and IDE chat.</p>
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
      <div class="card-header">EARNED NGT</div>
      <div class="card-value ngt-val">{fmtNgt(totalNgt)} NGT</div>
      <div class="ngt-note">0 until peer finalize</div>
    </div>
  </div>
  <p class="honest-note">
    NAAN harvests over Tor and files drafts. It does not credit the stealth wallet.
    NGT mints only after M-of-N votes and PoE finalize.
  </p>

  <div class="card">
    <div class="card-header">CURRENT TASK</div>
    <div class="task-txt">{currentTask || "IDLE"}</div>
  </div>

  {#if lastBypass && lastBypass.cve}
    <div class="section-title">LAST DEFENSIVE NOTE</div>
    <div class="card bypass-card">
      <div class="bypass-row"><span class="bp-label">CVE</span><span class="bp-val cve-id mono">{lastBypass.cve}</span></div>
      <div class="bypass-row"><span class="bp-label">PROTECTION</span><span class="bp-val">{lastBypass.protection}</span></div>
      <div class="bypass-row"><span class="bp-label">METHOD</span><span class="bp-val">{lastBypass.method}</span></div>
      <div class="bypass-row"><span class="bp-label">TRANSPORT</span><span class="bp-val">{lastBypass.transport}</span></div>
      <div class="bypass-row"><span class="bp-label">TTFB</span><span class="bp-val">{lastBypass.ttfb_ms}ms</span></div>
      <div class="bypass-row"><span class="bp-label">HTTP</span><span class="bp-val">{lastBypass.http}</span></div>
      <div class="bypass-row"><span class="bp-label">BYTES</span><span class="bp-val">{lastBypass.bytes}</span></div>
    </div>
  {/if}

  {#if bypassList.length > 0}
    <div class="section-title">INTEL COUNTERS</div>
    <div class="card counter-grid">
      {#each bypassList as [cve, count]}
        <div class="counter-item">
          <span class="cve-id mono">{cve}</span>
          <span class="counter-val">{count}x</span>
        </div>
      {/each}
    </div>
  {/if}

  <div class="section-title">AGENT LOG</div>
  <div class="chat-box" bind:this={logBox}>
    {#each agentLog as entry}
      <div class="chat-line">
        <span class="chat-ts">[{fmtTime(entry.ts)}]</span>
        <span class="chat-msg">{entry.msg}</span>
      </div>
    {:else}
      <div class="chat-line empty">No agent activity</div>
    {/each}
  </div>

  <div class="section-title">CONFIG</div>
  <div class="card">
    <div class="form-group">
      <label>TOPICS</label>
      <input
        type="text"
        bind:value={topicPreferences}
        placeholder="AI, crypto, systems"
        on:input={markDirty}
      />
    </div>
    <div class="form-group">
      <label>SOURCES</label>
      <div class="src-row">
        <button class="fbtn active" type="button" disabled>TOR</button>
      </div>
      <p class="hint">Harvest is Tor SOCKS only. Non-onion topics go to onion search. No direct-IP path.</p>
    </div>
    <div class="form-group">
      <label>TICK: {tickInterval}s</label>
      <input type="range" min="10" max="600" bind:value={tickInterval} on:input={markDirty} />
    </div>
    <div class="form-group">
      <label>MAX DRAFTS / EPOCH</label>
      <input type="text" bind:value={budgetLimit} on:input={markDirty} />
    </div>
    <button class="btn-primary" type="button" disabled={busy} on:click={saveConfig}>SAVE</button>
    {#if configMsg}<span class="success-msg save-inline">{configMsg}</span>{/if}
  </div>

  {#if observatory.length > 0}
    <div class="section-title">OBSERVATORY</div>
    <div class="table-wrap">
      <table>
        <thead><tr><th>AGENT</th><th>TASK</th><th>STATUS</th></tr></thead>
        <tbody>
          {#each observatory as row}
            <tr>
              <td>{row.agent}</td>
              <td>{row.task}</td>
              <td><span class="tag">{row.status}</span></td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
  {/if}

  <div class="section-title">HISTORY</div>
  <div class="table-wrap">
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
          <tr><td colspan="3" class="empty-row">None</td></tr>
        {/each}
      </tbody>
    </table>
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

  .btn-primary,
  .btn-secondary {
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

  .status-lbl { font-weight: 700; }
  .status-lbl.active { color: var(--ok); }
  .status-lbl.cooldown { color: var(--warn); }
  .status-lbl.quarantine { color: var(--err); }

  .ctrl-actions {
    margin-top: 6px;
  }

  .grid-4 {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 10px;
    margin-bottom: 8px;
  }

  .ngt-val {
    color: var(--ok);
    font-size: 15px;
  }

  .ngt-note {
    font-size: 11px;
    color: var(--text-secondary);
    margin-top: 4px;
    line-height: 1.4;
  }

  .honest-note {
    font-size: 12px;
    color: var(--text-secondary);
    margin: 0 0 14px;
    line-height: 1.5;
  }

  .warn-text {
    font-size: 12px;
    color: var(--warn);
    margin: 8px 0 0;
    line-height: 1.5;
  }

  .ok-line {
    font-size: 12px;
    color: var(--ok);
    margin: 8px 0 0;
    line-height: 1.5;
  }

  .save-inline {
    display: inline-block;
    margin: 0 0 0 10px;
    padding: 6px 10px;
    vertical-align: middle;
  }

  .task-txt {
    font-size: 13px;
    color: var(--text-primary);
    margin-top: 4px;
    line-height: 1.5;
  }

  .bypass-card {
    font-size: 13px;
  }

  .bypass-row {
    display: flex;
    justify-content: space-between;
    gap: 12px;
    padding: 8px 0;
    border-bottom: 1px solid var(--border);
  }

  .bypass-row:last-child {
    border-bottom: none;
    padding-bottom: 0;
  }

  .bp-label {
    color: var(--text-secondary);
    font-weight: 600;
    font-size: 11px;
    letter-spacing: 0.06em;
  }

  .bp-val {
    color: var(--text-primary);
    text-align: right;
  }

  .cve-id {
    color: var(--err);
    font-weight: 600;
  }

  .mono {
    font-family: var(--font-mono);
    font-size: 12px;
  }

  .counter-grid {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
  }

  .counter-item {
    display: flex;
    gap: 8px;
    align-items: center;
    font-size: 12px;
    padding: 6px 10px;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    background: rgba(0, 0, 0, 0.28);
  }

  .counter-val {
    color: var(--ok);
    font-weight: 700;
  }

  .chat-box {
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 12px 14px;
    max-height: 240px;
    overflow-y: auto;
    font-family: var(--font-mono);
    font-size: 12px;
    line-height: 1.55;
    user-select: text;
    pointer-events: auto;
    margin-bottom: 10px;
    image-rendering: auto;
  }

  .chat-line {
    display: flex;
    gap: 8px;
  }

  .chat-line.empty {
    color: var(--text-secondary);
    font-family: var(--font);
    font-size: 13px;
  }

  .chat-ts {
    color: var(--text-faint);
    flex-shrink: 0;
  }

  .chat-msg {
    color: var(--text-primary);
    word-break: break-word;
  }

  .src-row {
    display: flex;
    gap: 6px;
    margin-top: 6px;
  }

  .fbtn {
    font-family: var(--font);
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0.06em;
    padding: 6px 14px;
    border: 1px solid var(--border);
    border-radius: var(--radius-full, 999px);
    color: var(--text-secondary);
    background: rgba(0, 0, 0, 0.28);
    transition: background var(--dur) var(--ease), border-color var(--dur) var(--ease), color var(--dur) var(--ease);
  }

  .fbtn.active {
    color: var(--text-primary);
    background: rgba(255, 255, 255, 0.12);
    border-color: rgba(255, 255, 255, 0.22);
  }

  .fbtn:disabled {
    opacity: 1;
    cursor: default;
  }

  .hint {
    font-size: 12px;
    color: var(--text-secondary);
    margin: 8px 0 0;
    line-height: 1.5;
  }

  input[type="range"] {
    width: 100%;
    padding: 0;
    border: none;
    background: none;
    accent-color: var(--text-primary);
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
