<script lang="ts">
  // NAAN cell map. Rooms follow the harvest path. Walker is naan-walk.svg.
  import { onMount } from "svelte";
  import naanWalk from "../../../assets/sprites/naan-walk.svg";

  export let status: string = "OFF";
  export let task: string = "";
  export let lastLog: string = "";

  const TILE = 16;
  const COLS = 26;
  const ROWS = 12;

  type RoomId = "bed" | "tor" | "lymph" | "recipe" | "poe" | "hall";
  type Room = {
    id: RoomId;
    name: string;
    sn: string;
    x1: number;
    y1: number;
    x2: number;
    y2: number;
    fill: string;
  };

  const ROOMS: Room[] = [
    { id: "bed", name: "BED", sn: "IDLE", x1: 0, y1: 4, x2: 7, y2: 11, fill: "#3a3b41" },
    { id: "tor", name: "TOR", sn: "FETCH", x1: 10, y1: 0, x2: 16, y2: 5, fill: "#34383a" },
    { id: "lymph", name: "LYMPH", sn: "GATE", x1: 10, y1: 7, x2: 16, y2: 11, fill: "#3a302a" },
    { id: "recipe", name: "RECIPE", sn: "DRAFT", x1: 18, y1: 0, x2: 25, y2: 5, fill: "#3a302a" },
    { id: "poe", name: "POE", sn: "VOTE", x1: 18, y1: 7, x2: 25, y2: 11, fill: "#2b3340" },
    { id: "hall", name: "HALL", sn: "", x1: 8, y1: 6, x2: 9, y2: 8, fill: "#31333a" },
  ];

  const HALLS = [
    { x1: 8, y1: 6, x2: 9, y2: 8 },
    { x1: 13, y1: 5, x2: 13, y2: 7 },
    { x1: 16, y1: 2, x2: 17, y2: 3 },
    { x1: 16, y1: 8, x2: 17, y2: 9 },
  ];

  const DESK = [
    { label: "IN", x: 11, y: 2 },
    { label: "BAY", x: 13, y: 2 },
    { label: "OUT", x: 15, y: 2 },
  ];

  const W = COLS * TILE;
  const H = ROWS * TILE;

  const reduceMotion =
    typeof matchMedia !== "undefined" &&
    matchMedia("(prefers-reduced-motion: reduce)").matches;

  function inferRoom(s: string, t: string, log: string): RoomId {
    const st = (s || "").toUpperCase();
    const blob = `${t} ${log}`.toLowerCase();
    if (st === "QUARANTINE") return "lymph";
    if (st !== "ACTIVE") return "bed";
    if (/fetch fail|failed/.test(blob)) return "tor";
    if (/fetch|tor|onion|harvest|http|clearnet/.test(blob)) return "tor";
    if (/lymph|replay|hash|gate|captcha|ocr/.test(blob)) return "lymph";
    if (/extract|recipe|draft|submit/.test(blob)) return "recipe";
    if (/poe|vote|final|know|accept/.test(blob)) return "poe";
    return "tor";
  }

  function tileColor(x: number, y: number): string {
    for (const r of ROOMS) {
      if (x >= r.x1 && x <= r.x2 && y >= r.y1 && y <= r.y2) return r.fill;
    }
    for (const h of HALLS) {
      if (x >= h.x1 && x <= h.x2 && y >= h.y1 && y <= h.y2) return "#31333a";
    }
    return "#000000";
  }

  $: roomId = inferRoom(status, task, lastLog);
  $: room = ROOMS.find((r) => r.id === roomId) || ROOMS[0];
  $: walking = (status || "").toUpperCase() === "ACTIVE";
  $: face = room.id === "bed" ? -1 : 1;
  $: hereX = ((room.x1 + room.x2) / 2 / COLS) * 100;
  $: hereY = ((room.y1 + room.y2) / 2 / ROWS) * 100;

  let canvas: HTMLCanvasElement | null = null;

  function paintDeck() {
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    ctx.imageSmoothingEnabled = false;
    ctx.fillStyle = "#000000";
    ctx.fillRect(0, 0, W, H);
    for (let y = 0; y < ROWS; y++) {
      for (let x = 0; x < COLS; x++) {
        const c = tileColor(x, y);
        if (c === "#000000") continue;
        ctx.fillStyle = c;
        ctx.fillRect(x * TILE, y * TILE, TILE, TILE);
        if ((x + y) % 2 === 0) {
          ctx.fillStyle = "rgba(0,0,0,0.22)";
          ctx.fillRect(x * TILE, y * TILE, TILE, 1);
          ctx.fillRect(x * TILE, y * TILE, 1, TILE);
        }
      }
    }
  }

  onMount(() => {
    paintDeck();
  });

  $: if (canvas) paintDeck();

  function pctBox(r: Room): string {
    const left = (r.x1 / COLS) * 100;
    const top = (r.y1 / ROWS) * 100;
    const w = ((r.x2 - r.x1 + 1) / COLS) * 100;
    const h = ((r.y2 - r.y1 + 1) / ROWS) * 100;
    return `left:${left}%;top:${top}%;width:${w}%;height:${h}%`;
  }
