<script lang="ts">
  import { onMount } from "svelte";
  import { harvestList, harvestGet } from "../../lib/rpc";
  import { convertFileSrc } from "@tauri-apps/api/core";

  interface HarvestAsset {
    sha256: string;
    mime: string;
    bytes: number;
    vt: string;
    file: string;
  }

  interface HarvestEntry {
    draft_sha256: string;
    topic: string;
    title: string;
    text: string;
    bypass: {
      cve: string;
      protection: string;
      method: string;
      transport: string;
      ttfb_ms: number;
      bytes: number;
    };
    assets: HarvestAsset[];
    node_id_hash: string;
    timestamp: number;
  }

  let entries: HarvestEntry[] = [];
  let selected: HarvestEntry | null = null;
  let loading = true;
  let offset = 0;
  const limit = 50;

  onMount(() => loadEntries());

  async function loadEntries() {
    loading = true;
    try {
      const raw = await harvestList(offset, limit);
      entries = JSON.parse(raw);
    } catch { entries = []; }
    loading = false;
  }

  async function selectEntry(entry: HarvestEntry) {
    try {
      const raw = await harvestGet(entry.draft_sha256);
      selected = JSON.parse(raw);
    } catch {
      selected = entry;
    }
  }

  function clearSelection() { selected = null; }

  function fmtTime(ts: number): string {
    return new Date(ts).toLocaleString("en-US", {
      month: "short", day: "numeric", hour: "2-digit", minute: "2-digit"
    });
  }

  function fmtBytes(b: number): string {
    if (b < 1024) return b + "B";
    if (b < 1048576) return (b / 1024).toFixed(1) + "KB";
    return (b / 1048576).toFixed(1) + "MB";
  }

  function vtColor(vt: string): string {
    if (vt === "clean") return "green";
    if (vt.startsWith("malicious")) return "red";
    if (vt === "unknown" || vt === "unchecked") return "yellow";
    return "gray";
  }

  function isImage(mime: string): boolean {
    return mime.startsWith("image/");
  }

  function assetSrc(asset: HarvestAsset): string {
    try { return convertFileSrc(asset.file); } catch { return ""; }
  }
</script>

