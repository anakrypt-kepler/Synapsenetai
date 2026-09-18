<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { listen } from "@tauri-apps/api/event";
  import { aiComplete, subscribeEvent, rpcCall, modelLoad, getStatus, parseStatus } from "../../lib/rpc";
  import { nodeStatus } from "../../lib/store";
  import KsSpinner from "./KsSpinner.svelte";

  interface ChatMessage {
    role: "user" | "assistant" | "tool";
    content: string;
    toolName?: string;
    collapsed?: boolean;
    timestamp: number;
  }

  let messages: ChatMessage[] = [];
  let inputValue = "";
  let streaming = false;
  let thinking = false;
  let caretLive = false;
  let draft = "";
  let draftOn = false;
  let web4Enabled = false;
  let messagesContainer: HTMLElement;
  let localModels: { name: string; path: string }[] = [];
  let selectedPath = "";
  let loadingModel = false;
  let modelErr = "";
  let gen = 0;

  const reduceMotion =
    typeof matchMedia !== "undefined" &&
    matchMedia("(prefers-reduced-motion: reduce)").matches;

  onMount(async () => {
    await refreshLocalModels();
    try {
      await subscribeEvent("ai.stream");
    } catch {}

    await listen("synapsed:ai.stream", (event: any) => {
      const data = event.payload;
      if (data && data.payload) {
        try {
          const parsed = JSON.parse(data.payload);
          if (parsed.token) {
            thinking = false;
            draftOn = true;
            caretLive = true;
            appendStreamToken(parsed.token);
          }
          if (parsed.tool_call) {
            addToolMessage(parsed.tool_call);
          }
          if (parsed.done) {
            if (draft) {
              messages = [
                ...messages,
                { role: "assistant", content: draft, timestamp: Date.now() },
              ];
            }
            draft = "";
            draftOn = false;
            streaming = false;
            window.setTimeout(() => {
              if (!streaming) caretLive = false;
            }, 800);
          }
        } catch {}
      }
    });
  });

  onDestroy(() => {
    gen += 1;
  });

  function appendStreamToken(token: string) {
    draftOn = true;
    draft += token;
    scrollToBottom();
  }

  function addToolMessage(toolCall: { name: string; output: string }) {
    messages = [
      ...messages,
      {
        role: "tool",
        content: toolCall.output,
        toolName: toolCall.name,
        collapsed: true,
        timestamp: Date.now(),
      },
    ];
    scrollToBottom();
  }

  function typeOut(full: string, my: number): Promise<void> {
    caretLive = true;
    draftOn = true;
    draft = reduceMotion || !full ? full : "";
    if (reduceMotion || !full) {
      messages = [
        ...messages,
        { role: "assistant", content: full, timestamp: Date.now() },
      ];
      draftOn = false;
      draft = "";
      return Promise.resolve();
    }
    const cps = full.length > 400 ? 140 : 78;
    const start = performance.now();
    let shown = 0;
    let lastPaint = 0;
    const minPaint = 1000 / 90;
    return new Promise((resolve) => {
      const step = (now: number) => {
        if (my !== gen) {
          resolve();
          return;
        }
        const n = Math.min(full.length, Math.floor(((now - start) * cps) / 1000));
        if (n !== shown && now - lastPaint >= minPaint) {
          shown = n;
          lastPaint = now;
          draft = full.slice(0, shown);
          scrollToBottom();
        } else if (n === full.length && shown !== n) {
          shown = n;
          draft = full;
        }
        if (shown < full.length) requestAnimationFrame(step);
        else {
          messages = [
            ...messages,
            { role: "assistant", content: full, timestamp: Date.now() },
          ];
          draftOn = false;
          draft = "";
          resolve();
        }
      };
      requestAnimationFrame(step);
    });
  }

  async function sendMessage() {
    const text = inputValue.trim();
    if (!text || streaming) return;

    if (text.startsWith("/tangent")) {
      messages = [];
      inputValue = "";
      return;
    }

    const my = ++gen;
    messages = [
      ...messages,
      { role: "user", content: text, timestamp: Date.now() },
    ];
    inputValue = "";
    streaming = true;
    thinking = true;
    draftOn = false;
    draft = "";
    caretLive = false;
    scrollToBottom();

    if (!$nodeStatus.model_loaded) {
      thinking = false;
      messages = [
        ...messages,
        { role: "assistant", content: "LOAD GGUF IN SET", timestamp: Date.now() },
      ];
      streaming = false;
      scrollToBottom();
      return;
    }

    try {
      const result = await aiComplete(text);
      if (my !== gen) return;
      thinking = false;
      if (result) {
        try {
          const parsed = JSON.parse(result);
          if (parsed.error) {
            const err = String(parsed.error);
            const hint = /no model/i.test(err) ? "LOAD GGUF IN SET" : `Error: ${err}`;
            messages = [
              ...messages,
              { role: "assistant", content: hint, timestamp: Date.now() },
            ];
          } else if (parsed.text) {
            await typeOut(String(parsed.text), my);
          }
        } catch {
          if (!streaming) {
            messages = [
              ...messages,
              { role: "assistant", content: result, timestamp: Date.now() },
            ];
          }
        }
      }
    } catch (e: any) {
      if (my === gen) {
        thinking = false;
        messages = [
          ...messages,
          {
            role: "assistant",
            content: `Error: ${e.message || e}`,
            timestamp: Date.now(),
          },
        ];
      }
    }

    if (my !== gen) return;
    thinking = false;
    draftOn = false;
    streaming = false;
    window.setTimeout(() => {
      if (my === gen) caretLive = false;
    }, 800);
    scrollToBottom();
  }

  function toggleToolCollapse(index: number) {
    if (messages[index] && messages[index].role === "tool") {
      messages[index].collapsed = !messages[index].collapsed;
      messages = [...messages];
    }
  }

  function copyCode(code: string) {
    navigator.clipboard.writeText(code);
  }

  function scrollToBottom() {
    requestAnimationFrame(() => {
      if (messagesContainer) {
        messagesContainer.scrollTop = messagesContainer.scrollHeight;
      }
    });
  }

  function handleKeydown(event: KeyboardEvent) {
    if (event.key === "Enter" && !event.shiftKey) {
      event.preventDefault();
      sendMessage();
    }
  }

  function extractCodeBlocks(text: string): { type: "text" | "code"; content: string; lang?: string }[] {
    const parts: { type: "text" | "code"; content: string; lang?: string }[] = [];
    const regex = /```(\w*)\n([\s\S]*?)```/g;
    let lastIndex = 0;
    let match;

    while ((match = regex.exec(text)) !== null) {
      if (match.index > lastIndex) {
        parts.push({ type: "text", content: text.slice(lastIndex, match.index) });
      }
      parts.push({ type: "code", content: match[2], lang: match[1] || undefined });
      lastIndex = regex.lastIndex;
    }

    if (lastIndex < text.length) {
      parts.push({ type: "text", content: text.slice(lastIndex) });
    }

    if (parts.length === 0) {
      parts.push({ type: "text", content: text });
    }

    return parts;
  }

  async function refreshLocalModels() {
    try {
      const cat = JSON.parse(await rpcCall("model.catalog", "{}"));
      const rows = Array.isArray(cat.models) ? cat.models : [];
      localModels = rows
        .filter((r: { installed?: boolean; path?: string }) => r.installed && r.path)
        .map((r: { name: string; path: string }) => ({ name: r.name, path: r.path }));
      const live = $nodeStatus.model_path || "";
      if (live && localModels.some((m) => m.path === live)) {
        selectedPath = live;
      } else if ($nodeStatus.model_name) {
        const hit = localModels.find(
          (m) => m.path.endsWith($nodeStatus.model_name) || m.name === $nodeStatus.model_name
        );
        if (hit) selectedPath = hit.path;
      }
    } catch {}
  }

  async function pickLocalModel() {
    if (!selectedPath || loadingModel) return;
    modelErr = "";
    loadingModel = true;
    try {
      const raw = JSON.parse(await modelLoad(selectedPath));
      if (raw.error) {
        modelErr = String(raw.error);
        return;
      }
      try {
        nodeStatus.set(parseStatus(await getStatus()));
      } catch {}
    } catch (e: unknown) {
      modelErr = e instanceof Error ? e.message : "LOAD FAILED";
    } finally {
      loadingModel = false;
    }
  }