</script>

<div class="station-wrap">
  <div class="station-head">
    <span class="station-title">CELL</span>
    <span class="station-where">{room.name} · {room.sn}</span>
  </div>

  <div class="station" style="aspect-ratio:{COLS}/{ROWS}" aria-label="NAAN cell">
    <canvas class="deck" bind:this={canvas} width={W} height={H} aria-hidden="true"></canvas>

    {#each ROOMS as r}
      {#if r.id !== "hall"}
        <div class="room-label" class:on={r.id === roomId} style={pctBox(r)}>
          <span class="rn">{r.name}</span>
          <span class="rs">{r.sn}</span>
        </div>
      {/if}
    {/each}

    {#each DESK as p}
      <div
        class="desk"
        style="left:{(p.x / COLS) * 100}%;top:{(p.y / ROWS) * 100}%;width:{(2 / COLS) * 100}%;height:{(1 / ROWS) * 100}%"
      >
        {p.label}
      </div>
    {/each}

    <div
      class="walker"
      class:bob={walking && !reduceMotion}
      style="left:{hereX}%;top:{hereY}%;transform:translate(-50%,-80%) scaleX({face})"
    >
      <img src={naanWalk} alt="" width="20" height="20" draggable="false" />
    </div>
  </div>

  {#if task}
    <div class="station-task">{task}</div>
  {/if}
</div>

<style>
  .station-wrap {
    margin: 0 0 14px;
    padding: 0;
  }

  .station-head {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 10px;
    margin-bottom: 8px;
  }

  .station-title,
  .station-where,
  .rn,
  .rs,
  .desk,
  .station-task {
    font-family: var(--font);
    letter-spacing: 0;
  }

  .station-title {
    font-size: 9px;
    color: var(--text-secondary);
  }

  .station-where {
    font-size: 8px;
    color: var(--cy, #00e5ff);
  }

  .station {
    position: relative;
    width: 100%;
    background: #000;
    border: 1px solid var(--border);
    overflow: hidden;
    image-rendering: pixelated;
  }

  .deck {
    display: block;
    width: 100%;
    height: 100%;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
  }

  .room-label {
    position: absolute;
    display: flex;
    flex-direction: column;
    justify-content: flex-start;
    padding: 3px 4px;
    pointer-events: none;
    border: 1px solid rgba(255, 255, 255, 0.08);
    box-sizing: border-box;
  }

  .room-label.on {
    border-color: var(--cy, #00e5ff);
  }

  .rn {
    font-size: 7px;
    color: rgba(245, 245, 247, 0.72);
  }

  .rs {
    font-size: 6px;
    color: rgba(161, 161, 166, 0.9);
    margin-top: 2px;
  }

  .desk {
    position: absolute;
    font-size: 6px;
    color: #000;
    background: #c8c8cc;
    display: flex;
    align-items: center;
    justify-content: center;
    pointer-events: none;
    box-sizing: border-box;
  }

  .walker {
    position: absolute;
    width: 7.7%;
    z-index: 3;
    transition: left 0.7s linear, top 0.7s linear;
    image-rendering: pixelated;
    pointer-events: none;
  }

  .walker img {
    width: 100%;
    height: auto;
    display: block;
    image-rendering: pixelated;
  }

  .walker.bob img {
    animation: naan-bob 0.45s steps(2) infinite;
  }

  @keyframes naan-bob {
    0% {
      transform: translateY(0);
    }
    50% {
      transform: translateY(-1px);
    }
    100% {
      transform: translateY(0);
    }
  }

  .station-task {
    margin-top: 8px;
    font-size: 8px;
    color: var(--text-secondary);
    line-height: 1.5;
    overflow-wrap: anywhere;
  }

  @media (prefers-reduced-motion: reduce) {
    .walker {
      transition: none;
    }
    .walker.bob img {
      animation: none;
    }
  }
</style>
