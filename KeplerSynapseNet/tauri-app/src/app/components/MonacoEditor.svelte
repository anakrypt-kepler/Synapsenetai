<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { theme } from "../../lib/theme";
  import { rpcCall } from "../../lib/rpc";

  export let files: { name: string; content: string; language: string }[] = [
    { name: "untitled.py", content: "", language: "python" },
  ];
  export let activeFileIndex = 0;

  let editorContainer: HTMLElement;
  let editor: any = null;
  let monaco: any = null;
  let models: Map<number, any> = new Map();

  let unsubTheme: (() => void) | null = null;

  onMount(async () => {
    const monacoModule = await import("monaco-editor");
    monaco = monacoModule;

    editor = monaco.editor.create(editorContainer, {
      value: files[activeFileIndex]?.content || "",
      language: files[activeFileIndex]?.language || "plaintext",
      theme: getMonacoTheme(),
      fontFamily: "'JetBrains Mono', monospace",
      fontSize: 13,
      minimap: { enabled: false },
      scrollBeyondLastLine: false,
      renderWhitespace: "none",
      lineNumbers: "on",
      glyphMargin: false,
      folding: true,
      automaticLayout: true,
      tabSize: 2,
      wordWrap: "off",
      suggestOnTriggerCharacters: true,
      quickSuggestions: true,
      padding: { top: 8 },
    });

    for (let i = 0; i < files.length; i++) {
      const model = monaco.editor.createModel(
        files[i].content,
        files[i].language
      );
      models.set(i, model);
    }

    if (models.has(activeFileIndex)) {
      editor.setModel(models.get(activeFileIndex));
    }

    registerGhostCompletions();

    unsubTheme = theme.subscribe((t) => {
      if (editor) {
        editor.updateOptions({ theme: t === "dark" ? "vs-dark" : "vs" });
      }
    });
  });

  onDestroy(() => {
    if (unsubTheme) unsubTheme();
    if (editor) editor.dispose();
    models.forEach((m) => m.dispose());
  });

  function getMonacoTheme(): string {
    let current = "dark";
    theme.subscribe((t) => (current = t))();
    return current === "dark" ? "vs-dark" : "vs";
  }

  function switchFile(index: number) {
    if (index === activeFileIndex) return;
    activeFileIndex = index;
    if (models.has(index) && editor) {
      editor.setModel(models.get(index));
    }
  }

  function registerGhostCompletions() {
    if (!monaco || !editor) return;

    monaco.languages.registerInlineCompletionsProvider("*", {
      provideInlineCompletions: async (
        model: any,
        position: any,
        _context: any,
        _token: any
      ) => {
        const textUntilPosition = model.getValueInRange({
          startLineNumber: Math.max(1, position.lineNumber - 20),
          startColumn: 1,
          endLineNumber: position.lineNumber,
          endColumn: position.column,
        });

        try {
          const result = await rpcCall(
            "ai.complete",
            JSON.stringify({ prompt: textUntilPosition, max_tokens: 128 })
          );
          const parsed = JSON.parse(result);
          if (parsed.text) {
            return {
              items: [
                {
                  insertText: parsed.text,
                  range: {
                    startLineNumber: position.lineNumber,
                    startColumn: position.column,
                    endLineNumber: position.lineNumber,
                    endColumn: position.column,
                  },
                },
              ],
            };
          }
        } catch {}

        return { items: [] };
      },
      freeInlineCompletions: () => {},
    });
  }

  export function getValue(): string {
    if (editor) return editor.getValue();
    return "";
  }

  export function setValue(content: string) {
    if (editor) editor.setValue(content);
  }
</script>

<div class="monaco-wrapper">
  <div class="file-tabs">
    {#each files as file, i}
      <button
        class="file-tab"
        class:active={i === activeFileIndex}
        on:click={() => switchFile(i)}
      >
        {file.name}
      </button>
    {/each}
  </div>
  <div class="editor-container" bind:this={editorContainer}></div>
</div>

<style>
  .monaco-wrapper {
    display: flex;
    flex-direction: column;
    height: 100%;
    border: 1px solid var(--border);
  }

  .file-tabs {
    display: flex;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
    overflow-x: auto;
    flex-shrink: 0;
  }

  .file-tab {
    padding: 6px 14px;
    font-size: 11px;
    border: none;
    border-radius: 0;
    border-right: 1px solid var(--border);
    color: var(--text-secondary);
    background: none;
    white-space: nowrap;
    position: relative;
    transition: color 0.15s ease, background 0.15s ease;
  }

  .file-tab:hover {
    color: var(--text-primary);
    background: var(--surface-alt);
  }

  .file-tab.active {
    color: var(--text-primary);
    background: var(--bg);
    border-right: 1px solid var(--border);
  }

  .file-tab.active::after {
    content: "";
    position: absolute;
    bottom: 0;
    left: 0;
    right: 0;
    height: 2px;
    background: var(--accent);
  }

  .editor-container {
    flex: 1;
    min-height: 0;
  }
</style>
