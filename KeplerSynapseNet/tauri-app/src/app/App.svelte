<script lang="ts">
  // Root shell: wizard on first launch, then boot libsynapsed and poll status.
  // Each tab is a route component; keep tab ids in sync with lib/store.ts.
  import { onMount, onDestroy } from "svelte";
  import { fly, fade } from "svelte/transition";
  import "../styles/global.css";
  import "../styles/dark.css";
  import { activeTab, tabSlideDir, showSetupWizard, startStatusPolling, stopStatusPolling } from "../lib/store";
  import { checkFirstLaunch, initEngine } from "../lib/rpc";
  import { DUR_TAB_MS, TAB_SLIDE_PX, easeMenuDecel } from "../lib/hyprEase";
  import TopBar from "./components/TopBar.svelte";
  import StatusBar from "./components/StatusBar.svelte";
  import KsSpinner from "./components/KsSpinner.svelte";
  import SetupWizard from "./components/SetupWizard.svelte";
  import Dashboard from "./routes/Dashboard.svelte";
  import Wallet from "./routes/Wallet.svelte";
  import Transfers from "./routes/Transfers.svelte";
  import Blocks from "./routes/Blocks.svelte";
  import Knowledge from "./routes/Knowledge.svelte";
  import NaanAgent from "./routes/NaanAgent.svelte";
  import Harvest from "./routes/Harvest.svelte";
  import Exploits from "./routes/Exploits.svelte";
  import Messages from "./routes/Messages.svelte";
  import Ide from "./routes/Ide.svelte";
  import Network from "./routes/Network.svelte";
  import Rental from "./routes/Rental.svelte";
  import Settings from "./routes/Settings.svelte";

  let ready = false;

  const reduceMotion =
    typeof matchMedia !== "undefined" &&
    matchMedia("(prefers-reduced-motion: reduce)").matches;

  const tabSlideMs = reduceMotion ? 0 : DUR_TAB_MS;

  $: slideInX = $tabSlideDir * TAB_SLIDE_PX;

  onMount(async () => {
    try {
      await bootEngine();
      const isFirst = await checkFirstLaunch();
      if (isFirst) showSetupWizard.set(true);
    } catch {
      showSetupWizard.set(true);
    }
    ready = true;
  });

  onDestroy(() => {
    stopStatusPolling();
  });

  async function bootEngine() {
    try {
      await initEngine();
    } catch {}
    startStatusPolling();
  }

  function onSetupComplete() {
    showSetupWizard.set(false);
  }
</script>

{#if !ready}
  <div class="boot-screen">
    <KsSpinner size={56} />
  </div>
{:else if $showSetupWizard}
  <SetupWizard on:complete={onSetupComplete} />
{:else}
  <div class="app-layout">
    <TopBar />
    <div class="app-body">
      <main class="app-content">
        {#key $activeTab}
          <div
            class="tab-pane"
            in:fly={{ x: slideInX, duration: tabSlideMs, easing: easeMenuDecel, opacity: 1 }}
            out:fade={{ duration: reduceMotion ? 0 : 80 }}
          >
            {#if $activeTab === "dashboard"}
              <Dashboard />
            {:else if $activeTab === "wallet"}
              <Wallet />
            {:else if $activeTab === "transfers"}
              <Transfers />
            {:else if $activeTab === "blocks"}
              <Blocks />
            {:else if $activeTab === "knowledge"}
              <Knowledge />
            {:else if $activeTab === "naan"}
              <NaanAgent />
            {:else if $activeTab === "harvest"}
              <Harvest />
            {:else if $activeTab === "exploits"}
              <Exploits />
            {:else if $activeTab === "messages"}
              <Messages />
            {:else if $activeTab === "ide"}
              <Ide />
            {:else if $activeTab === "network"}
              <Network />
            {:else if $activeTab === "rental"}
              <Rental />
            {:else if $activeTab === "settings"}
              <Settings />
            {/if}
          </div>
        {/key}
      </main>
    </div>
    <div class="mascot-slot" aria-hidden="true">
      <img
        class="mascot"
        src="/header.gif?v=alpha"
        alt=""
        draggable="false"
      />
    </div>
    <StatusBar />
  </div>
{/if}

<style>
  .boot-screen {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 100vw;
    height: 100vh;
    background: #000000;
    animation: boot-in var(--dur-tab) var(--ease-menu) both;
  }

  @keyframes boot-in {
    from { opacity: 0; }
    to { opacity: 1; }
  }

  .app-layout {
    position: relative;
    display: flex;
    flex-direction: column;
    height: 100vh;
    width: 100vw;
    background: #000000;
  }

  .app-content {
    flex: 1;
    min-height: 0;
    overflow: hidden;
    position: relative;
    contain: layout paint;
  }

  .app-body {
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
  }

  .tab-pane {
    position: absolute;
    inset: 0;
    overflow: hidden;
    contain: layout paint;
  }

  .mascot-slot {
    position: absolute;
    right: 8px;
    bottom: calc(var(--statusbar-h) + 6px);
    z-index: 8;
    width: var(--mascot-size);
    height: var(--mascot-size);
    overflow: hidden;
    contain: strict;
    isolation: isolate;
    pointer-events: none;
  }

  .mascot {
    width: 100%;
    height: 100%;
    object-fit: contain;
    image-rendering: pixelated;
    background: transparent;
  }
</style>
