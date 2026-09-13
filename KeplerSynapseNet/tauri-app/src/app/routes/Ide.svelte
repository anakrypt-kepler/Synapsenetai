<script lang="ts">
  // Built-in editor. Submit a patch as a PoE code contribution (poe.submit_code).
  // NGT is the acceptance reward, not a hash-mining payout.
  import { onMount } from "svelte";
  import ChatPanel from "../components/ChatPanel.svelte";
  import { rpcCall } from "../../lib/rpc";
  import { activeTab, nodeStatus } from "../../lib/store";

  let files: { name: string; content: string }[] = [
    { name: "main.rs", content: '// SynapseNet code\nfn main() {\n    println!("Hello, SynapseNet!");\n}' },
  ];
  let activeFile = 0;
  let nextUntitled = 2;
  let submitResult = "";
  let submitting = false;
  let poeStats = { current_epoch: 0, total_entries: 0, chain_height: 0, next_epoch_in: "0h 0m" };
  let myCode: { title: string; kind?: string; status: string; ngt_earned: string }[] = [];

  onMount(async () => {
    await loadPoe();
  });

  async function loadPoe() {
    try {
      const raw = await rpcCall("poe.stats", "{}");
      poeStats = { ...poeStats, ...JSON.parse(raw) };
    } catch {}
    try {
      const raw = await rpcCall("knowledge.my_submissions", "{}");
      const parsed = JSON.parse(raw);
      const all = parsed.submissions || [];
      myCode = all.filter((s: { kind?: string }) => (s.kind || "knowledge") === "code");
    } catch {}
  }

  function addFile() {
    files = [...files, { name: `untitled${nextUntitled}.rs`, content: "" }];
    nextUntitled += 1;
    activeFile = files.length - 1;
  }

  async function submitToPoe() {
    const file = files[activeFile];
    if (!file || !file.content.trim()) {
      submitResult = "EMPTY PATCH";
      return;
    }
    submitting = true;
    submitResult = "";
    try {
      const result = await rpcCall(
        "poe.submit_code",
        JSON.stringify({ title: file.name, patch: file.content })
      );
      const parsed = JSON.parse(result);
      if (parsed.error) {
        submitResult = String(parsed.error).toUpperCase();
      } else {
        submitResult = parsed.status === "pending"
          ? "PENDING · NGT ON FINALIZE"
          : (parsed.message || parsed.status || "SUBMITTED");
      }
      await loadPoe();
    } catch {
      submitResult = "FAILED";
    }
    submitting = false;
  }
</script>

