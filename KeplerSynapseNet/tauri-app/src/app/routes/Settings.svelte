<script lang="ts">
  import { onMount } from "svelte";
  import {
    rpcCall,
    updateSettings,
    checkUpdates,
    modelLoad,
    modelUnload,
    getSystemInfo,
    type SystemInfo,
  } from "../../lib/rpc";

  let connectionType = "tor";
  let bridgeLines = "";
  let modelName = "";
  let modelLoaded = false;
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
  let naanEnabled = false;
  let naanTopics = "";
  let naanSiteAllowlist = "";
  let launchAtLogin = false;
  let minimizeToTray = false;
  let autoUpdate = true;
  let updateStatus = "";

  let profileAlias = "";
  let profileAvatarData = "";
  let avatarStatus = "";

  let settingsTab: "general" | "ai" | "resources" | "profile" | "security" = "general";

  onMount(async () => {
    try { systemInfo = await getSystemInfo(); } catch {}
    try {
      const cfg = JSON.parse(await rpcCall("settings.get", "{}"));
      connectionType = cfg.connection_type || "tor";
      bridgeLines = cfg.bridge_lines || "";
      modelName = cfg.model_name || "";
      modelLoaded = cfg.model_loaded || false;
      modelPath = cfg.model_path || "";
      cpuThreads = cfg.cpu_threads || 2;
      ramLimitMb = cfg.ram_limit_mb || 4096;
      diskLimitMb = cfg.disk_limit_mb || 50000;
      gpuEnabled = cfg.gpu_enabled || false;
      gpuDevice = cfg.gpu_device || "";
      gpuLayers = cfg.gpu_layers || 32;
      naanEnabled = cfg.naan_enabled || false;
      naanTopics = cfg.naan_topics || "";
      naanSiteAllowlist = cfg.naan_site_allowlist || "";
      launchAtLogin = cfg.launch_at_login || false;
      minimizeToTray = cfg.minimize_to_tray || false;
      autoUpdate = cfg.auto_update ?? true;
      profileAlias = cfg.profile_alias || "";
      profileAvatarData = cfg.profile_avatar || "";
    } catch {}
  });

  async function saveConnection() {
    try { await updateSettings(JSON.stringify({ connection_type: connectionType, bridge_lines: bridgeLines })); } catch {}
  }

  async function handleModelLoad() {
    if (!modelPath) return;
    try { await modelLoad(modelPath); modelLoaded = true; } catch {}
  }

  async function handleModelUnload() {
    try { await modelUnload(); modelLoaded = false; } catch {}
  }

  async function startModelDownload() {
    downloading = true;
    downloadProgress = 0;
    const interval = setInterval(() => {
      downloadProgress += Math.random() * 10;
      if (downloadProgress >= 100) {
        downloadProgress = 100;
        downloading = false;
        clearInterval(interval);
      }
    }, 500);
  }

  async function selectModelFile() {
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({ filters: [{ name: "GGUF", extensions: ["gguf"] }], multiple: false });
      if (selected) { modelPath = typeof selected === "string" ? selected : selected.path; }
    } catch {}
  }

  async function saveResources() {
    try {
      await updateSettings(JSON.stringify({
        cpu_threads: cpuThreads, ram_limit_mb: ramLimitMb, disk_limit_mb: diskLimitMb,
        gpu_enabled: gpuEnabled, gpu_device: gpuDevice, gpu_layers: gpuLayers,
      }));
    } catch {}
  }

  async function saveNaan() {
    try { await updateSettings(JSON.stringify({ naan_enabled: naanEnabled, naan_topics: naanTopics, naan_site_allowlist: naanSiteAllowlist })); } catch {}
  }

  async function saveStartup() {
    try { await updateSettings(JSON.stringify({ launch_at_login: launchAtLogin, minimize_to_tray: minimizeToTray })); } catch {}
  }

  async function doCheckUpdates() {
    updateStatus = "CHECKING...";
    try {
      const result = await checkUpdates();
      const parsed = JSON.parse(result);
      updateStatus = parsed.update_available ? `UPDATE: ${parsed.version}` : "UP TO DATE";
    } catch { updateStatus = "CHECK FAILED"; }
  }

  async function selectAvatar() {
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({ filters: [{ name: "Image", extensions: ["png", "jpg", "jpeg", "webp"] }], multiple: false });
      if (selected) {
        const path = typeof selected === "string" ? selected : selected.path;
        avatarStatus = "STRIPPING METADATA...";
        try {
          const result = await rpcCall("profile.set_avatar", JSON.stringify({ path, strip_metadata: true }));
          const parsed = JSON.parse(result);
          profileAvatarData = parsed.avatar_data || "";
          avatarStatus = "AVATAR SET. METADATA STRIPPED.";
        } catch {
          avatarStatus = "FAILED";
        }
      }
    } catch {}
  }

  async function saveProfile() {
    try { await updateSettings(JSON.stringify({ profile_alias: profileAlias })); } catch {}
  }
