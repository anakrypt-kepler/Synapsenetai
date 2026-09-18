<script lang="ts">
  import { nodeStatus, connectionLabel } from "../../lib/store";

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
  {#if $nodeStatus.onion}
  <div class="statusbar-item" title={$nodeStatus.onion}>
    HS:{$nodeStatus.onion.slice(0, 8)}
  </div>
  {/if}
  <div class="statusbar-item right">
    {$nodeStatus.version}
  </div>
</footer>

<style>
  .statusbar {
    display: flex;
    align-items: center;
    height: var(--statusbar-h);
    border-top: 1px solid var(--border);
    background: #000000;
    padding: 0 10px;
    font-family: var(--font);
    font-size: 8px;
    color: var(--text-secondary);
    flex-shrink: 0;
    gap: 0;
    letter-spacing: 0;
    font-weight: 400;
    text-transform: uppercase;
    overflow-x: auto;
  }

  .statusbar-item {
    display: flex;
    align-items: center;
    padding: 0 8px;
    white-space: nowrap;
    gap: 6px;
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
    width: 7px;
    height: 7px;
    border-radius: 0;
    background: var(--err);
    flex-shrink: 0;
    image-rendering: pixelated;
  }

  .conn-indicator.connected {
    background: var(--ok);
  }

  @keyframes blink-tor {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.38; }
  }

  .tor-blink {
    animation: blink-tor 2.4s ease-in-out infinite;
  }

  @media (prefers-reduced-motion: reduce) {
    .tor-blink {
      animation: none;
    }
  }
</style>