{#if selected}
<div class="content-area">
  <button class="btn-back" on:click={clearSelection}>[ BACK ]</button>
  <div class="section-title">{selected.title}</div>

  <div class="grid-2">
    <div class="card">
      <div class="card-header">TOPIC</div>
      <div class="card-value">{selected.topic}</div>
    </div>
    <div class="card">
      <div class="card-header">TIME</div>
      <div class="card-value">{fmtTime(selected.timestamp)}</div>
    </div>
  </div>

  {#if selected.bypass.cve}
  <div class="section-title">BYPASS</div>
  <div class="grid-2">
    <div class="card">
      <div class="card-header">CVE</div>
      <div class="card-value cve-badge">{selected.bypass.cve}</div>
    </div>
    <div class="card">
      <div class="card-header">METHOD</div>
      <div class="card-value">{selected.bypass.method}</div>
    </div>
  </div>
  <div class="grid-2">
    <div class="card">
      <div class="card-header">TRANSPORT</div>
      <div class="card-value">{selected.bypass.transport}</div>
    </div>
    <div class="card">
      <div class="card-header">TTFB</div>
      <div class="card-value">{selected.bypass.ttfb_ms}ms</div>
    </div>
  </div>
  {/if}

  {#if selected.assets.length > 0}
  <div class="section-title">ASSETS ({selected.assets.length})</div>
  <div class="img-grid">
    {#each selected.assets.filter(a => isImage(a.mime)) as asset}
      <div class="img-thumb">
        <img src={assetSrc(asset)} alt={asset.sha256.slice(0,8)} loading="lazy" />
        <div class="img-meta">
          <span class="vt-dot" style="background:{vtColor(asset.vt)}"></span>
          {fmtBytes(asset.bytes)}
        </div>
      </div>
    {/each}
  </div>
  <table>
    <thead><tr><th>SHA-256</th><th>TYPE</th><th>SIZE</th><th>VT</th></tr></thead>
    <tbody>
      {#each selected.assets as asset}
        <tr>
          <td class="mono">{asset.sha256.slice(0,16)}...</td>
          <td>{asset.mime}</td>
          <td>{fmtBytes(asset.bytes)}</td>
          <td><span class="vt-dot" style="background:{vtColor(asset.vt)}"></span> {asset.vt}</td>
        </tr>
      {/each}
    </tbody>
  </table>
  {/if}

  {#if selected.text}
  <div class="section-title">TEXT</div>
  <div class="text-block">{selected.text}</div>
  {/if}

  <div class="section-title">IDENTITY</div>
  <div class="card">
    <div class="meta-row">DRAFT: <span class="mono">{selected.draft_sha256.slice(0,24)}...</span></div>
    <div class="meta-row">NODE: <span class="mono">{selected.node_id_hash.slice(0,24)}...</span></div>
  </div>
</div>

{:else}

<div class="content-area">
  <div class="section-title">KNOWLEDGE HARVEST</div>
  {#if loading}
    <div class="loading">LOADING...</div>
  {:else if entries.length === 0}
    <div class="empty-state">NO HARVESTS YET. START NAAN AGENT TO BEGIN MINING.</div>
  {:else}
    <table>
      <thead>
        <tr>
          <th>TIME</th>
          <th>TOPIC</th>
          <th>TITLE</th>
          <th>CVE</th>
          <th>ASSETS</th>
          <th>VT</th>
        </tr>
      </thead>
      <tbody>
        {#each entries as entry}
          <tr class="clickable" on:click={() => selectEntry(entry)}>
            <td class="nowrap">{fmtTime(entry.timestamp)}</td>
            <td><span class="tag">{entry.topic}</span></td>
            <td class="title-cell">{entry.title.slice(0,50)}</td>
            <td>
              {#if entry.bypass?.cve}
                <span class="cve-badge">{entry.bypass.cve.replace("NAAN-CVE-2026-","")}</span>
              {/if}
            </td>
            <td>{entry.assets?.length || 0}</td>
            <td>
              {#if entry.assets?.length}
                {#each entry.assets as a}
                  <span class="vt-dot" style="background:{vtColor(a.vt)}"></span>
                {/each}
              {:else}
                <span class="vt-dot" style="background:gray"></span>
              {/if}
            </td>
          </tr>
        {/each}
      </tbody>
    </table>
    <div class="pagination">
      {#if offset > 0}
        <button class="btn-primary" on:click={() => { offset -= limit; loadEntries(); }}>[ PREV ]</button>
      {/if}
      {#if entries.length >= limit}
        <button class="btn-primary" on:click={() => { offset += limit; loadEntries(); }}>[ NEXT ]</button>
      {/if}
    </div>
  {/if}
</div>
{/if}

<style>
  .btn-back {
    background: none;
    border: 1px solid var(--border);
    color: var(--text-primary);
    font-family: inherit;
    font-size: 9px;
    padding: 4px 8px;
    cursor: pointer;
    margin-bottom: 8px;
  }
  .btn-back:hover { background: var(--bg-hover); }

  .clickable { cursor: pointer; }
  .clickable:hover { background: var(--bg-hover); }

  .nowrap { white-space: nowrap; }

  .title-cell {
    max-width: 200px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .cve-badge {
    background: var(--bg-hover);
    border: 1px solid var(--border);
    padding: 1px 4px;
    font-size: 8px;
    font-family: monospace;
    color: var(--text-primary);
  }

  .vt-dot {
    display: inline-block;
    width: 6px;
    height: 6px;
    border-radius: 50%;
    margin-right: 2px;
  }

  .img-grid {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 12px;
  }

  .img-thumb {
    width: 120px;
    border: 1px solid var(--border);
    background: var(--bg-secondary);
    padding: 4px;
  }

  .img-thumb img {
    width: 100%;
    height: 80px;
    object-fit: cover;
    display: block;
  }

  .img-meta {
    font-size: 8px;
    color: var(--text-secondary);
    margin-top: 4px;
    display: flex;
    align-items: center;
    gap: 4px;
  }

  .text-block {
    background: var(--bg-secondary);
    border: 1px solid var(--border);
    padding: 8px;
    font-family: monospace;
    font-size: 8px;
    line-height: 1.6;
    max-height: 300px;
    overflow-y: auto;
    white-space: pre-wrap;
    word-break: break-all;
    color: var(--text-primary);
  }

  .mono { font-family: monospace; font-size: 8px; }

  .meta-row {
    font-size: 8px;
    color: var(--text-secondary);
    margin: 4px 0;
  }

  .loading, .empty-state {
    text-align: center;
    padding: 24px;
    color: var(--text-secondary);
    font-size: 10px;
  }

  .pagination {
    display: flex;
    gap: 8px;
    justify-content: center;
    margin-top: 12px;
  }
</style>
