<script lang="ts">
  // Root shell: wizard on first launch, then boot libsynapsed and poll status.
  // Visited tabs stay mounted. A {#key} on the active id used to destroy the
  // route, so scroll, fields, and NAAN walkers started over on the way back.
  import { onMount, onDestroy } from "svelte";
  import "../styles/global.css";
  import "../styles/dark.css";
  import { activeTab, tabSlideDir, showSetupWizard, startStatusPolling, stopStatusPolling, type TabId } from "../lib/store";
  import { checkFirstLaunch, initEngine } from "../lib/rpc";
  import { loadStationLookFromSettings } from "../lib/stationSkins";
  import { initCellBgm } from "../lib/cellBgm";
  import { DUR_TAB_MS, TAB_SLIDE_PX } from "../lib/hyprEase";
  import { pageBusy } from "../lib/pageLoad";
  import TopBar from "./components/TopBar.svelte";
  import NoticeRail from "./components/NoticeRail.svelte";
  import StatusBar from "./components/StatusBar.svelte";
  import MinerLoader from "./components/MinerLoader.svelte";
  import SetupWizard from "./components/SetupWizard.svelte";
  import Dashboard from "./routes/Dashboard.svelte";
  import Wallet from "./routes/Wallet.svelte";
  import Transfers from "./routes/Transfers.svelte";
  import Blocks from "./routes/Blocks.svelte";
  import Knowledge from "./routes/Knowledge.svelte";
  import Find from "./routes/Find.svelte";
  import NaanAgent from "./routes/NaanAgent.svelte";
  import Harvest from "./routes/Harvest.svelte";
  import Exploits from "./routes/Exploits.svelte";
  import Messages from "./routes/Messages.svelte";
  import Ide from "./routes/Ide.svelte";
  import Network from "./routes/Network.svelte";
  import Rental from "./routes/Rental.svelte";
  import Settings from "./routes/Settings.svelte";
  import GuideDock from "./components/GuideDock.svelte";

  let ready = false;
  // Once a tab has been opened, its component stays alive under the others.
  let mountedTabs: TabId[] = [];

  $: if (!mountedTabs.includes($activeTab)) {
    mountedTabs = [...mountedTabs, $activeTab];
  }

  const reduceMotion =
    typeof matchMedia !== "undefined" &&
    matchMedia("(prefers-reduced-motion: reduce)").matches;

  const tabSlideMs = reduceMotion ? 0 : DUR_TAB_MS;

  $: slideInX = $tabSlideDir * TAB_SLIDE_PX;

  onMount(async () => {
    initCellBgm();
    try {
      await bootEngine();
      const isFirst = await checkFirstLaunch();
      if (isFirst) showSetupWizard.set(true);
    } catch {
      showSetupWizard.set(true);
    }
    try {
      await loadStationLookFromSettings();
    } catch {}
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
    <MinerLoader width={280} label="Booting node" />
    <div class="boot-word">SYNAPSENET</div>
    <div class="boot-sub">BOOTING NODE</div>
  </div>
{:else if $showSetupWizard}
  <SetupWizard on:complete={onSetupComplete} />
  <NoticeRail />
{:else}
  <div class="app-layout">
    <TopBar />
    <div class="app-body">
      <main class="app-content">
        {#if $pageBusy}
          <div class="page-load" role="status" aria-label="Loading"></div>
        {/if}
        {#each mountedTabs as id (id)}
          <div
            class="tab-pane"
            class:is-live={id === $activeTab}
            class:tab-slide={id === $activeTab && tabSlideMs > 0}
            data-tab={id}
            style:--tab-in={slideInX + "px"}
            style:--tab-slide-ms={tabSlideMs + "ms"}
            aria-hidden={id !== $activeTab}
            inert={id !== $activeTab}
          >
            {#if id === "dashboard"}
              <Dashboard />
            {:else if id === "wallet"}
              <Wallet />
            {:else if id === "transfers"}
              <Transfers />
            {:else if id === "blocks"}
              <Blocks />
            {:else if id === "knowledge"}
              <Knowledge />
            {:else if id === "find"}
              <Find />
            {:else if id === "naan"}
              <NaanAgent />
            {:else if id === "harvest"}
              <Harvest />
            {:else if id === "exploits"}
              <Exploits />
            {:else if id === "messages"}
              <Messages />
            {:else if id === "ide"}
              <Ide />
            {:else if id === "network"}
              <Network />
            {:else if id === "rental"}
              <Rental />
            {:else if id === "settings"}
              <Settings />
            {/if}
          </div>
        {/each}
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
    <GuideDock />
    <StatusBar />
  </div>
  <NoticeRail />
{/if}

<style>
  .boot-screen {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 18px;
    width: 100vw;
    height: 100vh;
    background: #000000;
    animation: boot-in var(--dur-tab) var(--ease-menu) both;
  }

  .boot-word {
    font-family: var(--font);
    font-size: 15px;
    letter-spacing: 0;
    color: var(--text-primary);
  }

  .boot-sub {
    font-family: var(--font);
    font-size: 8px;
    letter-spacing: 0;
    color: var(--text-faint);
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
    background: var(--bg);
  }

  .app-body {
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
  }

  /* No outgoing fade and no paint containment: both flash a black layer on WebView2.
     Hidden panes stay in layout (visibility, not display:none) so scroll, the editor,
     and the NAAN canvas keep their size and in-memory positions. */
  .tab-pane {
    position: absolute;
    inset: 0;
    overflow: hidden;
    background: var(--bg);
    visibility: hidden;
    pointer-events: none;
    z-index: 0;
  }

  .tab-pane.is-live {
    visibility: visible;
    pointer-events: auto;
    z-index: 1;
  }

  .tab-pane.tab-slide {
    animation: tab-slide var(--tab-slide-ms) var(--ease-menu) backwards;
  }

  @keyframes tab-slide {
    from { transform: translate3d(var(--tab-in), 0, 0); }
    to { transform: translate3d(0, 0, 0); }
  }

  /* Pixel mascot slot: keep header.gif, frame it as a hotbar cell (square, hairline). */
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
    border: 1px solid var(--border);
    border-radius: 0;
    background: #000000;
  }

  .mascot {
    width: 100%;
    height: 100%;
    object-fit: contain;
    image-rendering: pixelated;
    background: transparent;
  }
</style>
