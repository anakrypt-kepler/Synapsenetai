<script lang="ts">
  import { onMount } from "svelte";
  import { listen } from "@tauri-apps/api/event";
  import { aiComplete, rpcCall, subscribeEvent } from "../../lib/rpc";

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
  let web4Enabled = false;
  let messagesContainer: HTMLElement;

  onMount(async () => {
    try {
      await subscribeEvent("ai.stream");
    } catch {}

    await listen("synapsed:ai.stream", (event: any) => {
      const data = event.payload;
      if (data && data.payload) {
        try {
          const parsed = JSON.parse(data.payload);
          if (parsed.token) {
            appendStreamToken(parsed.token);
          }
          if (parsed.tool_call) {
            addToolMessage(parsed.tool_call);
          }
          if (parsed.done) {
            streaming = false;
          }
        } catch {}
      }
    });
  });

  function appendStreamToken(token: string) {
    if (messages.length > 0 && messages[messages.length - 1].role === "assistant") {
      messages[messages.length - 1].content += token;
      messages = [...messages];
    } else {
      messages = [
        ...messages,
        { role: "assistant", content: token, timestamp: Date.now() },
      ];
    }
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

  async function sendMessage() {
    const text = inputValue.trim();
    if (!text || streaming) return;

    if (text.startsWith("/tangent")) {
      messages = [];
      inputValue = "";
      return;
    }

    messages = [
      ...messages,
      { role: "user", content: text, timestamp: Date.now() },
    ];
    inputValue = "";
    streaming = true;
    scrollToBottom();

    try {
      const params: any = { prompt: text };
      if (web4Enabled) {
        params.web4 = true;
      }
      const result = await aiComplete(text);
      if (result) {
        try {
          const parsed = JSON.parse(result);
          if (parsed.text && !streaming) {
            messages = [
              ...messages,
              {
                role: "assistant",
                content: parsed.text,
                timestamp: Date.now(),
              },
            ];
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
      messages = [
        ...messages,
        {
          role: "assistant",
          content: `Error: ${e.message || e}`,
          timestamp: Date.now(),
        },
      ];
    }

    streaming = false;
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
</script>

<div class="chat-panel">
  <div class="chat-header">
    <span class="chat-title">AI</span>
    <button
      class="web4-toggle"
      class:active={web4Enabled}
      on:click={() => (web4Enabled = !web4Enabled)}
    >
      W4
    </button>
  </div>
  <div class="chat-messages" bind:this={messagesContainer}>
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
    {#if streaming}
      <div class="streaming-indicator">
        <span class="blink-cursor">_</span>
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
      >
    </button>
  </div>
</div>

<style>
  .chat-panel {
    display: flex;
    flex-direction: column;
    height: 100%;
    background: #000000;
  }

  .chat-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 4px 8px;
    border-bottom: 1px solid var(--border);
    flex-shrink: 0;
  }

  .chat-title {
    font-size: 8px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 1px;
    color: var(--text-secondary);
  }

  .web4-toggle {
    font-size: 8px;
    padding: 2px 6px;
    border: 1px solid var(--border);
    border-radius: 0;
    color: var(--text-secondary);
    background: none;
  }

  .web4-toggle.active {
    border-color: var(--text-primary);
    color: #000;
    background: var(--text-primary);
  }

  .chat-messages {
    flex: 1;
    overflow-y: auto;
    padding: 8px;
    display: flex;
    flex-direction: column;
    gap: 6px;
    background: #000000;
  }

  .chat-msg {
    font-size: 10px;
    line-height: 1.6;
    color: var(--text-primary);
  }

  .chat-msg.user {
    color: var(--text-secondary);
    padding: 6px 8px;
    border-left: 2px solid var(--text-primary);
    background: var(--accent-muted);
  }

  .chat-msg.assistant {
    color: var(--text-primary);
  }

  .tool-header {
    display: flex;
    align-items: center;
    gap: 4px;
    border: none;
    padding: 2px 0;
    font-size: 8px;
    color: var(--text-secondary);
    background: none;
  }

  .tool-header:hover {
    color: var(--text-primary);
    background: none;
  }

  .tool-icon { font-size: 8px; width: 10px; }
  .tool-name { font-weight: 700; }

  .tool-output {
    font-size: 8px;
    padding: 6px 8px;
    border: 1px solid var(--border);
    overflow-x: auto;
    margin: 2px 0;
    white-space: pre-wrap;
    color: var(--text-secondary);
  }

  .code-block {
    margin: 4px 0;
    border: 1px solid var(--border);
    overflow: hidden;
  }

  .code-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 3px 6px;
    border-bottom: 1px solid var(--border);
    font-size: 8px;
    color: var(--text-secondary);
  }

  .copy-btn {
    font-size: 8px;
    padding: 1px 4px;
    border: 1px solid var(--border);
    border-radius: 0;
    color: var(--text-secondary);
    background: none;
  }

  .copy-btn:hover {
    color: var(--text-primary);
    border-color: var(--text-primary);
  }

  .code-content {
    padding: 6px 8px;
    font-size: 10px;
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

  .streaming-indicator { padding: 2px 0; }

  @keyframes blink {
    0%, 100% { opacity: 1; }
    50% { opacity: 0; }
  }

  .blink-cursor {
    animation: blink 0.8s step-end infinite;
    color: var(--text-primary);
  }

  .chat-input-area {
    display: flex;
    gap: 4px;
    padding: 6px 8px;
    border-top: 1px solid var(--border);
    flex-shrink: 0;
  }

  .chat-input {
    flex: 1;
    resize: none;
    border: 1px solid var(--border);
    border-radius: 0;
    background: #000000;
    color: var(--text-primary);
    padding: 4px 6px;
    font-size: 10px;
  }

  .chat-input:focus {
    border-color: var(--text-primary);
  }

  .send-btn {
    align-self: flex-end;
    padding: 4px 10px;
    font-size: 10px;
  }
</style>
