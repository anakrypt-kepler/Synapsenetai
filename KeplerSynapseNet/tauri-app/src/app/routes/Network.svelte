<script lang="ts">
  // NET MVP: you are a Tor cell. Dead directory onions are not peers.
  import { onMount, onDestroy } from "svelte";
  import { rpcCall } from "../../lib/rpc";

  interface PeerInfo {
    address: string;
    transport: string;
    latency_ms: number;
    connected_since: string;
    online?: boolean;
    seen_ago_ms?: number;
    up_ms?: number;
    alias?: string;
    avatar?: string;
  }

  let peers: PeerInfo[] = [];
  let ownOnion = "";
  let hsPublished = false;
  let hsReachable = false;
  let socksPort = 0;
  let listenPort = 0;
  let torStatus = { bootstrap: "0%", circuits: 0, bridge_status: "none" };
  let discovery = { dns_queries: 0, peer_exchange: 0 };
  let bandwidth = { inbound_kbps: 0, outbound_kbps: 0 };
  let copyMsg = "";
  let pollHandle: ReturnType<typeof setInterval> | null = null;
  let youAlias = "";
  let youAvatar = "";

  onMount(async () => {
    await loadNetworkInfo();
    pollHandle = setInterval(loadNetworkInfo, 4000);
  });

  onDestroy(() => { if (pollHandle) clearInterval(pollHandle); });

  async function loadNetworkInfo() {
    try {
      const result = await rpcCall("network.info", "{}");
      const parsed = JSON.parse(result);
      peers = parsed.peers || [];
      ownOnion = parsed.own_onion || "";
      hsPublished = !!parsed.hs_published;
      hsReachable = !!parsed.hs_reachable;
      socksPort = parsed.socks_port || 0;
      listenPort = parsed.listen_port || 0;
      torStatus = parsed.tor || torStatus;
      discovery = parsed.discovery || discovery;
      bandwidth = parsed.bandwidth || bandwidth;
      youAlias = parsed.profile?.alias || "";
      youAvatar = parsed.profile?.avatar || "";
      const self = (parsed.peers || []).find((p: PeerInfo) => p.connected_since === "YOU");
      if (self?.alias) youAlias = self.alias;
      if (self?.avatar) youAvatar = self.avatar;
    } catch {}
  }

  function hostOf(addr: string): string {
    return (addr || "").replace(/:\d+$/, "").toLowerCase();
  }

  function isShownRemote(p: PeerInfo): boolean {
    if (p.connected_since === "YOU") return false;
    if (ownOnion && hostOf(p.address) === hostOf(ownOnion)) return false;
    return !!(p.alias || "").trim() || !!avatarUrl(p.avatar);
  }

  $: listed = peers
    .filter((p) => p.connected_since !== "LOCAL" && p.connected_since !== "local")
    .filter((p) => {
      if (p.connected_since === "YOU") return true;
      return isShownRemote(p);
    })
    .slice()
    .sort((a, b) => {
      if (a.connected_since === "YOU") return -1;
      if (b.connected_since === "YOU") return 1;
      return Number(!!b.online) - Number(!!a.online);
    });
  $: remotes = listed.filter((p) => p.connected_since !== "YOU");
  $: youUp = listed.some((p) => p.connected_since === "YOU" && p.online);
  $: remoteUp = remotes.filter((p) => p.online).length;
  $: onTor = hsPublished || torStatus.bootstrap === "100%";
  $: meshMap = buildMeshMap(remotes, youUp || hsPublished, youAlias, youAvatar);

  const MAP_W = 720;
  const MAP_H = 420;
  const YOU_R = 26;
  const PEER_R = 15;
  const ZOOM_MIN = 0.5;
  const ZOOM_MAX = 3;

  type MeshNode = {
    id: string;
    x: number;
    y: number;
    r: number;
    label: string;
    kind: "you" | "peer";
    online: boolean;
    avatar?: string;
  };
  type MeshEdge = { x1: number; y1: number; x2: number; y2: number; hub: boolean };

  function hash32(s: string): number {
    let h = 2166136261;
    for (let i = 0; i < s.length; i++) {
      h ^= s.charCodeAt(i);
      h = Math.imul(h, 16777619);
    }
    return h >>> 0;
  }

  // Nick only if they set one. Never put an onion on the map.
  function shortPeerLabel(p: PeerInfo): string {
    return (p.alias || "").trim();
  }

  function tableLabel(p: PeerInfo): string {
    if (p.connected_since === "YOU") return (p.alias || youAlias || "YOU").trim();
    return (p.alias || "").trim() || "PEER";
  }

  // Clipped portrait only for real data:image URLs. YOU uses local profile; remotes use that peer only.
  function avatarUrl(src: string | undefined): string {
    if (!src) return "";
    return src.startsWith("data:image") ? src : "";
  }

  function clipId(id: string): string {
    return "av-" + id.replace(/[^a-zA-Z0-9_-]/g, "_");
  }

  function buildMeshMap(
    rows: PeerInfo[],
    showYou: boolean,
    alias: string,
    avatar: string,
  ): { nodes: MeshNode[]; edges: MeshEdge[] } {
    const cx = MAP_W / 2;
    const cy = MAP_H / 2;
    const nodes: MeshNode[] = [];
    const edges: MeshEdge[] = [];
    if (showYou) {
      nodes.push({
        id: "you",
        x: cx,
        y: cy,
        r: YOU_R,
        label: alias || "YOU",
        kind: "you",
        online: true,
        avatar: avatarUrl(avatar),
      });
    }
    const n = rows.length;
    if (n === 0) return { nodes, edges };

    const ringR = Math.min(158, 90 + n * 12);
    const peersDrawn: MeshNode[] = rows.map((p, i) => {
      const ang = -Math.PI / 2 + (2 * Math.PI * i) / n;
      const jitter = ((hash32(p.address) % 17) - 8) * 0.7;
      return {
        id: p.address,
        x: cx + Math.cos(ang) * (ringR + jitter),
        y: cy + Math.sin(ang) * (ringR + jitter),
        r: PEER_R,
        label: shortPeerLabel(p),
        kind: "peer" as const,
        online: !!p.online,
        avatar: avatarUrl(p.avatar),
      };
    });
    nodes.push(...peersDrawn);
    // Hub spokes YOU → each remote, then mesh sticks between remotes.
    for (const pt of peersDrawn) {
      if (showYou) edges.push({ x1: cx, y1: cy, x2: pt.x, y2: pt.y, hub: true });
    }
    for (let i = 0; i < peersDrawn.length; i++) {
      for (let j = i + 1; j < peersDrawn.length; j++) {
        edges.push({
          x1: peersDrawn[i].x,
          y1: peersDrawn[i].y,
          x2: peersDrawn[j].x,
          y2: peersDrawn[j].y,
          hub: false,
        });
      }
    }
    return { nodes, edges };
  }

  // Wheel zooms toward the cursor. Drag pans. Page scroll still works off the map.
  let mapSvg: SVGSVGElement | null = null;
  let mapZoom = 1;
  let mapPanX = 0;
  let mapPanY = 0;
  let mapDragging = false;
  let lastPtrX = 0;
  let lastPtrY = 0;
  const pointers = new Map<number, { x: number; y: number }>();
  let pinchStartDist = 0;
  let pinchStartZoom = 1;

  function clampZoom(z: number): number {
    return Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, z));
  }

  function clientToViewBox(clientX: number, clientY: number): { x: number; y: number } | null {
    if (!mapSvg) return null;
    const rect = mapSvg.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return null;
    const scale = Math.min(rect.width / MAP_W, rect.height / MAP_H);
    const ox = (rect.width - MAP_W * scale) / 2;
    const oy = (rect.height - MAP_H * scale) / 2;
    return {
      x: (clientX - rect.left - ox) / scale,
      y: (clientY - rect.top - oy) / scale,
    };
  }

  function applyZoomAt(svgX: number, svgY: number, nextZoom: number) {
    const z = clampZoom(nextZoom);
    const wx = (svgX - mapPanX) / mapZoom;
    const wy = (svgY - mapPanY) / mapZoom;
    mapPanX = svgX - wx * z;
    mapPanY = svgY - wy * z;
    mapZoom = z;
  }

  function onMapWheel(e: WheelEvent) {
    const p = clientToViewBox(e.clientX, e.clientY);
    if (!p) return;
    const factor = Math.exp(-e.deltaY * 0.0016);
    applyZoomAt(p.x, p.y, mapZoom * factor);
  }

  function onMapPointerDown(e: PointerEvent) {
    e.preventDefault();
    const el = e.currentTarget as HTMLElement;
    el.setPointerCapture(e.pointerId);
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pointers.size === 1) {
      mapDragging = true;
      lastPtrX = e.clientX;
      lastPtrY = e.clientY;
    } else if (pointers.size >= 2) {
      mapDragging = false;
      const pts = [...pointers.values()];
      pinchStartDist = Math.hypot(pts[0].x - pts[1].x, pts[0].y - pts[1].y);
      pinchStartZoom = mapZoom;
    }
  }

  function onMapPointerMove(e: PointerEvent) {
    if (!pointers.has(e.pointerId)) return;
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pointers.size >= 2 && pinchStartDist > 0) {
      const pts = [...pointers.values()];
      const dist = Math.hypot(pts[0].x - pts[1].x, pts[0].y - pts[1].y);
      const mid = { x: (pts[0].x + pts[1].x) / 2, y: (pts[0].y + pts[1].y) / 2 };
      const p = clientToViewBox(mid.x, mid.y);
      if (p && dist > 0) applyZoomAt(p.x, p.y, pinchStartZoom * (dist / pinchStartDist));
      return;
    }
    if (!mapDragging) return;
    const p0 = clientToViewBox(lastPtrX, lastPtrY);
    const p1 = clientToViewBox(e.clientX, e.clientY);
    if (p0 && p1) {
      mapPanX += p1.x - p0.x;
      mapPanY += p1.y - p0.y;
    }
    lastPtrX = e.clientX;
    lastPtrY = e.clientY;
  }

  function onMapPointerUp(e: PointerEvent) {
    pointers.delete(e.pointerId);
    if (pointers.size === 1) {
      const rem = [...pointers.values()][0];
      lastPtrX = rem.x;
      lastPtrY = rem.y;
      mapDragging = true;
      pinchStartDist = 0;
    } else if (pointers.size === 0) {
      mapDragging = false;
      pinchStartDist = 0;
    }
  }

  function hsLabel(): string {
    if (hsReachable) return "REACHABLE";
    if (hsPublished) return "PUBLISHED";
    return "BOOTSTRAP";
  }

  function meshLabel(): string {
    if (!onTor) return "○ WAITING TOR";
    if (youUp && remoteUp > 0) return "● MESH LIVE";
    if (youUp || hsPublished) return "● YOU ARE LIVE";
    return "○ PUBLISHING HS";
  }

  function fmtPing(p: PeerInfo): string {
    if (p.connected_since === "YOU") {
      if (p.latency_ms > 0) return `${p.latency_ms}ms`;
      return hsReachable ? "…" : "HS";
    }
    if (!p.online) return "—";
    if (p.latency_ms > 0) return `${p.latency_ms}ms`;
    return "…";
  }

  function fmtAge(ms: number | undefined): string {
    if (ms == null || ms < 0) return "—";
    const s = Math.floor(ms / 1000);
    if (s < 60) return `${s}s`;
    if (s < 3600) return `${Math.floor(s / 60)}m`;
    return `${Math.floor(s / 3600)}h`;
  }

  function upCell(p: PeerInfo): string {
    if (p.connected_since === "YOU") return hsReachable ? "REACHABLE" : "PUBLISHED";
    if (p.online) return "LIVE " + fmtAge(p.up_ms || 0);
    if (p.connected_since === "SEED") return "DOWN";
    return "SEEN " + fmtAge(p.seen_ago_ms);
  }

  function isNew(p: PeerInfo): boolean {
    return p.connected_since === "PEER" && !!p.online && (p.up_ms || 999999) < 20000;
  }

  async function copyOnion() {
    copyMsg = "";
    if (!ownOnion) return;
    try {
      await navigator.clipboard.writeText(ownOnion + ":8333");
      copyMsg = "COPIED";
    } catch {
      copyMsg = "COPY FAILED";
    }
  }