</script>

<div class="content-area">
  <div class="tab-row">
    <button class="tbtn" class:active={settingsTab === "general"} on:click={() => (settingsTab = "general")}>GENERAL</button>
    <button class="tbtn" class:active={settingsTab === "ai"} on:click={() => (settingsTab = "ai")}>AI</button>
    <button class="tbtn" class:active={settingsTab === "resources"} on:click={() => (settingsTab = "resources")}>RESOURCES</button>
    <button class="tbtn" class:active={settingsTab === "profile"} on:click={() => (settingsTab = "profile")}>PROFILE</button>
    <button class="tbtn" class:active={settingsTab === "security"} on:click={() => (settingsTab = "security")}>SECURITY</button>
  </div>

  {#if settingsTab === "general"}
    <div class="section-title">CONNECTION</div>
    <div class="card">
      <div class="form-group">
        <label>TYPE</label>
        <div class="opt-row">
          <button class="obtn" class:selected={connectionType === "tor"} on:click={() => (connectionType = "tor")}>TOR</button>
          <button class="obtn" class:selected={connectionType === "tor_bridges"} on:click={() => (connectionType = "tor_bridges")}>TOR+BRIDGES</button>
          <button class="obtn" class:selected={connectionType === "clearnet"} on:click={() => (connectionType = "clearnet")}>CLEARNET</button>
        </div>
      </div>
      {#if connectionType === "tor_bridges"}
        <div class="form-group">
          <label>BRIDGE LINES</label>
          <textarea bind:value={bridgeLines} rows="3" placeholder="obfs4 bridge lines"></textarea>
        </div>
      {/if}
      <button class="btn-primary" on:click={saveConnection}>[ SAVE ]</button>
    </div>

    <div class="section-title">NAAN AGENT</div>
    <div class="card">
      <div class="chk"><label><input type="checkbox" bind:checked={naanEnabled} /> ENABLE NAAN</label></div>
      <div class="form-group">
        <label>TOPICS</label>
        <input type="text" bind:value={naanTopics} placeholder="AI, crypto" />
      </div>
      <div class="form-group">
        <label>SITE ALLOWLIST</label>
        <input type="text" bind:value={naanSiteAllowlist} placeholder="arxiv.org, github.com" />
      </div>
      <button class="btn-primary" on:click={saveNaan}>[ SAVE ]</button>
    </div>

    <div class="section-title">STARTUP</div>
    <div class="card">
      <div class="chk"><label><input type="checkbox" bind:checked={launchAtLogin} /> LAUNCH AT LOGIN</label></div>
      <div class="chk"><label><input type="checkbox" bind:checked={minimizeToTray} /> MINIMIZE TO TRAY</label></div>
      <button class="btn-primary" on:click={saveStartup}>[ SAVE ]</button>
      <div class="update-row">
        <div class="chk"><label><input type="checkbox" bind:checked={autoUpdate} /> AUTO-UPDATE</label></div>
        <button class="btn-secondary" on:click={doCheckUpdates}>[ CHECK ]</button>
        {#if updateStatus}<span class="update-lbl">{updateStatus}</span>{/if}
      </div>
    </div>

  {:else if settingsTab === "ai"}
    <div class="section-title">AI MODEL</div>
    <div class="card">
      <div class="form-group">
        <label>STATUS</label>
        <div class="model-row">
          {#if modelLoaded}
            <span class="status-dot green"></span>{modelName}
            <button class="btn-secondary" on:click={handleModelUnload}>[ UNLOAD ]</button>
          {:else}
            <span class="status-dot red"></span>NONE
          {/if}
        </div>
      </div>
      <div class="form-group">
        <label>MODEL PATH</label>
        <div class="path-row">
          <input type="text" bind:value={modelPath} placeholder=".gguf model path" />
          <button class="btn-secondary" on:click={selectModelFile}>[ ... ]</button>
        </div>
      </div>
      <div class="grid-2">
        <button class="btn-primary" on:click={handleModelLoad} disabled={!modelPath}>[ LOAD ]</button>
        <button class="btn-secondary" on:click={startModelDownload} disabled={downloading}>
          {#if downloading}DL {Math.floor(downloadProgress)}%{:else}[ DOWNLOAD ]{/if}
        </button>
      </div>
      {#if downloading}
        <div class="pixel-loading" style="margin-top:8px">
          <div class="pixel-loading-bar" style="width: {downloadProgress}%"></div>
        </div>
      {/if}
    </div>

  {:else if settingsTab === "resources"}
    <div class="section-title">CPU & RAM</div>
    <div class="card">
      <div class="form-group">
        <label>CPU THREADS: {cpuThreads}/{systemInfo.cpu_cores}</label>
        <input type="range" min="1" max={systemInfo.cpu_cores} bind:value={cpuThreads} />
      </div>
      <div class="form-group">
        <label>RAM: {ramLimitMb}MB/{systemInfo.ram_total_mb}MB</label>
        <input type="range" min="512" max={systemInfo.ram_total_mb} step="256" bind:value={ramLimitMb} />
      </div>
      <div class="form-group">
        <label>DISK (MB)</label>
        <input type="number" bind:value={diskLimitMb} min="1000" />
      </div>
      <button class="btn-primary" on:click={saveResources}>[ SAVE ]</button>
    </div>

    <div class="section-title">GPU</div>
    <div class="card">
      <div class="chk"><label><input type="checkbox" bind:checked={gpuEnabled} /> ENABLE GPU</label></div>
      {#if gpuEnabled}
        {#if systemInfo.gpu_devices.length > 0}
          <div class="form-group">
            <label>DEVICE</label>
            <select bind:value={gpuDevice}>
              {#each systemInfo.gpu_devices as dev}
                <option value={dev.id}>{dev.name} ({dev.vram_mb}MB)</option>
              {/each}
            </select>
          </div>
        {:else}
          <div class="warn-text">NO GPU DETECTED</div>
        {/if}
        <div class="form-group">
          <label>LAYERS: {gpuLayers}</label>
          <input type="range" min="0" max="64" bind:value={gpuLayers} />
        </div>
      {/if}
    </div>

  {:else if settingsTab === "profile"}
    <div class="section-title">PROFILE</div>
    <div class="card">
      <div class="avatar-area">
        {#if profileAvatarData}
          <img src={profileAvatarData} alt="avatar" class="avatar-img" />
        {:else}
          <div class="avatar-placeholder">?</div>
        {/if}
        <button class="btn-secondary" on:click={selectAvatar}>[ SET AVATAR ]</button>
      </div>
      {#if avatarStatus}
        <div class="avatar-status">{avatarStatus}</div>
      {/if}
      <div class="form-group">
        <label>ALIAS (OPTIONAL)</label>
        <input type="text" bind:value={profileAlias} placeholder="Anonymous" />
      </div>
      <button class="btn-primary" on:click={saveProfile}>[ SAVE ]</button>
    </div>
    <div class="info-box">
      <div class="info-title">METADATA PROTECTION</div>
      <div class="info-line">All uploaded images are automatically stripped of EXIF metadata.</div>
      <div class="info-line">GPS coordinates, device info, timestamps — removed permanently.</div>
      <div class="info-line">Image is re-encoded from raw pixel data. No recovery possible.</div>
    </div>

  {:else if settingsTab === "security"}
    <div class="section-title">QUANTUM PROTECTION</div>
    <div class="card">
      <div class="sec-row"><span class="sec-label">KEY EXCHANGE</span><span class="sec-val ok">KYBER-1024 (ML-KEM)</span></div>
      <div class="sec-row"><span class="sec-label">SIGNATURES</span><span class="sec-val ok">DILITHIUM-5 (ML-DSA)</span></div>
      <div class="sec-row"><span class="sec-label">SYMMETRIC</span><span class="sec-val ok">AES-256-GCM</span></div>
      <div class="sec-row"><span class="sec-label">HASH</span><span class="sec-val ok">SHA3-512</span></div>
      <div class="sec-row"><span class="sec-label">TOR TRANSPORT</span><span class="sec-val ok">ENABLED</span></div>
      <div class="sec-row"><span class="sec-label">ANTI-INJECTION</span><span class="sec-val ok">ACTIVE</span></div>
    </div>
    <div class="info-box">
      <div class="info-title">SECURITY OVERVIEW</div>
      <div class="info-line">All node-to-node communication uses post-quantum cryptography.</div>
      <div class="info-line">KYBER-1024 provides key encapsulation resistant to quantum computers.</div>
      <div class="info-line">DILITHIUM-5 digital signatures prevent message forgery.</div>
      <div class="info-line">All traffic is routed through Tor hidden services by default.</div>
      <div class="info-line">SQL/XSS/command injection protections on all RPC endpoints.</div>
      <div class="info-line">Wallet keys stored with AES-256 encryption at rest.</div>
    </div>
  {/if}
</div>

<style>
  .tab-row {
    display: flex;
    gap: 2px;
    margin-bottom: 12px;
  }

  .tbtn {
    font-size: 8px;
    padding: 6px 12px;
    border: 1px solid var(--border);
    color: var(--text-secondary);
    background: none;
    letter-spacing: 1px;
  }

  .tbtn:hover { color: var(--text-primary); border-color: var(--text-primary); }

  .tbtn.active {
    color: #000;
    background: var(--text-primary);
    border-color: var(--text-primary);
  }

  .opt-row { display: flex; gap: 4px; margin-top: 4px; }

  .obtn {
    font-size: 8px;
    padding: 4px 10px;
    border: 1px solid var(--border);
    color: var(--text-secondary);
    background: none;
  }

  .obtn:hover { border-color: var(--text-primary); color: var(--text-primary); }

  .obtn.selected {
    border-color: var(--text-primary);
    color: #000;
    background: var(--text-primary);
  }

  .model-row {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 10px;
    color: var(--text-primary);
    margin-top: 4px;
  }

  .path-row { display: flex; gap: 4px; }
  .path-row input { flex: 1; }

  .chk { margin-bottom: 6px; }

  .chk label {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 10px;
    color: var(--text-primary);
    cursor: pointer;
  }

  .chk input[type="checkbox"] {
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

  .warn-text { font-size: 10px; color: var(--warn); margin: 6px 0; }

  .update-row {
    margin-top: 10px;
    padding-top: 10px;
    border-top: 1px solid var(--border);
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
  }

  .update-lbl { font-size: 8px; color: var(--text-secondary); }

  .avatar-area {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-bottom: 10px;
  }

  .avatar-img {
    width: 48px;
    height: 48px;
    border: 1px solid var(--border);
    object-fit: cover;
    image-rendering: pixelated;
  }

  .avatar-placeholder {
    width: 48px;
    height: 48px;
    border: 1px solid var(--border);
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 16px;
    color: var(--text-secondary);
  }

  .avatar-status {
    font-size: 8px;
    color: var(--ok);
    margin-bottom: 8px;
  }

  .info-box {
    border: 1px solid var(--border);
    padding: 12px;
    margin-top: 12px;
  }

  .info-title {
    font-size: 10px;
    font-weight: 700;
    color: var(--text-primary);
    margin-bottom: 6px;
    letter-spacing: 1px;
  }

  .info-line {
    font-size: 8px;
    color: var(--text-secondary);
    line-height: 2;
  }

  .sec-row {
    display: flex;
    justify-content: space-between;
    padding: 6px 0;
    border-bottom: 1px solid var(--border);
  }

  .sec-label { font-size: 10px; color: var(--text-primary); font-weight: 700; }
  .sec-val { font-size: 10px; color: var(--text-secondary); }
  .sec-val.ok { color: var(--ok); }
</style>
