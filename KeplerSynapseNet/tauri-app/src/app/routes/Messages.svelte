<script lang="ts">
  // Node messages. msg.send writes ~/.synapsenet/messages.jsonl then dials
  // onion:8333 over Tor SOCKS. v3 is ML-KEM-768 + crypto_box_seal; v2 is
  // X25519 seal only when the peer has no kem_pk. Body never goes on the wire.
  // delivered=false means the local log still saved; the onion hop failed.
  import { onMount, onDestroy, tick } from "svelte";
  import { rpcCall } from "../../lib/rpc";

  interface NodeMessage {
    from: string;
    to: string;
    body: string;
    ts: number;
    encrypted?: boolean;
    quantum_signed?: boolean;
  }

  interface Conversation {
    peer: string;
    last_msg: string;
    ts: number;
  }

  let messages: NodeMessage[] = [];
  let conversations: Conversation[] = [];
  let pendingPeers: string[] = [];
  let activePeer = "";
  let newPeer = "";
  let inputMsg = "";
  let sending = false;
  let sendError = "";
  let sendNote = "";
  let lastDelivered: boolean | null = null;
  let peerHybrid: Record<string, boolean> = {};
  let kyberReal = false;
  let pollHandle: ReturnType<typeof setInterval> | null = null;
  let msgListEl: HTMLElement | null = null;
  let peerNick: Record<string, string> = {};

  onMount(async () => {
    try {
      const q = JSON.parse(await rpcCall("crypto.status", "{}"));
      kyberReal = q.kyber_real === true;
    } catch {}
    await loadConversations();
    pollHandle = setInterval(loadConversations, 5000);
  });

  onDestroy(() => { if (pollHandle) clearInterval(pollHandle); });

  function hostOf(addr: string): string {
    return (addr || "").replace(/:\d+$/, "").toLowerCase();
  }

  function displayPeer(peer: string): string {
    const h = hostOf(peer);
    return peerNick[h] || peerNick[peer] || "PEER";
  }

  function normalizePeer(raw: string): string {
    let p = raw.trim();
    const onionPort = p.match(/^(.*\.onion):(\d+)$/i);
    if (onionPort) p = onionPort[1];
    return p;
  }

  function looksLikeWallet(peer: string): boolean {
    return peer.startsWith("SN") && peer.length >= 40;
  }

  async function loadConversations() {
    try {
      const result = await rpcCall("msg.list", "{}");
      const parsed = JSON.parse(result);
      const listed: Conversation[] = parsed.conversations || [];
      listed.sort((a, b) => (b.ts || 0) - (a.ts || 0));
      const seen = new Set(listed.map((c) => c.peer));
      pendingPeers = pendingPeers.filter((p) => !seen.has(p));
      for (const p of pendingPeers) {
        listed.push({ peer: p, last_msg: "", ts: 0 });
      }
      conversations = listed;
    } catch {}
    try {
      const net = JSON.parse(await rpcCall("network.info", "{}"));
      const nicks: Record<string, string> = {};
      for (const p of net.peers || []) {
        const alias = String(p.alias || "").trim();
        if (!alias) continue;
        nicks[hostOf(p.address || "")] = alias;
      }
      peerNick = nicks;
    } catch {}
    if (activePeer) await loadMessages(activePeer);
  }

  async function loadMessages(peer: string) {
    activePeer = peer;
    try {
      const result = await rpcCall("msg.get", JSON.stringify({ peer }));
      const parsed = JSON.parse(result);
      messages = parsed.messages || [];
    } catch {
      messages = [];
    }
    await tick();
    scrollToEnd(false);
  }

  function startConversation() {
    sendError = "";
    sendNote = "";
    lastDelivered = null;
    const peer = normalizePeer(newPeer);
    if (!peer) {
      sendError = "peer required";
      return;
    }
    if (looksLikeWallet(peer)) {
      sendError = "that looks like a wallet address, not a node onion";
      return;
    }
    if (!pendingPeers.includes(peer) && !conversations.some((c) => c.peer === peer)) {
      pendingPeers = [...pendingPeers, peer];
      conversations = [...conversations, { peer, last_msg: "", ts: 0 }];
    }
    newPeer = "";
    loadMessages(peer);
  }

  async function sendMessage() {
    sendError = "";
    sendNote = "";
    lastDelivered = null;
    const peer = activePeer.trim();
    const body = inputMsg.trim();
    if (!peer || !body) {
      sendError = "peer and body required";
      return;
    }
    if (sending) return;
    sending = true;
    try {
      const result = await rpcCall("msg.send", JSON.stringify({ peer, body }));
      const parsed = JSON.parse(result);
      if (parsed.error) {
        sendError = String(parsed.error);
      } else {
        inputMsg = "";
        lastDelivered = !!parsed.delivered;
        if (parsed.hybrid === true) {
          peerHybrid = { ...peerHybrid, [peer]: true };
        }
        if (parsed.delivered) {
          sendNote = parsed.hybrid && kyberReal ? "TOR · HYBRID SEAL" : parsed.sealed ? "TOR · SEALED" : "TOR · DELIVERED";
        } else {
          sendNote = String(parsed.note || "saved locally; live Tor delivery failed or peer is not a v3 onion");
        }
        await loadConversations();
        await tick();
        scrollToEnd(true);
      }
    } catch (e: any) {
      sendError = e?.message || "send failed";
    }
    sending = false;
  }

  function handleMsgKey(e: KeyboardEvent) {
    if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      sendMessage();
    }
  }

  function handlePeerKey(e: KeyboardEvent) {
    if (e.key === "Enter") {
      e.preventDefault();
      startConversation();
    }
  }

  function scrollToEnd(force: boolean) {
    if (!msgListEl) return;
    const gap = msgListEl.scrollHeight - msgListEl.scrollTop - msgListEl.clientHeight;
    if (force || gap < 80) msgListEl.scrollTop = msgListEl.scrollHeight;
  }

  function fmtTime(ts: number): string {
    if (!ts) return "--:--";
    const d = new Date(ts);
    if (isNaN(d.getTime())) return "--:--";
    return `${d.getHours().toString().padStart(2, "0")}:${d.getMinutes().toString().padStart(2, "0")}`;
  }

  // Engine from=walletAddress_, to=peer onion. Never show the wallet string in a bubble.
  function isOutgoing(msg: NodeMessage): boolean {
    if (msg.from && msg.from === activePeer) return false;
    return true;
  }

  function bubbleLabel(msg: NodeMessage): string {
    return isOutgoing(msg) ? "YOU" : "PEER";
  }

  $: sealBadge = kyberReal && peerHybrid[activePeer] ? "TOR · HYBRID SEAL" : "TOR · SEALED";