<div class="ide-layout">
  <div class="ide-editor">
    <div class="editor-tabs">
      {#each files as file, i}
        <button class="editor-tab" class:active={activeFile === i} type="button" on:click={() => (activeFile = i)}>
          {file.name}
        </button>
      {/each}
      <button class="editor-tab tab-new" type="button" on:click={addFile}>+</button>
    </div>
    <div class="editor-content">
      {#if files[activeFile]}
        <textarea
          class="code-textarea"
          bind:value={files[activeFile].content}
          spellcheck="false"
        ></textarea>
      {/if}
    </div>
    <div class="ide-earn">
      <div class="earn-row">
        <span>Wallet {$nodeStatus.balance} NGT</span>
        <span>Chain #{$nodeStatus.last_block || poeStats.chain_height}</span>
        <span>Epoch {poeStats.current_epoch}</span>
      </div>
      <div class="earn-hint">
        PoE code: submit → recorded on your chain → NGT on finalize (votes), RingCT to stealth. Not hash mining. Author stays public.
      </div>
      {#if myCode.length > 0}
        <div class="code-subs">
          {#each myCode.slice(-4).reverse() as sub}
            <div class="code-sub">
              <span>{sub.title}</span>
              <span>{sub.status}</span>
              <span>{sub.ngt_earned} NGT</span>
            </div>
          {/each}
        </div>
      {/if}
    </div>
    <div class="editor-status">
      <span>LN:{files[activeFile] ? files[activeFile].content.split("\n").length : 0}</span>
      <button class="btn-secondary sub-btn" type="button" on:click={submitToPoe} disabled={submitting}>PoE</button>
      {#if submitResult}
        <span class="sub-result">{submitResult}</span>
      {/if}
    </div>
  </div>
  <div class="ide-chat">
    {#if !$nodeStatus.model_loaded}
      <button class="chat-model-hint" type="button" on:click={() => activeTab.set("settings")}>
        Load GGUF in Settings. LLM is chat, not consensus.
      </button>
    {/if}
    <div class="chat-wrap">
      <ChatPanel />
    </div>
  </div>
</div>

<style>
  /* Glass IDE chrome. Editor well stays a dark mono surface. */
  .ide-layout {
    display: flex;
    height: 100%;
    background: #000000;
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", system-ui, sans-serif);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    image-rendering: auto;
    color: var(--text-primary);
  }

  .ide-editor {
    flex: 1;
    display: flex;
    flex-direction: column;
    border-right: 1px solid var(--border);
    min-width: 0;
  }

  .editor-tabs {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 8px 10px;
    border-bottom: 1px solid var(--border);
    flex-shrink: 0;
    overflow-x: auto;
    background: var(--surface);
    backdrop-filter: blur(22px) saturate(140%);
    -webkit-backdrop-filter: blur(22px) saturate(140%);
  }

  .editor-tab {
    padding: 6px 12px;
    font-family: inherit;
    font-size: 12px;
    font-weight: 500;
    border: 1px solid transparent;
    border-radius: var(--radius-sm, 10px);
    background: transparent;
    color: var(--text-secondary);
    letter-spacing: 0;
    white-space: nowrap;
    image-rendering: auto;
    -webkit-font-smoothing: antialiased;
    transition:
      background var(--dur-fast, 200ms) var(--ease-fast, cubic-bezier(0.34, 0.8, 0.34, 1)),
      color var(--dur-fast, 200ms) var(--ease-fast, cubic-bezier(0.34, 0.8, 0.34, 1)),
      border-color var(--dur-fast, 200ms) var(--ease-fast, cubic-bezier(0.34, 0.8, 0.34, 1));
  }

  .editor-tab:hover:not(:disabled) {
    color: var(--text-primary);
    background: var(--accent-muted);
    border-color: transparent;
  }

  .editor-tab.active {
    background: var(--accent-muted);
    color: var(--text-primary);
    border-color: var(--border);
  }

  .tab-new {
    color: var(--text-secondary);
    min-width: 32px;
    font-size: 16px;
    line-height: 1;
    padding: 4px 10px;
  }

  .editor-content {
    flex: 1;
    overflow: hidden;
    margin: 8px;
    border-radius: var(--radius, 14px);
    border: 1px solid var(--border);
    background: #000000;
    min-height: 0;
  }

  .code-textarea {
    width: 100%;
    height: 100%;
    resize: none;
    border: none;
    border-radius: 0;
    background: #000000;
    color: var(--text-primary);
    font-family: var(--font-mono, ui-monospace, "SF Mono", SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace);
    font-size: 13px;
    font-variant-ligatures: none;
    padding: 12px 14px;
    line-height: 1.55;
    tab-size: 4;
    white-space: pre;
    overflow: auto;
    letter-spacing: 0;
    caret-color: #e8e8ed;
    image-rendering: auto;
    -webkit-font-smoothing: antialiased;
  }

  .code-textarea:focus {
    outline: none;
    border: none;
  }

  .editor-status {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 8px 12px;
    border-top: 1px solid var(--border);
    font-size: 12px;
    color: var(--text-secondary);
    flex-shrink: 0;
    letter-spacing: 0;
    background: var(--surface);
    backdrop-filter: blur(22px) saturate(140%);
    -webkit-backdrop-filter: blur(22px) saturate(140%);
  }

  .sub-btn {
    font-family: inherit;
    font-size: 12px;
    font-weight: 600;
    padding: 6px 14px;
    border-radius: var(--radius-sm, 10px);
    letter-spacing: 0;
    image-rendering: auto;
    transition:
      background var(--dur-fast, 200ms) var(--ease-fast, cubic-bezier(0.34, 0.8, 0.34, 1)),
      color var(--dur-fast, 200ms) var(--ease-fast, cubic-bezier(0.34, 0.8, 0.34, 1)),
      border-color var(--dur-fast, 200ms) var(--ease-fast, cubic-bezier(0.34, 0.8, 0.34, 1));
  }

  .sub-result {
    font-size: 12px;
    color: var(--ok);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    max-width: 420px;
  }

  .ide-earn {
    border-top: 1px solid var(--border);
    padding: 10px 12px;
    flex-shrink: 0;
    display: flex;
    flex-direction: column;
    gap: 6px;
    background: var(--surface);
    backdrop-filter: blur(22px) saturate(140%);
    -webkit-backdrop-filter: blur(22px) saturate(140%);
  }

  .earn-row {
    display: flex;
    gap: 16px;
    font-size: 12px;
    font-weight: 500;
    color: var(--text-primary);
    letter-spacing: 0;
    flex-wrap: wrap;
  }

  .earn-hint {
    font-size: 12px;
    color: var(--text-secondary);
    letter-spacing: 0;
    line-height: 1.45;
  }

  .code-subs {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .code-sub {
    display: flex;
    justify-content: space-between;
    gap: 8px;
    font-size: 12px;
    color: var(--text-secondary);
    letter-spacing: 0;
  }

  .ide-chat {
    width: 320px;
    flex-shrink: 0;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    /* Keep chat above the 56px bottom-right mascot overlay. */
    padding-bottom: 56px;
    background: var(--surface);
    backdrop-filter: blur(22px) saturate(140%);
    -webkit-backdrop-filter: blur(22px) saturate(140%);
  }

  .chat-model-hint {
    flex-shrink: 0;
    border: none;
    border-bottom: 1px solid var(--border);
    border-radius: 0;
    background: transparent;
    color: var(--text-secondary);
    font-family: inherit;
    font-size: 12px;
    letter-spacing: 0;
    line-height: 1.45;
    text-align: left;
    padding: 10px 12px;
    image-rendering: auto;
    -webkit-font-smoothing: antialiased;
  }

  .chat-model-hint:hover:not(:disabled) {
    color: var(--text-primary);
    background: var(--accent-muted);
    border-color: var(--border);
  }

  .chat-wrap {
    flex: 1;
    min-height: 0;
    overflow: hidden;
  }
</style>
