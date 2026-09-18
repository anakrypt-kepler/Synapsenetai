<script lang="ts">
  // One-screen SET: Tor by default, GGUF load from path, NAAN persist, resources, profile.
  import { onMount } from "svelte";
  import {
    rpcCall,
    updateSettings,
    checkUpdates,
    modelLoad,
    modelUnload,
    getSystemInfo,
    getStatus,
    type SystemInfo,
  } from "../../lib/rpc";
  import ModelCatalog from "../components/ModelCatalog.svelte";
  import PixelStage from "../components/sprites/PixelStage.svelte";
  import PixelSprite from "../components/sprites/PixelSprite.svelte";
  import gearSprite from "../../assets/sprites/gear.svg";
  import SkinPicker from "../components/SkinPicker.svelte";
  import {
    stationCatalog,
    stationLook,
    saveStationLook,
    loadStationLookFromSettings,
    type StationLook,
  } from "../../lib/stationSkins";

  let connectionType = "tor";
  let bridgeLines = "";
  let modelName = "";
  let modelLoaded = false;
  let modelPath = "";
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
  let profileAlias = "";
  let profileAvatarData = "";

  let loadStatus = "";
  let loadOk = false;
  let connStatus = "";
  let connOk = false;
  let modelStatusMsg = "";
  let modelOk = false;
  let resStatus = "";
  let resOk = false;
  let naanStatus = "";
  let naanOk = false;
  let startStatus = "";
  let startOk = false;
  let updateStatus = "";
  let updateOk = false;
  let profileStatus = "";
  let profileOk = false;
  let avatarStatus = "";
  let avatarOk = false;
  let stationStatus = "";
  let stationOk = false;
  let agent = $stationLook.agent;
  let floor = $stationLook.floor;
  let wall = $stationLook.wall;
  let shell = $stationLook.shell;
  let pqcKem = "ML-KEM-768 / KYBER768";
  let pqcSig = "ML-DSA-65 / DILITHIUM3";
  let pqcHashSig = "SLH-DSA-SHA2-128S / SPHINCS+";
  let pqcBackend = "UNKNOWN";
  let pqcMsg = "HYBRID KYBER+X25519 NODE_MSG OVER TOR";
  let pqcReal = false;

  let modelBusy = false;
  let saving = "";

  onMount(async () => {
    await loadAll();
  });

  function failText(e: unknown): string {
    if (e instanceof Error && e.message) return e.message.toUpperCase();
    const s = String(e || "FAILED");
    return s.toUpperCase();
  }

  function parseRpc(raw: string): Record<string, unknown> {
    try {
      const p = JSON.parse(raw);
      if (p && typeof p === "object") return p as Record<string, unknown>;
      return { error: "BAD RESPONSE" };
    } catch {
      return { error: "BAD RESPONSE" };
    }
  }

  function rpcErr(parsed: Record<string, unknown>): string {
    if (parsed.error != null && parsed.error !== "") return String(parsed.error).toUpperCase();
    return "";
  }

  function asBool(v: unknown, fallback: boolean): boolean {
    if (typeof v === "boolean") return v;
    return fallback;
  }

  function asNum(v: unknown, fallback: number): number {
    const n = Number(v);
    return Number.isFinite(n) ? n : fallback;
  }

  function asStr(v: unknown, fallback: string): string {
    return typeof v === "string" ? v : fallback;
  }

  async function refreshModelFromEngine(): Promise<{ ok: boolean; error: string }> {
    try {
      const parsed = parseRpc(await rpcCall("model.status", "{}"));
      const err = rpcErr(parsed);
      if (err) {
        modelLoaded = false;
        return { ok: false, error: err };
      }
      modelLoaded = parsed.loaded === true;
      const name = asStr(parsed.name, "");
      if (name) modelName = name;
      return { ok: true, error: "" };
    } catch (e) {
      modelLoaded = false;
      return { ok: false, error: failText(e) };
    }
  }

  function applyPqc(q: Record<string, unknown>) {
    if (rpcErr(q)) return;
    pqcKem = asStr(q.kem, pqcKem);
    pqcSig = asStr(q.sig, pqcSig);
    pqcHashSig = asStr(q.hash_sig, pqcHashSig);
    pqcBackend = asStr(q.backend, asStr(q.pqc_backend, pqcBackend));
    pqcMsg = asStr(q.msg, pqcMsg);
    pqcReal = q.kyber_real === true && q.dilithium_real === true && q.sphincs_real === true;
    if (!pqcReal && pqcBackend.toLowerCase() === "liboqs") pqcReal = true;
  }

  async function loadPqc() {
    try {
      applyPqc(parseRpc(await rpcCall("crypto.status", "{}")));
    } catch {}
    if (pqcBackend === "UNKNOWN") {
      try {
        applyPqc(parseRpc(await getStatus()));
      } catch {}
    }
  }

  async function loadAll() {
    loadStatus = "LOADING...";
    loadOk = false;
    await loadPqc();
    let sysNote = "";
    try {
      systemInfo = await getSystemInfo();
    } catch (e) {
      sysNote = "SYSTEM INFO: " + failText(e) + ". ";
    }
    try {
      const parsed = parseRpc(await rpcCall("settings.get", "{}"));
      const err = rpcErr(parsed);
      if (err) {
        loadStatus = sysNote + err;
        loadOk = false;
        connectionType = "tor";
        return;
      }
      const ct = asStr(parsed.connection_type, "tor");
      connectionType = (ct === "tor" || ct === "tor_bridges") ? ct : "tor";
      bridgeLines = asStr(parsed.bridge_lines, "");
      modelName = asStr(parsed.model_name, "");
      modelPath = asStr(parsed.model_path, "");
      cpuThreads = asNum(parsed.cpu_threads, 2);
      ramLimitMb = asNum(parsed.ram_limit_mb, 4096);
      diskLimitMb = asNum(parsed.disk_limit_mb, 50000);
      gpuEnabled = asBool(parsed.gpu_enabled, false);
      gpuDevice = asStr(parsed.gpu_device, "");
      gpuLayers = asNum(parsed.gpu_layers, 32);
      if (systemInfo.gpu_devices.length > 0) {
        const known = systemInfo.gpu_devices.some((d) => d.id === gpuDevice);
        if (!known) gpuDevice = systemInfo.gpu_devices[0].id;
        if (!gpuEnabled && !asStr(parsed.gpu_device, "")) {
          gpuEnabled = true;
        }
      }
      naanEnabled = asBool(parsed.naan_enabled, false);
      naanTopics = asStr(parsed.naan_topics, "");
      naanSiteAllowlist = asStr(parsed.naan_site_allowlist, "");
      launchAtLogin = asBool(parsed.launch_at_login, false);
      minimizeToTray = asBool(parsed.minimize_to_tray, false);
      autoUpdate = parsed.auto_update == null ? true : asBool(parsed.auto_update, true);
      profileAlias = asStr(parsed.profile_alias, "");
      profileAvatarData = asStr(parsed.profile_avatar, "");
      const look: StationLook = await loadStationLookFromSettings();
      agent = look.agent;
      floor = look.floor;
      wall = look.wall;
      shell = look.shell;
    } catch (e) {
      loadStatus = sysNote + failText(e);
      loadOk = false;
      connectionType = "tor";
      return;
    }
    const st = await refreshModelFromEngine();
    if (!st.ok) {
      loadStatus = sysNote + "SETTINGS LOADED. MODEL STATUS: " + st.error;
      loadOk = false;
    } else {
      loadStatus = sysNote + "SETTINGS LOADED";
      loadOk = !sysNote;
    }
    await loadPqc();
  }

  async function persist(patch: Record<string, unknown>): Promise<string> {
    const parsed = parseRpc(await updateSettings(JSON.stringify(patch)));
    const err = rpcErr(parsed);
    if (err) return err;
    if (parsed.ok === false) return "SAVE FAILED";
    return "";
  }

  async function saveConnection() {
    connStatus = "SAVING...";
    connOk = false;
    saving = "conn";
    try {
      const patch: Record<string, unknown> = { connection_type: connectionType };
      if (connectionType === "tor_bridges") patch.bridge_lines = bridgeLines;
      const err = await persist(patch);
      if (err) {
        connStatus = err;
        connOk = false;
      } else {
        connStatus = "CONNECTION SAVED";
        connOk = true;
      }
    } catch (e) {
      connStatus = failText(e);
      connOk = false;
    }
    saving = "";
  }

  async function handleModelLoad() {
    modelStatusMsg = "";
    modelOk = false;
    if (!modelPath) {
      modelStatusMsg = "PATH REQUIRED";
      modelOk = false;
      return;
    }
    modelBusy = true;
    modelStatusMsg = "LOADING...";
    try {
      const parsed = parseRpc(await modelLoad(modelPath));
      const err = rpcErr(parsed);
      const st = await refreshModelFromEngine();
      if (err) {
        modelLoaded = false;
        modelStatusMsg = err;
        modelOk = false;
        return;
      }
      if (!st.ok) {
        modelLoaded = false;
        modelStatusMsg = "LOAD STATUS: " + st.error;
        modelOk = false;
        return;
      }
      if (!modelLoaded) {
        modelStatusMsg = "LOAD FAILED";
        modelOk = false;
        return;
      }
      const persistErr = await persist({ model_path: modelPath });
      modelName = asStr(parsed.model, modelName) || modelPath.split("/").pop() || modelPath;
      modelStatusMsg = persistErr ? "MODEL LOADED. PATH SAVE: " + persistErr : "MODEL LOADED";
      modelOk = !persistErr;
    } catch (e) {
      await refreshModelFromEngine();
      modelStatusMsg = failText(e);
      modelOk = false;
    } finally {
      modelBusy = false;
    }
  }

  async function handleModelUnload() {
    modelBusy = true;
    modelStatusMsg = "UNLOADING...";
    modelOk = false;
    try {
      const parsed = parseRpc(await modelUnload());
      const err = rpcErr(parsed);
      const st = await refreshModelFromEngine();
      if (err) {
        modelStatusMsg = err;
        modelOk = false;
        return;
      }
      if (!st.ok) {
        modelLoaded = false;
        modelName = "";
        modelStatusMsg = "UNLOADED. STATUS: " + st.error;
        modelOk = false;
        return;
      }
      if (modelLoaded) {
        modelStatusMsg = "UNLOAD FAILED";
        modelOk = false;
        return;
      }
      modelName = "";
      modelStatusMsg = "MODEL UNLOADED";
      modelOk = true;
    } catch (e) {
      await refreshModelFromEngine();
      modelStatusMsg = failText(e);
      modelOk = false;
    } finally {
      modelBusy = false;
    }
  }

  function onCatalogReady(ev: CustomEvent<{ path: string }>) {
    if (ev.detail?.path) {
      modelPath = ev.detail.path;
      modelName = modelPath.split("/").pop() || modelPath;
      modelLoaded = true;
      modelOk = true;
      modelStatusMsg = "MODEL READY: " + modelName;
    }
  }

  async function selectModelFile() {
    modelStatusMsg = "";
    modelOk = false;
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({ filters: [{ name: "GGUF", extensions: ["gguf"] }], multiple: false });
      if (!selected) return;
      modelPath = typeof selected === "string" ? selected : (selected as { path?: string }).path || "";
      if (!modelPath) {
        modelStatusMsg = "NO PATH SELECTED";
        modelOk = false;
      }
    } catch (e) {
      modelStatusMsg = failText(e);
      modelOk = false;
    }
  }

  async function saveResources() {
    resStatus = "SAVING...";
    resOk = false;
    saving = "res";
    try {
      const err = await persist({
        cpu_threads: cpuThreads,
        ram_limit_mb: ramLimitMb,
        disk_limit_mb: diskLimitMb,
        gpu_enabled: gpuEnabled,
        gpu_device: gpuDevice,
        gpu_layers: gpuLayers,
      });
      if (err) {
        resStatus = err;
        resOk = false;
      } else {
        resStatus = "RESOURCES SAVED";
        resOk = true;
      }
    } catch (e) {
      resStatus = failText(e);
      resOk = false;
    }
    saving = "";
  }

  async function saveNaan() {
    naanStatus = "SAVING...";
    naanOk = false;
    saving = "naan";
    try {
      const err = await persist({
        naan_enabled: naanEnabled,
        naan_topics: naanTopics,
        naan_site_allowlist: naanSiteAllowlist,
      });
      if (err) {
        naanStatus = err;
        naanOk = false;
      } else {
        naanStatus = naanEnabled ? "NAAN ON — AGENT STARTED" : "NAAN OFF — AGENT STOPPED";
        naanOk = true;
      }
    } catch (e) {
      naanStatus = failText(e);
      naanOk = false;
    }
    saving = "";
  }

  async function saveStartup() {
    startStatus = "SAVING...";
    startOk = false;
    saving = "start";
    try {
      const err = await persist({
        launch_at_login: launchAtLogin,
        minimize_to_tray: minimizeToTray,
        auto_update: autoUpdate,
      });
      if (err) {
        startStatus = err;
        startOk = false;
      } else {
        startStatus = "STARTUP SAVED";
        startOk = true;
      }
    } catch (e) {
      startStatus = failText(e);
      startOk = false;
    }
    saving = "";
  }

  async function doCheckUpdates() {
    updateStatus = "CHECKING...";
    updateOk = false;
    try {
      const parsed = parseRpc(await checkUpdates());
      const err = rpcErr(parsed);
      if (err) {
        updateStatus = err;
        updateOk = false;
        return;
      }
      if (parsed.update_available) {
        updateStatus = "UPDATE: " + asStr(parsed.version, "?");
        updateOk = true;
      } else {
        updateStatus = "UP TO DATE " + asStr(parsed.current || parsed.version, "");
        updateOk = true;
      }
    } catch (e) {
      updateStatus = failText(e);
      updateOk = false;
    }
  }

  async function selectAvatar() {
    avatarStatus = "";
    avatarOk = false;
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({
        filters: [{ name: "Image", extensions: ["png", "jpg", "jpeg", "webp"] }],
        multiple: false,
      });
      if (!selected) return;
      const path = typeof selected === "string" ? selected : (selected as { path?: string }).path || "";
      if (!path) {
        avatarStatus = "NO FILE SELECTED";
        avatarOk = false;
        return;
      }
      avatarStatus = "STRIPPING METADATA...";
      const parsed = parseRpc(await rpcCall("profile.set_avatar", JSON.stringify({ path, strip_metadata: true })));
      const err = rpcErr(parsed);
      if (err) {
        avatarStatus = err;
        avatarOk = false;
        return;
      }
      const data = asStr(parsed.avatar_data, "");
      if (data) profileAvatarData = data;
      avatarStatus = data ? "AVATAR SET. METADATA STRIPPED." : "AVATAR SET. METADATA STRIPPED. PREVIEW NOT RETURNED.";
      avatarOk = true;
    } catch (e) {
      avatarStatus = failText(e);
      avatarOk = false;
    }
  }

  async function saveStation() {
    stationStatus = "SAVING...";
    stationOk = false;
    saving = "station";
    try {
      const err = await saveStationLook({ agent, floor, wall, shell });
      if (err) {
        stationStatus = err.toUpperCase();
        stationOk = false;
      } else {
        stationStatus = "STATION SAVED";
        stationOk = true;
      }
    } catch (e) {
      stationStatus = failText(e);
      stationOk = false;
    }
    saving = "";
  }

  async function saveProfile() {
    profileStatus = "SAVING...";
    profileOk = false;
    saving = "profile";
    try {
      const err = await persist({ profile_alias: profileAlias });
      if (err) {
        profileStatus = err;
        profileOk = false;
      } else {
        profileStatus = "PROFILE SAVED";
        profileOk = true;
      }
    } catch (e) {
      profileStatus = failText(e);
      profileOk = false;
    }
    saving = "";
  }