</script>

<div class="msg-layout">
  <div class="msg-sidebar">
    <div class="sidebar-header">NODES</div>
    <div class="sidebar-compose">
      <input
        class="peer-input"
        bind:value={newPeer}
        on:keydown={handlePeerKey}
        placeholder="peer nick or onion"
        spellcheck="false"
      />
      <button class="btn-secondary start-btn" type="button" on:click={startConversation}>START</button>
    </div>
    {#each conversations as conv (conv.peer)}
      <button
        class="peer-btn"
        class:active={activePeer === conv.peer}
        type="button"
        on:click={() => loadMessages(conv.peer)}
      >
        <span class="peer-addr">{displayPeer(conv.peer)}</span>
        <span class="peer-last">{conv.last_msg ? conv.last_msg.slice(0, 20) : "new chat"}</span>
      </button>
    {:else}
      <div class="no-peers">Start from a live NET peer. Nicks show if they set one.</div>
    {/each}
  </div>

  <div class="msg-main">
    {#if activePeer}
      <div class="msg-header">
        <span class="msg-peer">{displayPeer(activePeer)}</span>
        <div class="msg-badges">
          <span class="msg-crypto local">LOCAL LOG</span>
          <span class="msg-crypto torsealed">{sealBadge}</span>
        </div>
      </div>
      <div class="msg-list" bind:this={msgListEl}>
        {#each messages as msg (msg.ts + msg.body)}
          <div class="msg-bubble" class:outgoing={isOutgoing(msg)}>
            <div class="msg-meta">
              <span class="msg-from">{bubbleLabel(msg)}</span>
              <span class="msg-time">{fmtTime(msg.ts)}</span>
            </div>
            <div class="msg-body">{msg.body}</div>
          </div>
        {:else}
          <div class="msg-empty">No messages — stored locally when you send</div>
        {/each}
      </div>
      {#if sendError}
        <div class="error-msg send-err">{sendError}</div>
      {/if}
      {#if sendNote}
        <div class="send-note" class:ok={lastDelivered === true} class:warn={lastDelivered === false}>{sendNote}</div>
      {/if}
      <div class="msg-input-area">
        <textarea
          class="msg-input"
          bind:value={inputMsg}
          on:keydown={handleMsgKey}
          placeholder="Sealed over Tor"
          rows="2"
        ></textarea>
        <button class="btn-primary send-btn" type="button" on:click={sendMessage} disabled={sending}>
          {#if sending}
            <span class="ks-busy"><span class="ks-spinner"></span> SEND</span>
          {:else}
            SEND
          {/if}
        </button>
      </div>
    {:else}
      <div class="msg-empty-state">
        <div class="empty-title">NODE MESSAGING</div>
        <div class="empty-desc">Each send is sealed to that peer’s box key, then dialed over Tor SOCKS. The wire has no plaintext body.</div>
        <div class="empty-desc">Delivery fails closed if Tor cannot reach them; the local log still keeps the send.</div>
        <div class="empty-compose">
          <input
            class="peer-input wide"
            bind:value={newPeer}
            on:keydown={handlePeerKey}
            placeholder="peer nick or onion"
            spellcheck="false"
          />
          <button class="btn-primary" type="button" on:click={startConversation}>START CHAT</button>
        </div>
        {#if sendError}
          <div class="error-msg send-err">{sendError}</div>
        {/if}
      </div>
    {/if}
  </div>
</div>

<style>
  .msg-layout {
    display: flex;
    height: 100%;
    background: transparent;
    font-family: var(--font);
    font-size: 13px;
    line-height: 1.45;
    color: var(--text-primary);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    image-rendering: auto;
  }

  .msg-layout :global(button),
  .msg-layout :global(input),
  .msg-layout :global(textarea) {
    font-family: var(--font);
    font-size: 13px;
    border-radius: var(--radius-sm);
    image-rendering: auto;
    -webkit-font-smoothing: antialiased;
    transition: background var(--dur) var(--ease), border-color var(--dur) var(--ease), color var(--dur) var(--ease);
  }

  .msg-sidebar {
    width: 220px;
    border-right: 1px solid var(--border);
    overflow-y: auto;
    flex-shrink: 0;
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
  }

  .sidebar-header {
    padding: 12px 14px;
    font-size: 11px;
    font-weight: 600;
    color: var(--text-secondary);
    letter-spacing: 0.08em;
    border-bottom: 1px solid var(--border);
  }

  .sidebar-compose {
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 12px 14px;
    border-bottom: 1px solid var(--border);
  }

  .peer-input {
    width: 100%;
    font-family: var(--font-mono);
    font-size: 12px;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    background: rgba(0, 0, 0, 0.45);
    color: var(--text-primary);
    padding: 8px 10px;
  }

  .peer-input.wide {
    font-size: 13px;
    min-width: 240px;
  }

  .peer-input:focus {
    border-color: rgba(255, 255, 255, 0.32);
    outline: none;
  }

  .start-btn {
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0.04em;
    padding: 8px 10px;
  }

  .peer-btn {
    display: flex;
    flex-direction: column;
    width: 100%;
    padding: 10px 14px;
    border: none;
    border-radius: 0;
    border-bottom: 1px solid var(--border);
    background: none;
    text-align: left;
    gap: 3px;
    transition: background var(--dur) var(--ease);
  }

  .peer-btn:hover { background: var(--accent-muted); }

  .peer-btn.active {
    background: var(--accent-muted);
    box-shadow: inset 2px 0 0 var(--text-primary);
  }

  .peer-addr {
    font-family: var(--font-mono);
    font-size: 12px;
    color: var(--text-primary);
    font-weight: 600;
  }

  .peer-last {
    font-size: 12px;
    color: var(--text-secondary);
  }

  .no-peers {
    padding: 18px 14px;
    font-size: 13px;
    color: var(--text-secondary);
    line-height: 1.5;
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
    padding: 10px 16px;
    border-bottom: 1px solid var(--border);
    flex-shrink: 0;
    gap: 8px;
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
  }

  .msg-peer {
    font-family: var(--font-mono);
    font-size: 12px;
    color: var(--text-primary);
    font-weight: 600;
    word-break: break-all;
  }

  .msg-badges {
    display: flex;
    gap: 8px;
    flex-shrink: 0;
  }

  .msg-crypto {
    font-size: 11px;
    letter-spacing: 0.04em;
    white-space: nowrap;
    font-weight: 600;
    padding: 3px 8px;
    border-radius: var(--radius-full, 999px);
    border: 1px solid var(--border);
  }

  .msg-crypto.local {
    color: var(--warn);
  }

  .msg-crypto.torsealed {
    color: var(--ok);
  }

  .send-note {
    font-size: 12px;
    padding: 6px 16px;
    flex-shrink: 0;
  }

  .send-note.ok { color: var(--ok); }
  .send-note.warn { color: var(--warn); }

  .msg-list {
    flex: 1;
    overflow-y: auto;
    padding: 16px;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .msg-bubble {
    padding: 10px 14px;
    border: 1px solid var(--border);
    border-radius: 16px;
    max-width: 76%;
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
    align-self: flex-start;
  }

  .msg-bubble.outgoing {
    align-self: flex-end;
    background: rgba(255, 255, 255, 0.12);
    border-color: rgba(255, 255, 255, 0.16);
  }

  .msg-meta {
    display: flex;
    gap: 8px;
    font-size: 11px;
    color: var(--text-secondary);
    margin-bottom: 4px;
  }

  .msg-from { font-weight: 700; }
  .msg-time { color: var(--text-faint); }

  .msg-body {
    font-size: 14px;
    color: var(--text-primary);
    line-height: 1.5;
    word-break: break-word;
  }

  .msg-empty {
    font-size: 13px;
    color: var(--text-secondary);
    text-align: center;
    padding: 28px;
  }

  .send-err {
    margin: 0 12px;
    flex-shrink: 0;
    font-family: var(--font);
    font-size: 12px;
    border-radius: var(--radius-sm);
  }

  .msg-input-area {
    display: flex;
    gap: 10px;
    padding: 12px 16px;
    border-top: 1px solid var(--border);
    flex-shrink: 0;
    background: var(--surface);
    backdrop-filter: saturate(140%) blur(var(--blur));
    -webkit-backdrop-filter: saturate(140%) blur(var(--blur));
    align-items: flex-end;
  }

  .msg-input {
    flex: 1;
    resize: none;
    font-size: 14px;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    background: rgba(0, 0, 0, 0.45);
    color: var(--text-primary);
    padding: 10px 12px;
    font-family: var(--font);
  }

  .msg-input:focus {
    border-color: rgba(255, 255, 255, 0.32);
    outline: none;
  }

  .send-btn {
    font-weight: 600;
    letter-spacing: 0.04em;
    padding: 10px 16px;
    border-radius: var(--radius-sm);
    align-self: stretch;
  }

  .msg-empty-state {
    flex: 1;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 10px;
    padding: 24px;
    padding-bottom: 72px;
  }

  .empty-title {
    font-size: 15px;
    font-weight: 600;
    color: var(--text-primary);
    letter-spacing: 0.06em;
  }

  .empty-desc {
    font-size: 13px;
    color: var(--text-secondary);
    text-align: center;
    max-width: 420px;
    line-height: 1.5;
  }

  .empty-compose {
    display: flex;
    gap: 8px;
    margin-top: 8px;
    width: 100%;
    max-width: 440px;
    justify-content: center;
  }

  .btn-primary {
    font-weight: 600;
    letter-spacing: 0.04em;
    padding: 8px 16px;
  }

  .ks-busy {
    display: inline-flex;
    align-items: center;
    gap: 8px;
  }

  .ks-busy .ks-spinner {
    width: 16px;
    height: 16px;
  }
</style>
