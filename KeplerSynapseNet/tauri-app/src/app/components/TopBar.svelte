<script lang="ts">
  import { activeTab, tabs, type TabId } from "../../lib/store";
  import ThemeToggle from "./ThemeToggle.svelte";

  function setTab(id: TabId) {
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
  <div class="topbar-right">
    <ThemeToggle />
  </div>
</nav>

<style>
  .topbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    height: 38px;
    border-bottom: 1px solid var(--border);
    background: var(--surface);
    padding: 0 6px;
    flex-shrink: 0;
    box-shadow: var(--shadow-sm);
  }

  .topbar-tabs {
    display: flex;
    align-items: stretch;
    gap: 0;
    overflow-x: auto;
    height: 100%;
  }

  .tab-btn {
    border: none;
    border-radius: 0;
    padding: 0 14px;
    font-size: 12px;
    font-weight: 500;
    color: var(--text-secondary);
    background: none;
    white-space: nowrap;
    height: 38px;
    position: relative;
    transition: color 0.15s ease;
  }

  .tab-btn:hover {
    color: var(--text-primary);
    border: none;
    background: var(--surface-alt);
  }

  .tab-btn.active {
    color: var(--text-primary);
    border: none;
    background: none;
  }

  .tab-btn.active::after {
    content: "";
    position: absolute;
    bottom: 0;
    left: 4px;
    right: 4px;
    height: 2px;
    background: var(--accent);
    border-radius: 2px 2px 0 0;
  }

  .topbar-right {
    display: flex;
    align-items: center;
    flex-shrink: 0;
  }
</style>