</script>

<div class="chat-panel">
  <div class="chat-header">
    <span class="chat-title">AI</span>
    <div class="header-right">
      <select
        class="model-pick"
        bind:value={selectedPath}
        disabled={loadingModel}
        on:change={pickLocalModel}
      >
        <option value="">Local GGUF</option>
        {#each localModels as m}
          <option value={m.path}>{m.name}</option>
        {/each}
      </select>
      <button
        class="web4-toggle"
        class:active={web4Enabled}
        on:click={() => (web4Enabled = !web4Enabled)}
      >
        W4
      </button>
    </div>
  </div>
  {#if modelErr}
    <div class="model-err">{modelErr}</div>
  {/if}
  <div class="chat-messages" bind:this={messagesContainer}>
    {#if messages.length === 0 && !$nodeStatus.model_loaded}
      <div class="chat-empty">Pick a local GGUF above, or download one in SET.</div>
    {/if}
    {#each messages as msg, i}
      <div class="chat-msg {msg.role}">
        {#if msg.role === "tool"}
          <button class="tool-header" on:click={() => toggleToolCollapse(i)}>
            <span class="tool-icon">{msg.collapsed ? "+" : "-"}</span>
            <span class="tool-name">{msg.toolName}</span>
          </button>
          {#if !msg.collapsed}
            <pre class="tool-output">{msg.content}</pre>
          {/if}
        {:else}
          {#each extractCodeBlocks(msg.content) as block}
            {#if block.type === "code"}
              <div class="code-block">
                <div class="code-header">
                  <span>{block.lang || "code"}</span>
                  <button class="copy-btn" on:click={() => copyCode(block.content)}>copy</button>
                </div>
                <pre class="code-content">{block.content}</pre>
              </div>
            {:else}
              <span class="msg-text">{block.content}</span>
            {/if}
          {/each}
        {/if}
      </div>
    {/each}
    {#if draftOn}
      <div class="chat-msg assistant live">
        <span class="msg-text">{draft}</span>
        {#if caretLive}
          <span class="kitty-caret" aria-hidden="true"></span>
        {/if}
      </div>
    {/if}
    {#if thinking}
      <div class="think-row" aria-busy="true">
        <KsSpinner size={48} />
      </div>
    {/if}
  </div>
  <div class="chat-input-area">
    <textarea
      class="chat-input"
      bind:value={inputValue}
      on:keydown={handleKeydown}
      placeholder="Message..."
      rows="2"
    ></textarea>
    <button class="send-btn btn-primary" on:click={sendMessage} disabled={streaming || !inputValue.trim()}>
    {#if streaming}
      <KsSpinner size={16} />
    {:else}
      >
    {/if}
    </button>
  </div>
</div>

<style>
  .chat-panel {
    display: flex;
    flex-direction: column;
    height: 100%;
    background: transparent;
    font-family: var(--font-mono);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
  }

  .chat-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 8px 12px;
    border-bottom: 1px solid var(--border);
    flex-shrink: 0;
    background: var(--surface);
    backdrop-filter: blur(22px) saturate(140%);
    -webkit-backdrop-filter: blur(22px) saturate(140%);
  }

  .chat-title {
    font-family: var(--font);
    font-size: 9px;
    font-weight: 400;
    text-transform: uppercase;
    letter-spacing: 0;
    color: var(--text-secondary);
  }

  .header-right {
    display: flex;
    align-items: center;
    gap: 6px;
    min-width: 0;
  }

  .model-pick {
    max-width: 160px;
    font-size: 11px;
    padding: 4px 8px;
    border-radius: var(--radius-sm, 10px);
    background: var(--surface-solid, #111);
    color: var(--text-primary);
    border: 1px solid var(--border);
  }

  .model-err {
    font-size: 11px;
    color: var(--err, #ff453a);
    padding: 4px 12px;
    flex-shrink: 0;
  }

  .web4-toggle {
    font-family: var(--font);
    font-size: 9px;
    font-weight: 400;
    padding: 5px 8px;
    border: 1px solid var(--border);
    border-radius: 0;
    color: var(--text-secondary);
    background: none;
    letter-spacing: 0;
    transition:
      background var(--dur-fast, 200ms) var(--ease-fast, cubic-bezier(0.34, 0.8, 0.34, 1)),
      color var(--dur-fast, 200ms) var(--ease-fast, cubic-bezier(0.34, 0.8, 0.34, 1)),
      border-color var(--dur-fast, 200ms) var(--ease-fast, cubic-bezier(0.34, 0.8, 0.34, 1));
  }

  .web4-toggle.active {
    border-color: var(--text-primary);
    color: #000;
    background: var(--text-primary);
  }

  .chat-messages {
    flex: 1;
    overflow-y: auto;
    padding: 10px 12px;
    display: flex;
    flex-direction: column;
    gap: 8px;
    background: transparent;
  }

  .chat-empty {
    font-size: 13px;
    color: var(--text-secondary);
    letter-spacing: 0;
    line-height: 1.5;
  }

  .chat-msg {
    font-size: 13px;
    line-height: 1.55;
    color: var(--text-primary);
  }

  .chat-msg.user {
    color: var(--text-primary);
    padding: 8px 10px;
    border-left: none;
    background: var(--accent-muted);
    border-radius: var(--radius-sm, 10px);
    animation: ks-popin var(--dur-enter, 180ms) var(--ease, cubic-bezier(0.05, 0.7, 0.1, 1)) both;
  }

  .chat-msg.assistant {
    color: var(--text-primary);
  }

  .chat-msg.assistant.live {
    animation: none;
  }

  .tool-header {
    display: flex;
    align-items: center;
    gap: 6px;
    border: none;
    border-radius: var(--radius-sm, 10px);
    padding: 4px 0;
    font-family: inherit;
    font-size: 12px;
    color: var(--text-secondary);
    background: none;
    image-rendering: auto;
  }

  .tool-header:hover {
    color: var(--text-primary);
    background: none;
  }

  .tool-icon { font-size: 12px; width: 12px; }
  .tool-name { font-weight: 600; }

  .tool-output {
    font-family: var(--font-mono, ui-monospace, "SF Mono", SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace);
    font-size: 12px;
    padding: 8px 10px;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm, 10px);
    overflow-x: auto;
    margin: 4px 0;
    white-space: pre-wrap;
    color: var(--text-secondary);
  }

  .code-block {
    margin: 6px 0;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm, 10px);
    overflow: hidden;
  }

  .code-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 6px 10px;
    border-bottom: 1px solid var(--border);
    font-size: 11px;
    color: var(--text-secondary);
  }

  .copy-btn {
    font-family: inherit;
    font-size: 11px;
    padding: 3px 8px;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm, 10px);
    color: var(--text-secondary);
    background: none;
    image-rendering: auto;
  }

  .copy-btn:hover {
    color: var(--text-primary);
    border-color: var(--text-primary);
  }

  .code-content {
    padding: 8px 10px;
    font-family: var(--font-mono, ui-monospace, "SF Mono", SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace);
    font-size: 12px;
    overflow-x: auto;
    background: #000000;
    margin: 0;
    white-space: pre;
    color: var(--text-primary);
  }

  .msg-text {
    white-space: pre-wrap;
    word-break: break-word;
  }

  .think-row {
    display: flex;
    justify-content: center;
    align-items: center;
    padding: 28px 12px 36px;
    animation: ks-popin var(--dur-enter, 400ms) var(--ease, cubic-bezier(0.05, 0.7, 0.1, 1)) both;
  }

  /* In-flow at the bottom of this panel — not viewport-fixed over the mascot. */
  .chat-input-area {
    display: flex;
    gap: 8px;
    padding: 10px 12px;
    border-top: 1px solid var(--border);
    flex-shrink: 0;
    position: relative;
    background: var(--surface);
    backdrop-filter: blur(22px) saturate(140%);
    -webkit-backdrop-filter: blur(22px) saturate(140%);
  }

  .chat-input {
    flex: 1;
    resize: none;
    border: 1px solid var(--border);
    border-radius: var(--radius, 14px);
    background: #000000;
    color: var(--text-primary);
    padding: 8px 12px;
    font-family: inherit;
    font-size: 13px;
    letter-spacing: 0;
    line-height: 1.45;
    image-rendering: auto;
    -webkit-font-smoothing: antialiased;
    caret-color: #e8e8ed;
    transition: border-color var(--dur-fast, 200ms) var(--ease-fast, cubic-bezier(0.34, 0.8, 0.34, 1));
  }

  .chat-input:focus {
    border-color: var(--text-primary);
  }

  .send-btn {
    align-self: flex-end;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 36px;
    min-height: 36px;
    padding: 8px 12px;
    font-family: inherit;
    font-size: 14px;
    border-radius: var(--radius-sm, 10px);
    image-rendering: auto;
  }

  @keyframes ks-popin {
    from { opacity: 0; }
    to { opacity: 1; }
  }

  @media (prefers-reduced-motion: reduce) {
    .chat-msg.user,
    .chat-msg.assistant,
    .think-row {
      animation: none;
    }
  }
</style>
