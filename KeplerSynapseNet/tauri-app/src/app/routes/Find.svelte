<script lang="ts">
  // FIND tab. Picks one search engine already built into the node and runs
  // the query through the session Tor. The client never sends a raw URL.
  import { onMount } from "svelte";
  import { beginPageLoad } from "../../lib/pageLoad";
  import { findEngines, findRead, findSearch } from "../../lib/rpc";

  type Engine = { id: string; name: string; query: boolean };
  type Hit = { title: string; host: string; path: string };

  let engines: Engine[] = [];
  let engineId = "torch";
  let query = "";
  let hits: Hit[] = [];
  let note = "";
  let error = "";
  let busy = false;
  let searched = false;
  let readTitle = "";
  let readText = "";
  let reading = false;
  type PageImage = { alt: string; mime: string; data: string };
  let pageImages: PageImage[] = [];
  let zoom = -1;

  $: chosen = engines.find((engine) => engine.id === engineId);
  $: queryable = chosen ? chosen.query : true;

  onMount(async () => {
    const done = beginPageLoad();
    try {
      const raw = await findEngines();
      const parsed = JSON.parse(raw);
      engines = Array.isArray(parsed.engines) ? parsed.engines : [];
      if (engines.length && !engines.some((engine) => engine.id === engineId)) {
        engineId = engines[0].id;
      }
      if (parsed.error) error = String(parsed.error);
    } catch {
      error = "Engine list unavailable";
    } finally {
      done();
    }
  });

  function onKey(event: KeyboardEvent) {
    if (event.key === "Enter") void runSearch();
  }

  async function runSearch() {
    error = "";
    note = "";
    readTitle = "";
    readText = "";
    pageImages = [];
    zoom = -1;
    if (queryable && !query.trim()) {
      error = "Type a query";
      return;
    }
    busy = true;
    searched = true;
    try {
      const raw = await findSearch(engineId, query.trim());
      const parsed = JSON.parse(raw);
      if (parsed.error) {
        hits = [];
        error = String(parsed.error);
      } else {
        hits = Array.isArray(parsed.results) ? parsed.results : [];
        note = parsed.note ? String(parsed.note) : "";
      }
    } catch {
      hits = [];
      error = "Search failed";
    }
    busy = false;
  }

  async function openHit(index: number) {
    const hit = hits[index];
    if (!hit || reading) return;
    error = "";
    readTitle = hit.title;
    readText = "";
    pageImages = [];
    zoom = -1;
    reading = true;
    try {
      const raw = await findRead(index);
      const parsed = JSON.parse(raw);
      if (parsed.error) {
        error = String(parsed.error);
        readTitle = "";
      } else {
        readText = String(parsed.text || "");
        pageImages = Array.isArray(parsed.images)
          ? parsed.images
              .filter((row: PageImage) => row && typeof row.data === "string" && typeof row.mime === "string")
              .slice(0, 4)
          : [];
      }
    } catch {
      error = "That page did not open";
      readTitle = "";
    }
    reading = false;
  }
</script>

<div class="content-area">
  <div class="page-col">
    <div class="section-title">TOR SEARCH</div>
    <div class="card finder">
      <div class="path-hint">
        Pick an engine already built into this node. The query goes out through Tor.
        This does not start the harvest agent.
      </div>
      <div class="form-group">
        <label for="find-engine">ENGINE</label>
        <select id="find-engine" bind:value={engineId} disabled={busy || engines.length === 0}>
          {#each engines as engine}
            <option value={engine.id}>{engine.name}</option>
          {/each}
        </select>
      </div>
      <div class="form-group">
        <label for="find-query">QUERY</label>
        <input
          id="find-query"
          type="text"
          bind:value={query}
          placeholder={queryable ? "Query" : "Directory. The query is not sent."}
          disabled={busy || !queryable}
          on:keydown={onKey}
        />
        <button class="btn-primary search-go" type="button" on:click={runSearch} disabled={busy || engines.length === 0}>
          {busy ? "SEARCHING" : "SEARCH"}
        </button>
      </div>
      {#if error}<div class="error-msg">{error}</div>{/if}
    </div>

    {#if searched}
      <div class="section-title">RESULTS</div>
      <div class="card">
        {#if note}<div class="path-hint">{note}</div>{/if}
        {#each hits as hit, index}
          <button class="result-item" type="button" on:click={() => openHit(index)} disabled={reading}>
            <div class="result-title">{hit.title}</div>
            <div class="result-meta">{hit.host}{hit.path}</div>
          </button>
        {:else}
          {#if !busy && !error}
            <div class="empty-search">No results</div>
          {/if}
        {/each}
        {#if reading}<div class="path-hint">Opening that page through Tor.</div>{/if}
      </div>
    {/if}

    {#if readText || pageImages.length}
      <div class="section-title">PAGE</div>
      <div class="card">
        <div class="result-title">{readTitle}</div>
        {#if pageImages.length}
          <div class="page-photos">
            {#each pageImages as image, index}
              <button class="photo" type="button" on:click={() => (zoom = index)}>
                <img src={"data:" + image.mime + ";base64," + image.data} alt={image.alt || "Page photo"} />
              </button>
            {/each}
          </div>
        {/if}
        {#if readText}<div class="page-text">{readText}</div>{/if}
      </div>
    {/if}
    {#if zoom >= 0 && pageImages[zoom]}
      <button class="photo-zoom" type="button" on:click={() => (zoom = -1)}>
        <img
          src={"data:" + pageImages[zoom].mime + ";base64," + pageImages[zoom].data}
          alt={pageImages[zoom].alt || "Page photo"}
        />
      </button>
    {/if}
  </div>
</div>

<style>
  .content-area {
    font-family: var(--font-mono);
    font-size: 12px;
    line-height: 1.5;
    color: var(--text-primary);
    padding-bottom: 84px;
  }

  .finder {
    max-width: 460px;
  }

  .search-go {
    margin-top: 10px;
  }

  .path-hint {
    font-size: 12px;
    color: var(--text-secondary);
    line-height: 1.5;
    margin-bottom: 12px;
  }

  .result-item {
    display: block;
    width: 100%;
    text-align: left;
    padding: 12px 0;
    border: none;
    border-bottom: 1px solid var(--border);
    border-radius: 0;
    background: none;
    color: inherit;
    cursor: pointer;
  }

  .result-item:last-child {
    border-bottom: none;
  }

  .result-title {
    font-size: 14px;
    font-weight: 600;
    color: var(--text-primary);
    margin-bottom: 4px;
  }

  .result-meta {
    font-size: 11px;
    color: var(--text-faint);
    word-break: break-all;
  }

  .empty-search {
    color: var(--text-secondary);
  }

  .page-text {
    white-space: pre-wrap;
    word-break: break-word;
    max-height: 320px;
    overflow: auto;
    color: var(--text-secondary);
  }

  .page-photos {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 12px;
  }

  .photo {
    width: 120px;
    height: 90px;
    padding: 0;
    border: 1px solid var(--border);
    background: #000;
    cursor: pointer;
  }

  .photo img,
  .photo-zoom img {
    display: block;
    width: 100%;
    height: 100%;
    object-fit: contain;
    image-rendering: auto;
  }

  .photo-zoom {
    position: fixed;
    inset: 48px 24px 36px;
    z-index: 40;
    padding: 12px;
    border: 1px solid var(--border);
    background: rgba(0, 0, 0, 0.94);
    cursor: zoom-out;
  }
</style>
