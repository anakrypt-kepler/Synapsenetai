<script lang="ts">
  import { createEventDispatcher, onMount } from "svelte";
  import {
    saveSetupConfig,
    getSystemInfo,
    walletCreate,
    walletRestore,
    initEngine,
    getStatus,
    parseStatus,
    rpcCall,
    modelLoad,
    type SetupConfig,
    type SystemInfo,
  } from "../../lib/rpc";
  import ModelCatalog from "./ModelCatalog.svelte";
  import SkinPicker from "./SkinPicker.svelte";
  import {
    stationCatalog,
    DEFAULT_STATION_LOOK,
    saveStationLook,
    type StationLook,
  } from "../../lib/stationSkins";

  const dispatch = createEventDispatcher();

  let step = 1;
  let walletMode = "";
  let seedPhrase = "";
  let generatedSeed = "";
  let generatedAddress = "";
  let seedConfirmed = false;
  let walletPassword = "";
  let restoreSeed = "";

  let connectionType = "tor";
  let bridgeLines = "";

  let aiModel = "skip";
  let modelPath = "";

  let systemInfo: SystemInfo = { cpu_cores: 4, ram_total_mb: 8192, gpu_devices: [] };
  let cpuThreads = 2;
  let ramLimitMb = 4096;
  let diskLimitMb = 50000;
  let gpuEnabled = false;
  let gpuDevice = "";
  let gpuLayers = 32;
  let launchAtStartup = false;

  let walletOk = false;
  let connectionOk = false;
  let modelStatus = "SKIPPED";
  let peersFound = 0;
  let readyChecking = true;

  let dataDir = "~/.synapsenet/";
  let walletFile = "~/.synapsenet/wallet.dat";

  let look: StationLook = { ...DEFAULT_STATION_LOOK };

  onMount(async () => {
    try {
      systemInfo = await getSystemInfo();
      cpuThreads = Math.max(1, Math.floor(systemInfo.cpu_cores / 2));
      const quarter = Math.floor(systemInfo.ram_total_mb * 0.25);
      ramLimitMb = Math.min(4096, quarter);
      if (systemInfo.gpu_devices.length > 0) {
        gpuEnabled = true;
        gpuDevice = systemInfo.gpu_devices[0].id;
      }
    } catch {}
  });

  async function handleWalletCreate() {
    walletMode = "create";
    try {
      const result = JSON.parse(await walletCreate());
      generatedSeed = result.seed || "";
      generatedAddress = result.address || "";
      if (!generatedSeed) generatedSeed = "WALLET CREATED BUT SEED MISSING — CHECK ~/.synapsenet/wallet.mnemonic";
    } catch (e) {
      generatedSeed = e instanceof Error ? e.message : "engine not ready";
      generatedAddress = "";
    }
  }

  function handleWalletRestore() {
    walletMode = "restore";
  }

  async function confirmSeed() {
    seedConfirmed = true;
  }

  async function doRestore() {
    try {
      const result = JSON.parse(await walletRestore(restoreSeed));
      generatedAddress = result.address || "";
      walletMode = "restored";
    } catch {
      walletMode = "restored";
      generatedAddress = "pending";
    }
  }

  function nextStep() {
    if (step < 6) step += 1;
    if (step === 6) runReadyChecks();
  }

  function prevStep() {
    if (step > 1) step -= 1;
  }

  function onCatalogReady(ev: CustomEvent<{ path: string; id: string }>) {
    modelPath = ev.detail.path || "";
    if (modelPath) aiModel = "download";
  }

  async function selectLocalFile() {
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({
        filters: [{ name: "GGUF Models", extensions: ["gguf"] }],
        multiple: false,
      });
      if (selected) {
        modelPath = typeof selected === "string" ? selected : selected.path;
        aiModel = "local";
      }
    } catch {}
  }

  async function runReadyChecks() {
    readyChecking = true;
    walletOk = walletMode === "create" || walletMode === "restore" || walletMode === "restored";

    try {
      await initEngine();
      const raw = await getStatus();
      const status = parseStatus(raw);
      connectionOk = status.connection !== "disconnected";
      peersFound = status.peers;
      if (status.model_loaded) {
        modelStatus = `LOADED: ${status.model_name}`;
      } else if (aiModel === "skip") {
        modelStatus = "SKIPPED";
      } else {
        modelStatus = "NOT LOADED";
      }
    } catch {
      connectionOk = false;
      peersFound = 0;
      modelStatus = aiModel === "skip" ? "SKIPPED" : "NOT LOADED";
    }

    readyChecking = false;
  }

  async function finishSetup() {
    const conn = connectionType === "tor_bridges" ? "tor_bridges" : "tor";
    const config: SetupConfig = {
      wallet_mode: walletMode,
      seed_phrase: walletMode === "restore" ? restoreSeed : null,
      password: walletPassword || null,
      connection_type: conn,
      bridge_lines: bridgeLines || null,
      ai_model: aiModel,
      model_path: modelPath || null,
      cpu_threads: cpuThreads,
      ram_limit_mb: ramLimitMb,
      disk_limit_mb: diskLimitMb,
      gpu_enabled: gpuEnabled,
      gpu_device: gpuDevice || null,
      gpu_layers: gpuLayers,
      launch_at_startup: launchAtStartup,
      mine_background: false,
    };

    try {
      await saveSetupConfig(config);
    } catch {}
    try {
      await rpcCall("settings.update", JSON.stringify({
        connection_type: conn,
        bridge_lines: bridgeLines || "",
        model_path: modelPath || "",
        cpu_threads: cpuThreads,
        ram_limit_mb: ramLimitMb,
        disk_limit_mb: diskLimitMb,
        gpu_enabled: gpuEnabled,
        gpu_device: gpuDevice || "",
        gpu_layers: gpuLayers,
        launch_at_login: launchAtStartup,
      }));
    } catch {}
    try {
      await saveStationLook(look);
    } catch {}
    if (modelPath) {
      try { await modelLoad(modelPath); } catch {}
    }

    dispatch("complete");
  }

  $: canProceedStep1 =
    (walletMode === "create" && seedConfirmed) ||
    (walletMode === "restore" && restoreSeed.trim().split(/\s+/).length === 24) ||
    walletMode === "restored";

  $: canProceedStep3 =
    aiModel === "skip" ||
    (aiModel === "local" && !!modelPath) ||
    (aiModel === "download" && !!modelPath);

  $: canFinish = walletOk;
