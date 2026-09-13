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
      fontFamily: 'ui-monospace, "SF Mono", SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace',
      fontSize: 13,
      fontLigatures: false,
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
    border-radius: var(--radius, 14px);
    overflow: hidden;
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", system-ui, sans-serif);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    image-rendering: auto;
  }

  .file-tabs {
    display: flex;
    gap: 6px;
    padding: 8px 10px;
    background: var(--surface);
    backdrop-filter: blur(22px) saturate(140%);
    -webkit-backdrop-filter: blur(22px) saturate(140%);
    border-bottom: 1px solid var(--border);
    overflow-x: auto;
    flex-shrink: 0;
  }

  .file-tab {
    padding: 6px 12px;
    font-family: inherit;
    font-size: 12px;
    border: 1px solid transparent;
    border-radius: var(--radius-sm, 10px);
    color: var(--text-secondary);
    background: none;
    white-space: nowrap;
    position: relative;
    letter-spacing: 0;
    image-rendering: auto;
    transition:
      color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      background var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .file-tab:hover {
    color: var(--text-primary);
    background: var(--accent-muted);
  }

  .file-tab.active {
    color: var(--text-primary);
    background: var(--accent-muted);
    border-color: var(--border);
  }

  .file-tab.active::after {
    display: none;
  }

  .editor-container {
    flex: 1;
    min-height: 0;
    background: #000000;
  }
</style>
