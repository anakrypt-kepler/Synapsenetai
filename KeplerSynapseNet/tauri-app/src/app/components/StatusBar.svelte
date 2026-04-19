<script lang="ts">
  import { nodeStatus, connectionColor, connectionLabel } from "../../lib/store";
</script>

<footer class="statusbar">
  <div class="statusbar-item">
    <span class="status-dot {$connectionColor}"></span>
    <span>{$connectionLabel}</span>
  </div>
  <div class="statusbar-divider"></div>
  <div class="statusbar-item">
    {$nodeStatus.peers} peers
  </div>
  <div class="statusbar-divider"></div>
  <div class="statusbar-item">
    {$nodeStatus.balance} NGT
  </div>
  <div class="statusbar-divider"></div>
  <div class="statusbar-item">
    NAAN: {$nodeStatus.naan_state}
  </div>
  <div class="statusbar-divider"></div>
  <div class="statusbar-item">
    {$nodeStatus.version}
  </div>
</footer>

<style>
  .statusbar {
    display: flex;
    align-items: center;
    height: 24px;
    border-top: 1px solid var(--border);
    background: var(--surface);
    padding: 0 12px;
    font-size: 11px;
    color: var(--text-secondary);
    flex-shrink: 0;
    gap: 0;
    letter-spacing: 0.2px;
  }

  .statusbar-item {
    display: flex;
    align-items: center;
    padding: 0 10px;
    white-space: nowrap;
    gap: 4px;
  }

  .statusbar-item:first-child {
    padding-left: 0;
  }

  .statusbar-divider {
    width: 1px;
    height: 10px;
    background: var(--border);
    flex-shrink: 0;
  }
</style>
