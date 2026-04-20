<script lang="ts">
  import ChatPanel from "../components/ChatPanel.svelte";
  import { poeSubmitCode, rpcCall } from "../../lib/rpc";

  let files: { name: string; content: string }[] = [
    { name: "main.rs", content: '// SynapseNet code\nfn main() {\n    println!("Hello, SynapseNet!");\n}' },
  ];
  let activeFile = 0;
  let submitResult = "";

  async function submitToPoe() {
    try {
      const file = files[activeFile];
      const result = await poeSubmitCode(file.name, file.content);
      const parsed = JSON.parse(result);
      submitResult = parsed.message || "SUBMITTED";
    } catch {
      submitResult = "FAILED";
    }
  }
</script>

<div class="ide-layout">
  <div class="ide-editor">
    <div class="editor-tabs">
      {#each files as file, i}
        <button class="editor-tab" class:active={activeFile === i} on:click={() => (activeFile = i)}>
          {file.name}
        </button>
      {/each}
    </div>
    <div class="editor-content">
      <textarea
        class="code-textarea"
        bind:value={files[activeFile].content}
        spellcheck="false"
      ></textarea>
    </div>
    <div class="editor-status">
      <span>LN:{files[activeFile].content.split("\n").length}</span>
      <button class="btn-secondary sub-btn" on:click={submitToPoe}>[ POE ]</button>
      {#if submitResult}
        <span class="sub-result">{submitResult}</span>
      {/if}
    </div>
  </div>
  <div class="ide-chat">
    <ChatPanel />
  </div>
</div>

<style>
  .ide-layout {
    display: flex;
    height: 100%;
    background: #000000;
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
    border-bottom: 1px solid var(--border);
    flex-shrink: 0;
  }

  .editor-tab {
    padding: 4px 12px;
    font-size: 8px;
    border: none;
    border-right: 1px solid var(--border);
    border-radius: 0;
    background: none;
    color: var(--text-secondary);
    letter-spacing: 1px;
  }

  .editor-tab.active {
    background: var(--accent-muted);
    color: var(--text-primary);
  }

  .editor-content {
    flex: 1;
    overflow: hidden;
  }

  .code-textarea {
    width: 100%;
    height: 100%;
    resize: none;
    border: none;
    background: #000000;
    color: var(--text-primary);
    font-family: 'Silkscreen', monospace;
    font-size: 10px;
    padding: 10px;
    line-height: 1.8;
    tab-size: 4;
    white-space: pre;
    overflow: auto;
  }

  .code-textarea:focus {
    outline: none;
    border: none;
  }

  .editor-status {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 3px 8px;
    border-top: 1px solid var(--border);
    font-size: 8px;
    color: var(--text-secondary);
    flex-shrink: 0;
    letter-spacing: 1px;
  }

  .sub-btn {
    font-size: 8px;
    padding: 2px 8px;
  }

  .sub-result {
    font-size: 8px;
    color: var(--ok);
  }

  .ide-chat {
    width: 300px;
    flex-shrink: 0;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }
</style>
