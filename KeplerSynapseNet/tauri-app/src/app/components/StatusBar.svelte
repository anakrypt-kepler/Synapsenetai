<script lang="ts">
  import { onMount } from "svelte";
  import { nodeStatus, connectionLabel } from "../../lib/store";
  import { cellBgmOn, initCellBgm, toggleCellBgm } from "../../lib/cellBgm";

  $: isTor = $nodeStatus.connection === "tor";
  $: isConnected = $nodeStatus.connection !== "disconnected";
  $: bgmOn = $cellBgmOn;

  onMount(() => {
    initCellBgm();
  });
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
  <div class="statusbar-item music">
    <button
      type="button"
      class="bgm-btn"
      class:muted={!bgmOn}
      title={bgmOn ? "Lullaby on — click to mute" : "Lullaby muted — click to play"}
      aria-label={bgmOn ? "Mute lullaby" : "Play lullaby"}
      aria-pressed={bgmOn}
      on:click={toggleCellBgm}
    >
      <svg class="bgm-ico" viewBox="0 0 12 9" width="12" height="9" aria-hidden="true" style="pointer-events:none">
        <path fill="currentColor" d="M0 3h2l3-3v9L2 6H0z" />
        {#if bgmOn}
          <path fill="currentColor" d="M7 2h1v5H7zM9 1h1v7H9z" />
        {:else}
          <path fill="currentColor" d="M7 2h1v1H7zM9 2h1v1H9zM8 3h1v1H8zM8 5h1v1H8zM7 6h1v1H7zM9 6h1v1H9z" />
        {/if}
      </svg>
    </button>
  </div>
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

  .statusbar-item.music {
    margin-left: auto;
    border-right: none;
    border-left: 1px solid var(--border);
    padding: 0 6px;
  }

  .statusbar-item.right {
    border-right: none;
  }

  .statusbar-item:last-child {
    border-right: none;
  }

  .bgm-btn {
    width: 18px;
    height: 18px;
    padding: 0;
    display: grid;
    place-items: center;
    border: 1px solid rgba(0, 229, 255, 0.42);
    background: #000000;
    color: var(--cy);
    flex-shrink: 0;
    image-rendering: pixelated;
  }

  .bgm-btn:hover:not(:disabled) {
    border-color: var(--cy);
    background: rgba(0, 229, 255, 0.1);
  }

  .bgm-btn.muted {
    color: var(--text-faint);
    border-color: var(--border);
  }

  .bgm-btn.muted:hover:not(:disabled) {
    border-color: rgba(255, 255, 255, 0.4);
    background: var(--accent-muted);
    color: var(--text-secondary);
  }

  .bgm-ico {
    display: block;
    image-rendering: pixelated;
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
