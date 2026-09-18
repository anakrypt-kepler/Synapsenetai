<script lang="ts">
  import { activeTab, tabSlideDir, tabs, type TabId } from "../../lib/store";

  // Real work only: set the active tab + slide direction. The old rounded
  // sliding pill (and its ResizeObserver/layout math) is gone — the active tab
  // is now an inverted pixel block, no geometry to measure.
  function setTab(id: TabId) {
    if (id === $activeTab) return;
    const from = tabs.findIndex((t) => t.id === $activeTab);
    const to = tabs.findIndex((t) => t.id === id);
    tabSlideDir.set(to > from ? 1 : -1);
    activeTab.set(id);
  }
</script>

<nav class="topbar">
  <div class="topbar-tabs">
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
    background: #000000;
    padding: 0;
    flex-shrink: 0;
  }

  .topbar-tabs {
    display: flex;
    align-items: stretch;
    gap: 0;
    overflow-x: auto;
    overflow-y: hidden;
    height: 100%;
    width: 100%;
    min-width: 0;
    padding: 0 6px;
  }

  .tab-btn {
    border: none;
    border-radius: 0;
    padding: 0 8px;
    font-family: var(--font);
    font-size: 9px;
    font-weight: 400;
    color: var(--text-secondary);
    background: transparent;
    white-space: nowrap;
    height: 100%;
    letter-spacing: 0;
    flex: 0 0 auto;
    /* 2px void keeps active blocks from touching; underline sits in the gap. */
    border-bottom: 2px solid transparent;
    transition:
      color var(--dur-fast) var(--ease-fast),
      background-color var(--dur-fast) var(--ease-fast),
      border-color var(--dur-fast) var(--ease-fast);
  }

  .tab-btn:hover:not(:disabled) {
    color: var(--text-primary);
    border: none;
    border-bottom: 2px solid var(--border);
    background: rgba(255, 255, 255, 0.06);
  }

  /* Active = inverted pixel block (Minecraft hotbar selected slot). */
  .tab-btn.active {
    color: #000000;
    background: var(--text-primary);
    border: none;
    border-bottom: 2px solid var(--cy);
  }
</style>
