<script lang="ts">
  import { onMount, tick } from "svelte";
  import { activeTab, tabSlideDir, tabs, type TabId } from "../../lib/store";

  let row: HTMLElement;
  let pillX = 0;
  let pillW = 0;
  let pillOn = false;

  function setTab(id: TabId) {
    if (id === $activeTab) return;
    const from = tabs.findIndex((t) => t.id === $activeTab);
    const to = tabs.findIndex((t) => t.id === id);
    tabSlideDir.set(to > from ? 1 : -1);
    activeTab.set(id);
  }

  async function layoutPill() {
    await tick();
    const active = row?.querySelector(".tab-btn.active") as HTMLElement | null;
    if (!active || !row) return;
    const rr = row.getBoundingClientRect();
    const ar = active.getBoundingClientRect();
    pillX = ar.left - rr.left + row.scrollLeft;
    pillW = ar.width;
    pillOn = true;
  }

  $: $activeTab, layoutPill();

  onMount(() => {
    layoutPill();
    const ro = new ResizeObserver(() => layoutPill());
    if (row) ro.observe(row);
    window.addEventListener("resize", layoutPill);
    return () => {
      ro.disconnect();
      window.removeEventListener("resize", layoutPill);
    };
  });
</script>

<nav class="topbar">
  <div class="topbar-tabs" bind:this={row}>
    <span
      class="tab-pill"
      class:on={pillOn}
      style="transform: translate3d({pillX}px, 0, 0); width: {pillW}px"
    ></span>
    {#each tabs as tab}
      <button
        class="tab-btn"
        class:active={$activeTab === tab.id}
        on:click={() => setTab(tab.id)}
      >
        {tab.label}
      </button>
    {/each}
  </div>
</nav>

<style>
  .topbar {
    display: flex;
    align-items: center;
    height: var(--topbar-h);
    border-bottom: 1px solid var(--border);
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
    padding: 0;
    flex-shrink: 0;
  }

  .topbar-tabs {
    display: flex;
    align-items: center;
    gap: 4px;
    overflow-x: auto;
    overflow-y: hidden;
    height: 100%;
    width: 100%;
    min-width: 0;
    padding: 0 10px;
    position: relative;
  }

  .tab-pill {
    position: absolute;
    top: 50%;
    left: 0;
    height: 30px;
    margin-top: -15px;
    border-radius: var(--radius-full);
    background: rgba(255, 255, 255, 0.12);
    pointer-events: none;
    opacity: 0;
    z-index: 0;
    transition: transform var(--dur-fast) var(--ease-menu), opacity var(--dur-fast) var(--ease);
  }

  .tab-pill.on {
    opacity: 1;
  }

  .tab-btn {
    border: none;
    border-radius: var(--radius-full);
    padding: 0 14px;
    font-family: var(--font);
    font-size: 11px;
    font-weight: 600;
    color: var(--text-secondary);
    background: transparent;
    white-space: nowrap;
    height: 30px;
    letter-spacing: 0.06em;
    flex: 0 0 auto;
    position: relative;
    z-index: 1;
    transition:
      color var(--dur-fast) var(--ease-fast),
      background-color var(--dur-fast) var(--ease-fast);
  }

  .tab-btn:hover:not(:disabled) {
    color: var(--text-primary);
    border: none;
    background: rgba(255, 255, 255, 0.06);
  }

  .tab-btn.active {
    color: var(--text-primary);
    background: transparent;
    border: none;
  }
</style>
