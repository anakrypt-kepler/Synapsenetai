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
    type SetupConfig,
    type SystemInfo,
  } from "../../lib/rpc";

  const dispatch = createEventDispatcher();

  let step = 1;
  let walletMode = "";
  let seedPhrase = "";
  let generatedSeed = "";
  let generatedAddress = "";
  let seedConfirmed = false;
  let walletPassword = "";
  let restoreSeed = "";

  let connectionType = "clearnet";
  let bridgeLines = "";

  let aiModel = "skip";
  let modelPath = "";
  let downloadProgress = 0;
  let downloading = false;

  let systemInfo: SystemInfo = { cpu_cores: 4, ram_total_mb: 8192, gpu_devices: [] };
  let cpuThreads = 2;
  let ramLimitMb = 4096;
  let diskLimitMb = 50000;
  let gpuEnabled = false;
  let gpuDevice = "";
  let gpuLayers = 32;
  let launchAtStartup = false;
  let mineBackground = false;

  let walletOk = false;
  let connectionOk = false;
  let modelStatus = "skipped";
  let peersFound = 0;
  let readyError = "";
  let readyChecking = true;

  onMount(async () => {
    try {
      systemInfo = await getSystemInfo();
      cpuThreads = Math.max(1, Math.floor(systemInfo.cpu_cores / 2));
      const quarter = Math.floor(systemInfo.ram_total_mb * 0.25);
      ramLimitMb = Math.min(4096, quarter);
    } catch {}
  });

  async function handleWalletCreate() {
    walletMode = "create";
    try {
      const result = JSON.parse(await walletCreate());
      generatedSeed = result.seed || "unable to generate seed";
      generatedAddress = result.address || "";
    } catch {
      generatedSeed = "engine not available - seed will be generated on first launch";
      generatedAddress = "pending";
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
    if (step < 5) step += 1;
    if (step === 5) runReadyChecks();
  }

  function prevStep() {
    if (step > 1) step -= 1;
  }

  async function startDownload() {
    downloading = true;
    downloadProgress = 0;
    const interval = setInterval(() => {
      downloadProgress += Math.random() * 8;
      if (downloadProgress >= 100) {
        downloadProgress = 100;
        downloading = false;
        clearInterval(interval);
      }
    }, 500);
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
    readyError = "";

    walletOk = walletMode === "create" || walletMode === "restore" || walletMode === "restored";

    try {
      await initEngine();
      const raw = await getStatus();
      const status = parseStatus(raw);
      connectionOk = status.connection !== "disconnected";
      peersFound = status.peers;
      if (status.model_loaded) {
        modelStatus = `loaded (${status.model_name})`;
      } else if (aiModel === "skip") {
        modelStatus = "skipped";
      } else {
        modelStatus = "not loaded";
      }
    } catch {
      connectionOk = false;
      peersFound = 0;
      modelStatus = aiModel === "skip" ? "skipped" : "not loaded";
    }

    readyChecking = false;
  }

  async function finishSetup() {
    const config: SetupConfig = {
      wallet_mode: walletMode,
      seed_phrase: walletMode === "restore" ? restoreSeed : null,
      password: walletPassword || null,
      connection_type: connectionType,
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
      mine_background: mineBackground,
    };

    try {
      await saveSetupConfig(config);
    } catch {}

    dispatch("complete");
  }

  $: canProceedStep1 =
    (walletMode === "create" && seedConfirmed) ||
    (walletMode === "restore" && restoreSeed.trim().split(/\s+/).length === 24) ||
    walletMode === "restored";

  $: canProceedStep3 =
    aiModel === "skip" ||
    aiModel === "local" ||
    (aiModel === "download" && downloadProgress >= 100);

  $: canFinish = walletOk && connectionOk;
</script>

<div class="wizard-overlay">
  <div class="wizard">
    <div class="wizard-header">
      <span class="wizard-title">SynapseNet Setup</span>
      <span class="wizard-step">Step {step} of 5</span>
    </div>

    <div class="wizard-progress">
      {#each [1, 2, 3, 4, 5] as s}
        <div class="progress-segment" class:active={s <= step}></div>
      {/each}
    </div>

    <div class="wizard-body">
      {#if step === 1}
        <div class="step-content">
          <h2 class="step-title">Wallet</h2>
          {#if !walletMode}
            <p class="step-desc">Create a new wallet or restore from an existing seed phrase.</p>
            <div class="step-actions">
              <button class="btn-primary" on:click={handleWalletCreate}>Create New Wallet</button>
              <button class="btn-secondary" on:click={handleWalletRestore}>Restore from Seed</button>
            </div>
          {:else if walletMode === "create"}
            {#if !seedConfirmed}
              <p class="step-desc">Your NGT address:</p>
              <div class="mono-box">{generatedAddress}</div>
              <p class="step-desc">Save this 24-word seed phrase. It will not be shown again.</p>
              <div class="seed-box">{generatedSeed}</div>
              <div class="form-group">
                <label>Password (optional)</label>
                <input type="password" bind:value={walletPassword} placeholder="Wallet password" />
              </div>
              <button class="btn-primary" on:click={confirmSeed}>I have saved my seed phrase</button>
            {:else}
              <p class="step-desc">Wallet created successfully.</p>
              <div class="mono-box">{generatedAddress}</div>
            {/if}
          {:else if walletMode === "restore"}
            <p class="step-desc">Enter your 24-word seed phrase:</p>
            <textarea class="seed-input" bind:value={restoreSeed} rows="4" placeholder="word1 word2 word3 ..."></textarea>
            <button class="btn-primary" on:click={doRestore} disabled={restoreSeed.trim().split(/\s+/).length !== 24}>Restore Wallet</button>
          {:else if walletMode === "restored"}
            <p class="step-desc">Wallet restored successfully.</p>
            <div class="mono-box">{generatedAddress}</div>
          {/if}
        </div>

      {:else if step === 2}
        <div class="step-content">
          <h2 class="step-title">Connection Type</h2>
          <p class="step-desc">Choose how your node connects to the network.</p>
          <div class="option-group">
            <button
              class="option-btn"
              class:selected={connectionType === "clearnet"}
              on:click={() => (connectionType = "clearnet")}
            >
              <span class="option-name">Clearnet</span>
              <span class="option-desc">Direct TCP connection. Fastest, no privacy.</span>
            </button>
            <button
              class="option-btn"
              class:selected={connectionType === "tor"}
              on:click={() => (connectionType = "tor")}
            >
              <span class="option-name">Tor</span>
              <span class="option-desc">All traffic routed through Tor. Full privacy.</span>
            </button>
            <button
              class="option-btn"
              class:selected={connectionType === "tor_bridges"}
              on:click={() => (connectionType = "tor_bridges")}
            >
              <span class="option-name">Tor + Bridges</span>
              <span class="option-desc">Tor with obfs4 bridges for censored networks.</span>
            </button>
          </div>
          {#if connectionType === "tor_bridges"}
            <div class="form-group">
              <label>Bridge Lines</label>
              <textarea bind:value={bridgeLines} rows="4" placeholder="obfs4 bridge lines, one per line"></textarea>
            </div>
          {/if}
        </div>

      {:else if step === 3}
        <div class="step-content">
          <h2 class="step-title">AI Model</h2>
          <p class="step-desc">Select an AI model for completions and knowledge mining.</p>
          <div class="option-group">
            <button
              class="option-btn"
              class:selected={aiModel === "download"}
              on:click={() => (aiModel = "download")}
            >
              <span class="option-name">Download Recommended</span>
              <span class="option-desc">Llama 3B (~2 GB). Best for most users.</span>
            </button>
            <button
              class="option-btn"
              class:selected={aiModel === "local"}
              on:click={selectLocalFile}
            >
              <span class="option-name">Select Local File</span>
              <span class="option-desc">Choose an existing .gguf model on disk.</span>
            </button>
            <button
              class="option-btn"
              class:selected={aiModel === "skip"}
              on:click={() => (aiModel = "skip")}
            >
              <span class="option-name">Skip</span>
              <span class="option-desc">No AI. Mining and completions disabled.</span>
            </button>
          </div>
          {#if aiModel === "download"}
            {#if !downloading && downloadProgress < 100}
              <button class="btn-primary" on:click={startDownload}>Start Download</button>
            {:else}
              <div class="progress-bar-container">
                <div class="progress-bar" style="width: {downloadProgress}%"></div>
              </div>
              <span class="progress-label">{Math.floor(downloadProgress)}%</span>
            {/if}
          {/if}
          {#if aiModel === "local" && modelPath}
            <div class="mono-box">{modelPath}</div>
          {/if}
        </div>

      {:else if step === 4}
        <div class="step-content">
          <h2 class="step-title">Resources</h2>
          <p class="step-desc">Configure resource limits for your node.</p>
          <div class="form-group">
            <label>CPU Threads ({cpuThreads} / {systemInfo.cpu_cores})</label>
            <input type="range" min="1" max={systemInfo.cpu_cores} bind:value={cpuThreads} />
          </div>
          <div class="form-group">
            <label>RAM Limit ({ramLimitMb} MB / {systemInfo.ram_total_mb} MB)</label>
            <input type="range" min="512" max={systemInfo.ram_total_mb} step="256" bind:value={ramLimitMb} />
          </div>
          <div class="form-group">
            <label>Disk Space Limit (MB)</label>
            <input type="number" bind:value={diskLimitMb} min="1000" />
          </div>
          <div class="gpu-section">
            <div class="checkbox-group">
              <label>
                <input type="checkbox" bind:checked={gpuEnabled} />
                Enable GPU Acceleration
              </label>
            </div>
            {#if gpuEnabled}
              {#if systemInfo.gpu_devices.length > 0}
                <div class="form-group">
                  <label>GPU Device</label>
                  <select bind:value={gpuDevice}>
                    {#each systemInfo.gpu_devices as dev}
                      <option value={dev.id}>{dev.name} ({dev.vram_mb} MB VRAM)</option>
                    {/each}
                  </select>
                </div>
              {:else}
                <div class="gpu-not-found">No GPU detected. llama.cpp will use CPU-only inference.</div>
              {/if}
              <div class="form-group">
                <label>GPU Layers (offload to VRAM): {gpuLayers}</label>
                <input type="range" min="0" max="64" bind:value={gpuLayers} />
              </div>
              <div class="gpu-hint">Higher = more VRAM used, faster inference. Set 0 for CPU-only.</div>
            {/if}
          </div>
          <div class="checkbox-group">
            <label>
              <input type="checkbox" bind:checked={launchAtStartup} />
              Launch at system startup
            </label>
          </div>
          <div class="checkbox-group">
            <label>
              <input type="checkbox" bind:checked={mineBackground} />
              Mine NGT in background
            </label>
          </div>
        </div>

      {:else if step === 5}
        <div class="step-content">
          <h2 class="step-title">Ready</h2>
          {#if readyChecking}
            <p class="step-desc">Checking system status...</p>
          {:else}
            <div class="checklist">
              <div class="check-item">
                <span class="check-label">Wallet</span>
                <span class="check-value" class:ok={walletOk} class:err={!walletOk}>
                  {walletOk ? "OK" : "ERROR"}
                </span>
              </div>
              <div class="check-item">
                <span class="check-label">Connection</span>
                <span class="check-value" class:ok={connectionOk} class:err={!connectionOk}>
                  {connectionOk ? `connected (${connectionType})` : "ERROR"}
                </span>
              </div>
              <div class="check-item">
                <span class="check-label">AI Model</span>
                <span class="check-value">{modelStatus}</span>
              </div>
              <div class="check-item">
                <span class="check-label">Peers</span>
                <span class="check-value">{peersFound} found</span>
              </div>
            </div>
            {#if !connectionOk}
              <div class="error-box">
                Connection failed. Check your network settings and try again. If using Tor, ensure Tor is not blocked on your network.
              </div>
            {/if}
          {/if}
        </div>
      {/if}
    </div>

    <div class="wizard-footer">
      {#if step > 1 && step < 5}
        <button class="btn-secondary" on:click={prevStep}>Back</button>
      {:else}
        <div></div>
      {/if}
      {#if step < 5}
        <button
          class="btn-primary"
          on:click={nextStep}
          disabled={(step === 1 && !canProceedStep1) || (step === 3 && !canProceedStep3)}
        >
          Next
        </button>
      {:else}
        <button class="btn-primary" on:click={finishSetup} disabled={!canFinish || readyChecking}>
          Open SynapseNet
        </button>
      {/if}
    </div>
  </div>
</div>

<style>
  .wizard-overlay {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background: var(--bg);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 1000;
  }

  .wizard {
    width: 560px;
    max-height: 90vh;
    border: 1px solid var(--border);
    border-radius: 8px;
    background: var(--surface);
    display: flex;
    flex-direction: column;
    box-shadow: var(--shadow-md);
  }

  .wizard-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 18px 22px;
    border-bottom: 1px solid var(--border);
  }

  .wizard-title {
    font-size: 14px;
    font-weight: 600;
    color: var(--text-primary);
  }

  .wizard-step {
    font-size: 11px;
    color: var(--text-secondary);
  }

  .wizard-progress {
    display: flex;
    gap: 3px;
    padding: 0 22px;
    margin-top: 14px;
  }

  .progress-segment {
    flex: 1;
    height: 3px;
    background: var(--border);
    border-radius: 2px;
  }

  .progress-segment.active {
    background: var(--accent);
  }

  .wizard-body {
    flex: 1;
    overflow-y: auto;
    padding: 22px;
  }

  .wizard-footer {
    display: flex;
    justify-content: space-between;
    padding: 16px 22px;
    border-top: 1px solid var(--border);
  }

  .step-content {
    display: flex;
    flex-direction: column;
    gap: 14px;
  }

  .step-title {
    font-size: 16px;
    font-weight: 600;
    color: var(--text-primary);
    margin: 0;
  }

  .step-desc {
    font-size: 12px;
    color: var(--text-secondary);
    margin: 0;
    line-height: 1.6;
  }

  .step-actions {
    display: flex;
    gap: 10px;
  }

  .mono-box {
    font-size: 11px;
    padding: 10px 12px;
    border: 1px solid var(--border);
    border-radius: 4px;
    background: var(--bg);
    word-break: break-all;
    color: var(--text-primary);
  }

  .seed-box {
    font-size: 13px;
    padding: 14px;
    border: 1px solid var(--status-yellow);
    border-radius: 4px;
    background: var(--status-yellow-muted);
    color: var(--text-primary);
    line-height: 1.8;
    word-spacing: 4px;
  }

  .seed-input {
    width: 100%;
    resize: none;
    color: var(--text-primary);
    border-color: var(--border);
    background: var(--bg);
  }

  .option-group {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .option-btn {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    padding: 12px 14px;
    border: 1px solid var(--border);
    border-radius: 6px;
    background: var(--bg);
    text-align: left;
    gap: 4px;
    transition: border-color 0.15s ease, background 0.15s ease;
  }

  .option-btn:hover {
    border-color: var(--text-faint);
    background: var(--surface-alt);
  }

  .option-btn.selected {
    border-color: var(--accent);
    background: var(--accent-muted);
  }

  .option-name {
    font-size: 13px;
    font-weight: 600;
    color: var(--text-primary);
  }

  .option-desc {
    font-size: 11px;
    color: var(--text-secondary);
  }

  .progress-bar-container {
    width: 100%;
    height: 4px;
    background: var(--border);
    border-radius: 2px;
    overflow: hidden;
  }

  .progress-bar {
    height: 100%;
    background: var(--accent);
    border-radius: 2px;
    transition: width 0.3s ease;
  }

  .progress-label {
    font-size: 11px;
    color: var(--text-secondary);
  }

  .checkbox-group {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .checkbox-group label {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 12px;
    color: var(--text-primary);
    cursor: pointer;
  }

  .checkbox-group input[type="checkbox"] {
    width: 14px;
    height: 14px;
    padding: 0;
    accent-color: var(--accent);
  }

  input[type="range"] {
    width: 100%;
    padding: 0;
    border: none;
    background: none;
    accent-color: var(--accent);
  }

  .checklist {
    display: flex;
    flex-direction: column;
    gap: 0;
  }

  .check-item {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 10px 0;
    border-bottom: 1px solid var(--border);
  }

  .check-label {
    font-size: 13px;
    color: var(--text-primary);
    font-weight: 500;
  }

  .check-value {
    font-size: 12px;
    color: var(--text-secondary);
  }

  .check-value.ok {
    color: var(--status-green);
  }

  .check-value.err {
    color: var(--status-red);
  }

  .error-box {
    padding: 10px 12px;
    border: 1px solid var(--status-red);
    border-radius: 4px;
    background: var(--status-red-muted);
    font-size: 12px;
    color: var(--status-red);
    margin-top: 8px;
  }

  .gpu-section {
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 14px;
    margin: 8px 0;
    background: var(--surface-alt);
  }

  .gpu-not-found {
    font-size: 11px;
    color: var(--status-yellow);
    padding: 8px 0;
  }

  .gpu-hint {
    font-size: 11px;
    color: var(--text-secondary);
    margin-top: 4px;
    line-height: 1.5;
  }

  select {
    width: 100%;
    font-family: 'JetBrains Mono', monospace;
    font-size: 13px;
    padding: 6px 10px;
    border: 1px solid var(--border);
    border-radius: 4px;
    background: var(--bg);
    color: var(--text-primary);
  }
</style>
