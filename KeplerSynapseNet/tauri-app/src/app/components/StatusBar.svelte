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
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
    padding: 0 12px;
    font-size: 11px;
    color: var(--text-secondary);
    flex-shrink: 0;
    gap: 0;
    letter-spacing: 0.04em;
    font-weight: 500;
    text-transform: uppercase;
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
    border-radius: var(--radius-full);
    background: var(--err);
    flex-shrink: 0;
  }

  .conn-indicator.connected {
    background: var(--text-primary);
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