</script>

<div class="wizard-overlay">
  <div class="wizard" class:wide={step === 5}>
    <div class="wizard-header">
      <span class="wizard-title">SYNAPSENET SETUP</span>
      <span class="wizard-step">STEP {step} OF 6</span>
    </div>

    <div class="wizard-progress" aria-hidden="true">
      {#each [1, 2, 3, 4, 5, 6] as s}
        <div class="progress-segment" class:active={s <= step}></div>
      {/each}
    </div>

    <div class="wizard-body">
      {#key step}
        <div class="step-pane">
          {#if step === 1}
            <div class="step-content">
              <h2 class="step-title">Wallet</h2>
              {#if !walletMode}
                <p class="step-desc">Create a new wallet or restore from a 24-word seed phrase.</p>
                <div class="step-desc path-info">Data directory: {dataDir}</div>
                <div class="step-desc path-info">Wallet file: {walletFile}</div>
                <div class="step-actions">
                  <button class="btn-primary" on:click={handleWalletCreate}>Create new</button>
                  <button class="btn-secondary" on:click={handleWalletRestore}>Restore</button>
                </div>
              {:else if walletMode === "create"}
                {#if !seedConfirmed}
                  <p class="step-desc">Your NGT address</p>
                  <div class="mono-box">{generatedAddress}</div>
                  <p class="step-desc warn-text">Save this 24-word seed phrase. It will not be shown again.</p>
                  <div class="seed-box">{generatedSeed}</div>
                  <div class="step-desc path-info">Saved to: {walletFile}</div>
                  <div class="form-group">
                    <label>Password (optional)</label>
                    <input type="password" bind:value={walletPassword} placeholder="Optional" />
                  </div>
                  <button class="btn-primary" on:click={confirmSeed}>I saved my seed</button>
                {:else}
                  <p class="step-desc">Wallet created.</p>
                  <div class="mono-box">{generatedAddress}</div>
                {/if}
              {:else if walletMode === "restore"}
                <p class="step-desc">Enter your 24-word seed phrase</p>
                <textarea class="seed-input" bind:value={restoreSeed} rows="4" placeholder="word1 word2 word3 …"></textarea>
                <button class="btn-primary" on:click={doRestore} disabled={restoreSeed.trim().split(/\s+/).length !== 24}>Restore</button>
              {:else if walletMode === "restored"}
                <p class="step-desc">Wallet restored.</p>
                <div class="mono-box">{generatedAddress}</div>
              {/if}
            </div>

          {:else if step === 2}
            <div class="step-content">
              <h2 class="step-title">Connection</h2>
              <p class="step-desc">SynapseNet is Tor-only. Mesh, wallet, MSG, and harvest all go through Tor. Bridges are for censored networks.</p>
              <div class="option-group">
                <button class="option-btn" class:selected={connectionType === "tor"} on:click={() => (connectionType = "tor")}>
                  <span class="option-name">Tor</span>
                  <span class="option-desc">All traffic through Tor hidden services. Auto-connects.</span>
                </button>
                <button class="option-btn" class:selected={connectionType === "tor_bridges"} on:click={() => (connectionType = "tor_bridges")}>
                  <span class="option-name">Tor + Bridges</span>
                  <span class="option-desc">Tor with obfs4 bridges. For censored networks.</span>
                </button>
              </div>
              {#if connectionType === "tor_bridges"}
                <div class="form-group">
                  <label>Bridge lines</label>
                  <textarea bind:value={bridgeLines} rows="4" placeholder="obfs4 bridge lines, one per line"></textarea>
                </div>
              {/if}
              {#if connectionType === "tor" || connectionType === "tor_bridges"}
                <div class="step-desc ok-text">Tor will be provisioned automatically on first launch.</div>
              {/if}
            </div>

          {:else if step === 3}
            <div class="step-content">
              <h2 class="step-title">AI Model</h2>
              <p class="step-desc">NAAN harvest works without an LLM. Load a GGUF for IDE chat and hard captchas. Pick a ready file — you do not need to hunt HuggingFace.</p>
              <div class="option-group">
                <button class="option-btn" class:selected={aiModel === "download"} on:click={() => (aiModel = "download")}>
                  <span class="option-name">Download</span>
                  <span class="option-desc">One-click Qwen2.5 Instruct GGUF from HuggingFace into ~/.synapsenet/models.</span>
                </button>
                <button class="option-btn" class:selected={aiModel === "local"} on:click={selectLocalFile}>
                  <span class="option-name">Local file</span>
                  <span class="option-desc">Select a .gguf you already have.</span>
                </button>
                <button class="option-btn" class:selected={aiModel === "skip"} on:click={() => { aiModel = "skip"; }}>
                  <span class="option-name">Skip</span>
                  <span class="option-desc">No model now. Harvest still runs. Load later in SET.</span>
                </button>
              </div>
              {#if aiModel === "download"}
                <ModelCatalog on:ready={onCatalogReady} />
              {/if}
              {#if aiModel === "local" && modelPath}
                <div class="mono-box">{modelPath}</div>
              {/if}
              {#if aiModel === "download" && modelPath}
                <div class="mono-box">{modelPath}</div>
              {/if}
            </div>

          {:else if step === 4}
            <div class="step-content">
              <h2 class="step-title">Resources</h2>
              <p class="step-desc">CPU, RAM, disk, and GPU for local AI (IDE chat and captchas).</p>
              <div class="form-group">
                <label>CPU threads: {cpuThreads}/{systemInfo.cpu_cores}</label>
                <input type="range" min="1" max={systemInfo.cpu_cores} bind:value={cpuThreads} />
              </div>
              <div class="form-group">
                <label>RAM: {ramLimitMb} MB / {systemInfo.ram_total_mb} MB</label>
                <input type="range" min="512" max={systemInfo.ram_total_mb} step="256" bind:value={ramLimitMb} />
              </div>
              <div class="form-group">
                <label>Disk limit (MB)</label>
                <input type="number" bind:value={diskLimitMb} min="1000" />
              </div>
              <div class="form-group">
                <label>GPU for AI</label>
                <div class="checkbox-group">
                  <label><input type="checkbox" bind:checked={gpuEnabled} /> Enable GPU</label>
                </div>
              </div>
              {#if gpuEnabled}
                <div class="form-group">
                  <label>Device</label>
                  {#if systemInfo.gpu_devices.length > 0}
                    <select bind:value={gpuDevice}>
                      {#each systemInfo.gpu_devices as dev}
                        <option value={dev.id}>{dev.name}{dev.vram_mb ? ` (${dev.vram_mb} MB)` : ""}</option>
                      {/each}
                    </select>
                  {:else}
                    <p class="step-desc">No GPU detected. Inference stays on CPU.</p>
                  {/if}
                </div>
                <div class="form-group">
                  <label>GPU layers: {gpuLayers}</label>
                  <input type="range" min="0" max="99" bind:value={gpuLayers} />
                </div>
              {/if}
              <div class="checkbox-group">
                <label><input type="checkbox" bind:checked={launchAtStartup} /> Launch at startup (saved; OS autostart not wired yet)</label>
              </div>
            </div>

          {:else if step === 5}
            <div class="step-content">
              <h2 class="step-title">Station</h2>
              <p class="step-desc">Pick the 2D agent and location skins for the NAAN harvest map. You can change this later in SET.</p>
              <SkinPicker title="Agent" kind="agent" items={stationCatalog.agents} bind:value={look.agent} />
              <SkinPicker title="Floor" kind="tile" items={stationCatalog.floors} bind:value={look.floor} />
              <SkinPicker title="Wall" kind="tile" items={stationCatalog.walls} bind:value={look.wall} />
              <SkinPicker title="Hull" kind="tile" items={stationCatalog.shells} bind:value={look.shell} />
            </div>

          {:else if step === 6}
            <div class="step-content">
              <h2 class="step-title">Ready</h2>
              {#if readyChecking}
                <div class="waiting">
                  <span class="ks-spinner" aria-hidden="true"></span>
                  <p class="step-desc">Checking system…</p>
                </div>
              {:else}
                <div class="checklist">
                  <div class="check-item">
                    <span class="check-label">Wallet</span>
                    <span class="check-val" class:ok={walletOk} class:err={!walletOk}>
                      {walletOk ? "OK" : "ERR"}
                    </span>
                  </div>
                  <div class="check-item">
                    <span class="check-label">Connection</span>
                    <span class="check-val" class:ok={connectionOk} class:err={!connectionOk}>
                      {connectionOk ? connectionType.toUpperCase() : "WAITING"}
                    </span>
                  </div>
                  <div class="check-item">
                    <span class="check-label">Model</span>
                    <span class="check-val">{modelStatus}</span>
                  </div>
                  <div class="check-item">
                    <span class="check-label">Peers</span>
                    <span class="check-val">{peersFound}</span>
                  </div>
                </div>
                <div class="step-desc path-info">Data: {dataDir}</div>
                <div class="step-desc path-info">Wallet: {walletFile}</div>
              {/if}
            </div>
          {/if}
        </div>
      {/key}
    </div>

    <div class="wizard-footer">
      {#if step > 1 && step < 6}
        <button class="btn-secondary" on:click={prevStep}>Back</button>
      {:else}
        <div></div>
      {/if}
      {#if step < 6}
        <button class="btn-primary" on:click={nextStep} disabled={(step === 1 && !canProceedStep1) || (step === 3 && !canProceedStep3)}>
          Continue
        </button>
      {:else}
        <button class="btn-primary" on:click={finishSetup} disabled={!canFinish || readyChecking}>
          Enter
        </button>
      {/if}
    </div>
  </div>
</div>

<style>
  /* Sheet chrome: system sans, glass, no mascot. */
  .wizard-overlay {
    position: fixed;
    top: 0; left: 0; right: 0; bottom: 0;
    background: #000000;
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 1000;
    padding: 0;
    font-family: var(--font-mono);
    font-size: 12px;
    line-height: 1.5;
    color: var(--text-primary, #f5f5f7);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
  }

  /* Full-bleed void: no modal card floating on grey. */
  .wizard {
    width: 100%;
    max-width: 640px;
    height: 100%;
    max-height: 100%;
    border: none;
    border-radius: 0;
    background: none;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  .wizard.wide {
    max-width: 920px;
  }

  .wizard-header {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    padding: 22px 24px 8px;
  }

  .wizard-title {
    font-family: var(--font);
    font-size: 13px;
    font-weight: 400;
    color: var(--text-primary, #f5f5f7);
    letter-spacing: 0;
  }

  .wizard-step {
    font-family: var(--font);
    font-size: 8px;
    color: var(--text-secondary, #a1a1a6);
  }

  /* 5 pixel ticks, not a rounded bar. */
  .wizard-progress {
    display: flex;
    gap: 8px;
    padding: 12px 24px 0;
  }

  .progress-segment {
    flex: 0 0 auto;
    width: 16px;
    height: 10px;
    border-radius: 0;
    background: transparent;
    border: 1px solid var(--border, rgba(255, 255, 255, 0.14));
    image-rendering: pixelated;
  }

  .progress-segment.active {
    background: var(--ok);
    border-color: var(--ok);
  }

  .wizard-body {
    flex: 1;
    overflow-y: auto;
    padding: 22px 24px 8px;
  }

  .wizard-footer {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 16px 24px 22px;
    gap: 12px;
  }

  .step-pane {
    animation: ks-step-in 200ms var(--ease, cubic-bezier(0.22, 1, 0.36, 1)) both;
  }

  @keyframes ks-step-in {
    from { opacity: 0; transform: translateY(8px); }
    to { opacity: 1; transform: translateY(0); }
  }

  .step-content {
    display: flex;
    flex-direction: column;
    gap: 14px;
  }

  .step-title {
    font-family: var(--font);
    font-size: 12px;
    font-weight: 400;
    color: var(--text-primary, #f5f5f7);
    margin: 0 0 4px;
    letter-spacing: 0;
    line-height: 1.6;
  }

  .step-desc {
    font-size: 13px;
    color: var(--text-secondary, #a1a1a6);
    margin: 0;
    line-height: 1.5;
  }

  .path-info {
    color: var(--text-faint, #6e6e73);
    font-size: 12px;
    font-family: var(--font-mono, ui-monospace, "SF Mono", "Cascadia Code", "JetBrains Mono", Consolas, monospace);
    word-break: break-all;
  }

  .warn-text {
    color: var(--warn);
  }

  .ok-text {
    color: var(--ok);
  }

  .step-actions {
    display: flex;
    gap: 10px;
    flex-wrap: wrap;
    margin-top: 4px;
  }

  .mono-box {
    font-family: var(--font-mono, ui-monospace, "SF Mono", "Cascadia Code", "JetBrains Mono", Consolas, monospace);
    font-size: 12px;
    padding: 12px 14px;
    border: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    border-radius: var(--radius-sm, 10px);
    background: rgba(255, 255, 255, 0.04);
    word-break: break-all;
    color: var(--text-primary, #f5f5f7);
  }

  .seed-box {
    font-family: var(--font-mono, ui-monospace, "SF Mono", "Cascadia Code", "JetBrains Mono", Consolas, monospace);
    font-size: 13px;
    padding: 16px;
    border: 1px solid var(--warn);
    border-radius: var(--radius-sm, 10px);
    background: var(--warn-muted);
    color: var(--text-primary, #f5f5f7);
    line-height: 1.7;
    word-spacing: 4px;
  }

  .seed-input {
    width: 100%;
    resize: none;
    min-height: 96px;
  }

  .option-group {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .wizard .option-btn {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    padding: 16px 18px;
    border: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    border-radius: var(--radius, 14px);
    background: transparent;
    text-align: left;
    gap: 6px;
    font-family: inherit;
    font-size: 13px;
    font-weight: 500;
    color: inherit;
    cursor: pointer;
    transition: background var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .wizard .option-btn:hover {
    border-color: rgba(255, 255, 255, 0.22);
    background: rgba(255, 255, 255, 0.04);
  }

  .wizard .option-btn.selected {
    border-color: rgba(255, 255, 255, 0.28);
    background: rgba(255, 255, 255, 0.12);
  }

  .option-name {
    font-family: var(--font);
    font-size: 11px;
    font-weight: 400;
    color: var(--text-primary, #f5f5f7);
    line-height: 1.6;
  }

  .option-desc {
    font-size: 13px;
    color: var(--text-secondary, #a1a1a6);
    line-height: 1.45;
  }

  .checkbox-group {
    margin-bottom: 4px;
  }

  .checkbox-group label {
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 13px;
    color: var(--text-primary, #f5f5f7);
    cursor: pointer;
  }

  .checkbox-group input[type="checkbox"] {
    width: 14px;
    height: 14px;
    padding: 0;
    accent-color: var(--text-primary, #f5f5f7);
  }

  input[type="range"] {
    width: 100%;
    padding: 0;
    border: none;
    background: none;
    accent-color: var(--text-primary, #f5f5f7);
  }

  .checklist {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .check-item {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 12px 14px;
    border: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    border-radius: var(--radius-sm, 10px);
    background: rgba(255, 255, 255, 0.03);
  }

  .check-label {
    font-size: 13px;
    color: var(--text-primary, #f5f5f7);
    font-weight: 600;
  }

  .check-val {
    font-size: 12px;
    color: var(--text-secondary, #a1a1a6);
    font-family: var(--font-mono, ui-monospace, "SF Mono", "Cascadia Code", "JetBrains Mono", Consolas, monospace);
  }

  .check-val.ok {
    color: var(--ok);
  }

  .check-val.err {
    color: var(--err);
  }

  .waiting {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 14px;
    padding: 28px 8px 12px;
  }

  .waiting .ks-spinner {
    width: 36px;
    height: 36px;
  }

  .wizard :global(button) {
    font-family: var(--font);
    font-size: 10px;
    font-weight: 400;
    letter-spacing: 0;
    border-radius: 0;
    padding: 10px 16px;
    transition: background var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .wizard :global(input),
  .wizard :global(textarea),
  .wizard :global(select) {
    font-family: inherit;
    font-size: 13px;
    border-radius: var(--radius-sm, 10px);
    padding: 10px 12px;
    background: rgba(0, 0, 0, 0.35);
    border: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    color: var(--text-primary, #f5f5f7);
    transition: border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  select {
    width: 100%;
  }

  @media (prefers-reduced-motion: reduce) {
    .step-pane { animation: none; }
    .progress-segment,
    .wizard .option-btn,
    .wizard :global(button),
    .wizard :global(input),
    .wizard :global(textarea),
    .wizard :global(select) {
      transition: none;
    }
  }
</style>
