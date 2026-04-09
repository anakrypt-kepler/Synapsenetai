<script lang="ts">
  import { onMount } from "svelte";
  import { theme } from "../../lib/theme";
  import { nodeStatus } from "../../lib/store";
  import { rpcCall, updateSettings, checkUpdates, modelLoad, modelUnload, getSystemInfo, type SystemInfo } from "../../lib/rpc";

  let connectionType = "clearnet";
  let bridgeLines = "";

  let currentModel = "";
  let cpuThreads = 2;
  let ramLimitMb = 4096;
  let diskLimitMb = 50000;
  let systemInfo: SystemInfo = { cpu_cores: 4, ram_total_mb: 8192, gpu_devices: [] };

  let gpuEnabled = false;
  let gpuDevice = "";
  let gpuLayers = 32;

  let naanEnabled = false;
  let naanTopics = "";
  let siteAllowlist = "";

  let launchAtLogin = false;
  let minimizeToTray = false;

  let autoUpdate = true;
  let updateStatus = "";
  let currentVersion = "v0.1.0-V4";

  onMount(async () => {
    try {
      systemInfo = await getSystemInfo();
    } catch (e: any) {
      console.error("Failed to get system info:", e);
    }
    try {
      const result = await rpcCall("settings.get", "{}");
      const parsed = JSON.parse(result);
      connectionType = parsed.connection_type || "clearnet";
      bridgeLines = parsed.bridge_lines || "";
      currentModel = parsed.model_name || "";
      cpuThreads = parsed.cpu_threads || Math.floor(systemInfo.cpu_cores / 2);
      ramLimitMb = parsed.ram_limit_mb || 4096;
      diskLimitMb = parsed.disk_limit_mb || 50000;
      naanEnabled = parsed.naan_enabled || false;
      naanTopics = parsed.naan_topics || "";
      siteAllowlist = parsed.naan_allowlist || "";
      launchAtLogin = parsed.launch_at_login || false;
      minimizeToTray = parsed.minimize_to_tray || false;
      autoUpdate = parsed.auto_update !== false;
      gpuEnabled = parsed.gpu_enabled || false;
      gpuDevice = parsed.gpu_device || "";
      gpuLayers = parsed.gpu_layers || 32;
    } catch (e: any) {
      console.error("Failed to load settings:", e);
    }
  });

  async function saveSettings() {
    try {
      await updateSettings(
        JSON.stringify({
          connection_type: connectionType,
          bridge_lines: bridgeLines,
          cpu_threads: cpuThreads,
          ram_limit_mb: ramLimitMb,
          disk_limit_mb: diskLimitMb,
          gpu_enabled: gpuEnabled,
          gpu_device: gpuDevice,
          gpu_layers: gpuLayers,
          naan_enabled: naanEnabled,
          naan_topics: naanTopics,
          naan_allowlist: siteAllowlist,
          launch_at_login: launchAtLogin,
          minimize_to_tray: minimizeToTray,
          auto_update: autoUpdate,
        })
      );
    } catch (e: any) {
      console.error("Failed to save settings:", e);
    }
  }

  async function handleLoadModel() {
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({
        filters: [{ name: "GGUF Models", extensions: ["gguf"] }],
        multiple: false,
      });
      if (selected) {
        const path = typeof selected === "string" ? selected : selected.path;
        await modelLoad(path);
        currentModel = path.split("/").pop() || path;
      }
    } catch (e: any) {
      console.error("Failed to load model:", e);
    }
  }

  async function handleDownloadModel() {
    try {
      await rpcCall("model.download", JSON.stringify({ model: "llama-3b" }));
    } catch (e: any) {
      console.error("Model download failed:", e);
    }
  }

  async function handleUnloadModel() {
    try {
      await modelUnload();
      currentModel = "";
    } catch (e: any) {
      console.error("Failed to unload model:", e);
    }
  }

  async function handleCheckUpdates() {
    updateStatus = "Checking...";
    try {
      const result = await checkUpdates();
      const parsed = JSON.parse(result);
      updateStatus = parsed.available ? `Update available: ${parsed.version}` : "Up to date.";
    } catch (e: any) {
      console.error("Update check failed:", e);
      updateStatus = "Check failed.";
    }
  }
</script>

