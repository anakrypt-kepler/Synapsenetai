<script lang="ts">
  // Harvested page assets from NAAN. Local SHA-256 names only; no source URLs.
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

  interface HarvestBypass {
    cve: string;
    protection: string;
    method: string;
    transport: string;
    ttfb_ms: number;
    bytes: number;
  }

  interface HarvestEntry {
    draft_sha256: string;
    topic: string;
    title: string;
    text: string;
    bypass: HarvestBypass;
    assets: HarvestAsset[];
    node_id_hash: string;
    timestamp: number;
  }

  type VtKind = "clean" | "malicious" | "unknown" | "unchecked" | "none";

  let entries: HarvestEntry[] = [];
  let selected: HarvestEntry | null = null;
  let loading = true;
  let offset = 0;
  let listTotal: number | null = null;
  let brokenImgs: Record<string, boolean> = {};
  const limit = 50;

  $: canPrev = offset > 0;
  $: canNext = listTotal != null
    ? offset + entries.length < listTotal
    : entries.length >= limit;
  $: showNext = listTotal != null || entries.length >= limit;
  $: showPager = entries.length > 0 || offset > 0;

  onMount(() => loadEntries());

  async function loadEntries() {
    loading = true;
    try {
      const raw = await harvestList(offset, limit);
      const parsed = parseListPayload(raw);
      entries = parsed.entries;
      listTotal = parsed.total;
      if (offset < 0) offset = 0;
    } catch {
      entries = [];
      listTotal = null;
    }
    loading = false;
  }

  // harvest.list today is a raw JSON array. Also accept {entries|items} plus optional total.
  function parseListPayload(raw: string): { entries: HarvestEntry[]; total: number | null } {
    let data: unknown;
    try {
      data = JSON.parse(raw);
    } catch {
      return { entries: [], total: null };
    }
    if (Array.isArray(data)) {
      return { entries: data.map(normalizeEntry), total: null };
    }
    if (!data || typeof data !== "object") {
      return { entries: [], total: null };
    }
    const obj = data as Record<string, unknown>;
    if (obj.error) return { entries: [], total: null };
    const arr = obj.entries ?? obj.items ?? obj.harvests;
    const totalRaw = obj.total ?? obj.count ?? obj.total_count;
    const total = typeof totalRaw === "number" && isFinite(totalRaw) && totalRaw >= 0
      ? totalRaw
      : null;
    if (Array.isArray(arr)) {
      return { entries: arr.map(normalizeEntry), total };
    }
    return { entries: [], total };
  }

  function asRecord(v: unknown): Record<string, unknown> {
    return v && typeof v === "object" && !Array.isArray(v)
      ? v as Record<string, unknown>
      : {};
  }

  function str(v: unknown): string {
    return typeof v === "string" ? v : v == null ? "" : String(v);
  }

  function num(v: unknown): number {
    const n = typeof v === "number" ? v : Number(v);
    return isFinite(n) ? n : 0;
  }

  function normalizeAsset(raw: unknown): HarvestAsset {
    const o = asRecord(raw);
    return {
      sha256: str(o.sha256 || o.hash),
      mime: str(o.mime || o.mime_type || o.mimeGuess),
      bytes: num(o.bytes ?? o.size),
      vt: str(o.vt || o.vtVerdict || o.vt_verdict),
      file: str(o.file || o.localPath || o.path),
    };
  }

  function normalizeBypass(raw: unknown): HarvestBypass {
    const o = asRecord(raw);
    return {
      cve: str(o.cve || o.cveId || o.cve_id),
      protection: str(o.protection || o.protectionType),
      method: str(o.method || o.bypassMethod),
      transport: str(o.transport),
      ttfb_ms: num(o.ttfb_ms ?? o.ttfbMs),
      bytes: num(o.bytes),
    };
  }

  function normalizeEntry(raw: unknown): HarvestEntry {
    const o = asRecord(raw);
    const assetsRaw = Array.isArray(o.assets) ? o.assets : [];
    return {
      draft_sha256: str(o.draft_sha256 || o.sha256),
      topic: str(o.topic),
      title: str(o.title),
      text: str(o.text),
      bypass: normalizeBypass(o.bypass),
      assets: assetsRaw.map(normalizeAsset),
      node_id_hash: str(o.node_id_hash || o.nodeIdHash),
      timestamp: num(o.timestamp || o.ts),
    };
  }

  async function selectEntry(entry: HarvestEntry) {
    brokenImgs = {};
    selected = entry;
    if (!entry.draft_sha256) return;
    try {
      const raw = await harvestGet(entry.draft_sha256);
      const parsed = JSON.parse(raw);
      if (!parsed || typeof parsed !== "object" || parsed.error) return;
      selected = normalizeEntry(parsed);
    } catch {
      selected = entry;
    }
  }

  function clearSelection() {
    selected = null;
    brokenImgs = {};
  }

  function goPrev() {
    if (!canPrev) return;
    offset = Math.max(0, offset - limit);
    loadEntries();
  }

  function goNext() {
    if (!canNext) return;
    offset += limit;
    loadEntries();
  }

  function toMillis(ts: number): number {
    if (!ts || ts < 0) return 0;
    if (ts < 1e11) return ts * 1000;
    return ts;
  }

  function fmtTime(ts: number): string {
    const ms = toMillis(ts);
    if (!ms) return "—";
    const d = new Date(ms);
    if (isNaN(d.getTime()) || d.getFullYear() < 2000) return "—";
    return d.toLocaleString("en-US", {
      month: "short", day: "numeric", hour: "2-digit", minute: "2-digit"
    });
  }

  function fmtBytes(b: number): string {
    if (!b || b < 0) return "0B";
    if (b < 1024) return b + "B";
    if (b < 1048576) return (b / 1024).toFixed(1) + "KB";
    return (b / 1048576).toFixed(1) + "MB";
  }

  function shortId(s: string, n = 24): string {
    if (!s) return "—";
    return s.length > n ? s.slice(0, n) + "..." : s;
  }

  // unknown/unchecked are not clean. Only "clean" means a VT hit with 0 malicious.
  function vtKind(vt: string): VtKind {
    const v = (vt || "").toLowerCase().trim();
    if (!v || v === "-" || v === "none") return "none";
    if (v.startsWith("malicious")) return "malicious";
    if (v === "clean" || v === "ok") return "clean";
    if (v === "unknown" || v === "unk" || v === "not_found" || v === "notfound") return "unknown";
    if (v === "unchecked" || v === "unscanned" || v === "pending" || v === "error") return "unchecked";
    return "unchecked";
  }

  function vtLabel(vt: string): string {
    const kind = vtKind(vt);
    if (kind === "clean") return "clean";
    if (kind === "malicious") {
      const n = (vt || "").split(":")[1];
      return n ? "malicious (" + n + ")" : "malicious";
    }
    if (kind === "unknown") return "unknown";
    if (kind === "unchecked") return "unchecked";
    return "no scan";
  }

  function vtColor(vt: string): string {
    const kind = vtKind(vt);
    if (kind === "clean") return "green";
    if (kind === "malicious") return "red";
    if (kind === "unknown") return "orange";
    if (kind === "unchecked") return "yellow";
    return "gray";
  }

  function worstVt(assets: HarvestAsset[]): string {
    if (!assets.length) return "";
    const rank: Record<VtKind, number> = {
      malicious: 4, unknown: 3, unchecked: 2, clean: 1, none: 0
    };
    let worst = assets[0].vt;
    let best = rank[vtKind(worst)] || 0;
    for (const a of assets) {
      const r = rank[vtKind(a.vt)] || 0;
      if (r > best) {
        best = r;
        worst = a.vt;
      }
    }
    return worst;
  }

  function hasCve(cve: string): boolean {
    const v = (cve || "").trim().toLowerCase();
    return !!v && v !== "-" && v !== "none" && v !== "null" && v !== "n/a";
  }

  function cveLabel(cve: string): string {
    return cve.replace(/^NAAN-CVE-2026-/, "");
  }

  function isImage(mime: string): boolean {
    return (mime || "").toLowerCase().startsWith("image/");
  }

  function assetSrc(asset: HarvestAsset): string {
    if (!asset.file) return "";
    try {
      return convertFileSrc(asset.file) || "";
    } catch {
      return "";
    }
  }

  function markBroken(sha: string) {
    if (!sha || brokenImgs[sha]) return;
    brokenImgs = { ...brokenImgs, [sha]: true };
  }

  function showImg(asset: HarvestAsset): boolean {
    return isImage(asset.mime) && !!assetSrc(asset) && !brokenImgs[asset.sha256 || asset.file];
  }

  // Strip source URLs from any displayed text. Do not render url fields.
  function redactUrls(s: string): string {
    if (!s) return "";
    return s
      .replace(/\bhttps?:\/\/[^\s<>"'`]+/gi, "[redacted]")
      .replace(/\bwww\.[^\s<>"'`]+/gi, "[redacted]")
      .replace(/\b[a-z0-9.-]+\.onion(?:\/[^\s<>"'`]*)?/gi, "[redacted]");
  }

  function displayText(s: string): string {
    return redactUrls(s);
  }
</script>

{#if selected}
<div class="content-area">
  <button type="button" class="btn-back" on:click={clearSelection}>BACK</button>
  <div class="section-title">{displayText(selected.title) || "UNTITLED"}</div>

  <div class="grid-2">
    <div class="card">
      <div class="card-header">TOPIC</div>
      <div class="card-value">{displayText(selected.topic) || "—"}</div>
    </div>
    <div class="card">
      <div class="card-header">TIME</div>
      <div class="card-value">{fmtTime(selected.timestamp)}</div>
    </div>
  </div>

  {#if hasCve(selected.bypass?.cve)}
  <div class="section-title">BYPASS</div>
  <div class="grid-2">
    <div class="card">
      <div class="card-header">CVE</div>
      <div class="card-value cve-badge">{cveLabel(selected.bypass.cve)}</div>
    </div>
    <div class="card">
      <div class="card-header">METHOD</div>
      <div class="card-value">{displayText(selected.bypass.method) || "—"}</div>
    </div>
  </div>
  <div class="grid-2">
    <div class="card">
      <div class="card-header">TRANSPORT</div>
      <div class="card-value">{displayText(selected.bypass.transport) || "—"}</div>
    </div>
    <div class="card">
      <div class="card-header">TTFB</div>
      <div class="card-value">{selected.bypass.ttfb_ms}ms</div>
    </div>
  </div>
  {/if}

  {#if selected.assets.length > 0}
  <div class="section-title">ASSETS ({selected.assets.length})</div>
  {#if selected.assets.some(a => isImage(a.mime))}
  <div class="img-grid">
    {#each selected.assets.filter(a => isImage(a.mime)) as asset}
      <div class="img-thumb">
        {#if showImg(asset)}
          <img
            src={assetSrc(asset)}
            alt=""
            loading="lazy"
            on:error={() => markBroken(asset.sha256 || asset.file)}
          />
        {:else}
          <div class="img-fallback">
            {asset.mime || "image"} · {fmtBytes(asset.bytes)}
          </div>
        {/if}
        <div class="img-meta">
          <span class="vt-dot" style="background:{vtColor(asset.vt)}"></span>
          {vtLabel(asset.vt)} · {fmtBytes(asset.bytes)}
        </div>
      </div>
    {/each}
  </div>
  {/if}
  <div class="table-wrap">
    <table>
      <thead><tr><th>SHA-256</th><th>TYPE</th><th>SIZE</th><th>VT</th></tr></thead>
      <tbody>
        {#each selected.assets as asset}
          <tr>
            <td class="mono">{shortId(asset.sha256, 16)}</td>
            <td>{asset.mime || "—"}</td>
            <td>{fmtBytes(asset.bytes)}</td>
            <td>
              <span class="vt-dot" style="background:{vtColor(asset.vt)}"></span>
              {vtLabel(asset.vt)}
            </td>
          </tr>
        {/each}
      </tbody>
    </table>
  </div>
  {/if}

  {#if selected.text}
  <div class="section-title">TEXT</div>
  <div class="text-block">{displayText(selected.text)}</div>
  {/if}

  <div class="section-title">IDENTITY</div>
  <div class="card">
    <div class="meta-row">DRAFT: <span class="mono">{shortId(selected.draft_sha256)}</span></div>
    <div class="meta-row">NODE: <span class="mono">{shortId(selected.node_id_hash)}</span></div>
  </div>
</div>

{:else}

<div class="content-area">
  <div class="section-title">KNOWLEDGE HARVEST</div>
  {#if loading}
    <div class="loading">
      <span class="ks-spinner"></span>
      <span>Loading harvests</span>
    </div>
  {:else if entries.length === 0 && offset === 0}
    <div class="empty-state">No harvests yet. Run NAAN.</div>
  {:else if entries.length === 0}
    <div class="empty-state">No more entries.</div>
    <div class="pagination">
      <button type="button" class="btn-primary" disabled={!canPrev} on:click={goPrev}>PREV</button>
    </div>
  {:else}
    <div class="table-wrap">
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
              <td><span class="tag">{displayText(entry.topic) || "—"}</span></td>
              <td class="title-cell">{displayText(entry.title).slice(0, 50) || "—"}</td>
              <td>
                {#if hasCve(entry.bypass?.cve)}
                  <span class="cve-badge">{cveLabel(entry.bypass.cve)}</span>
                {/if}
              </td>
              <td>{entry.assets.length}</td>
              <td>
                {#if entry.assets.length}
                  <span class="vt-dot" style="background:{vtColor(worstVt(entry.assets))}"></span>
                  {vtLabel(worstVt(entry.assets))}
                {:else}
                  <span class="vt-dot" style="background:gray"></span>
                  no scan
                {/if}
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
    {#if showPager}
    <div class="pagination">
      <button type="button" class="btn-primary" disabled={!canPrev} on:click={goPrev}>PREV</button>
      {#if showNext}
        <button type="button" class="btn-primary" disabled={!canNext} on:click={goNext}>NEXT</button>
      {/if}
    </div>
    {/if}
  {/if}
</div>
{/if}

<style>
  .content-area {
    font-family: var(--font);
    font-size: 13px;
    line-height: 1.45;
    color: var(--text-primary);
    background: transparent;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    image-rendering: auto;
    padding-bottom: 84px;
  }

  .section-title {
    font-family: var(--font);
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.08em;
    color: var(--text-secondary);
    margin-bottom: 10px;
    margin-top: 20px;
  }

  .section-title:first-child {
    margin-top: 0;
  }

  .card {
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 16px;
    margin-bottom: 10px;
    transition: border-color var(--dur) var(--ease), box-shadow var(--dur) var(--ease);
  }

  .card:hover {
    border-color: rgba(255, 255, 255, 0.18);
  }

  .card-header {
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.06em;
    color: var(--text-secondary);
    margin-bottom: 6px;
  }

  .card-value {
    font-size: 15px;
    font-weight: 600;
    color: var(--text-primary);
    overflow-wrap: anywhere;
  }

  .content-area :global(button) {
    font-family: var(--font);
    font-size: 13px;
    border-radius: var(--radius-sm);
    image-rendering: auto;
    -webkit-font-smoothing: antialiased;
    transition: background var(--dur) var(--ease), border-color var(--dur) var(--ease), color var(--dur) var(--ease);
  }

  .btn-primary {
    font-weight: 600;
    letter-spacing: 0.04em;
    padding: 8px 16px;
  }

  .btn-back {
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text-primary);
    font-family: var(--font);
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0.04em;
    padding: 8px 14px;
    cursor: pointer;
    margin-bottom: 12px;
    transition: background var(--dur) var(--ease), border-color var(--dur) var(--ease);
  }

  .btn-back:hover {
    background: var(--accent-muted);
    border-color: rgba(255, 255, 255, 0.22);
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
    font-family: var(--font);
    font-size: 13px;
    margin: 0;
  }

  .table-wrap th {
    font-family: var(--font);
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.06em;
    color: var(--text-secondary);
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

  .clickable {
    cursor: pointer;
    transition: background var(--dur) var(--ease);
  }
  .clickable:hover { background: var(--accent-muted); }

  .nowrap { white-space: nowrap; }

  .title-cell {
    max-width: 220px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .cve-badge {
    background: rgba(0, 0, 0, 0.35);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: 2px 8px;
    font-size: 12px;
    font-family: var(--font-mono);
    color: var(--text-primary);
  }

  .vt-dot {
    display: inline-block;
    width: 7px;
    height: 7px;
    border-radius: 50%;
    margin-right: 4px;
  }

  .img-grid {
    display: flex;
    flex-wrap: wrap;
    gap: 10px;
    margin-bottom: 12px;
  }

  .img-thumb {
    width: 132px;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    background: var(--surface);
    padding: 6px;
  }

  .img-thumb img {
    width: 100%;
    height: 88px;
    object-fit: cover;
    display: block;
    border-radius: 8px;
    image-rendering: auto;
  }

  .img-fallback {
    width: 100%;
    height: 88px;
    display: flex;
    align-items: center;
    justify-content: center;
    text-align: center;
    padding: 6px;
    font-size: 11px;
    color: var(--text-secondary);
    word-break: break-word;
  }

  .img-meta {
    font-size: 11px;
    color: var(--text-secondary);
    margin-top: 6px;
    display: flex;
    align-items: center;
    gap: 4px;
  }

  .text-block {
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px;
    font-family: var(--font);
    font-size: 13px;
    line-height: 1.55;
    max-height: 300px;
    overflow-y: auto;
    white-space: pre-wrap;
    word-break: break-word;
    color: var(--text-primary);
  }

  .mono {
    font-family: var(--font-mono);
    font-size: 12px;
  }

  .meta-row {
    font-size: 12px;
    color: var(--text-secondary);
    margin: 6px 0;
  }

  .loading,
  .empty-state {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 10px;
    text-align: center;
    padding: 32px 16px;
    color: var(--text-secondary);
    font-size: 13px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
  }

  .loading .ks-spinner {
    width: 28px;
    height: 28px;
  }

  .pagination {
    display: flex;
    gap: 8px;
    justify-content: center;
    margin-top: 12px;
  }
</style>
