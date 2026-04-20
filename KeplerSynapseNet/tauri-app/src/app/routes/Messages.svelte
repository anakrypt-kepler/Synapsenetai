<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { rpcCall } from "../../lib/rpc";

  interface NodeMessage {
    from: string;
    to: string;
    body: string;
    ts: number;
    encrypted: boolean;
    quantum_signed: boolean;
  }

  let messages: NodeMessage[] = [];
  let conversations: { peer: string; last_msg: string; ts: number }[] = [];
  let activePeer = "";
  let inputMsg = "";
  let sending = false;
  let pollHandle: ReturnType<typeof setInterval> | null = null;

  onMount(async () => {
    await loadConversations();
    pollHandle = setInterval(loadConversations, 5000);
  });

  onDestroy(() => { if (pollHandle) clearInterval(pollHandle); });

  async function loadConversations() {
    try {
      const result = await rpcCall("msg.list", "{}");
      const parsed = JSON.parse(result);
      conversations = parsed.conversations || [];
    } catch {}
    if (activePeer) await loadMessages(activePeer);
  }

  async function loadMessages(peer: string) {
    activePeer = peer;
    try {
      const result = await rpcCall("msg.get", JSON.stringify({ peer }));
      const parsed = JSON.parse(result);
      messages = parsed.messages || [];
    } catch { messages = []; }
  }

  async function sendMessage() {
    if (!inputMsg.trim() || !activePeer || sending) return;
    sending = true;
    try {
      await rpcCall("msg.send", JSON.stringify({
        peer: activePeer,
        body: inputMsg,
        e2e: true,
        quantum: true,
      }));
      inputMsg = "";
      await loadMessages(activePeer);
    } catch {}
    sending = false;
  }

  function handleKey(e: KeyboardEvent) {
    if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      sendMessage();
    }
  }

  function fmtTime(ts: number): string {
    const d = new Date(ts);
    return `${d.getHours().toString().padStart(2,"0")}:${d.getMinutes().toString().padStart(2,"0")}`;
  }

  function shortAddr(addr: string): string {
    if (addr.length > 16) return addr.slice(0, 8) + ".." + addr.slice(-6);
    return addr;
  }
</script>

