<script lang="ts">
  // Shows only while the NAAN harvest loop is running. Left is the miner
  // animation. Right says where the agent is, what it is doing, and its mood.
  // kind "blocks" tells the same moment as a walk onto the local PoE chain.
  import { nodeStatus } from "../../lib/store";
  import MinerLoader from "./MinerLoader.svelte";

  export let kind: "main" | "blocks" = "main";

  const LIVE = new Set(["active", "cooldown", "budget_exhausted", "stopping"]);

  $: state = ($nodeStatus.naan_state || "off").toLowerCase();
  $: live = LIVE.has(state);
  $: task = ($nodeStatus.naan_task || "").trim();
  $: topics = ($nodeStatus.naan_topics || "").trim();
  $: height = $nodeStatus.last_block || 0;
  $: mood = kind === "blocks" ? moodOnChain(state, task, height) : moodOnHarvest(state, task);
  $: where = kind === "blocks"
    ? `Local PoE height ${height}`
    : (topics ? `Tor · ${topics}` : "Tor");
  $: what = task || (kind === "blocks"
    ? "Researching. A filed draft is the step onto the chain."
    : "Harvest loop is running.");

  function moodOnHarvest(st: string, current: string): string {
    const t = current.toLowerCase();
    if (st === "stopping") return "Winding down. Letting the current page finish.";
    if (st === "budget_exhausted") return "Spent for this epoch. Resting until the next tick.";
    if (st === "cooldown" || t.includes("waiting")) return "Between ticks. Waiting for the next topic.";
    if (t.includes("fetch failed")) return "Annoyed. The page did not come back over Tor.";
    if (t.includes("duplicate")) return "Already saw this. Skipping it.";
    if (t.includes("extracted")) return "Pleased. A draft is in.";
    if (t.includes("fetching")) return "Locked in. Pulling a page over Tor.";
    return "Working. The harvest loop is on.";
  }

  function moodOnChain(st: string, current: string, chainHeight: number): string {
    const t = current.toLowerCase();
    if (st === "stopping") return "Stepping off the chain. No new block from this pass.";
    if (t.includes("extracted")) {
      return `Filing the note at height ${chainHeight}. A draft becomes a PoE block, not a mined hash.`;
    }
    if (t.includes("fetching")) return "Out researching. The walk onto the chain happens when the draft is filed.";
    if (t.includes("duplicate") || t.includes("fetch failed")) return "Nothing new to file. The chain stays where it is.";
    return `Walking the local chain at height ${chainHeight}. Research lands here after the harvest.`;
  }
</script>

{#if live}
  <section class="naan-live" aria-label={kind === "blocks" ? "Agent on the chain" : "NAAN agent"}>
    <div class="live-art">
      <MinerLoader width={220} label={kind === "blocks" ? "Agent on the chain" : "NAAN agent working"} />
    </div>
    <div class="live-copy">
      <div class="live-kicker">{kind === "blocks" ? "ON THE CHAIN" : "NAAN AGENT"}</div>
      <div class="live-status">{state.replace(/_/g, " ").toUpperCase()}</div>
      <p class="live-line"><span>WHERE</span> {where}</p>
      <p class="live-line"><span>WHAT</span> {what}</p>
      <p class="live-line"><span>MOOD</span> {mood}</p>
    </div>
  </section>
{/if}

<style>
  .naan-live {
    display: grid;
    grid-template-columns: minmax(160px, 240px) minmax(0, 1fr);
    gap: 18px 22px;
    align-items: center;
    margin: 0 0 14px;
    padding: 8px 0 14px;
    border-top: 1px solid var(--border);
  }

  .live-art {
    min-width: 0;
  }

  .live-copy {
    min-width: 0;
  }

  .live-kicker {
    font-family: var(--font);
    font-size: 9px;
    color: var(--text-secondary);
  }

  .live-status {
    margin: 4px 0 8px;
    font-family: var(--font);
    font-size: 15px;
    color: var(--text-primary);
    line-height: 1.4;
  }

  .live-line {
    margin: 0 0 6px;
    font-family: var(--font-mono);
    font-size: 12px;
    line-height: 1.45;
    color: var(--text-primary);
    overflow-wrap: anywhere;
  }

  .live-line span {
    display: inline-block;
    min-width: 52px;
    margin-right: 8px;
    font-family: var(--font);
    font-size: 8px;
    color: var(--text-secondary);
  }

  @media (max-width: 720px) {
    .naan-live {
      grid-template-columns: 1fr;
    }
  }
</style>
