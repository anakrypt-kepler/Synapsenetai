<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import "../styles/dark.css";
  import "../styles/light.css";
  import { activeTab, showSetupWizard, startStatusPolling, stopStatusPolling } from "../lib/store";
  import { checkFirstLaunch, initEngine } from "../lib/rpc";
  import TopBar from "./components/TopBar.svelte";
  import StatusBar from "./components/StatusBar.svelte";
  import SetupWizard from "./components/SetupWizard.svelte";
  import Dashboard from "./routes/Dashboard.svelte";
  import Wallet from "./routes/Wallet.svelte";
  import Transfers from "./routes/Transfers.svelte";
  import Knowledge from "./routes/Knowledge.svelte";
  import NaanAgent from "./routes/NaanAgent.svelte";
  import Ide from "./routes/Ide.svelte";
  import Network from "./routes/Network.svelte";
  import Settings from "./routes/Settings.svelte";

  let ready = false;

  onMount(async () => {
    try {
      const isFirst = await checkFirstLaunch();
      if (isFirst) {
        showSetupWizard.set(true);
      } else {
        await bootEngine();
      }
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
    bootEngine();
  }
</script>

{#if ready}
  {#if $showSetupWizard}
    <SetupWizard on:complete={onSetupComplete} />
  {:else}
    <div class="app-layout">
      <TopBar />
      <main class="app-content">
        {#if $activeTab === "dashboard"}
          <Dashboard />
        {:else if $activeTab === "wallet"}
          <Wallet />
        {:else if $activeTab === "transfers"}
          <Transfers />
        {:else if $activeTab === "knowledge"}
          <Knowledge />
        {:else if $activeTab === "naan"}
          <NaanAgent />
        {:else if $activeTab === "ide"}
          <Ide />
        {:else if $activeTab === "network"}
          <Network />
        {:else if $activeTab === "settings"}
          <Settings />
        {/if}
      </main>
      <StatusBar />
    </div>
  {/if}
{/if}

<style>
  .app-layout {
    display: flex;
    flex-direction: column;
    height: 100vh;
    width: 100vw;
    background: var(--bg);
  }

  .app-content {
    flex: 1;
    min-height: 0;
    overflow: hidden;
  }
</style>