</script>

<div class="content-area ks-set">
  <div class="page-col">
  <PixelStage height={60}>
    <PixelSprite src={gearSprite} anim="rotate" size={24} label="settings" />
  </PixelStage>
  {#if loadStatus}
    <div class="flash" class:ok={loadOk} class:err={!loadOk && !loadStatus.endsWith("...")}>{loadStatus}</div>
  {/if}

  <div class="section-title">Connection</div>
  <div class="card">
    <div class="form-group">
      <label>Type</label>
      <div class="opt-row">
        <button class="obtn" class:selected={connectionType === "tor"} on:click={() => (connectionType = "tor")}>Tor</button>
        <button class="obtn" class:selected={connectionType === "tor_bridges"} on:click={() => (connectionType = "tor_bridges")}>Tor + Bridges</button>
      </div>
    </div>
    <p class="hint">Tor-only. Mesh never uses direct TCP.</p>
    {#if connectionType === "tor_bridges"}
      <div class="form-group">
        <label>Bridge lines</label>
        <textarea bind:value={bridgeLines} rows="3" placeholder="obfs4 bridge lines"></textarea>
      </div>
    {/if}
    <button class="btn-primary" on:click={saveConnection} disabled={saving === "conn"}>Save</button>
    {#if connStatus}<div class="flash" class:ok={connOk} class:err={!connOk && !connStatus.endsWith("...")}>{connStatus}</div>{/if}
  </div>

  <div class="section-title">AI Model</div>
  <div class="card">
    <div class="form-group">
      <label>Status</label>
      <div class="model-row">
        {#if modelLoaded}
          <span class="status-dot green"></span>{modelName || "Loaded"}
          <button class="btn-secondary" on:click={handleModelUnload} disabled={modelBusy}>Unload</button>
        {:else}
          <span class="status-dot red"></span>None
        {/if}
      </div>
    </div>
    <div class="form-group">
      <label>Model path</label>
      <div class="path-row">
        <input type="text" bind:value={modelPath} placeholder=".gguf model path" />
        <button class="btn-secondary" on:click={selectModelFile}>Choose</button>
      </div>
    </div>
    <p class="hint">Harvest works without a GGUF. Download one here for IDE chat and hard captchas.</p>
    <ModelCatalog on:ready={onCatalogReady} />
    <button class="btn-primary" on:click={handleModelLoad} disabled={!modelPath || modelBusy}>Load</button>
    {#if modelStatusMsg}<div class="flash" class:ok={modelOk} class:err={!modelOk && !modelStatusMsg.endsWith("...")}>{modelStatusMsg}</div>{/if}
  </div>

  <div class="section-title">Resources</div>
  <div class="card">
    <div class="form-group">
      <label>CPU threads: {cpuThreads}/{systemInfo.cpu_cores}</label>
      <input type="range" min="1" max={systemInfo.cpu_cores} bind:value={cpuThreads} />
    </div>
    <div class="form-group">
      <label>RAM: {ramLimitMb} MB / {systemInfo.ram_total_mb} MB</label>
      <input type="range" min="512" max={systemInfo.ram_total_mb} step="256" bind:value={ramLimitMb} />
    </div>
    <div class="form-group">
      <label>Disk (MB)</label>
      <input type="number" bind:value={diskLimitMb} min="1000" />
    </div>
    <div class="chk"><label><input type="checkbox" bind:checked={gpuEnabled} /> Enable GPU</label></div>
    <p class="hint">Offload GGUF layers to this GPU for IDE chat and captchas.</p>
    {#if gpuEnabled}
      {#if systemInfo.gpu_devices.length > 0}
        <div class="form-group">
          <label>Device</label>
          <select bind:value={gpuDevice}>
            {#each systemInfo.gpu_devices as dev}
              <option value={dev.id}>{dev.name}{dev.vram_mb ? ` (${dev.vram_mb} MB)` : ""}</option>
            {/each}
          </select>
        </div>
      {:else}
        <div class="warn-text">No GPU detected — llama.cpp will stay on CPU until a device shows up.</div>
      {/if}
      <div class="form-group">
        <label>Layers: {gpuLayers}</label>
        <input type="range" min="0" max="99" bind:value={gpuLayers} />
      </div>
    {/if}
    <button class="btn-primary" on:click={saveResources} disabled={saving === "res"}>Save</button>
    {#if resStatus}<div class="flash" class:ok={resOk} class:err={!resOk && !resStatus.endsWith("...")}>{resStatus}</div>{/if}
  </div>

  <div class="section-title">NAAN Agent</div>
  <div class="card">
    <div class="chk"><label><input type="checkbox" bind:checked={naanEnabled} /> Enable NAAN</label></div>
    <div class="hint">This checkbox starts and stops the agent. Topics below are what it searches.</div>
    <div class="form-group">
      <label>Topics</label>
      <input type="text" bind:value={naanTopics} placeholder="AI, crypto" />
    </div>
    <div class="form-group">
      <label>Site allowlist</label>
      <input type="text" bind:value={naanSiteAllowlist} placeholder="arxiv.org, github.com" />
    </div>
    <button class="btn-primary" on:click={saveNaan} disabled={saving === "naan"}>Save</button>
    {#if naanStatus}<div class="flash" class:ok={naanOk} class:err={!naanOk && !naanStatus.endsWith("...")}>{naanStatus}</div>{/if}
  </div>

  <div class="section-title">Station</div>
  <div class="card">
    <p class="hint">These 2D skins paint the NAAN harvest map. Pick in SET or during first-run setup.</p>
    <div class="station-skins">
      <SkinPicker title="Agent" kind="agent" items={stationCatalog.agents} bind:value={agent} />
      <SkinPicker title="Floor" kind="tile" items={stationCatalog.floors} bind:value={floor} />
      <SkinPicker title="Wall" kind="tile" items={stationCatalog.walls} bind:value={wall} />
      <SkinPicker title="Hull" kind="tile" items={stationCatalog.shells} bind:value={shell} />
    </div>
    <button class="btn-primary" on:click={saveStation} disabled={saving === "station"}>Save</button>
    {#if stationStatus}<div class="flash" class:ok={stationOk} class:err={!stationOk && !stationStatus.endsWith("...")}>{stationStatus}</div>{/if}
  </div>

  <div class="section-title">Profile</div>
  <div class="card">
    <div class="avatar-area">
      {#if profileAvatarData}
        <img src={profileAvatarData} alt="avatar" class="avatar-img" />
      {:else}
        <div class="avatar-placeholder">?</div>
      {/if}
      <button class="btn-secondary" on:click={selectAvatar}>Set avatar</button>
    </div>
    {#if avatarStatus}<div class="flash" class:ok={avatarOk} class:err={!avatarOk && !avatarStatus.endsWith("...")}>{avatarStatus}</div>{/if}
    <div class="form-group">
      <label>Alias (optional)</label>
      <input type="text" bind:value={profileAlias} placeholder="Anonymous" />
    </div>
    <button class="btn-primary" on:click={saveProfile} disabled={saving === "profile"}>Save</button>
    {#if profileStatus}<div class="flash" class:ok={profileOk} class:err={!profileOk && !profileStatus.endsWith("...")}>{profileStatus}</div>{/if}
    <div class="hint">Image is resized, re-encoded, and EXIF/GPS/device tags are dropped. The circle on NET is this avatar; peers receive a 48px copy over Tor, not the original file.</div>
  </div>

  <div class="section-title">Startup</div>
  <div class="card">
    <div class="chk"><label><input type="checkbox" bind:checked={launchAtLogin} /> Launch at login</label></div>
    <div class="chk"><label><input type="checkbox" bind:checked={minimizeToTray} /> Minimize to tray</label></div>
    <div class="chk"><label><input type="checkbox" bind:checked={autoUpdate} /> Auto-update</label></div>
    <div class="hint">These flags are saved. OS autostart, tray, and an update feed are not wired yet.</div>
    <button class="btn-primary" on:click={saveStartup} disabled={saving === "start"}>Save</button>
    {#if startStatus}<div class="flash" class:ok={startOk} class:err={!startOk && !startStatus.endsWith("...")}>{startStatus}</div>{/if}
    <div class="update-row">
      <button class="btn-secondary" on:click={doCheckUpdates}>Check</button>
      {#if updateStatus}<span class="flash inline" class:ok={updateOk} class:err={!updateOk && !updateStatus.endsWith("...")}>{updateStatus}</span>{/if}
    </div>
  </div>

  <div class="section-title">Quantum protection</div>
  <div class="card">
    <div class="sec-row"><span class="sec-label">Handshake KEM</span><span class="sec-val" class:ok={pqcReal} class:warn={!pqcReal}>{pqcKem}</span></div>
    <div class="sec-row"><span class="sec-label">Signatures</span><span class="sec-val" class:ok={pqcReal} class:warn={!pqcReal}>{pqcSig}</span></div>
    <div class="sec-row"><span class="sec-label">Hash sigs</span><span class="sec-val" class:ok={pqcReal} class:warn={!pqcReal}>{pqcHashSig}</span></div>
    <div class="sec-row"><span class="sec-label">Backend</span><span class="sec-val" class:ok={pqcReal} class:warn={!pqcReal}>{pqcBackend.toUpperCase()}</span></div>
    <div class="sec-row"><span class="sec-label">MSG tab</span><span class="sec-val" class:ok={!pqcMsg.toUpperCase().includes("PLAINTEXT") && !pqcMsg.toUpperCase().includes("NOT PQC")} class:warn={pqcMsg.toUpperCase().includes("PLAINTEXT") || pqcMsg.toUpperCase().includes("NOT PQC")}>{pqcMsg.toUpperCase()}</span></div>
    <div class="sec-row"><span class="sec-label">Symmetric</span><span class="sec-val ok">AES-256-GCM</span></div>
    <div class="sec-row"><span class="sec-label">Hash</span><span class="sec-val ok">SHA3-512</span></div>
    <div class="sec-row">
      <span class="sec-label">Tor transport</span>
      <span class="sec-val ok">Enabled</span>
    </div>
    <div class="hint">Handshake, signatures, and Tor are post-quantum or onion-wrapped. MSG is hybrid Kyber+X25519 over Tor when the peer advertised kem_pk; otherwise X25519 crypto_box_seal only (not PQC). The body is not on the wire. Destination onion is still visible to the Tor circuit, like any hidden-service dial.</div>
  </div>
  </div>
</div>

<style>
  .ks-set {
    font-family: var(--font-mono);
    font-size: 12px;
    line-height: 1.5;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    color: var(--text-primary, #f5f5f7);
  }

  .ks-set .card {
    background: none;
    border: none;
    border-top: 1px solid var(--border);
    border-radius: 0;
    padding: 14px 2px;
    margin-bottom: 8px;
  }

  .ks-set .section-title {
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0.02em;
    color: var(--text-secondary, #a1a1a6);
    margin-bottom: 8px;
    margin-top: 22px;
    text-transform: none;
  }

  .ks-set .section-title:first-child {
    margin-top: 0;
  }

  .ks-set :global(.form-group label) {
    font-size: 12px;
    letter-spacing: 0.02em;
    color: var(--text-secondary, #a1a1a6);
    font-weight: 600;
    text-transform: none;
  }

  .ks-set :global(input),
  .ks-set :global(textarea),
  .ks-set :global(select) {
    font-family: inherit;
    font-size: 13px;
    border-radius: var(--radius-sm, 10px);
    padding: 10px 12px;
    background: rgba(0, 0, 0, 0.35);
    border: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    color: var(--text-primary, #f5f5f7);
    transition: border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .ks-set :global(button) {
    font-family: var(--font);
    font-size: 10px;
    font-weight: 400;
    letter-spacing: 0;
    border-radius: 0;
    padding: 9px 16px;
    transition: background var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .opt-row {
    display: flex;
    gap: 8px;
    margin-top: 6px;
    flex-wrap: wrap;
  }

  .ks-set .obtn {
    font-family: var(--font);
    font-size: 10px;
    font-weight: 400;
    padding: 9px 14px;
    border: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    border-radius: 0;
    color: var(--text-secondary, #a1a1a6);
    background: transparent;
    cursor: pointer;
    transition: background var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .ks-set .obtn:hover {
    border-color: rgba(255, 255, 255, 0.22);
    color: var(--text-primary, #f5f5f7);
    background: rgba(255, 255, 255, 0.04);
  }

  .ks-set .obtn.selected {
    border-color: rgba(255, 255, 255, 0.28);
    color: var(--text-primary, #f5f5f7);
    background: rgba(255, 255, 255, 0.12);
  }

  .model-row {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 13px;
    color: var(--text-primary, #f5f5f7);
    margin-top: 4px;
  }

  .status-dot {
    width: 8px;
    height: 8px;
    border-radius: 0;
    image-rendering: pixelated;
  }

  .path-row { display: flex; gap: 8px; }
  .path-row input { flex: 1; }

  .chk { margin-bottom: 8px; }

  .chk label {
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 13px;
    color: var(--text-primary, #f5f5f7);
    cursor: pointer;
  }

  .chk input[type="checkbox"] {
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

  .warn-text {
    font-size: 12px;
    color: var(--warn, #c9a227);
    line-height: 1.6;
    margin: 6px 0;
  }

  .hint {
    margin: 0 0 12px;
    font-size: 12px;
    letter-spacing: 0;
    color: var(--text-secondary, #a1a1a6);
    line-height: 1.5;
  }

  .station-skins {
    display: flex;
    flex-direction: column;
    gap: 16px;
    margin-bottom: 12px;
  }

  .update-row {
    margin-top: 12px;
    padding-top: 12px;
    border-top: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    display: flex;
    align-items: center;
    gap: 10px;
    flex-wrap: wrap;
  }

  .flash {
    margin-top: 10px;
    font-size: 12px;
    letter-spacing: 0;
    line-height: 1.5;
    color: var(--text-secondary, #a1a1a6);
  }

  .flash.inline { margin-top: 0; }

  .flash.ok { color: var(--ok); }
  .flash.err { color: var(--err); }

  .avatar-area {
    display: flex;
    align-items: center;
    gap: 14px;
    margin-bottom: 12px;
  }

  .avatar-img {
    width: 56px;
    height: 56px;
    border: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    border-radius: var(--radius, 14px);
    object-fit: cover;
    image-rendering: auto;
  }

  .avatar-placeholder {
    width: 56px;
    height: 56px;
    border: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    border-radius: var(--radius, 14px);
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 18px;
    color: var(--text-secondary, #a1a1a6);
    background: rgba(255, 255, 255, 0.04);
  }

  .sec-row {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 12px;
    padding: 12px 2px;
    margin-bottom: 8px;
    border: none;
    border-top: 1px solid var(--border);
    border-radius: 0;
    background: none;
  }

  .sec-label {
    font-size: 13px;
    color: var(--text-primary, #f5f5f7);
    font-weight: 600;
  }

  .sec-val {
    font-size: 12px;
    color: var(--text-secondary, #a1a1a6);
    font-family: var(--font-mono, ui-monospace, "SF Mono", "Cascadia Code", "JetBrains Mono", Consolas, monospace);
    text-align: right;
    word-break: break-word;
  }

  .sec-val.ok { color: var(--ok); }
  .sec-val.warn { color: var(--warn, #c9a227); }

  @media (prefers-reduced-motion: reduce) {
    .ks-set .obtn,
    .ks-set :global(button),
    .ks-set :global(input),
    .ks-set :global(textarea),
    .ks-set :global(select) {
      transition: none;
    }
  }
</style>