<div class="content-area">
  <div class="section-title">Connection</div>
  <div class="card">
    <div class="form-group">
      <label id="settings-conn-type-label">Connection Type</label>
      <div class="option-row" role="group" aria-labelledby="settings-conn-type-label">
        <button class="filter-btn" class:active={connectionType === "clearnet"} on:click={() => (connectionType = "clearnet")} aria-pressed={connectionType === "clearnet"}>Clearnet</button>
        <button class="filter-btn" class:active={connectionType === "tor"} on:click={() => (connectionType = "tor")} aria-pressed={connectionType === "tor"}>Tor</button>
        <button class="filter-btn" class:active={connectionType === "tor_bridges"} on:click={() => (connectionType = "tor_bridges")} aria-pressed={connectionType === "tor_bridges"}>Tor+Bridges</button>
      </div>
    </div>
    {#if connectionType === "tor_bridges"}
      <div class="form-group">
        <label for="settings-bridge-lines">Bridge Lines</label>
        <textarea id="settings-bridge-lines" bind:value={bridgeLines} rows="3" placeholder="obfs4 bridge lines"></textarea>
      </div>
    {/if}
  </div>

  <div class="section-title">AI Model</div>
  <div class="card">
    <div class="info-row">
      <span class="info-label">Current Model</span>
      <span class="info-value">{currentModel || "None"}</span>
    </div>
    <div class="button-row">
      <button class="btn-secondary" on:click={handleLoadModel}>Load Different Model</button>
      <button class="btn-secondary" on:click={handleDownloadModel}>Download New</button>
      <button class="btn-secondary" on:click={handleUnloadModel}>Unload</button>
    </div>
  </div>

  <div class="section-title">Resources</div>
  <div class="card">
    <div class="form-group">
      <label for="settings-cpu-threads">CPU Threads ({cpuThreads} / {systemInfo.cpu_cores})</label>
      <input id="settings-cpu-threads" type="range" min="1" max={systemInfo.cpu_cores} bind:value={cpuThreads} />
    </div>
    <div class="form-group">
      <label for="settings-ram-limit">RAM Limit ({ramLimitMb} MB / {systemInfo.ram_total_mb} MB)</label>
      <input id="settings-ram-limit" type="range" min="512" max={systemInfo.ram_total_mb} step="256" bind:value={ramLimitMb} />
    </div>
    <div class="form-group">
      <label for="settings-disk-limit">Disk Space Limit (MB)</label>
      <input id="settings-disk-limit" type="number" bind:value={diskLimitMb} min="1000" />
    </div>
  </div>

  <div class="section-title">GPU Acceleration</div>
  <div class="card">
    <div class="checkbox-group">
      <label>
        <input type="checkbox" bind:checked={gpuEnabled} />
        Enable GPU Offloading (llama.cpp CUDA / Metal / Vulkan)
      </label>
    </div>
    {#if gpuEnabled}
      {#if systemInfo.gpu_devices.length > 0}
        <div class="form-group">
          <label for="settings-gpu-device">GPU Device</label>
          <select id="settings-gpu-device" bind:value={gpuDevice} class="gpu-select">
            {#each systemInfo.gpu_devices as dev}
              <option value={dev.id}>{dev.name} ({dev.vram_mb} MB VRAM)</option>
            {/each}
          </select>
        </div>
      {:else}
        <div class="gpu-note">No GPU detected on this system.</div>
      {/if}
      <div class="form-group">
        <label for="settings-gpu-layers">GPU Layers: {gpuLayers} / 64</label>
        <input id="settings-gpu-layers" type="range" min="0" max="64" bind:value={gpuLayers} />
      </div>
      <div class="gpu-note">Number of model layers offloaded to GPU VRAM. Higher = faster, more VRAM.</div>
    {/if}
  </div>

  <div class="section-title">NAAN Agent</div>
  <div class="card">
    <div class="checkbox-group">
      <label>
        <input type="checkbox" bind:checked={naanEnabled} />
        Enable NAAN Agent
      </label>
    </div>
    <div class="form-group">
      <label for="settings-naan-topics">Topic Preferences</label>
      <input id="settings-naan-topics" type="text" bind:value={naanTopics} placeholder="AI, cryptography, distributed systems" />
    </div>
    <div class="form-group">
      <label for="settings-site-allowlist">Site Allowlist (one per line, clearnet + onion)</label>
      <textarea id="settings-site-allowlist" bind:value={siteAllowlist} rows="4" placeholder="example.com&#10;abcdef.onion"></textarea>
    </div>
  </div>

  <div class="section-title">Startup</div>
  <div class="card">
    <div class="checkbox-group">
      <label>
        <input type="checkbox" bind:checked={launchAtLogin} />
        Launch at login
      </label>
    </div>
    <div class="checkbox-group">
      <label>
        <input type="checkbox" bind:checked={minimizeToTray} />
        Minimize to tray
      </label>
    </div>
  </div>

  <div class="section-title">Updates</div>
  <div class="card">
    <div class="info-row">
      <span class="info-label">Current Version</span>
      <span class="info-value">{currentVersion}</span>
    </div>
    <div class="checkbox-group">
      <label>
        <input type="checkbox" bind:checked={autoUpdate} />
        Auto-update
      </label>
    </div>
    <div class="button-row">
      <button class="btn-secondary" on:click={handleCheckUpdates}>Check Now</button>
      {#if updateStatus}
        <span class="update-status">{updateStatus}</span>
      {/if}
    </div>
  </div>

  <div class="section-title">Theme</div>
  <div class="card">
    <div class="option-row" role="group" aria-label="Theme selection">
      <button class="filter-btn" class:active={$theme === "dark"} on:click={() => theme.set("dark")} aria-pressed={$theme === "dark"}>Dark</button>
      <button class="filter-btn" class:active={$theme === "light"} on:click={() => theme.set("light")} aria-pressed={$theme === "light"}>Light</button>
    </div>
  </div>

  <div class="save-row">
    <button class="btn-primary" on:click={saveSettings}>Save Settings</button>
  </div>
</div>

<style>
  .option-row {
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

  .info-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 10px;
  }

  .info-label {
    font-size: 11px;
    color: var(--text-secondary);
  }

  .info-value {
    font-size: 12px;
    color: var(--text-primary);
  }

  .button-row {
    display: flex;
    gap: 8px;
    align-items: center;
    margin-top: 8px;
    flex-wrap: wrap;
  }

  .checkbox-group {
    margin-bottom: 8px;
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

  .update-status {
    font-size: 11px;
    color: var(--text-secondary);
  }

  .save-row {
    margin-top: 20px;
    padding-bottom: 20px;
  }

  .gpu-select {
    width: 100%;
    font-family: 'JetBrains Mono', monospace;
    font-size: 12px;
    padding: 6px 10px;
    border: 1px solid var(--border);
    border-radius: 4px;
    background: var(--bg);
    color: var(--text-primary);
    margin-top: 4px;
  }

  .gpu-note {
    font-size: 11px;
    color: var(--text-secondary);
    margin-top: 6px;
    line-height: 1.5;
  }
</style>
