<script lang="ts">
  import MonacoEditor from "../components/MonacoEditor.svelte";
  import ChatPanel from "../components/ChatPanel.svelte";
  import { poeSubmitCode } from "../../lib/rpc";

  let editorRef: MonacoEditor;
  let submitResult = "";

  let files = [
    { name: "untitled.py", content: "", language: "python" },
  ];

  async function submitPatch() {
    submitResult = "";
    if (!editorRef) return;
    const code = editorRef.getValue();
    if (!code.trim()) return;
    try {
      const result = await poeSubmitCode(code);
      submitResult = "Submitted to PoE CODE.";
    } catch (e: any) {
      submitResult = `Error: ${e.message || e}`;
    }
  }
</script>

<div class="ide-layout">
  <div class="ide-editor">
    <div class="ide-toolbar">
      <button class="btn-secondary poe-btn" on:click={submitPatch}>PoE CODE Submit</button>
      {#if submitResult}
        <span class="submit-result">{submitResult}</span>
      {/if}
    </div>
    <div class="editor-area">
      <MonacoEditor bind:this={editorRef} {files} />
    </div>
  </div>
  <div class="ide-chat">
    <ChatPanel />
  </div>
</div>

<style>
  .ide-layout {
    display: flex;
    flex-direction: column;
    height: 100%;
    background: var(--bg);
  }

  .ide-editor {
    flex: 6;
    display: flex;
    flex-direction: column;
    min-height: 0;
  }

  .ide-toolbar {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 4px 10px;
    border-bottom: 1px solid var(--border);
    background: var(--surface);
    flex-shrink: 0;
  }

  .poe-btn {
    font-size: 11px;
    padding: 3px 10px;
    border-radius: 4px;
  }

  .submit-result {
    font-size: 11px;
    color: var(--text-secondary);
  }

  .editor-area {
    flex: 1;
    min-height: 0;
  }

  .ide-chat {
    flex: 4;
    min-height: 0;
  }
</style>
