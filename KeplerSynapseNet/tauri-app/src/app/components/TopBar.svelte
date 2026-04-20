<script lang="ts">
  import { activeTab, tabs, type TabId } from "../../lib/store";

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
</nav>

<style>
  .topbar {
    display: flex;
    align-items: center;
    height: 32px;
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
    height: 100%;
    width: 100%;
  }

  .tab-btn {
    border: none;
    border-right: 1px solid var(--border);
    border-radius: 0;
    padding: 0 12px;
    font-size: 8px;
    font-weight: 700;
    color: var(--text-secondary);
    background: none;
    white-space: nowrap;
    height: 32px;
    letter-spacing: 1px;
  }

  .tab-btn:hover {
    color: var(--text-primary);
    border: none;
    border-right: 1px solid var(--border);
    background: var(--accent-muted);
  }

  .tab-btn.active {
    color: #000000;
    background: var(--text-primary);
    border: none;
    border-right: 1px solid var(--border);
  }
</style>
