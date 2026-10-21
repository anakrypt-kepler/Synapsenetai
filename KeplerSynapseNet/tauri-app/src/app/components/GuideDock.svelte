<script lang="ts">
  // Desk helper. On by default, parked on the right. The harvest agent is a
  // different NAAN. Opening a tab reads that page at once. A typed message
  // goes to the GGUF. A change runs only after confirm or refuse.
  import { onDestroy, onMount } from "svelte";
  import { rpcCall } from "../../lib/rpc";
  import InstalledModelSelect from "./InstalledModelSelect.svelte";
  import { activeTab, nodeStatus, tabSlideDir, tabs, type TabId } from "../../lib/store";

  type ChatLine = { role: "user" | "assistant"; text: string; shown: string };
  type Pending = { id: string; action: string; arg: string };
  type Piece = { text: string; hot: boolean; ask: boolean };

  const DIM_KEY = "synapsenet.guide.dimmed";
  const QUIET_MS = 20000;
  const NUDGE_MAX = 1;
  const labels: Record<string, string> = {
    dashboard: "MAIN",
    wallet: "WALLET",
    transfers: "SEND",
    blocks: "BLOCKS",
    knowledge: "KNOW",
    find: "FIND",
    naan: "NAAN",
    harvest: "HARVEST",
    exploits: "INTEL",
    messages: "MSG",
    ide: "IDE",
    network: "NET",
    rental: "RENT",
    settings: "SET",
  };

  let dimmed = false;
  let lines: ChatLine[] = [];
  let pending: Pending | null = null;
  let draft = "";
  let busy = false;
  let booted = false;
  let greeted = false;
  let bootedAt = 0;
  let greetedEarly = false;
  let correctedTor = false;
  let naanWasActive = false;
  let scroller: HTMLDivElement | null = null;
  let nudges = 0;
  let seenTab = "";
  let live = "";
  let liveIndex = -1;
  let typeGen = 0;
  let quietTimer: ReturnType<typeof setTimeout> | null = null;
  let typeTimer: ReturnType<typeof setTimeout> | null = null;
  let pageTimer: ReturnType<typeof setTimeout> | null = null;

  function hideLine(text: string): boolean {
    return text.startsWith("The model did not answer");
  }

  function isTab(id: string): id is TabId {
    return tabs.some((tab) => tab.id === id);
  }

  function openTab(id: string) {
    if (!isTab(id) || id === $activeTab) return;
    const from = tabs.findIndex((tab) => tab.id === $activeTab);
    const to = tabs.findIndex((tab) => tab.id === id);
    tabSlideDir.set(to > from ? 1 : -1);
    activeTab.set(id);
  }

  function screen() {
    const status = $nodeStatus;
    return {
      tab: $activeTab,
      connection: status.connection,
      peers: status.peers,
      naan_state: status.naan_state,
      naan_task: status.naan_task || "",
      naan_topics: status.naan_topics || "",
      model_name: status.model_name || "",
      tor_bootstrap: status.tor_bootstrap || "",
      balance: status.balance || "",
    };
  }

  function scrollDown() {
    if (!scroller) return;
    scroller.scrollTop = scroller.scrollHeight;
  }

  function readPending(value: unknown): Pending | null {
    if (!value || typeof value !== "object") return null;
    const rec = value as Record<string, unknown>;
    const id = typeof rec.id === "string" ? rec.id : "";
    const action = typeof rec.action === "string" ? rec.action : "";
    const arg = typeof rec.arg === "string" ? rec.arg : "";
    if (!id || !action) return null;
    return { id, action, arg };
  }

  function pushPiece(out: Piece[], text: string, from: number, to: number, askFrom: number, hot: boolean) {
    if (from >= to) return;
    if (askFrom > from && askFrom < to) {
      out.push({ text: text.slice(from, askFrom), hot, ask: false });
      out.push({ text: text.slice(askFrom, to), hot, ask: true });
      return;
    }
    out.push({ text: text.slice(from, to), hot, ask: from >= askFrom });
  }

  function pieces(text: string): Piece[] {
    const q = text.lastIndexOf("?");
    let askFrom = text.length;
    if (q >= 0) {
      const head = text.slice(0, q);
      const cut = Math.max(head.lastIndexOf("."), head.lastIndexOf("!"), head.lastIndexOf("\n"));
      askFrom = cut >= 0 ? cut + 1 : 0;
    }
    const re = /\b(?:MAIN|WALLET|SEND|BLOCKS|KNOW|FIND|NAAN|HARVEST|INTEL|MSG|IDE|NET|RENT|SET|Tor|NGT|GGUF)\b/gi;
    const out: Piece[] = [];
    let i = 0;
    let match: RegExpExecArray | null;
    while ((match = re.exec(text))) {
      const start = match.index;
      if (start > i) pushPiece(out, text, i, start, askFrom, false);
      pushPiece(out, text, start, start + match[0].length, askFrom, true);
      i = start + match[0].length;
    }
    if (i < text.length) pushPiece(out, text, i, text.length, askFrom, false);
    return out;
  }

  function drawn(line: ChatLine, index: number): string {
    if (index === liveIndex) return live;
    return line.shown;
  }

  function armQuiet() {
    if (quietTimer) clearTimeout(quietTimer);
    quietTimer = null;
    if (nudges >= NUDGE_MAX || dimmed) return;
    quietTimer = setTimeout(() => {
      quietTimer = null;
      void poke();
    }, QUIET_MS);
  }

  function typeLine(index: number) {
    if (typeTimer) clearTimeout(typeTimer);
    const gen = ++typeGen;
    const full = lines[index]?.text ?? "";
    let i = 0;
    liveIndex = index;
    live = "";
    const tick = () => {
      if (gen !== typeGen) return;
      const line = lines[index];
      if (!line || line.text !== full) {
        if (liveIndex === index) liveIndex = -1;
        return;
      }
      i = Math.min(full.length, i + (full.length > 240 ? 2 : 1));
      live = full.slice(0, i);
      scrollDown();
      if (i < full.length) {
        typeTimer = setTimeout(tick, 18);
        return;
      }
      line.shown = full;
      lines = lines;
      liveIndex = -1;
      typeTimer = null;
      armQuiet();
    };
    tick();
  }

  function pushAssistant(text: string) {
    lines = [...lines, { role: "assistant", text, shown: "" }];
    typeLine(lines.length - 1);
    queueMicrotask(scrollDown);
  }

  async function call(op: string, body: Record<string, unknown>) {
    const raw = await rpcCall("guide." + op, JSON.stringify({ ...body, screen: screen() }));
    try {
      return JSON.parse(raw) as Record<string, unknown>;
    } catch {
      return { error: "Guide did not answer." };
    }
  }

  async function loadHistory() {
    const parsed = await call("history", {});
    const rows = Array.isArray(parsed.messages) ? parsed.messages : [];
    const next: ChatLine[] = [];
    for (const row of rows) {
      if (!row || typeof row !== "object") continue;
      const rec = row as Record<string, unknown>;
      const role = rec.role === "user" ? "user" : rec.role === "assistant" ? "assistant" : "";
      const text = typeof rec.text === "string" ? rec.text : "";
      if (!role || !text || hideLine(text)) continue;
      next.push({ role, text, shown: text });
    }
    lines = next;
    pending = readPending(parsed.pending);
    queueMicrotask(scrollDown);
  }

  async function observe(event: string) {
    if (busy) return "busy";
    busy = true;
    try {
      const parsed = await call("observe", { event });
      if (parsed.skipped === "no_model") return "no_model";
      if (parsed.error === "busy") return "busy";
      if (typeof parsed.error === "string" && parsed.error && !parsed.text) {
        pushAssistant(parsed.error);
        return "error";
      }
      const text = typeof parsed.text === "string" ? parsed.text : "";
      if (text && !hideLine(text)) pushAssistant(text);
      pending = readPending(parsed.proposal);
      return "ok";
    } catch {
      return "error";
    } finally {
      busy = false;
    }
  }

  async function poke() {
    if (dimmed || pending) return;
    if (busy || liveIndex >= 0 || draft.trim()) {
      armQuiet();
      return;
    }
    const last = lines[lines.length - 1];
    if (!last || last.role !== "assistant" || nudges >= NUDGE_MAX) return;
    nudges += 1;
    const result = await observe("nudge");
    if (result !== "ok") {
      nudges -= 1;
      armQuiet();
    }
  }

  async function send() {
    const text = draft.trim();
    if (!text || busy) return;
    draft = "";
    pending = null;
    nudges = 0;
    busy = true;
    lines = [...lines, { role: "user", text, shown: text }];
    queueMicrotask(scrollDown);
    try {
      const parsed = await call("say", { text });
      if (parsed.error === "busy") {
        pushAssistant("I am still finishing the last reply.");
        return;
      }
      const reply = typeof parsed.text === "string" ? parsed.text : "";
      if (reply && !hideLine(reply)) pushAssistant(reply);
      else if (typeof parsed.error === "string" && parsed.error) pushAssistant(parsed.error);
      pending = readPending(parsed.proposal);
    } catch {
      pushAssistant("I could not reach the model.");
    } finally {
      busy = false;
    }
  }

  async function decide(op: "confirm" | "refuse") {
    if (!pending || busy) return;
    const current = pending;
    busy = true;
    try {
      const parsed = await call(op, { id: current.id });
      pending = null;
      const text = typeof parsed.text === "string" ? parsed.text : "";
      if (text) pushAssistant(text);
      else if (typeof parsed.error === "string" && parsed.error) {
        pushAssistant("That request is already closed.");
      }
      if (op === "confirm" && parsed.ok === true && parsed.action === "open_tab" && typeof parsed.arg === "string") {
        openTab(parsed.arg);
      }
    } finally {
      busy = false;
    }
  }

  async function clearChat() {
    if (busy) return;
    if (quietTimer) clearTimeout(quietTimer);
    if (pageTimer) clearTimeout(pageTimer);
    if (typeTimer) clearTimeout(typeTimer);
    typeGen += 1;
    liveIndex = -1;
    pending = null;
    nudges = 0;
    draft = "";
    lines = [];
    try {
      const parsed = await call("clear", {});
      if (parsed.error === "busy") {
        await loadHistory();
        return;
      }
    } catch {
      await loadHistory();
      return;
    }
    void observe("page");
  }

  function toggleDim() {
    dimmed = !dimmed;
    try {
      localStorage.setItem(DIM_KEY, dimmed ? "1" : "0");
    } catch {
      /* the panel still toggles for this session */
    }
    if (!dimmed) armQuiet();
  }

  function proposalText(item: Pending): string {
    if (item.action === "naan_start") return "Start the harvest agent?";
    if (item.action === "naan_stop") return "Stop the harvest agent?";
    if (item.action === "open_tab") return `Open ${labels[item.arg] || "that tab"}?`;
    return "Apply this change?";
  }

  function onKey(event: KeyboardEvent) {
    if (event.key === "Enter" && !event.shiftKey) {
      event.preventDefault();
      void send();
    }
  }

  onMount(async () => {
    try {
      dimmed = localStorage.getItem(DIM_KEY) === "1";
    } catch {
      dimmed = false;
    }
    try {
      await loadHistory();
    } catch {
      lines = [];
    }
    naanWasActive = $nodeStatus.naan_state === "active";
    bootedAt = Date.now();
    booted = true;
  });

  onDestroy(() => {
    if (quietTimer) clearTimeout(quietTimer);
    if (typeTimer) clearTimeout(typeTimer);
    if (pageTimer) clearTimeout(pageTimer);
  });

  $: if (booted && $nodeStatus.model_loaded && !greeted && !busy) {
    const boot = String($nodeStatus.tor_bootstrap || "");
    const full = boot.includes("100");
    const gaveUp = bootedAt > 0 && Date.now() - bootedAt > 45000;
    if (full || gaveUp) {
      greeted = true;
      greetedEarly = !full;
      const event = lines.some((line) => line.role === "assistant") ? "checkin" : "hello";
      naanWasActive = $nodeStatus.naan_state === "active";
      void observe(event).then((result) => {
        if (result === "busy" || result === "no_model") greeted = false;
      });
    }
  }

  $: if (booted && greetedEarly && !correctedTor && !busy && !dimmed && String($nodeStatus.tor_bootstrap || "").includes("100")) {
    correctedTor = true;
    void observe("page");
  }

  $: if (booted && greeted && !busy && $nodeStatus.naan_state === "active" && !naanWasActive) {
    naanWasActive = true;
    void observe("naan_started").then((result) => {
      if (result !== "ok") naanWasActive = false;
    });
  }

  $: if ($nodeStatus.naan_state !== "active") naanWasActive = false;

  $: if (booted && $activeTab !== seenTab) {
    const first = seenTab === "";
    seenTab = $activeTab;
    if (!first && greeted && !dimmed) {
      if (pageTimer) clearTimeout(pageTimer);
      pageTimer = setTimeout(() => {
        pageTimer = null;
        if (!busy && !dimmed) void observe("page");
      }, 700);
    }
  }
