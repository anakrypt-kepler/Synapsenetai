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

  let connectionType = "tor";
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
  let modelStatus = "SKIPPED";
  let peersFound = 0;
  let readyChecking = true;

  let dataDir = "~/.synapsenet/";
  let walletFile = "~/.synapsenet/wallet.dat";

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

  $: canFinish = walletOk;
</script>

<div class="wizard-overlay">
  <div class="wizard">
    <div class="wizard-header">
      <span class="wizard-title">SYNAPSENET SETUP</span>
      <span class="wizard-step">[{step}/5]</span>
    </div>

    <div class="wizard-progress">
      {#each [1, 2, 3, 4, 5] as s}
        <div class="progress-segment" class:active={s <= step}></div>
      {/each}
    </div>

    <div class="wizard-body">
      {#if step === 1}
        <div class="step-content">
          <h2 class="step-title">WALLET</h2>
          {#if !walletMode}
            <p class="step-desc">Create a new wallet or restore from seed phrase.</p>
            <div class="step-desc path-info">Data directory: {dataDir}</div>
            <div class="step-desc path-info">Wallet file: {walletFile}</div>
            <div class="step-actions">
              <button class="btn-primary" on:click={handleWalletCreate}>[ CREATE NEW ]</button>
              <button class="btn-secondary" on:click={handleWalletRestore}>[ RESTORE ]</button>
            </div>
          {:else if walletMode === "create"}
            {#if !seedConfirmed}
              <p class="step-desc">YOUR NGT ADDRESS:</p>
              <div class="mono-box">{generatedAddress}</div>
              <p class="step-desc warn-text">SAVE THIS 24-WORD SEED PHRASE. IT WILL NOT BE SHOWN AGAIN.</p>
              <div class="seed-box">{generatedSeed}</div>
              <div class="step-desc path-info">Saved to: {walletFile}</div>
              <div class="form-group">
                <label>PASSWORD (OPTIONAL)</label>
                <input type="password" bind:value={walletPassword} placeholder="..." />
              </div>
              <button class="btn-primary" on:click={confirmSeed}>[ I SAVED MY SEED ]</button>
            {:else}
              <p class="step-desc">WALLET CREATED.</p>
              <div class="mono-box">{generatedAddress}</div>
            {/if}
          {:else if walletMode === "restore"}
            <p class="step-desc">ENTER 24-WORD SEED PHRASE:</p>
            <textarea class="seed-input" bind:value={restoreSeed} rows="4" placeholder="word1 word2 word3 ..."></textarea>
            <button class="btn-primary" on:click={doRestore} disabled={restoreSeed.trim().split(/\s+/).length !== 24}>[ RESTORE ]</button>
          {:else if walletMode === "restored"}
            <p class="step-desc">WALLET RESTORED.</p>
            <div class="mono-box">{generatedAddress}</div>
          {/if}
        </div>

      {:else if step === 2}
        <div class="step-content">
          <h2 class="step-title">CONNECTION</h2>
          <p class="step-desc">Select network transport. Tor is recommended.</p>
          <div class="option-group">
            <button class="option-btn" class:selected={connectionType === "tor"} on:click={() => (connectionType = "tor")}>
              <span class="option-name">[ TOR ]</span>
              <span class="option-desc">All traffic through Tor hidden services. Full privacy. Auto-connects.</span>
            </button>
            <button class="option-btn" class:selected={connectionType === "tor_bridges"} on:click={() => (connectionType = "tor_bridges")}>
              <span class="option-name">[ TOR + BRIDGES ]</span>
              <span class="option-desc">Tor with obfs4 bridges. For censored networks.</span>
            </button>
            <button class="option-btn" class:selected={connectionType === "clearnet"} on:click={() => (connectionType = "clearnet")}>
              <span class="option-name">[ CLEARNET ]</span>
              <span class="option-desc">Direct TCP. Fast but no privacy. Not recommended.</span>
            </button>
          </div>
          {#if connectionType === "tor_bridges"}
            <div class="form-group">
              <label>BRIDGE LINES</label>
              <textarea bind:value={bridgeLines} rows="4" placeholder="obfs4 bridge lines, one per line"></textarea>
            </div>
          {/if}
          {#if connectionType === "tor" || connectionType === "tor_bridges"}
            <div class="step-desc ok-text">Tor will be provisioned automatically on first launch.</div>
          {/if}
        </div>

      {:else if step === 3}
        <div class="step-content">
          <h2 class="step-title">AI MODEL</h2>
          <p class="step-desc">Select AI model for completions and knowledge mining.</p>
          <div class="option-group">
            <button class="option-btn" class:selected={aiModel === "download"} on:click={() => (aiModel = "download")}>
              <span class="option-name">[ DOWNLOAD ]</span>
              <span class="option-desc">Llama 3B GGUF (~2 GB). Recommended.</span>
            </button>
            <button class="option-btn" class:selected={aiModel === "local"} on:click={selectLocalFile}>
              <span class="option-name">[ LOCAL FILE ]</span>
              <span class="option-desc">Select .gguf model from disk.</span>
            </button>
            <button class="option-btn" class:selected={aiModel === "skip"} on:click={() => (aiModel = "skip")}>
              <span class="option-name">[ SKIP ]</span>
              <span class="option-desc">No AI model. You can load one later.</span>
            </button>
          </div>
          {#if aiModel === "download"}
            {#if !downloading && downloadProgress < 100}
              <button class="btn-primary" on:click={startDownload}>[ START DOWNLOAD ]</button>
            {:else}
              <div class="pixel-loading">
                <div class="pixel-loading-bar" style="width: {downloadProgress}%"></div>
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
          <h2 class="step-title">RESOURCES</h2>
          <p class="step-desc">Configure resource limits.</p>
          <div class="form-group">
            <label>CPU THREADS: {cpuThreads}/{systemInfo.cpu_cores}</label>
            <input type="range" min="1" max={systemInfo.cpu_cores} bind:value={cpuThreads} />
          </div>
          <div class="form-group">
            <label>RAM: {ramLimitMb}MB/{systemInfo.ram_total_mb}MB</label>
            <input type="range" min="512" max={systemInfo.ram_total_mb} step="256" bind:value={ramLimitMb} />
          </div>
          <div class="form-group">
            <label>DISK LIMIT (MB)</label>
            <input type="number" bind:value={diskLimitMb} min="1000" />
          </div>
          {#if systemInfo.gpu_devices.length > 0}
            <div class="form-group">
              <label>GPU</label>
              <div class="checkbox-group">
                <label><input type="checkbox" bind:checked={gpuEnabled} /> ENABLE GPU</label>
              </div>
            </div>
            {#if gpuEnabled}
              <div class="form-group">
                <label>DEVICE</label>
                <select bind:value={gpuDevice}>
                  {#each systemInfo.gpu_devices as dev}
                    <option value={dev.id}>{dev.name} ({dev.vram_mb}MB)</option>
                  {/each}
                </select>
              </div>
              <div class="form-group">
                <label>GPU LAYERS: {gpuLayers}</label>
                <input type="range" min="0" max="64" bind:value={gpuLayers} />
              </div>
            {/if}
          {/if}
          <div class="checkbox-group">
            <label><input type="checkbox" bind:checked={launchAtStartup} /> LAUNCH AT STARTUP</label>
          </div>
          <div class="checkbox-group">
            <label><input type="checkbox" bind:checked={mineBackground} /> MINE IN BACKGROUND</label>
          </div>
        </div>

      {:else if step === 5}
        <div class="step-content">
          <h2 class="step-title">READY</h2>
          {#if readyChecking}
            <p class="step-desc blink-text">CHECKING SYSTEM...</p>
          {:else}
            <div class="checklist">
              <div class="check-item">
                <span class="check-label">WALLET</span>
                <span class="check-val" class:ok={walletOk} class:err={!walletOk}>
                  {walletOk ? "OK" : "ERR"}
                </span>
              </div>
              <div class="check-item">
                <span class="check-label">CONNECTION</span>
                <span class="check-val" class:ok={connectionOk} class:err={!connectionOk}>
                  {connectionOk ? connectionType.toUpperCase() : "WAITING"}
                </span>
              </div>
              <div class="check-item">
                <span class="check-label">MODEL</span>
                <span class="check-val">{modelStatus}</span>
              </div>
              <div class="check-item">
                <span class="check-label">PEERS</span>
                <span class="check-val">{peersFound}</span>
              </div>
            </div>
            <div class="step-desc path-info">Data: {dataDir}</div>
            <div class="step-desc path-info">Wallet: {walletFile}</div>
          {/if}
        </div>
      {/if}
    </div>

    <div class="wizard-footer">
      {#if step > 1 && step < 5}
        <button class="btn-secondary" on:click={prevStep}>[ BACK ]</button>
      {:else}
        <div></div>
      {/if}
      {#if step < 5}
        <button class="btn-primary" on:click={nextStep} disabled={(step === 1 && !canProceedStep1) || (step === 3 && !canProceedStep3)}>
          [ NEXT ]
        </button>
      {:else}
        <button class="btn-primary" on:click={finishSetup} disabled={!canFinish || readyChecking}>
          [ ENTER ]
        </button>
      {/if}
    </div>
  </div>
</div>

<style>
  .wizard-overlay {
    position: fixed;
    top: 0; left: 0; right: 0; bottom: 0;
    background: #000000;
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 1000;
  }

  .wizard {
    width: 520px;
    max-height: 90vh;
    border: 1px solid var(--border);
    background: #000000;
    display: flex;
    flex-direction: column;
  }

  .wizard-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 12px 16px;
    border-bottom: 1px solid var(--border);
  }

  .wizard-title {
    font-size: 12px;
    font-weight: 700;
    color: var(--text-primary);
    letter-spacing: 2px;
  }

  .wizard-step {
    font-size: 10px;
    color: var(--text-secondary);
  }

  .wizard-progress {
    display: flex;
    gap: 2px;
    padding: 0 16px;
    margin-top: 12px;
  }

  .progress-segment {
    flex: 1;
    height: 4px;
    background: var(--border);
  }

  .progress-segment.active {
    background: var(--text-primary);
  }

  .wizard-body {
    flex: 1;
    overflow-y: auto;
    padding: 16px;
  }

  .wizard-footer {
    display: flex;
    justify-content: space-between;
    padding: 12px 16px;
    border-top: 1px solid var(--border);
  }

  .step-content {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .step-title {
    font-size: 12px;
    font-weight: 700;
    color: var(--text-primary);
    margin: 0;
    letter-spacing: 2px;
  }

  .step-desc {
    font-size: 10px;
    color: var(--text-secondary);
    margin: 0;
    line-height: 1.6;
  }

  .path-info {
    color: var(--text-faint);
    font-size: 8px;
    letter-spacing: 0.5px;
  }

  .warn-text {
    color: var(--warn);
  }

  .ok-text {
    color: var(--ok);
  }

  .step-actions {
    display: flex;
    gap: 8px;
  }

  .mono-box {
    font-size: 10px;
    padding: 8px;
    border: 1px solid var(--border);
    word-break: break-all;
    color: var(--text-primary);
  }

  .seed-box {
    font-size: 10px;
    padding: 12px;
    border: 1px solid var(--warn);
    background: var(--warn-muted);
    color: var(--text-primary);
    line-height: 2;
    word-spacing: 4px;
  }

  .seed-input {
    width: 100%;
    resize: none;
  }

  .option-group {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .option-btn {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    padding: 10px 12px;
    border: 1px solid var(--border);
    background: #000000;
    text-align: left;
    gap: 4px;
  }

  .option-btn:hover {
    border-color: var(--text-primary);
  }

  .option-btn.selected {
    border-color: var(--text-primary);
    background: var(--accent-muted);
  }

  .option-name {
    font-size: 10px;
    font-weight: 700;
    color: var(--text-primary);
  }

  .option-desc {
    font-size: 8px;
    color: var(--text-secondary);
  }

  .progress-label {
    font-size: 10px;
    color: var(--text-primary);
  }

  .checkbox-group {
    margin-bottom: 6px;
  }

  .checkbox-group label {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 10px;
    color: var(--text-primary);
    cursor: pointer;
  }

  .checkbox-group input[type="checkbox"] {
    width: 12px;
    height: 12px;
    padding: 0;
    accent-color: var(--text-primary);
  }

  input[type="range"] {
    width: 100%;
    padding: 0;
    border: none;
    background: none;
    accent-color: var(--text-primary);
  }

  .checklist {
    display: flex;
    flex-direction: column;
  }

  .check-item {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 8px 0;
    border-bottom: 1px solid var(--border);
  }

  .check-label {
    font-size: 10px;
    color: var(--text-primary);
    font-weight: 700;
  }

  .check-val {
    font-size: 10px;
    color: var(--text-secondary);
  }

  .check-val.ok {
    color: var(--ok);
  }

  .check-val.err {
    color: var(--err);
  }

  @keyframes blink-anim {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.3; }
  }

  .blink-text {
    animation: blink-anim 1s step-end infinite;
  }

  select {
    width: 100%;
    font-size: 10px;
    padding: 6px 8px;
    border: 1px solid var(--border);
    background: #000000;
    color: var(--text-primary);
  }
</style>
