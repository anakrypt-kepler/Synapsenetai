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
    <span class="chat-title">AI Chat</span>
    <button
      class="web4-toggle"
      class:active={web4Enabled}
      on:click={() => (web4Enabled = !web4Enabled)}
    >
      Web4
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
      Send
    </button>
  </div>
</div>

<style>
  .chat-panel {
    display: flex;
    flex-direction: column;
    height: 100%;
    border: 1px solid var(--border);
    border-top: none;
    background: var(--surface);
  }

  .chat-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 6px 10px;
    border-bottom: 1px solid var(--border);
    background: var(--surface);
    flex-shrink: 0;
  }

  .chat-title {
    font-size: 10px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.6px;
    color: var(--text-secondary);
  }

  .web4-toggle {
    font-size: 10px;
    padding: 2px 8px;
    border: 1px solid var(--border);
    border-radius: 3px;
    color: var(--text-secondary);
    background: none;
    transition: border-color 0.15s ease, color 0.15s ease, background 0.15s ease;
  }

  .web4-toggle.active {
    border-color: var(--accent);
    color: var(--accent);
    background: var(--accent-muted);
  }

  .chat-messages {
    flex: 1;
    overflow-y: auto;
    padding: 12px;
    display: flex;
    flex-direction: column;
    gap: 10px;
    background: var(--bg);
  }

  .chat-msg {
    font-size: 12px;
    line-height: 1.65;
    color: var(--text-primary);
  }

  .chat-msg.user {
    color: var(--text-secondary);
    padding: 8px 10px;
    border-left: 2px solid var(--accent);
    background: var(--accent-muted);
    border-radius: 0 4px 4px 0;
  }

  .chat-msg.assistant {
    color: var(--text-primary);
  }

  .tool-header {
    display: flex;
    align-items: center;
    gap: 6px;
    border: none;
    border-radius: 0;
    padding: 4px 0;
    font-size: 11px;
    color: var(--text-secondary);
    background: none;
    transition: color 0.15s ease;
  }

  .tool-header:hover {
    color: var(--text-primary);
    background: none;
  }

  .tool-icon {
    font-size: 10px;
    width: 12px;
  }

  .tool-name {
    font-weight: 600;
  }

  .tool-output {
    font-size: 11px;
    padding: 8px 10px;
    background: var(--surface-alt);
    border: 1px solid var(--border);
    border-radius: 4px;
    overflow-x: auto;
    margin: 4px 0;
    white-space: pre-wrap;
    color: var(--text-secondary);
  }

  .code-block {
    margin: 6px 0;
    border: 1px solid var(--border);
    border-radius: 4px;
    overflow: hidden;
  }

  .code-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 4px 8px;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
    font-size: 10px;
    color: var(--text-secondary);
  }

  .copy-btn {
    font-size: 10px;
    padding: 1px 6px;
    border: 1px solid var(--border);
    border-radius: 3px;
    color: var(--text-secondary);
    background: none;
    transition: color 0.15s ease, border-color 0.15s ease;
  }

  .copy-btn:hover {
    color: var(--text-primary);
    border-color: var(--text-faint);
  }

  .code-content {
    padding: 10px 12px;
    font-size: 12px;
    overflow-x: auto;
    background: var(--bg);
    margin: 0;
    white-space: pre;
    color: var(--text-primary);
  }

  .msg-text {
    white-space: pre-wrap;
    word-break: break-word;
  }

  .streaming-indicator {
    padding: 4px 0;
  }

  @keyframes blink {
    0%, 100% { opacity: 1; }
    50% { opacity: 0; }
  }

  .blink-cursor {
    animation: blink 1s step-end infinite;
    color: var(--accent);
  }

  .chat-input-area {
    display: flex;
    gap: 8px;
    padding: 8px 10px;
    border-top: 1px solid var(--border);
    background: var(--surface);
    flex-shrink: 0;
  }

  .chat-input {
    flex: 1;
    resize: none;
    border: 1px solid var(--border);
    border-radius: 4px;
    background: var(--bg);
    color: var(--text-primary);
    padding: 6px 8px;
    font-size: 12px;
    transition: border-color 0.15s ease, box-shadow 0.15s ease;
  }

  .chat-input:focus {
    border-color: var(--accent);
    box-shadow: 0 0 0 2px var(--accent-muted);
  }

  .send-btn {
    align-self: flex-end;
    padding: 6px 14px;
    font-size: 11px;
  }
</style>