</script>

<aside class="guide" class:dimmed aria-label="Guide">
  {#if dimmed}
    <button class="rail" type="button" on:click={toggleDim}>GUIDE</button>
  {:else}
    <header class="head">
      <div>
        <div class="name" class:live={busy || liveIndex >= 0}>GUIDE</div>
        <div class="sub">{busy || liveIndex >= 0 ? "writing · US English" : "Helper · US English"}</div>
      </div>
      <div class="head-actions">
        <button class="dim" type="button" on:click={clearChat} disabled={busy} aria-label="Clear guide chat">CLEAR</button>
        <button class="dim" type="button" on:click={toggleDim} aria-label="Dim guide">DIM</button>
      </div>
    </header>
    <div class="model-pick">
      <InstalledModelSelect settingKey="guide_model_path" label="CHAT MODEL" />
    </div>
    <div class="log" bind:this={scroller}>
      {#if lines.length === 0 && !$nodeStatus.model_loaded}
        <p class="hint">Load a GGUF in SET. I talk once that model is in memory.</p>
      {:else if lines.length === 0 && busy}
        <p class="hint">Looking at the window...</p>
      {/if}
      {#each lines as line, index}
        <p class={line.role} class:fresh={line.role === "assistant" && index === lines.length - 1}>
          {#each pieces(drawn(line, index)) as piece, pieceIndex (pieceIndex)}
            {#if piece.hot}
              <span class="hot" class:q={piece.ask}>{piece.text}</span>
            {:else if piece.ask}
              <span class="q">{piece.text}</span>
            {:else}
              {piece.text}
            {/if}
          {/each}{#if liveIndex === index}<span class="caret"></span>{/if}
        </p>
      {/each}
      {#if busy}
        <p class="hint writing">writing<span class="caret"></span></p>
      {/if}
    </div>
    {#if pending}
      <div class="ask">
        <p>{proposalText(pending)}</p>
        <div class="ask-row">
          <button type="button" on:click={() => decide("confirm")} disabled={busy}>CONFIRM</button>
          <button type="button" on:click={() => decide("refuse")} disabled={busy}>REFUSE</button>
        </div>
      </div>
    {/if}
    <form class="composer" on:submit|preventDefault={send}>
      <textarea
        bind:value={draft}
        rows="2"
        placeholder="Talk to Guide"
        spellcheck="true"
        on:keydown={onKey}
      ></textarea>
      <button type="submit" disabled={busy || !draft.trim()}>SEND</button>
    </form>
  {/if}
</aside>

<style>
  .guide {
    position: absolute;
    right: 10px;
    top: 50%;
    transform: translateY(-50%);
    z-index: 20;
    width: 320px;
    height: min(480px, calc(100vh - var(--topbar-h) - var(--statusbar-h) - 28px));
    display: flex;
    flex-direction: column;
    background: #000000;
    border: 1px solid var(--border);
    color: var(--text-primary);
  }

  .guide.dimmed {
    width: 28px;
    height: 132px;
    opacity: 0.55;
  }

  .guide.dimmed:hover {
    opacity: 1;
  }

  .rail {
    width: 100%;
    height: 100%;
    border: none;
    background: transparent;
    color: var(--text-secondary);
    font-family: var(--font);
    font-size: 8px;
    letter-spacing: 0;
    writing-mode: vertical-rl;
    cursor: pointer;
  }

  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    padding: 8px 8px 6px;
    border-bottom: 1px solid var(--border);
    flex: 0 0 auto;
  }

  .name {
    font-family: var(--font);
    font-size: 8px;
    letter-spacing: 0;
  }

  .name.live {
    color: #39ff14;
  }

  .sub {
    margin-top: 4px;
    font-family: var(--font-mono);
    font-size: 11px;
    color: var(--text-secondary);
  }

  .head-actions {
    display: flex;
    gap: 6px;
    flex: 0 0 auto;
  }

  .model-pick {
    flex: 0 0 auto;
    padding: 6px 8px 8px;
    border-bottom: 1px solid var(--border);
  }

  .dim,
  .composer button,
  .ask button {
    border: 1px solid var(--border);
    background: #000000;
    color: var(--text-primary);
    font-family: var(--font);
    font-size: 8px;
    padding: 7px 8px;
    cursor: pointer;
  }

  .dim:hover,
  .composer button:hover:not(:disabled),
  .ask button:hover:not(:disabled) {
    background: var(--accent-muted);
  }

  .dim:disabled {
    opacity: 0.45;
    cursor: default;
  }

  .log {
    flex: 1 1 auto;
    min-height: 0;
    overflow: auto;
    padding: 8px;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .log p {
    font-family: var(--font-mono);
    font-size: 12px;
    line-height: 1.45;
    white-space: pre-wrap;
    word-break: break-word;
  }

  .user {
    color: var(--text-primary);
  }

  .assistant {
    color: var(--ok);
  }

  .fresh {
    border-left: 2px solid #39ff14;
    padding-left: 6px;
  }

  .hot {
    color: #f4fff4;
    background: rgba(57, 255, 20, 0.16);
    box-shadow: inset 0 -1px 0 #39ff14;
    padding: 0 3px;
  }

  .q {
    color: #f5f5f7;
  }

  .hot.q {
    color: #39ff14;
    background: rgba(255, 255, 255, 0.08);
  }

  .caret {
    display: inline-block;
    width: 7px;
    height: 11px;
    margin-left: 2px;
    background: #39ff14;
    vertical-align: -1px;
    animation: guide-blink 1s steps(1) infinite;
  }

  @keyframes guide-blink {
    50% {
      opacity: 0;
    }
  }

  .hint {
    color: var(--text-secondary);
  }

  .writing {
    letter-spacing: 0.08em;
  }

  .ask {
    flex: 0 0 auto;
    border-top: 1px solid var(--border);
    padding: 8px;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .ask p {
    font-family: var(--font-mono);
    font-size: 12px;
  }

  .ask-row {
    display: flex;
    gap: 6px;
  }

  .composer {
    flex: 0 0 auto;
    display: flex;
    gap: 6px;
    align-items: flex-end;
    padding: 8px;
    border-top: 1px solid var(--border);
  }

  textarea {
    flex: 1 1 auto;
    resize: none;
    border: 1px solid var(--border);
    background: #000000;
    color: var(--text-primary);
    font-family: var(--font-mono);
    font-size: 12px;
    line-height: 1.4;
    padding: 6px;
    min-width: 0;
  }

  textarea:focus {
    outline: none;
    border-color: var(--text-secondary);
  }

  button:disabled {
    opacity: 0.45;
    cursor: default;
  }
</style>
