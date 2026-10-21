<script lang="ts">
  // Picks an already downloaded GGUF for one role. Empty means the SET model.
  import { onMount } from "svelte";
  import { rpcCall, updateSettings } from "../../lib/rpc";

  export let settingKey: "guide_model_path" | "naan_model_path";
  export let label = "MODEL";

  type Row = { name: string; path: string };

  let rows: Row[] = [];
  let path = "";
  let note = "";
  let busy = false;
  let ready = false;

  function leaf(value: string): string {
    const norm = value.replace(/\\/g, "/");
    const cut = norm.lastIndexOf("/");
    return cut >= 0 ? norm.slice(cut + 1) : norm;
  }

  async function load() {
    try {
      const settings = JSON.parse(await rpcCall("settings.get", "{}"));
      const saved = typeof settings[settingKey] === "string" ? settings[settingKey] : "";
      const cat = JSON.parse(await rpcCall("model.catalog", "{}"));
      const models = Array.isArray(cat.models) ? cat.models : [];
      rows = models
        .filter((row) => row && row.installed === true && typeof row.path === "string" && row.path)
        .map((row) => ({
          name: String(row.name || row.filename || leaf(row.path)),
          path: String(row.path),
        }));
      const match = rows.find((row) => leaf(row.path).toLowerCase() === leaf(saved).toLowerCase());
      path = match ? match.path : "";
      ready = true;
    } catch (e) {
      note = e instanceof Error ? e.message : "MODEL LIST FAILED";
    }
  }

  async function pick() {
    if (!ready) return;
    busy = true;
    note = "";
    try {
      const raw = JSON.parse(await updateSettings(JSON.stringify({ [settingKey]: path })));
      if (raw.error) note = String(raw.error).toUpperCase();
    } catch (e) {
      note = e instanceof Error ? e.message.toUpperCase() : "SAVE FAILED";
    } finally {
      busy = false;
    }
  }

  onMount(() => {
    void load();
  });
</script>

<label class="role-model">
  <span>{label}</span>
  <select bind:value={path} on:change={pick} disabled={busy || !ready}>
    <option value="">Same as SET</option>
    {#each rows as row}
      <option value={row.path}>{row.name}</option>
    {/each}
  </select>
</label>
{#if note}<p class="role-note">{note}</p>{/if}

<style>
  .role-model {
    display: flex;
    flex-direction: column;
    gap: 4px;
    min-width: 0;
    margin-bottom: 8px;
  }

  .role-model span {
    font-family: var(--font);
    font-size: 8px;
    letter-spacing: 0;
    color: var(--text-secondary);
  }

  select {
    width: 100%;
    max-width: 100%;
    box-sizing: border-box;
    border: 1px solid var(--border);
    background: #000000;
    color: var(--text-primary);
    font-family: var(--font-mono);
    font-size: 11px;
    padding: 6px 8px;
  }

  select:disabled {
    opacity: 0.55;
  }

  .role-note {
    margin: 0;
    font-family: var(--font-mono);
    font-size: 11px;
    color: var(--err, #ff5a5a);
  }
</style>