</script>

<div class="content-area net-page">
  <div class="section-title">THIS SESSION</div>
  <div class="card">
    <div class="card-header">YOUR ONION — NEW EACH LAUNCH. THIS IS HOW YOU SHOW UP.</div>
    <div class="onion-row">
      <div class="card-value onion-line">{ownOnion || "BOOTSTRAPPING TOR…"}</div>
      <button class="btn-secondary" on:click={copyOnion} disabled={!ownOnion}>[ COPY ]</button>
    </div>
    {#if copyMsg}<div class="ok-line">{copyMsg}</div>{/if}
    <div class="hint">Share host:port. Inbound is Tor Port 8333 → 127.0.0.1:{listenPort || "—"}.</div>
  </div>

  <div class="section-title">PEER MAP</div>
  <div class="peer-map">
    <div
      class="map-viewport"
      class:panning={mapDragging}
      role="application"
      aria-label="Peer map"
      on:wheel|preventDefault|stopPropagation={onMapWheel}
      on:pointerdown={onMapPointerDown}
      on:pointermove={onMapPointerMove}
      on:pointerup={onMapPointerUp}
      on:pointercancel={onMapPointerUp}
      on:lostpointercapture={onMapPointerUp}
    >
      <svg
        bind:this={mapSvg}
        viewBox="0 0 {MAP_W} {MAP_H}"
        width="100%"
        height="400"
        preserveAspectRatio="xMidYMid meet"
      >
        <g transform="translate({mapPanX} {mapPanY}) scale({mapZoom})">
          {#each meshMap.edges as e}
            <line
              x1={e.x1}
              y1={e.y1}
              x2={e.x2}
              y2={e.y2}
              stroke={e.hub ? "rgba(230,230,235,0.82)" : "rgba(190,190,198,0.55)"}
              stroke-width={e.hub ? 2.35 : 1.55}
            />
          {/each}
          {#each meshMap.nodes as n}
            <g transform="translate({n.x} {n.y})">
              {#if n.avatar}
                <defs>
                  <clipPath id={clipId(n.id)}><circle r={n.r} /></clipPath>
                </defs>
                <image
                  href={n.avatar}
                  x={-n.r}
                  y={-n.r}
                  width={n.r * 2}
                  height={n.r * 2}
                  clip-path={"url(#" + clipId(n.id) + ")"}
                  preserveAspectRatio="xMidYMid slice"
                />
                <circle
                  r={n.r}
                  fill="none"
                  stroke={n.kind === "you" ? "#30d158" : "rgba(168,168,176,0.85)"}
                  stroke-width={n.kind === "you" ? 2.4 : 1.5}
                />
              {:else}
                <circle
                  r={n.r}
                  fill={n.kind === "you" ? "#30d158" : n.online ? "#8e8e93" : "#4a4a4e"}
                  stroke={n.kind === "you" ? "#30d158" : "rgba(168,168,176,0.55)"}
                  stroke-width={n.kind === "you" ? 2.4 : 1.4}
                />
              {/if}
              {#if n.label}
                <text
                  class="map-label"
                  y={n.r + 16}
                  fill={n.kind === "you" ? "var(--text-primary)" : "var(--text-secondary)"}
                  font-size={n.kind === "you" ? 12 : 11}
                  font-weight={n.kind === "you" ? 600 : 500}
                  font-family="-apple-system, BlinkMacSystemFont, 'SF Pro Text', 'Segoe UI', system-ui, sans-serif"
                  text-anchor="middle"
                >{n.label}</text>
              {/if}
            </g>
          {/each}
          {#if !hsPublished && remotes.length === 0}
            <text class="map-label" x={MAP_W / 2} y={MAP_H / 2 + 6} fill="var(--text-secondary)" font-size="12" font-family="-apple-system, BlinkMacSystemFont, 'SF Pro Text', 'Segoe UI', system-ui, sans-serif" text-anchor="middle">
              NO CELL YET
            </text>
          {/if}
        </g>
      </svg>
    </div>
    <div class="map-hint">Wheel zooms. Drag pans.</div>
    {#if remotes.length === 0 && (youUp || hsPublished)}
      <div class="map-hint">Alone for now. When others join, they fan out around you as a mesh.</div>
    {/if}
  </div>

  <div class="grid-3">
    <div class="card">
      <div class="card-header">HIDDEN SERVICE</div>
      <div class="card-value" class:hs-reachable={hsReachable}>{hsLabel()}</div>
    </div>
    <div class="card">
      <div class="card-header">SOCKS</div>
      <div class="card-value">{socksPort || "—"}</div>
    </div>
    <div class="card">
      <div class="card-header">LISTEN</div>
      <div class="card-value">{listenPort || "—"}</div>
    </div>
  </div>

  <div class="section-title peers-header">
    <span>MESH — YOU {youUp ? "1" : "0"} · REMOTE {remoteUp} UP</span>
    <span class="net-status {onTor ? 'online' : 'connecting'}">{meshLabel()}</span>
  </div>
  <div class="table-wrap">
    <table>
      <thead><tr><th></th><th>NODE</th><th>ROLE</th><th>PING</th><th>UP</th></tr></thead>
      <tbody>
        {#each listed as peer}
          <tr class:live-row={peer.online} class:you-row={peer.connected_since === "YOU"} class:new-row={isNew(peer)}>
            <td class="dot-cell"><span class="peer-dot {peer.online ? 'online' : 'offline'}">{peer.online ? "●" : "○"}</span></td>
            <td>{tableLabel(peer)}
              {#if peer.connected_since === "YOU"}<span class="up-badge">YOU</span>{/if}
              {#if isNew(peer)}<span class="up-badge">NEW</span>{/if}
            </td>
            <td>{peer.connected_since}</td>
            <td>{fmtPing(peer)}</td>
            <td>{upCell(peer)}</td>
          </tr>
        {:else}
          <tr><td colspan="5" class="empty-row">TOR STILL PUBLISHING YOUR ONION</td></tr>
        {/each}
      </tbody>
    </table>
  </div>
  {#if listed.length > 0 && remotes.length === 0}
    <div class="hint">You are the cell. Share your onion so someone can join.</div>
  {/if}

  <div class="section-title">TOR</div>
  <div class="grid-3">
    <div class="card">
      <div class="card-header">BOOTSTRAP</div>
      <div class="card-value">{torStatus.bootstrap}</div>
    </div>
    <div class="card">
      <div class="card-header">CIRCUITS</div>
      <div class="card-value">{torStatus.circuits}</div>
    </div>
    <div class="card">
      <div class="card-header">BRIDGES</div>
      <div class="card-value">{torStatus.bridge_status.toUpperCase()}</div>
    </div>
  </div>

  <div class="section-title">THROUGHPUT</div>
  <div class="grid-3">
    <div class="card">
      <div class="card-header">IN</div>
      <div class="card-value">{bandwidth.inbound_kbps} KB/S</div>
    </div>
    <div class="card">
      <div class="card-header">OUT</div>
      <div class="card-value">{bandwidth.outbound_kbps} KB/S</div>
    </div>
    <div class="card">
      <div class="card-header">PEX</div>
      <div class="card-value">{discovery.peer_exchange}</div>
    </div>
  </div>
</div>

<style>
  /* Sans chrome + glass cards. Graph positions stay the same. */
  .net-page {
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", system-ui, sans-serif);
    font-size: 13px;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    image-rendering: auto;
    padding-bottom: 72px;
  }

  .net-page :global(.section-title) {
    font-family: inherit;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.06em;
  }

  .net-page :global(.card) {
    border-radius: var(--radius, 14px);
    background: var(--surface);
    backdrop-filter: blur(22px) saturate(140%);
    -webkit-backdrop-filter: blur(22px) saturate(140%);
  }

  .net-page :global(.card-header) {
    font-family: inherit;
    font-size: 11px;
    letter-spacing: 0.06em;
  }

  .net-page :global(.card-value) {
    font-size: 20px;
    font-weight: 600;
    letter-spacing: 0;
  }

  .net-page :global(button) {
    font-family: inherit;
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0;
    border-radius: var(--radius-sm, 10px);
    image-rendering: auto;
    -webkit-font-smoothing: antialiased;
    transition:
      background var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1)),
      border-color var(--dur, 280ms) var(--ease, cubic-bezier(0.22, 1, 0.36, 1));
  }

  .net-page :global(input) {
    font-family: var(--font-mono, ui-monospace, "SF Mono", SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace);
    font-size: 13px;
    letter-spacing: 0;
    border-radius: var(--radius-sm, 10px);
    image-rendering: auto;
    -webkit-font-smoothing: antialiased;
  }

  .net-page :global(table) {
    font-size: 13px;
  }

  .net-page :global(th) {
    font-family: inherit;
    font-size: 11px;
    letter-spacing: 0.06em;
  }

  .net-page :global(code) {
    font-family: var(--font-mono, ui-monospace, "SF Mono", SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace);
    font-size: 12px;
  }

  .peers-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
  }

  .net-status {
    font-family: inherit;
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0.02em;
  }

  .net-status.online {
    color: #00c853;
  }

  .net-status.connecting {
    color: var(--text-secondary);
  }

  .dot-cell {
    width: 18px;
    text-align: center;
    padding: 0 4px;
  }

  .peer-dot {
    font-size: 10px;
  }

  .peer-dot.online {
    color: #00c853;
  }

  .peer-dot.offline {
    color: #555;
  }

  .peer-map {
    border: 1px solid var(--border);
    border-radius: var(--radius, 14px);
    padding: 8px 8px 4px;
    margin-top: 4px;
    background: #0a0a0a;
    overflow: hidden;
  }

  .map-viewport {
    height: 400px;
    overflow: hidden;
    touch-action: none;
    overscroll-behavior: contain;
    user-select: none;
    cursor: grab;
  }

  .map-viewport.panning {
    cursor: grabbing;
  }

  .peer-map svg {
    display: block;
    height: 400px;
  }

  .map-hint {
    padding: 0 10px 10px;
    font-size: 12px;
    color: var(--text-faint);
    letter-spacing: 0;
    line-height: 1.4;
  }

  .table-wrap {
    border: 1px solid var(--border);
    border-radius: var(--radius, 14px);
    overflow: hidden;
    background: var(--surface);
    backdrop-filter: blur(22px) saturate(140%);
    -webkit-backdrop-filter: blur(22px) saturate(140%);
  }

  .table-wrap :global(th),
  .table-wrap :global(td) {
    border-bottom-color: var(--border);
  }

  .table-wrap :global(tr:last-child td) {
    border-bottom: none;
  }

  .map-label {
    font-family: var(--font, -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", system-ui, sans-serif);
    -webkit-font-smoothing: antialiased;
  }

  .empty-row {
    text-align: center;
    color: var(--text-secondary);
    padding: 16px;
  }

  .live-row code {
    color: var(--text-primary);
  }

  .you-row {
    background: color-mix(in srgb, var(--ok) 8%, transparent);
  }

  .new-row {
    background: color-mix(in srgb, var(--ok) 10%, transparent);
  }

  .up-badge {
    font-size: 10px;
    font-weight: 600;
    padding: 2px 7px;
    background: var(--ok);
    color: #000;
    letter-spacing: 0.04em;
    margin-left: 8px;
    display: inline-block;
    vertical-align: middle;
    border-radius: 999px;
  }

  .onion-line {
    font-family: var(--font-mono, ui-monospace, "SF Mono", SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace);
    font-size: 13px;
    letter-spacing: 0;
    word-break: break-all;
    line-height: 1.4;
    flex: 1;
  }

  .onion-row {
    display: flex;
    gap: 8px;
    align-items: flex-start;
  }

  .hint {
    margin-top: 8px;
    font-size: 12px;
    color: var(--text-secondary);
    line-height: 1.5;
    letter-spacing: 0;
  }

  .ok-line {
    margin-top: 6px;
    font-size: 12px;
    color: var(--ok);
    letter-spacing: 0;
  }

  .hs-reachable {
    color: #00c853;
  }
</style>
