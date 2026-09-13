<script lang="ts">
  // Curated HuggingFace GGUF list. Engine allowlists ids; UI never sends a raw URL.
  import { createEventDispatcher, onDestroy, onMount } from "svelte";
  import { rpcCall, modelLoad } from "../../lib/rpc";

  const dispatch = createEventDispatcher();

  export let compact = false;

  interface CatalogRow {
    id: string;
    name: string;
    filename: string;
    size_bytes: number;
    ram_mb: number;
    note: string;
    installed: boolean;
    path: string;
  }

  let models: CatalogRow[] = [];
  let dir = "";
  let note = "";
  let err = "";
  let busyId = "";
  let dl = {
    running: false,
    done: false,
    failed: false,
    pct: 0,
    bytes: 0,
    total: 0,
    id: "",
    error: "",
    path: "",
  };
  let poll: ReturnType<typeof setInterval> | null = null;
  let emittedPath = "";

  onMount(() => {
    refresh();
  });

  onDestroy(() => {
    if (poll) clearInterval(poll);
  });

  function ensurePoll() {
    if (poll) return;
    poll = setInterval(() => { tick(); }, 1000);
  }

  function stopPollIfIdle() {
    if (dl.running) return;
    if (poll) {
      clearInterval(poll);
      poll = null;
    }
  }

  function fmtMb(n: number): string {
    if (!Number.isFinite(n) || n <= 0) return "0 MB";
    return (n / (1024 * 1024)).toFixed(0) + " MB";
  }

  async function refresh() {
    try {
      const cat = JSON.parse(await rpcCall("model.catalog", "{}"));
      models = Array.isArray(cat.models) ? cat.models : [];
      dir = typeof cat.dir === "string" ? cat.dir : "";
      note = typeof cat.note === "string" ? cat.note : "";
      err = cat.error ? String(cat.error) : "";
    } catch (e) {
      err = e instanceof Error ? e.message : String(e);
    }
    await tick();
  }

  async function tick() {
    try {
      const st = JSON.parse(await rpcCall("model.download.status", "{}"));
      dl = {
        running: !!st.running,
        done: !!st.done,
        failed: !!st.failed,
        pct: Number(st.pct) || 0,
        bytes: Number(st.bytes) || 0,
        total: Number(st.total) || 0,
        id: typeof st.id === "string" ? st.id : "",
        error: typeof st.error === "string" ? st.error : "",
        path: typeof st.path === "string" ? st.path : "",
      };
      if (dl.done && !dl.failed && dl.path && dl.path !== emittedPath) {
        emittedPath = dl.path;
        dispatch("ready", { path: dl.path, id: dl.id });
      }
      if (dl.running) ensurePoll();
      else stopPollIfIdle();
    } catch {
      // Engine may still be booting.
    }
  }

  async function startDownload(id: string) {
    err = "";
    busyId = id;
    try {
      const raw = JSON.parse(await rpcCall("model.download", JSON.stringify({ id, via: "direct" })));
      if (raw.error) {
        err = String(raw.error).toUpperCase();
        return;
      }
      if (raw.already && raw.path) {
        dispatch("ready", { path: raw.path, id });
        await refresh();
        return;
      }
      await tick();
      ensurePoll();
    } catch (e) {
      err = e instanceof Error ? e.message.toUpperCase() : "DOWNLOAD FAILED";
    } finally {
      busyId = "";
    }
  }

  async function useInstalled(row: CatalogRow) {
    if (!row.path) return;
    err = "";
    busyId = row.id;
    try {
      const raw = JSON.parse(await modelLoad(row.path));
      if (raw.error) {
        err = String(raw.error).toUpperCase();
        return;
      }
      dispatch("ready", { path: row.path, id: row.id });
      await refresh();
    } catch (e) {
      err = e instanceof Error ? e.message.toUpperCase() : "LOAD FAILED";
    } finally {
      busyId = "";
    }
  }

  async function cancelDownload() {
    try {
      await rpcCall("model.download.cancel", "{}");
      await tick();
    } catch {}
  }
</script>

