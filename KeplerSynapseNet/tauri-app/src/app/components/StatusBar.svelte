<script lang="ts">
  import { nodeStatus, connectionColor, connectionLabel } from "../../lib/store";

  $: isTor = $nodeStatus.connection === "tor";
  $: isConnected = $nodeStatus.connection !== "disconnected";
</script>

<footer class="statusbar">
  <div class="statusbar-item">
    <span class="conn-indicator" class:connected={isConnected} class:tor-blink={isTor}></span>
    <span class:tor-blink={isTor}>{$connectionLabel}</span>
  </div>
  <div class="statusbar-item">
    {$nodeStatus.peers}P
  </div>
  <div class="statusbar-item">
    {$nodeStatus.balance}NGT
  </div>
  <div class="statusbar-item">
    NAAN:{$nodeStatus.naan_state.toUpperCase()}
  </div>
  <div class="statusbar-item">
    BLK#{$nodeStatus.last_block}
  </div>
  <div class="statusbar-item right">
    {$nodeStatus.version}
  </div>
</footer>

<style>
  .statusbar {
    display: flex;
    align-items: center;
    height: 20px;
    border-top: 1px solid var(--border);
    background: #000000;
    padding: 0 8px;
    font-size: 8px;
    color: var(--text-secondary);
    flex-shrink: 0;
    gap: 0;
    letter-spacing: 1px;
    font-weight: 700;
    text-transform: uppercase;
  }

  .statusbar-item {
    display: flex;
    align-items: center;
    padding: 0 8px;
    white-space: nowrap;
    gap: 4px;
    border-right: 1px solid var(--border);
  }

  .statusbar-item:first-child {
    padding-left: 0;
  }

  .statusbar-item.right {
    margin-left: auto;
    border-right: none;
    border-left: 1px solid var(--border);
  }

  .statusbar-item:last-child {
    border-right: none;
  }

  .conn-indicator {
    width: 6px;
    height: 6px;
    background: var(--err);
    flex-shrink: 0;
  }

  .conn-indicator.connected {
    background: var(--text-primary);
  }

  @keyframes blink-tor {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.15; }
  }

  .tor-blink {
    animation: blink-tor 1.5s step-end infinite;
  }
</style>
