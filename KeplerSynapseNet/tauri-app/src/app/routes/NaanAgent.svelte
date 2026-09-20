<script lang="ts">
  // NAAN harvests drafts over Tor. It does not mint NGT. Pay is PoE finalize.
  import { onMount, onDestroy, afterUpdate } from "svelte";
  import { naanControl, naanConfigUpdate, rpcCall } from "../../lib/rpc";
  import NaanStation from "../components/sprites/NaanStation.svelte";
  import SynapseHoloLogo from "../components/sprites/SynapseHoloLogo.svelte";
  import NaanAgentHud from "../components/naan/NaanAgentHud.svelte";
  import NaanCrewRoster from "../components/naan/NaanCrewRoster.svelte";
  import NaanHoldStash from "../components/naan/NaanHoldStash.svelte";
  import {
    naanRooms,
    naanCrew,
    ROOM_META,
    MAX_CREW,
    togglePrimaryRoom,
    addCrewMember,
    removeCrewMember,
    toggleCrewRoom,
    setCrewSkin,
    setCrewPrimaryModel,
    setCrewOwnModel,
    loadNaanDeckFromSettings,
  } from "../../lib/naanCrew";
  import { stationCatalog, findAgent, agentPortraitSrc, stationLook } from "../../lib/stationSkins";
  import { harvestLevel } from "../../lib/naanXp";
  import { PRIMARY_NAAN_ID } from "../../lib/synapseHolo";

  let agentStatus = "OFF";
  let agentScore = { band: "-", submissions: 0, approval_rate: "0%" };
  let approved = 0;
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
  let focusedId = PRIMARY_NAAN_ID;
  let nowLine = "";
  type EngineAgent = {
    id: string;
    state: string;
    error: string;
    current_task: string;
    task_id: string;
    model_mode: string;
    model_name: string;
    model_path: string;
    model_ready: boolean;
    inference_state: string;
    submissions: number;
    approved: number;
    log: { ts: number; msg: string }[];
    history: { title: string; result: string; ngt_earned: string }[];
  };
  let engineAgents: Record<string, EngineAgent> = {};

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
    await loadNaanDeckFromSettings();
    await loadAgentState();
    pollHandle = setInterval(loadAgentState, 3000);
  });

  onDestroy(() => { if (pollHandle) clearInterval(pollHandle); });

  afterUpdate(() => {
    if (!logBox || focusLog.length === prevLogLen) return;
    prevLogLen = focusLog.length;
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
      approved = Number(parsed.approved) || 0;
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
      const nextAgents: Record<string, EngineAgent> = {};
      if (Array.isArray(parsed.agents)) {
        for (const row of parsed.agents) {
          if (!row || typeof row !== "object") continue;
          const id = String(row.id || "");
          if (!id) continue;
          nextAgents[id] = {
            id,
            state: String(row.state || "off").toUpperCase(),
            error: String(row.error || ""),
            current_task: String(row.current_task || ""),
            task_id: String(row.task_id || ""),
            model_mode: String(row.model_mode || "primary"),
            model_name: String(row.model_name || ""),
            model_path: String(row.model_path || ""),
            model_ready: row.model_ready === true,
            inference_state: String(row.inference_state || "idle"),
            submissions: Number(row.submissions) || 0,
            approved: Number(row.approved) || 0,
            log: Array.isArray(row.log)
              ? row.log.map((e: any) => ({ ts: Number(e?.ts) || 0, msg: e?.text || e?.msg || "" }))
              : [],
            history: Array.isArray(row.history)
              ? row.history.map((h: any) => ({
                  title: h?.title || "",
                  result: h?.status || h?.result || "",
                  ngt_earned: fmtNgt(h?.ngt ?? h?.ngt_earned),
                }))
              : [],
          };
        }
      }
      if (!nextAgents[PRIMARY_NAAN_ID]) {
        nextAgents[PRIMARY_NAAN_ID] = {
          id: PRIMARY_NAAN_ID,
          state: agentStatus,
          error: "",
          current_task: currentTask,
          task_id: "",
          model_mode: "primary",
          model_name: modelName,
          model_path: "",
          model_ready: inferenceReady,
          inference_state: inferenceReady ? "ready" : "idle",
          submissions: agentScore.submissions,
          approved,
          log: agentLog,
          history: submissionHistory,
        };
      }
      engineAgents = nextAgents;
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

  async function startAgent(id: string = PRIMARY_NAAN_ID) {
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
      const raw = await naanControl("start", id);
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

  async function stopAgent(id: string = PRIMARY_NAAN_ID) {
    if (busy) return;
    actionError = "";
    configMsg = "";
    busy = true;
    try {
      const raw = await naanControl("stop", id);
      const parsed = readRpcJson(raw);
      if (parsed.error) {
        actionError = String(parsed.error).toUpperCase();
        return;
      }
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
  $: focusAgent = engineAgents[focusedId];
  $: focusState = (focusAgent?.state || (focusedId === PRIMARY_NAAN_ID ? agentStatus : "OFF")).toUpperCase();
  $: statusWarn = focusState === "COOLDOWN" || focusState === "BUDGET_EXHAUSTED";
  $: stopping = focusState === "STOPPING";
  $: focusLog = focusAgent?.log?.length ? focusAgent.log : (focusedId === PRIMARY_NAAN_ID ? agentLog : []);
  $: lastLog = focusLog.length ? focusLog[focusLog.length - 1].msg : "";
  $: xp = harvestLevel({
    submissions: focusAgent?.submissions ?? (focusedId === PRIMARY_NAAN_ID ? agentScore.submissions : 0),
    ngt: focusedId === PRIMARY_NAAN_ID ? totalNgt : 0,
  });
  $: harvestFocus = focusedId === PRIMARY_NAAN_ID;
  $: focusSkin = harvestFocus
    ? $stationLook.agent
    : ($naanCrew.find((c) => c.id === focusedId)?.skin || $stationLook.agent);
  $: agentActiveMap = Object.fromEntries(
    Object.values(engineAgents).map((a) => [a.id, a.state === "ACTIVE"]),
  );
  $: crewSubmissions = {
    [PRIMARY_NAAN_ID]: engineAgents[PRIMARY_NAAN_ID]?.submissions ?? agentScore.submissions,
    primary: engineAgents[PRIMARY_NAAN_ID]?.submissions ?? agentScore.submissions,
    ...Object.fromEntries($naanCrew.map((c) => [c.id, engineAgents[c.id]?.submissions ?? 0])),
  };
  $: roster = [
    {
      id: PRIMARY_NAAN_ID,
      name: findAgent($stationLook.agent).label,
      portrait: agentPortraitSrc($stationLook.agent),
      level: harvestLevel({ submissions: engineAgents[PRIMARY_NAAN_ID]?.submissions ?? agentScore.submissions, ngt: totalNgt }).level,
      working: (engineAgents[PRIMARY_NAAN_ID]?.state || agentStatus) === "ACTIVE",
      harvest: true,
    },
    ...$naanCrew.map((c) => {
      const ea = engineAgents[c.id];
      const sub = ea?.submissions ?? 0;
      return {
        id: c.id,
        name: findAgent(c.skin).label,
        portrait: agentPortraitSrc(c.skin),
        level: harvestLevel({ submissions: sub, ngt: 0 }).level,
        working: ea?.state === "ACTIVE",
        harvest: true,
      };
    }),
  ];
  let hudKind: "down" | "working" | "on" = "on";
  $: hudKind = pollError || focusState === "QUARANTINE"
    ? "down"
    : (focusState === "ACTIVE" || stopping)
      ? "working"
      : "on";
  $: hudStatus = pollError || focusState === "QUARANTINE"
    ? "OFFLINE"
    : focusState === "WAITING_FOR_MODEL" || focusAgent?.inference_state === "waiting_for_model"
      ? "WAITING FOR MODEL"
      : focusState === "LOADING" || focusAgent?.inference_state === "loading"
        ? "LOADING MODEL"
        : (focusState === "ACTIVE" || stopping)
          ? "WORKING"
          : focusState === "ERROR"
            ? "ERROR"
            : focusState === "OFF"
              ? "IDLE"
              : focusState;
  $: doneRows = (focusAgent?.history || (harvestFocus ? submissionHistory : [])).slice(-4).reverse();
  $: logTail = focusLog.slice(-4).map((e) => ({ ts: fmtTime(e.ts), msg: e.msg }));
  $: if (focusedId !== PRIMARY_NAAN_ID && !$naanCrew.some((c) => c.id === focusedId)) {
    focusedId = PRIMARY_NAAN_ID;
  }

  function onCrewSkin(id: string, ev: Event) {
    const el = ev.currentTarget;
    if (el instanceof HTMLSelectElement) void setCrewSkin(id, el.value);
  }

  async function usePrimaryModel(id: string) {
    actionError = "";
    const err = await setCrewPrimaryModel(id);
    if (err) actionError = err.toUpperCase();
  }

  async function selectOwnModel(id: string) {
    actionError = "";
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({ filters: [{ name: "GGUF", extensions: ["gguf"] }], multiple: false });
      if (!selected) return;
      const path = typeof selected === "string" ? selected : (selected as { path?: string }).path || "";
      const err = await setCrewOwnModel(id, path);
      if (err) actionError = err.toUpperCase();
    } catch (e) {
      actionError = fmtErr(e, "SELECT MODEL FAILED");
    }
  }
</script>

<div class="content-area">
  {#if pollError}
    <div class="error-msg">{pollError}</div>
  {/if}
  {#if actionError}
    <div class="error-msg">{actionError}</div>
  {/if}

  <div class="page-col">
  <SynapseHoloLogo />
  <NaanStation
    status={agentStatus}
    agentActive={agentActiveMap}
    task={focusAgent?.current_task || currentTask}
    lastLog={lastLog}
    submissions={focusAgent?.submissions ?? agentScore.submissions}
    crewSubmissions={crewSubmissions}
    ngt={totalNgt}
    bind:focusedId
    bind:nowLine
  />
  <NaanHoldStash
    name={findAgent(focusSkin).label}
    submissions={focusAgent?.submissions ?? (harvestFocus ? agentScore.submissions : 0)}
    level={xp.level}
  />

  <div class="hud-row">
    <NaanCrewRoster rows={roster} bind:focusedId />
    <div class="hud-col">
      <NaanAgentHud
        name={findAgent(focusSkin).label}
        portrait={agentPortraitSrc(focusSkin)}
        harvest={true}
        statusKind={hudKind}
        statusText={hudStatus}
        model={focusAgent?.inference_state === "waiting_for_model"
          ? "WAITING FOR MODEL"
          : (focusAgent?.model_name || modelName)}
        runs={focusAgent?.submissions ?? (harvestFocus ? agentScore.submissions : 0)}
        level={xp.level}
        kudos={focusAgent?.approved ?? (harvestFocus ? approved : 0)}
        {nowLine}
        purpose={topicPreferences}
        done={doneRows}
        {logTail}
      />
    </div>
  </div>

  <div class="main-grid">
    <div class="card">
      <div class="card-header">STATUS</div>
      <div
        class="card-value status-lbl"
        class:active={focusState === "ACTIVE"}
        class:cooldown={statusWarn}
        class:quarantine={focusState === "QUARANTINE"}
      >
        {focusState}
      </div>
    </div>
    <div class="card">
      <div class="card-header">CONTROL</div>
      <div class="ctrl-actions">
        {#if focusedId !== PRIMARY_NAAN_ID}
          <button class="btn-secondary" type="button" disabled={busy} on:click={() => selectOwnModel(focusedId)}>
            SELECT MODEL
          </button>
          <button class="btn-secondary" type="button" disabled={busy} on:click={() => usePrimaryModel(focusedId)}>
            USE PRIMARY MODEL
          </button>
        {/if}
        {#if focusState === "ACTIVE" || stopping}
          <button class="btn-secondary" type="button" disabled={busy || stopping} on:click={() => stopAgent(focusedId)}>
            {#if stopping || busy}
              <span class="ks-busy"><span class="ks-spinner"></span> STOPPING</span>
            {:else}
              STOP AGENT
            {/if}
          </button>
        {:else}
          <button class="btn-primary" type="button" disabled={busy || !topicPreferences.trim()} on:click={() => startAgent(focusedId)}>
            {#if busy}
              <span class="ks-busy"><span class="ks-spinner"></span> START</span>
            {:else}
              START AGENT
            {/if}
          </button>
        {/if}
        {#if focusAgent?.error}
          <p class="warn-text">{focusAgent.error.toUpperCase()}</p>
        {/if}
        {#if focusAgent?.inference_state === "waiting_for_model"}
          <p class="warn-text">Waiting for Model. One llama.cpp worker; harvest fetch still runs in parallel.</p>
        {:else if focusedId !== PRIMARY_NAAN_ID && ($naanCrew.find((c) => c.id === focusedId)?.modelMode === "own")}
          <p class="ok-line">Own GGUF {$naanCrew.find((c) => c.id === focusedId)?.modelPath.split("/").pop() || ""}. Inference is queued, not parallel.</p>
        {:else if inferenceReady}
          <p class="ok-line">LLM {modelName || "GGUF"} is in RAM. Extra agents share it (queued inference, parallel fetch).</p>
        {:else if modelLoaded}
          <p class="warn-text">GGUF {modelName || "on disk"} is registered. START AGENT loads it for hard captchas and chat.</p>
        {:else}
          <p class="warn-text">No GGUF yet. Harvest still runs. Load a model in SET for hard captchas and IDE chat.</p>
        {/if}
      </div>
    </div>
  </div>

  <p class="honest-note">
    NAAN harvests over Tor and files drafts. It does not credit the stealth wallet.
    NGT mints only after M-of-N votes and PoE finalize.
  </p>

  <div class="card">
    <div class="card-header">COMMAND BLOCKS</div>
    <div class="blk-row">
      {#each ROOM_META as r}
        <button
          class="blk"
          class:on={$naanRooms.includes(r.id)}
          type="button"
          disabled={$naanRooms.includes(r.id) && $naanRooms.length <= 1}
          on:click={() => togglePrimaryRoom(r.id)}
        >
          {r.name}
        </button>
      {/each}
    </div>
    <p class="hint">Square toggles add or remove rooms and their props. At least one block stays so the walker can stand. Layout only — harvest still needs Start Agent.</p>
  </div>

  <div class="card">
    <div class="card-header">CREW</div>
    <div class="crew-actions">
      <button
        class="btn-secondary"
        type="button"
        disabled={$naanCrew.length >= MAX_CREW}
        on:click={() => addCrewMember()}
      >[ + AGENT ]</button>
    </div>
    {#each $naanCrew as c (c.id)}
      <div class="crew-block">
        <div class="crew-top">
          <span class="crew-name">{findAgent(c.skin).label}</span>
          <select
            class="crew-skin"
            value={c.skin}
            on:change={(e) => onCrewSkin(c.id, e)}
          >
            {#each stationCatalog.agents as a}
              <option value={a.id}>{a.label}</option>
            {/each}
          </select>
          <button class="btn-secondary" type="button" on:click={() => {
            if (focusedId === c.id) focusedId = PRIMARY_NAAN_ID;
            void removeCrewMember(c.id);
          }}>[ REMOVE ]</button>
        </div>
        <p class="hint">
          {c.modelMode === "own" && c.modelPath
            ? "Model: " + (c.modelPath.split("/").pop() || c.modelPath)
            : "Model: primary (shared weights, queued inference)"}
          {#if engineAgents[c.id]?.task_id}
            · task {engineAgents[c.id].task_id}
          {/if}
          {#if engineAgents[c.id]?.state}
            · {engineAgents[c.id].state}
          {/if}
        </p>
        <p class="hint">Crew rooms are station layout only. They do not split the harvest job.</p>
        <div class="blk-row">
          {#each ROOM_META as r}
            <button
              class="blk"
              class:on={c.rooms.includes(r.id)}
              type="button"
              disabled={c.rooms.includes(r.id) && c.rooms.length <= 1}
              on:click={() => toggleCrewRoom(c.id, r.id)}
            >
              {r.name}
            </button>
          {/each}
        </div>
        <div class="crew-actions">
          <button class="btn-secondary" type="button" disabled={busy} on:click={() => selectOwnModel(c.id)}>SELECT MODEL</button>
          <button class="btn-secondary" type="button" disabled={busy} on:click={() => usePrimaryModel(c.id)}>USE PRIMARY MODEL</button>
          {#if engineAgents[c.id]?.state === "ACTIVE" || engineAgents[c.id]?.state === "STOPPING"}
            <button class="btn-secondary" type="button" disabled={busy || engineAgents[c.id]?.state === "STOPPING"} on:click={() => { focusedId = c.id; void stopAgent(c.id); }}>STOP AGENT</button>
          {:else}
            <button class="btn-primary" type="button" disabled={busy || !topicPreferences.trim()} on:click={() => { focusedId = c.id; void startAgent(c.id); }}>START AGENT</button>
          {/if}
        </div>
      </div>
    {:else}
      <p class="hint">No extra NAAN on this station. + AGENT adds a body, then Use Primary Model or Select Model, then Start Agent. Extra agents do not autostart.</p>
    {/each}
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
    {#each focusLog as entry}
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
</div>

<style>
  .content-area {
    font-family: var(--font-mono);
    font-size: 12px;
    line-height: 1.5;
    color: var(--text-primary);
    background: transparent;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    height: 100%;
    overflow-y: auto;
    padding: 18px 20px 84px;
    box-sizing: border-box;
    --ph: var(--cy, #00e5ff);
    --ph-bright: #b8fbff;
    --ph-dim: #148a99;
    --ph-faint: #041418;
    --ph-glow: rgba(0, 229, 255, 0.5);
    --ph-glow2: rgba(0, 229, 255, 0.14);
    --gold: #ffd34a;
  }

  .page-col {
    max-width: 980px;
    margin-inline: auto;
    width: 100%;
  }

  .hud-row {
    display: grid;
    grid-template-columns: minmax(180px, 240px) minmax(0, 1fr);
    gap: 16px;
    margin: 0 0 14px;
    align-items: start;
  }

  .hud-col {
    min-width: 0;
  }

  @media (max-width: 720px) {
    .hud-row {
      grid-template-columns: 1fr;
    }
  }

  .section-title {
    font-family: var(--font);
    font-size: 9px;
    font-weight: 400;
    letter-spacing: 0;
    color: var(--text-secondary);
    margin-bottom: 10px;
    margin-top: 20px;
  }

  .card {
    background: none;
    border: none;
    border-top: 1px solid var(--border);
    border-radius: 0;
    padding: 16px 2px;
    margin-bottom: 10px;
  }

  .card:hover {
    border-color: rgba(255, 255, 255, 0.18);
  }

  .card-header {
    font-family: var(--font);
    font-size: 9px;
    font-weight: 400;
    letter-spacing: 0;
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

  .content-area :global(button) {
    font-family: var(--font);
    font-size: 10px;
    border-radius: 0;
    -webkit-font-smoothing: antialiased;
    transition: background var(--dur) var(--ease), border-color var(--dur) var(--ease), color var(--dur) var(--ease);
  }

  .content-area :global(input),
  .content-area :global(textarea),
  .content-area :global(select) {
    font-family: var(--font-mono);
    font-size: 12px;
    border-radius: 0;
    background: rgba(0, 0, 0, 0.45);
    border: 1px solid var(--border);
    color: var(--text-primary);
    padding: 8px 12px;
  }

  .content-area :global(input:focus),
  .content-area :global(textarea:focus),
  .content-area :global(select:focus) {
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

  /* Green terminal log on black. */
  .chat-msg {
    color: var(--ok);
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
    font-family: var(--font-mono);
    font-size: 12px;
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

  .blk-row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-top: 8px;
  }

  .blk {
    font-family: var(--font);
    font-size: 8px;
    letter-spacing: 0;
    color: var(--text-secondary);
    background: none;
    border: 1px solid var(--border);
    border-radius: 0;
    min-width: 72px;
    min-height: 36px;
    padding: 10px 8px;
    line-height: 1.3;
  }

  .blk.on {
    color: var(--text-primary);
    border-color: var(--cy, #00e5ff);
  }

  .blk:disabled {
    opacity: 0.45;
    cursor: default;
  }

  .crew-actions {
    margin-top: 6px;
  }

  .crew-block {
    margin-top: 14px;
    padding-top: 12px;
    border-top: 1px solid var(--border);
  }

  .crew-top {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    align-items: center;
  }

  .crew-name {
    font-family: var(--font);
    font-size: 9px;
    color: var(--text-primary);
    min-width: 0;
  }

  .crew-skin {
    font-family: var(--font-mono);
    font-size: 12px;
    border-radius: 0;
    background: rgba(0, 0, 0, 0.45);
    border: 1px solid var(--border);
    color: var(--text-primary);
    padding: 8px 12px;
    min-width: 140px;
  }
</style>