<div class="catalog" class:compact>
  <p class="note">{note || "Harvest works without a GGUF. Load one for IDE chat and hard captchas."}</p>
  {#if dir}<p class="dir">Save dir: {dir}</p>{/if}
  {#if err}<p class="err">{err}</p>{/if}

  {#each models as row}
    <div class="row" class:active={dl.id === row.id && dl.running}>
      <div class="meta">
        <span class="name">
          {#if busyId === row.id && !(dl.running && dl.id === row.id)}
            <span class="ks-spinner" aria-hidden="true"></span>
          {/if}
          {row.name}
        </span>
        <span class="size">{fmtMb(row.size_bytes)} · ~{row.ram_mb} MB RAM</span>
        <span class="desc">{row.note}</span>
      </div>
      <div class="actions">
        {#if row.installed}
          <button class="btn-secondary" disabled={busyId === row.id} on:click={() => useInstalled(row)}>Use</button>
        {:else if dl.running && dl.id === row.id}
          <button class="btn-secondary" on:click={cancelDownload}>Cancel</button>
        {:else}
          <button class="btn-primary" disabled={dl.running || busyId === row.id} on:click={() => startDownload(row.id)}>Download</button>
        {/if}
      </div>
      {#if dl.id === row.id && (dl.running || dl.failed)}
        <div class="dl-line">
          {#if dl.running}
            <span class="ks-spinner" aria-hidden="true"></span>
          {/if}
          <div class="bar-wrap">
            <div class="bar" style="width: {Math.min(100, dl.pct)}%"></div>
          </div>
        </div>
        <span class="prog">
          {#if dl.failed}Fail: {dl.error || "ERROR"}
          {:else}{dl.pct}% · {fmtMb(dl.bytes)} / {fmtMb(dl.total)}
          {/if}
        </span>
      {/if}
    </div>
  {/each}
</div>

<style>
  /* Rows are glass chips; bar width is real engine pct, never faked. */
  .catalog {
    display: flex;
    flex-direction: column;
    gap: 10px;
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", "Noto Sans", "Liberation Sans", sans-serif);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    image-rendering: auto;
  }

  .note, .dir {
    margin: 0;
    font-size: 12px;
    color: var(--text-secondary, #a1a1a6);
    line-height: 1.5;
  }

  .dir {
    color: var(--text-faint, #6e6e73);
    font-family: var(--font-mono, ui-monospace, "SF Mono", "Cascadia Code", "JetBrains Mono", Consolas, monospace);
    word-break: break-all;
  }

  .err {
    margin: 0;
    font-size: 12px;
    color: var(--err);
  }

  .row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px 12px;
    align-items: center;
    padding: 14px 16px;
    border: 1px solid var(--border, rgba(255, 255, 255, 0.12));
    border-radius: var(--radius, 14px);
    background: rgba(255, 255, 255, 0.04);
    transition: border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      background var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .row.active {
    border-color: rgba(255, 255, 255, 0.28);
    background: rgba(255, 255, 255, 0.08);
  }

  .meta {
    display: flex;
    flex-direction: column;
    gap: 4px;
    flex: 1;
    min-width: 180px;
  }

  .name {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    font-size: 14px;
    font-weight: 600;
    color: var(--text-primary, #f5f5f7);
  }

  .size, .desc {
    font-size: 12px;
    color: var(--text-secondary, #a1a1a6);
  }

  .actions {
    display: flex;
    gap: 8px;
  }

  .dl-line {
    display: flex;
    align-items: center;
    gap: 10px;
    width: 100%;
  }

  .bar-wrap {
    flex: 1;
    height: 6px;
    background: rgba(255, 255, 255, 0.08);
    border-radius: var(--radius-full, 999px);
    overflow: hidden;
  }

  .bar {
    height: 100%;
    background: var(--text-primary, #f5f5f7);
    border-radius: var(--radius-full, 999px);
    transition: width var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .prog {
    font-size: 12px;
    color: var(--text-primary, #f5f5f7);
    width: 100%;
  }

  .ks-spinner {
    width: 20px;
    height: 20px;
    flex-shrink: 0;
  }

  .catalog :global(button) {
    font-family: inherit;
    font-size: 13px;
    font-weight: 600;
    letter-spacing: 0;
    border-radius: var(--radius-sm, 10px);
    padding: 8px 14px;
    transition: background var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .catalog.compact .row {
    padding: 10px 12px;
  }

  .catalog.compact .name {
    font-size: 13px;
  }

  @media (prefers-reduced-motion: reduce) {
    .row, .bar, .catalog :global(button) { transition: none; }
  }
</style>