<div class="msg-layout">
  <div class="msg-sidebar">
    <div class="sidebar-header">NODES</div>
    {#each conversations as conv}
      <button
        class="peer-btn"
        class:active={activePeer === conv.peer}
        on:click={() => loadMessages(conv.peer)}
      >
        <span class="peer-addr">{shortAddr(conv.peer)}</span>
        <span class="peer-last">{conv.last_msg.slice(0, 20)}</span>
      </button>
    {:else}
      <div class="no-peers">NO CONVERSATIONS</div>
    {/each}
  </div>

  <div class="msg-main">
    {#if activePeer}
      <div class="msg-header">
        <span class="msg-peer">{shortAddr(activePeer)}</span>
        <span class="msg-crypto">E2E + QUANTUM</span>
      </div>
      <div class="msg-list">
        {#each messages as msg}
          <div class="msg-bubble" class:outgoing={msg.to === activePeer}>
            <div class="msg-meta">
              <span class="msg-from">{shortAddr(msg.from)}</span>
              <span class="msg-time">{fmtTime(msg.ts)}</span>
              {#if msg.quantum_signed}
                <span class="msg-q">Q</span>
              {/if}
            </div>
            <div class="msg-body">{msg.body}</div>
          </div>
        {:else}
          <div class="msg-empty">NO MESSAGES</div>
        {/each}
      </div>
      <div class="msg-input-area">
        <textarea
          class="msg-input"
          bind:value={inputMsg}
          on:keydown={handleKey}
          placeholder="Message..."
          rows="2"
        ></textarea>
        <button class="btn-primary" on:click={sendMessage} disabled={sending || !inputMsg.trim()}>
          [ SEND ]
        </button>
      </div>
    {:else}
      <div class="msg-empty-state">
        <div class="empty-title">NODE MESSAGING</div>
        <div class="empty-desc">SELECT A NODE TO START ENCRYPTED COMMUNICATION</div>
        <div class="empty-desc">ALL MESSAGES USE END-TO-END ENCRYPTION</div>
        <div class="empty-desc">WITH POST-QUANTUM KYBER-1024 KEY EXCHANGE</div>
        <div class="empty-desc">AND DILITHIUM-5 SIGNATURES</div>
      </div>
    {/if}
  </div>
</div>

<style>
  .msg-layout {
    display: flex;
    height: 100%;
    background: #000000;
  }

  .msg-sidebar {
    width: 200px;
    border-right: 1px solid var(--border);
    overflow-y: auto;
    flex-shrink: 0;
  }

  .sidebar-header {
    padding: 8px 10px;
    font-size: 8px;
    font-weight: 700;
    color: var(--text-secondary);
    letter-spacing: 1px;
    border-bottom: 1px solid var(--border);
  }

  .peer-btn {
    display: flex;
    flex-direction: column;
    width: 100%;
    padding: 8px 10px;
    border: none;
    border-bottom: 1px solid var(--border);
    background: none;
    text-align: left;
    gap: 2px;
  }

  .peer-btn:hover { background: var(--accent-muted); }

  .peer-btn.active {
    background: var(--accent-muted);
    border-left: 2px solid var(--text-primary);
  }

  .peer-addr {
    font-size: 8px;
    color: var(--text-primary);
    font-weight: 700;
  }

  .peer-last {
    font-size: 8px;
    color: var(--text-secondary);
  }

  .no-peers {
    padding: 16px 10px;
    font-size: 8px;
    color: var(--text-secondary);
  }

  .msg-main {
    flex: 1;
    display: flex;
    flex-direction: column;
    min-width: 0;
  }

  .msg-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 6px 12px;
    border-bottom: 1px solid var(--border);
    flex-shrink: 0;
  }

  .msg-peer {
    font-size: 10px;
    color: var(--text-primary);
    font-weight: 700;
  }

  .msg-crypto {
    font-size: 8px;
    color: var(--ok);
    letter-spacing: 1px;
  }

  .msg-list {
    flex: 1;
    overflow-y: auto;
    padding: 12px;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .msg-bubble {
    padding: 6px 8px;
    border: 1px solid var(--border);
    max-width: 80%;
  }

  .msg-bubble.outgoing {
    align-self: flex-end;
    border-color: var(--text-faint);
  }

  .msg-meta {
    display: flex;
    gap: 6px;
    font-size: 8px;
    color: var(--text-secondary);
    margin-bottom: 2px;
  }

  .msg-from { font-weight: 700; }
  .msg-time { color: var(--text-faint); }
  .msg-q { color: var(--ok); font-weight: 700; }

  .msg-body {
    font-size: 10px;
    color: var(--text-primary);
    line-height: 1.6;
    word-break: break-word;
  }

  .msg-empty {
    font-size: 8px;
    color: var(--text-secondary);
    text-align: center;
    padding: 24px;
  }

  .msg-input-area {
    display: flex;
    gap: 6px;
    padding: 8px;
    border-top: 1px solid var(--border);
    flex-shrink: 0;
  }

  .msg-input {
    flex: 1;
    resize: none;
    font-size: 10px;
    border: 1px solid var(--border);
    background: #000;
    color: var(--text-primary);
    padding: 6px 8px;
  }

  .msg-input:focus {
    border-color: var(--text-primary);
  }

  .msg-empty-state {
    flex: 1;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 8px;
    padding: 24px;
  }

  .empty-title {
    font-size: 12px;
    font-weight: 700;
    color: var(--text-primary);
    letter-spacing: 2px;
  }

  .empty-desc {
    font-size: 8px;
    color: var(--text-secondary);
    letter-spacing: 0.5px;
  }
</style>
