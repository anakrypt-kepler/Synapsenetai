<script lang="ts">
  // Harvest map in a 16:9 orbit field. Rooms stay square; only convex hull
  // corners get a tiny 45° chamfer (dump SHAPE.cornerN = 1). Floor stamps the
  // 8-period atlas. Props sit on integer tiles with dump bounds. Walk uses
  // foot anchors + gait ease, not tile-index crawling. Hull extras (solar
  // wings, dish, amber windows) are canvas dress — not walkable rooms.
  import { onDestroy, onMount } from "svelte";
  import {
    stationLook,
    findAgent,
    floorSrc,
    wallSrc,
    shellSrc,
    propSrc,
    inferHarvestRoom,
    stationCatalog,
    agentPoseSrc,
    harvestPose,
    agentShortName,
  } from "../../../lib/stationSkins";
  import type { Facing, HarvestRoomId } from "../../../lib/stationSkins";
  import { paintVoid } from "../../../lib/spaceVoid";
  import propViews from "../../../lib/propViews.json";
  import stationLayout from "../../../lib/stationLayout.json";
  import {
    naanRooms,
    naanCrew,
    loadNaanDeckFromSettings,
    type NaanCrewMember,
  } from "../../../lib/naanCrew";
  import { harvestLevel } from "../../../lib/naanXp";
  import { PRIMARY_NAAN_ID } from "../../../lib/synapseHolo";
  import AgentLevelChip from "./AgentLevelChip.svelte";

  export let status: string = "OFF";
  export let agentActive: Record<string, boolean> = {};
  export let task: string = "";
  export let lastLog: string = "";
  export let submissions: number = 0;
  export let ngt: number = 0;
  // Per-body harvest tallies when the parent has them. Missing keys still show Lv.
  export let crewSubmissions: Record<string, number> = {};
  export let focusedId: string = PRIMARY_NAAN_ID;
  export let nowLine: string = "";

  const TILE = Number(stationLayout.tile) || 16;
  const DUMP_T = Number(stationLayout.dumpT) || 12;
  const VIEW_COLS = Number(stationLayout.viewCols) || 80;
  const VIEW_ROWS = Number(stationLayout.viewRows) || 48;
  // Campus packer. JSON gridRows is one suite, not the whole station.
  const SUITE_W = Number((stationLayout as { suiteW?: number }).suiteW) || 25;
  const SUITE_H = Number((stationLayout as { suiteH?: number }).suiteH) || 12;
  const SPINE = Number((stationLayout as { spine?: number }).spine) || 3;
  const CELL_W = SUITE_W + SPINE;
  const CELL_H = SUITE_H + SPINE;
  const VW = VIEW_COLS * TILE;
  const VH = VIEW_ROWS * TILE;
  const SPEED = 34 / DUMP_T;
  const ACCEL = 150 / DUMP_T;
  const CORNER_LOOK = 2.5 / DUMP_T;
  const STRIDE = 1.4 / 8;
  const THINK = 0.55;
  const SETTLE = 0.7;
  const ZMIN = 0.7;
  const ZMAX = 3.4;
  const ZFIT = 1;
  const CHAM = Math.max(5, Math.round(TILE * 0.5));
  const DIR_A: Record<Facing, number> = {
    east: 0,
    "south-east": Math.PI / 4,
    south: Math.PI / 2,
    "south-west": Math.PI * 3 / 4,
    west: Math.PI,
    "north-west": -Math.PI * 3 / 4,
    north: -Math.PI / 2,
    "north-east": -Math.PI / 4,
  };

  type RoomKind = HarvestRoomId | "hall" | "hold";
  type Room = {
    id: string;
    kind: RoomKind;
    owner: string;
    name: string;
    sn: string;
    x1: number;
    y1: number;
    x2: number;
    y2: number;
  };
  type Hall = { x1: number; y1: number; x2: number; y2: number };
  type PropView = { w: number; h: number; bx: number; by: number; bw: number; bh: number };
  const VIEWS = propViews as Record<string, PropView>;

  // Data-driven room layout from stationLayout.json
  type LayoutLight = { tx: number; ty: number; r: number; rgb: string; a: number };
  type LayoutRoom = {
    x1: number; y1: number; x2: number; y2: number;
    seat: { tx: number; ty: number; face: string; work: boolean };
    blocks: number[][];
    props: Array<{ file: string; tx: number; ty: number; role: string }>;
    lights?: LayoutLight[];
  };
  // Warm/cold pools thrown on the deck by lamps, screens and vats. Painted in
  // world pixels so they follow a room when a crew suite is packed on campus.
  type DeckLight = LayoutLight & { owner: string };
  const LAYOUT_ROOMS = stationLayout.rooms as Record<string, LayoutRoom>;
  const LAYOUT_CORRIDORS = stationLayout.corridors as Array<{ x1: number; y1: number; x2: number; y2: number; label: string | null }>;
  const originY = Number((stationLayout as { originY?: number }).originY) || 10;

  function layoutRoom(kind: HarvestRoomId): LayoutRoom {
    return LAYOUT_ROOMS[kind] || LAYOUT_ROOMS.bed;
  }

  // Visual-only 7x5 locker if JSON hold has not landed yet.
  const HOLD_FALLBACK: LayoutRoom = {
    x1: 0, y1: 0, x2: 6, y2: 4,
    seat: { tx: 3, ty: 3, face: "south", work: false },
    blocks: [
      [0, 0, 3, 1],
      [4, 0, 2, 1],
      [0, 4, 2, 1],
    ],
    props: [
      { file: "quarters_lockerbank.png", tx: 0, ty: 0, role: "dress" },
      { file: "industrial_locker.png", tx: 4, ty: 0, role: "dress" },
      { file: "crate.png", tx: 0, ty: 4, role: "dress" },
    ],
  };

  type PlaceKind = HarvestRoomId | "hold";

  function layoutVisual(kind: PlaceKind): LayoutRoom {
    if (kind === "hold") return LAYOUT_ROOMS.hold || HOLD_FALLBACK;
    return layoutRoom(kind);
  }

  function visualSpec(kind: PlaceKind): { prop: string | null; terminal: string | null; chair?: string | null; title: string; sn: string } {
    if (kind !== "hold") return stationCatalog.rooms[kind];
    const hit = (stationCatalog as { rooms: Record<string, { prop: string | null; terminal: string | null; chair?: string | null; title: string; sn: string }> }).rooms.hold;
    return hit || { prop: "quarters_lockerbank.png", terminal: null, chair: null, title: "HOLD", sn: "STASH" };
  }

  const BASE: Record<HarvestRoomId, Room> = {} as Record<HarvestRoomId, Room>;
  const BASE_SEATS: Record<HarvestRoomId, Seat> = {} as Record<HarvestRoomId, Seat>;
  const ROOM_BLOCKS: Record<HarvestRoomId, Array<[number, number, number, number]>> = {} as Record<HarvestRoomId, Array<[number, number, number, number]>>;

  for (const kind of ["bed", "tor", "lymph", "recipe", "poe"] as HarvestRoomId[]) {
    const lr = layoutRoom(kind);
    const spec = stationCatalog.rooms[kind];
    BASE[kind] = {
      id: kind, kind, owner: "primary",
      name: spec.title, sn: spec.sn,
      x1: lr.x1, y1: lr.y1, x2: lr.x2, y2: lr.y2,
    };
    BASE_SEATS[kind] = {
      tx: lr.seat.tx, ty: lr.seat.ty,
      face: lr.seat.face as Facing,
      work: lr.seat.work,
    };
    ROOM_BLOCKS[kind] = lr.blocks.map((b) => b as unknown as [number, number, number, number]);
  }

  const PRIMARY_HALL: Room = {
    id: "hall", kind: "hall", owner: "primary", name: "HALL", sn: "",
    x1: stationLayout.hall.x1, y1: stationLayout.hall.y1,
    x2: stationLayout.hall.x2, y2: stationLayout.hall.y2,
  };

  const PRIMARY_HALLS: Hall[] = LAYOUT_CORRIDORS.map((c) => ({
    x1: c.x1, y1: c.y1, x2: c.x2, y2: c.y2,
  }));

  type Seat = { tx: number; ty: number; face: Facing; work: boolean };
  const PRIMARY_HALL_SEAT: Seat = {
    tx: stationLayout.hallSeat.tx, ty: stationLayout.hallSeat.ty,
    face: stationLayout.hallSeat.face as Facing,
    work: stationLayout.hallSeat.work,
  };

  const holdLr = layoutVisual("hold");
  const HOLD_BASE: Room = {
    id: "hold", kind: "hold", owner: "primary",
    name: visualSpec("hold").title, sn: visualSpec("hold").sn,
    x1: holdLr.x1, y1: holdLr.y1, x2: holdLr.x2, y2: holdLr.y2,
  };
  const HOLD_SEAT: Seat = {
    tx: holdLr.seat.tx, ty: holdLr.seat.ty,
    face: holdLr.seat.face as Facing,
    work: holdLr.seat.work,
  };
  const HOLD_BLOCKS: Array<[number, number, number, number]> = holdLr.blocks.map((b) => b as unknown as [number, number, number, number]);

  type Overlay = {
    room: string;
    kind: RoomKind;
    owner: string;
    src: string;
    file: string;
    tx: number;
    ty: number;
    prop: "prop" | "terminal" | "chair" | "dress";
  };

  function viewOf(file: string): PropView {
    return VIEWS[file] || { w: 1, h: 1, bx: 0, by: 0, bw: DUMP_T, bh: DUMP_T };
  }

  function baseOverlays(kind: PlaceKind, owner: string, roomId: string): Overlay[] {
    const out: Overlay[] = [];
    const spec = visualSpec(kind);
    const lr = layoutVisual(kind);
    const put = (file: string | null | undefined, tx: number, ty: number, prop: Overlay["prop"]) => {
      const src = propSrc(file);
      if (src && file) out.push({ room: roomId, kind, owner, src, file, tx, ty, prop });
    };
    for (const p of lr.props) {
      let file: string | null = p.file;
      if (file === "$prop") file = spec.prop;
      else if (file === "$terminal") file = spec.terminal;
      else if (file === "$chair") file = spec.chair || null;
      const role = (p.role === "terminal" || p.role === "chair" || p.role === "prop") ? p.role : "dress" as Overlay["prop"];
      put(file, p.tx, p.ty, role);
    }
    return out;
  }

  function shiftInto(ov: Overlay, from: Room, to: Room): Overlay {
    const dx = to.x1 - from.x1;
    const dy = to.y1 - from.y1;
    const v = viewOf(ov.file);
    let tx = ov.tx + dx;
    let ty = ov.ty + dy;
    const maxX = Math.max(to.x1, to.x2 - Math.max(0, v.w - 1));
    const maxY = Math.max(to.y1, to.y2 - Math.max(0, v.h - 1));
    tx = Math.max(to.x1, Math.min(maxX, tx));
    ty = Math.max(to.y1, Math.min(maxY, ty));
    return { ...ov, tx, ty, owner: to.owner, room: to.id, kind: to.kind };
  }

  function wingRoom(owner: string, kind: HarvestRoomId, x1: number, y1: number, x2: number, y2: number): Room {
    const spec = stationCatalog.rooms[kind];
    return {
      id: owner + ":" + kind,
      kind,
      owner,
      name: spec.title,
      sn: spec.sn,
      x1,
      y1,
      x2,
      y2,
    };
  }

  function clampSeat(s: Seat, room: Room): Seat {
    return {
      tx: Math.max(room.x1, Math.min(room.x2, s.tx)),
      ty: Math.max(room.y1, Math.min(room.y2, s.ty)),
      face: s.face,
      work: s.work,
    };
  }

  function shiftSeat(kind: PlaceKind, dest: Room): Seat {
    const src = kind === "hold" ? HOLD_BASE : BASE[kind];
    const base = kind === "hold" ? HOLD_SEAT : BASE_SEATS[kind];
    return clampSeat({
      tx: base.tx - src.x1 + dest.x1,
      ty: base.ty - src.y1 + dest.y1,
      face: base.face,
      work: base.work,
    }, dest);
  }

  type Deck = {
    rooms: Room[];
    halls: Hall[];
    overlays: Overlay[];
    ox: number;
    stCols: number;
    stRows: number;
    occ: Uint8Array;
    seats: Record<string, Partial<Record<RoomKind, Seat>>>;
    walk: Record<string, Set<string>>;
    enabled: Record<string, HarvestRoomId[]>;
    walkHalls: Record<string, Hall[]>;
    lights: DeckLight[];
  };

  function markWalk(set: Set<string>, x1: number, y1: number, x2: number, y2: number) {
    for (let y = y1; y <= y2; y++) for (let x = x1; x <= x2; x++) set.add(x + "," + y);
  }

  function blockFoot(set: Set<string>, tx: number, ty: number, w: number, h: number) {
    for (let y = ty; y < ty + h; y++) for (let x = tx; x < tx + w; x++) set.delete(x + "," + y);
  }

  function placeBlocks(set: Set<string>, kind: PlaceKind, dest: Room) {
    const src = kind === "hold" ? HOLD_BASE : BASE[kind];
    const dx = dest.x1 - src.x1;
    const dy = dest.y1 - src.y1;
    const blocks = kind === "hold" ? HOLD_BLOCKS : ROOM_BLOCKS[kind];
    for (const [x, y, w, h] of blocks) {
      const nx = x + dx;
      const ny = y + dy;
      const x0 = Math.max(nx, dest.x1);
      const y0 = Math.max(ny, dest.y1);
      const x1 = Math.min(nx + w - 1, dest.x2);
      const y1 = Math.min(ny + h - 1, dest.y2);
      if (x1 >= x0 && y1 >= y0) blockFoot(set, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
  }

  function roomLights(kind: PlaceKind, owner: string, dest: Room): DeckLight[] {
    const src = kind === "hold" ? HOLD_BASE : BASE[kind];
    const dx = dest.x1 - src.x1;
    const dy = dest.y1 - src.y1;
    const out: DeckLight[] = [];
    for (const l of layoutVisual(kind).lights || []) {
      const tx = l.tx + dx;
      const ty = l.ty + dy;
      if (tx < dest.x1 || tx > dest.x2 || ty < dest.y1 || ty > dest.y2) continue;
      out.push({ ...l, tx, ty, owner });
    }
    return out;
  }

  function occupancyGrid(stCols: number, stRows: number, rooms: Room[], halls: Hall[]): Uint8Array {
    const occ = new Uint8Array(Math.max(0, stCols * stRows));
    const stamp = (x1: number, y1: number, x2: number, y2: number) => {
      const xa = Math.max(0, Math.min(x1, x2));
      const xb = Math.min(stCols - 1, Math.max(x1, x2));
      const ya = Math.max(0, Math.min(y1, y2));
      const yb = Math.min(stRows - 1, Math.max(y1, y2));
      if (xb < xa || yb < ya) return;
      for (let y = ya; y <= yb; y++) {
        const row = y * stCols;
        for (let x = xa; x <= xb; x++) occ[row + x] = 1;
      }
    };
    for (const r of rooms) stamp(r.x1, r.y1, r.x2, r.y2);
    for (const h of halls) stamp(h.x1, h.y1, h.x2, h.y2);
    return occ;
  }

  function campusSlot(slot: number): { col: number; row: number; bx: number; by: number } {
    const colsPerRow = 1 + Math.floor((VIEW_COLS - 2 - SUITE_W) / CELL_W);
    const col = slot % colsPerRow;
    const row = Math.floor(slot / colsPerRow);
    return { col, row, bx: col * CELL_W, by: row * CELL_H };
  }

  function buildDeck(enabledPrimary: HarvestRoomId[], crew: NaanCrewMember[]): Deck {
    const rooms: Room[] = [PRIMARY_HALL];
    const halls: Hall[] = PRIMARY_HALLS.map((h) => ({ ...h }));
    const walkHalls: Record<string, Hall[]> = {
      primary: PRIMARY_HALLS.map((h) => ({ ...h })),
    };
    const overlays: Overlay[] = [];
    const seats: Record<string, Partial<Record<RoomKind, Seat>>> = {
      primary: { hall: PRIMARY_HALL_SEAT },
    };
    const enabled: Record<string, HarvestRoomId[]> = {
      primary: enabledPrimary.length ? [...enabledPrimary] : ["bed"],
    };
    const walk: Record<string, Set<string>> = { primary: new Set() };
    const lights: DeckLight[] = [];

    for (const id of enabled.primary) {
      const r = { ...BASE[id] };
      rooms.push(r);
      seats.primary[id] = { ...BASE_SEATS[id] };
      overlays.push(...baseOverlays(id, "primary", id));
      lights.push(...roomLights(id, "primary", r));
    }

    // Extra crew occupy campus slots 1..n. Primary stays on JSON coords at slot 0.
    const occupied: Array<{ col: number; row: number; bx: number; by: number }> = [
      { ...campusSlot(0) },
    ];
    crew.forEach((c, i) => {
      const pos = campusSlot(i + 1);
      occupied.push(pos);
      const { bx, by } = pos;
      const want: HarvestRoomId[] = c.rooms.length ? [...c.rooms] : ["tor"];
      enabled[c.id] = want;
      seats[c.id] = {};
      walk[c.id] = new Set();

      const hallY = by + 6;
      const northY1 = by;
      const northY2 = by + 5;
      const southY1 = by + 7;
      const southY2 = by + 11;
      const colX = [
        { x1: bx + 2, x2: bx + 8 },
        { x1: bx + 10, x2: bx + 16 },
        { x1: bx + 18, x2: bx + 24 },
      ];

      // Solid 25x12 hull: west pad, gap cols, hall row, and empty room slots.
      const plaza: Hall = { x1: bx, y1: by, x2: bx + SUITE_W - 1, y2: by + SUITE_H - 1 };
      const localHalls: Hall[] = [plaza];
      halls.push(plaza);
      walkHalls[c.id] = localHalls;

      const hallRoom: Room = {
        id: c.id + ":hall",
        kind: "hall",
        owner: c.id,
        name: "HALL",
        sn: "",
        x1: bx,
        y1: hallY,
        x2: bx + 1,
        y2: hallY + 2,
      };
      rooms.push(hallRoom);
      seats[c.id].hall = { tx: bx, ty: hallY + 1, face: "south", work: false };

      const putHarvest = (kind: HarvestRoomId, x1: number, y1: number, x2: number, y2: number) => {
        if (!want.includes(kind)) return;
        const dest = wingRoom(c.id, kind, x1, y1, x2, y2);
        rooms.push(dest);
        seats[c.id][kind] = shiftSeat(kind, dest);
        overlays.push(...baseOverlays(kind, c.id, dest.id).map((ov) => shiftInto(ov, BASE[kind], dest)));
        lights.push(...roomLights(kind, c.id, dest));
      };

      // North 3-across: TOR, RECIPE, POE. South: LYMPH, HOLD, BED.
      putHarvest("tor", colX[0].x1, northY1, colX[0].x2, northY2);
      putHarvest("recipe", colX[1].x1, northY1, colX[1].x2, northY2);
      putHarvest("poe", colX[2].x1, northY1, colX[2].x2, northY2);
      putHarvest("lymph", colX[0].x1, southY1, colX[0].x2, southY2);
      putHarvest("bed", colX[2].x1, southY1, colX[2].x2, southY2);

      const holdDest: Room = {
        id: c.id + ":hold",
        kind: "hold",
        owner: c.id,
        name: visualSpec("hold").title,
        sn: visualSpec("hold").sn,
        x1: colX[1].x1, y1: southY1, x2: colX[1].x2, y2: southY2,
      };
      rooms.push(holdDest);
      seats[c.id].hold = shiftSeat("hold", holdDest);
      overlays.push(...baseOverlays("hold", c.id, holdDest.id).map((ov) => shiftInto(ov, HOLD_BASE, holdDest)));
      lights.push(...roomLights("hold", c.id, holdDest));

      for (const kind of want) {
        if (!seats[c.id][kind]) seats[c.id][kind] = seats[c.id].hall;
      }
    });

    // 3-tile collars between orthogonally adjacent occupied slots. No long void bridges.
    for (let i = 0; i < occupied.length; i++) {
      for (let j = i + 1; j < occupied.length; j++) {
        const a = occupied[i];
        const b = occupied[j];
        const adjH = a.row === b.row && Math.abs(a.col - b.col) === 1;
        const adjV = a.col === b.col && Math.abs(a.row - b.row) === 1;
        if (!adjH && !adjV) continue;
        if (adjH) {
          const left = a.bx < b.bx ? a : b;
          halls.push({
            x1: left.bx + SUITE_W,
            y1: left.by + 5,
            x2: left.bx + SUITE_W + SPINE - 1,
            y2: left.by + 7,
          });
        } else {
          const top = a.by < b.by ? a : b;
          halls.push({
            x1: top.bx + 7,
            y1: top.by + SUITE_H,
            x2: top.bx + 9,
            y2: top.by + SUITE_H + SPINE - 1,
          });
        }
      }
    }

    let maxX = 0;
    let maxY = 0;
    for (const r of rooms) {
      if (r.x2 > maxX) maxX = r.x2;
      if (r.y2 > maxY) maxY = r.y2;
    }
    for (const h of halls) {
      if (h.x2 > maxX) maxX = h.x2;
      if (h.y2 > maxY) maxY = h.y2;
    }
    const stCols = maxX + 1;
    const stRows = maxY + 1;
    let ox = Math.max(1, Math.floor((VIEW_COLS - stCols) / 2));
    if (ox + stCols > VIEW_COLS) ox = Math.max(1, VIEW_COLS - stCols);

    const paintRooms = rooms.filter((r) => r.kind === "hall" || r.kind === "hold" || enabled[r.owner]?.includes(r.kind as HarvestRoomId));
    for (const owner of Object.keys(walk)) {
      const set = walk[owner];
      for (const r of rooms) {
        if (r.owner !== owner) continue;
        if (r.kind !== "hall" && r.kind !== "hold" && !enabled[owner]?.includes(r.kind as HarvestRoomId)) continue;
        markWalk(set, r.x1, r.y1, r.x2, r.y2);
      }
      for (const h of walkHalls[owner] || []) {
        markWalk(set, h.x1, h.y1, h.x2, h.y2);
      }
      for (const r of rooms) {
        if (r.owner !== owner || r.kind === "hall") continue;
        if (r.kind !== "hold" && !enabled[owner]?.includes(r.kind as HarvestRoomId)) continue;
        placeBlocks(set, r.kind as PlaceKind, r);
      }
    }

    overlays.sort((a, b) => a.ty + viewOf(a.file).h - (b.ty + viewOf(b.file).h));
    const occ = occupancyGrid(stCols, stRows, paintRooms, halls);
    return { rooms: paintRooms, halls, overlays, ox, stCols, stRows, occ, seats, walk, enabled, walkHalls, lights };
  }

  const reduceMotion =
    typeof matchMedia !== "undefined" &&
    matchMedia("(prefers-reduced-motion: reduce)").matches;

  $: look = $stationLook;
  $: agent = findAgent(look.agent);
  $: inferred = inferHarvestRoom(status, task, lastLog);
  $: active = (status || "").toUpperCase() === "ACTIVE";
  function harvestOn(owner: string): boolean {
    if (owner === PRIMARY_NAAN_ID || owner === "primary") return active;
    return !!agentActive[owner];
  }

  function isPrimaryOwner(owner: string): boolean {
    return owner === PRIMARY_NAAN_ID || owner === "primary";
  }

  function bodyXp(owner: string) {
    const primary = isPrimaryOwner(owner);
    const mapped = primary
      ? (crewSubmissions[PRIMARY_NAAN_ID] ?? crewSubmissions.primary ?? submissions)
      : (crewSubmissions[owner] ?? 0);
    return harvestLevel({ submissions: mapped, ngt: primary ? ngt : 0 });
  }

  let liveRooms: Room[] = [{ ...BASE.bed }, { ...BASE.tor }, { ...BASE.lymph }, { ...BASE.recipe }, { ...BASE.poe }];
  let liveHalls: Hall[] = PRIMARY_HALLS.map((h) => ({ ...h }));
  let overlays: Overlay[] = [];
  let ox = 19;
  const oy = originY;
  let deck: Deck = buildDeck(["bed", "tor", "lymph", "recipe", "poe"], []);
  let stCols = deck.stCols;
  let stRows = deck.stRows;
  let occ = deck.occ;
  let goalHot: Record<string, RoomKind> = { primary: "bed" };
  let frameBump = 0;
  let poseClock = 0;

  type Walker = {
    id: string;
    owner: string;
    skin: string;
    px: number;
    py: number;
    dir: Facing;
    faceA: number;
    spd: number;
    odo: number;
    sitting: boolean;
    working: boolean;
    path: Array<{ x: number; y: number }>;
    frame: number;
    lastGoal: string;
    thinkLeft: number;
    settleLeft: number;
    roamIdx: number;
    roamTimer: number;
  };

  function makeWalker(id: string, owner: string, skin: string, seat: Seat): Walker {
    const f = footOf(seat.tx, seat.ty);
    return {
      id,
      owner,
      skin,
      px: f.x,
      py: f.y,
      dir: seat.face,
      faceA: DIR_A[seat.face],
      spd: 0,
      odo: 0,
      sitting: true,
      working: false,
      path: [],
      frame: 0,
      lastGoal: "",
      thinkLeft: 0,
      settleLeft: 0,
      roamIdx: 0,
      roamTimer: 0,
    };
  }

  let hero = makeWalker("primary", "primary", "station_minion", PRIMARY_HALL_SEAT);
  let crewWalk: Walker[] = [];
  let deckSig = "";
  let crewSkinSig = "";

  function applyDeck(rooms: HarvestRoomId[], crew: NaanCrewMember[]) {
    const geom = JSON.stringify(rooms) + "|" + JSON.stringify(crew.map((c) => ({ id: c.id, rooms: c.rooms })));
    const skins = crew.map((c) => c.id + ":" + c.skin).join(",");
    if (geom === deckSig && skins === crewSkinSig) return;
    const geomChanged = geom !== deckSig;
    deckSig = geom;
    crewSkinSig = skins;
    if (!geomChanged) {
      for (const c of crew) {
        const w = crewWalk.find((x) => x.owner === c.id);
        if (w) w.skin = c.skin;
      }
      return;
    }
    deck = buildDeck(rooms, crew);
    liveRooms = deck.rooms;
    liveHalls = deck.halls;
    overlays = deck.overlays;
    ox = deck.ox;
    stCols = deck.stCols;
    stRows = deck.stRows;
    occ = deck.occ;
    hero.lastGoal = "";
    const keep = new Set(crew.map((c) => c.id));
    crewWalk = crewWalk.filter((w) => keep.has(w.owner));
    for (const c of crew) {
      const hall = deck.seats[c.id]?.hall || PRIMARY_HALL_SEAT;
      let w = crewWalk.find((x) => x.owner === c.id);
      if (!w) {
        w = makeWalker(c.id, c.id, c.skin, hall);
        crewWalk = [...crewWalk, w];
      } else {
        w.skin = c.skin;
        w.lastGoal = "";
      }
      const t = tileOf(w.px, w.py);
      if (!deck.walk[c.id]?.has(t.x + "," + t.y)) {
        const f = footOf(hall.tx, hall.ty);
        w.px = f.x;
        w.py = f.y;
        w.path = [];
        w.sitting = true;
      }
    }
    const ht = tileOf(hero.px, hero.py);
    if (!deck.walk.primary?.has(ht.x + "," + ht.y)) {
      const f = footOf(PRIMARY_HALL_SEAT.tx, PRIMARY_HALL_SEAT.ty);
      hero.px = f.x;
      hero.py = f.y;
      hero.path = [];
      hero.sitting = true;
    }
    paintDeck();
  }

  $: applyDeck($naanRooms, $naanCrew);
  $: hero.skin = look.agent;

  let stationEl: HTMLDivElement | null = null;
  let voidCv: HTMLCanvasElement | null = null;
  let deckCv: HTMLCanvasElement | null = null;
  let floorImg: HTMLImageElement | null = null;
  let wallImg: HTMLImageElement | null = null;
  let shellImg: HTMLImageElement | null = null;
  let paintGen = 0;
  let lastSkinKey = "";
  $: skinKey = `${look.floor}|${look.wall}|${look.shell}`;

  let raf = 0;
  let lastTs = 0;
  let acc = 0;
  let skyAcc = 0;
  let skyFrames = 0;
  let z = ZFIT;
  let tz = ZFIT;
  let panX = 0;
  let panY = 0;
  let tpanX = 0;
  let tpanY = 0;
  let drag: { id: number; x: number; y: number } | null = null;
  let pinch0 = 0;

  function inDeck(x: number, y: number): boolean {
    if (x < 0 || y < 0 || x >= stCols || y >= stRows) return false;
    return occ[y * stCols + x] !== 0;
  }

  function inCorridor(x: number, y: number): boolean {
    if (liveRooms.some((r) => r.kind !== "hall" && x >= r.x1 && x <= r.x2 && y >= r.y1 && y <= r.y2)) return false;
    return liveHalls.some((h) => x >= h.x1 && x <= h.x2 && y >= h.y1 && y <= h.y2);
  }

  function isWallRow(x: number, y: number): boolean {
    for (const r of liveRooms) {
      if (r.kind === "hall") continue;
      if (x >= r.x1 && x <= r.x2 && y === r.y1) return true;
    }
    return false;
  }

  function footOf(lx: number, ly: number): { x: number; y: number } {
    return { x: lx + 0.5, y: ly + 1 - 1 / TILE };
  }

  function tileOf(fx: number, fy: number): { x: number; y: number } {
    return { x: Math.floor(fx), y: Math.floor(fy) };
  }

  function seatFoot(s: Seat): { x: number; y: number } {
    return footOf(s.tx, s.ty);
  }

  function angNorm(a: number): number {
    return Math.atan2(Math.sin(a), Math.cos(a));
  }

  function bucketDir(a: number, cur: Facing): Facing {
    // Hysteresis: stay in current direction if within sector + small margin
    if (Math.abs(angNorm(a - DIR_A[cur])) < Math.PI / 8 + 0.08) return cur;
    let best: Facing = "south";
    let bd = Infinity;
    (Object.keys(DIR_A) as Facing[]).forEach((d) => {
      const t = Math.abs(angNorm(a - DIR_A[d]));
      if (t < bd) {
        bd = t;
        best = d;
      }
    });
    return best;
  }

  function stepGait(w: Walker, dx: number, dy: number, d: number, lastLeg: boolean, dt: number): number {
    const heading = d > 1e-4 ? Math.atan2(dy, dx) : w.faceA;
    const turn = angNorm(heading - w.faceA);
    const remain = Math.abs(turn);
    const maxW = 9;
    w.faceA = angNorm(w.faceA + Math.sign(turn) * Math.min(remain, maxW * dt));
    const error = Math.abs(angNorm(heading - w.faceA));
    const alignment = error >= Math.PI / 4 ? 0 : Math.pow(Math.cos(error * 2), 2);
    const want = (lastLeg ? Math.min(SPEED, Math.sqrt(Math.max(0, d) * 2 * ACCEL)) : SPEED) * alignment;
    const rate = ACCEL * dt;
    w.spd = w.spd < want ? Math.min(want, w.spd + rate) : Math.max(want, w.spd - rate);
    const step = error >= Math.PI / 4 ? 0 : Math.min(d, w.spd * dt);
    w.odo += step;
    w.dir = bucketDir(w.faceA, w.dir);
    return step;
  }

  function bfs(
    walkable: Set<string>,
    sx: number,
    sy: number,
    tx: number,
    ty: number,
  ): Array<{ x: number; y: number }> {
    const start = { x: Math.round(sx), y: Math.round(sy) };
    const goal = { x: Math.round(tx), y: Math.round(ty) };
    const sk = start.x + "," + start.y;
    const gk = goal.x + "," + goal.y;
    if (!walkable.has(gk)) return [];
    if (!walkable.has(sk)) return walkable.has(gk) ? [goal] : [];
    if (sk === gk) return [];
    const q: Array<{ x: number; y: number }> = [start];
    const prev = new Map<string, string>();
    prev.set(sk, sk);
    const dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
    for (let i = 0; i < q.length; i++) {
      const cur = q[i];
      if (cur.x === goal.x && cur.y === goal.y) break;
      for (const [dx, dy] of dirs) {
        const nx = cur.x + dx, ny = cur.y + dy;
        const k = nx + "," + ny;
        if (!walkable.has(k) || prev.has(k)) continue;
        prev.set(k, cur.x + "," + cur.y);
        q.push({ x: nx, y: ny });
      }
    }
    if (!prev.has(gk)) return [];
    const out: Array<{ x: number; y: number }> = [];
    let k = gk;
    while (k !== sk) {
      const [x, y] = k.split(",").map(Number);
      out.push({ x, y });
      k = prev.get(k) || sk;
    }
    out.reverse();
    return out;
  }

  function resolveGoal(owner: string, want: RoomKind): { id: RoomKind; seat: Seat } {
    const on = deck.enabled[owner] || [];
    const bag = deck.seats[owner] || {};
    const hall = bag.hall || PRIMARY_HALL_SEAT;
    if (want === "hold" && bag.hold) return { id: "hold", seat: bag.hold };
    if (want === "hall" && bag.hall) return { id: "hall", seat: hall };
    if (on.includes(want as HarvestRoomId) && bag[want]) return { id: want, seat: bag[want] as Seat };
    if (!on.length) return { id: "hall", seat: hall };
    const origin = bag[want]
      || (owner === "primary" && want !== "hall" && want !== "hold" ? BASE_SEATS[want] : undefined)
      || hall;
    let best: HarvestRoomId = on[0];
    let bd = Infinity;
    for (const id of on) {
      const s = bag[id];
      if (!s) continue;
      const d = Math.hypot(s.tx - origin.tx, s.ty - origin.ty);
      if (d < bd) {
        bd = d;
        best = id;
      }
    }
    const seat = bag[best] || hall;
    return { id: bag[best] ? best : "hall", seat };
  }

  function arrive(w: Walker, goal: RoomKind, seat: Seat) {
    const f = seatFoot(seat);
    w.px = f.x;
    w.py = f.y;
    w.dir = seat.face;
    w.faceA = DIR_A[seat.face];
    w.spd = 0;
    w.sitting = true;
    w.working = false;
    w.path = [];
    w.settleLeft = seat.work && harvestOn(w.owner) ? SETTLE : 0;
    if (reduceMotion && seat.work && harvestOn(w.owner)) {
      w.working = true;
      w.settleLeft = 0;
    }
    w.lastGoal = goal;
  }

  function goToSeat(w: Walker, goal: RoomKind, seat: Seat, walkable: Set<string>) {
    const dest = seatFoot(seat);
    const dist = Math.hypot(dest.x - w.px, dest.y - w.py);
    if (reduceMotion || dist < 0.18) {
      arrive(w, goal, seat);
      return;
    }
    w.sitting = false;
    w.working = false;
    w.settleLeft = 0;
    const cur = tileOf(w.px, w.py);
    const tiles = bfs(walkable, cur.x, cur.y, seat.tx, seat.ty);
    w.path = tiles.map((t) => footOf(t.x, t.y));
    const last = w.path[w.path.length - 1];
    if (!last || Math.hypot(last.x - dest.x, last.y - dest.y) > 0.01) w.path.push(dest);
    if (!w.path.length) arrive(w, goal, seat);
  }

  function seize(w: Walker, goal: RoomKind, seat: Seat) {
    const dest = seatFoot(seat);
    w.sitting = false;
    w.working = false;
    w.path = [];
    w.settleLeft = 0;
    w.lastGoal = goal;
    w.dir = Math.abs(dest.x - w.px) > Math.abs(dest.y - w.py)
      ? (dest.x > w.px ? "east" : "west")
      : (dest.y > w.py ? "south" : "north");
    w.faceA = DIR_A[w.dir];
    w.thinkLeft = reduceMotion ? 0 : THINK;
    if (w.thinkLeft <= 0) goToSeat(w, goal, seat, deck.walk[w.owner] || new Set());
  }

  function stepWalker(w: Walker, inferredRoom: RoomKind, dt: number) {
    const got = resolveGoal(w.owner, inferredRoom);
    goalHot[w.owner] = got.id;
    const walkable = deck.walk[w.owner] || new Set();
    if (w.lastGoal !== got.id) seize(w, got.id, got.seat);
    const seat = got.seat;
    const dest = seatFoot(seat);
    if (w.thinkLeft > 0) {
      w.thinkLeft -= dt;
      w.sitting = false;
      w.working = false;
      w.spd = 0;
      w.dir = Math.abs(dest.x - w.px) > Math.abs(dest.y - w.py)
        ? (dest.x > w.px ? "east" : "west")
        : (dest.y > w.py ? "south" : "north");
      w.faceA = DIR_A[w.dir];
      if (w.thinkLeft <= 0) goToSeat(w, got.id, seat, walkable);
      return;
    }
    if (w.path.length) {
      let next = w.path[0];
      let dx = next.x - w.px;
      let dy = next.y - w.py;
      let d = Math.hypot(dx, dy);
      let more = w.path.length > 1;
      while (more && (d < 1e-6 || d < CORNER_LOOK)) {
        w.path.shift();
        next = w.path[0];
        dx = next.x - w.px;
        dy = next.y - w.py;
        d = Math.hypot(dx, dy);
        more = w.path.length > 1;
      }
      if (!more && d < 0.09) {
        arrive(w, got.id, seat);
        return;
      }
      const step = stepGait(w, dx, dy, d, !more, dt);
      if (d > 1e-6 && step > 0) {
        w.px += (dx / d) * step;
        w.py += (dy / d) * step;
      }
      w.sitting = false;
      w.working = false;
      return;
    }
    w.px = dest.x;
    w.py = dest.y;
    w.dir = seat.face;
    w.faceA = DIR_A[seat.face];
    w.sitting = true;
    if (w.settleLeft > 0) {
      w.settleLeft -= dt;
      w.working = false;
      if (w.settleLeft <= 0) w.working = seat.work && harvestOn(w.owner);
      return;
    }
    w.working = seat.work && harvestOn(w.owner);
  }

  function walkerSrc(w: Walker): string {
    const pose = harvestPose(w.sitting, w.working);
    const poseFrame = pose === "walk" ? Math.floor(w.odo / STRIDE) : w.frame;
    return agentPoseSrc(findAgent(w.skin), pose, w.dir, poseFrame);
  }

  function poseWord(sitting: boolean, working: boolean): string {
    const p = harvestPose(sitting, working);
    return p === "type" ? "TYPE" : p === "sit" ? "SIT" : "WALK";
  }

  function ownerWalker(id: string): Walker {
    if (id === PRIMARY_NAAN_ID) return hero;
    return crewWalk.find((w) => w.owner === id) || hero;
  }

  function ownerRoom(owner: string): Room | undefined {
    const hot = goalHot[owner] || "";
    return liveRooms.find((r) => r.owner === owner && r.kind === hot)
      || liveRooms.find((r) => r.owner === owner);
  }

  function ownerTag(owner: string): string {
    if (owner === PRIMARY_NAAN_ID || owner === "primary") return "";
    const c = $naanCrew.find((x) => x.id === owner);
    return c ? agentShortName(c.skin) : "";
  }

  function wingOwners(): string[] {
    const seen: string[] = [];
    for (const r of liveRooms) {
      if (r.kind === "hall" || r.owner === "primary" || r.owner === PRIMARY_NAAN_ID) continue;
      if (!seen.includes(r.owner)) seen.push(r.owner);
    }
    return seen;
  }

  function pctBanner(owner: string): string {
    const owned = liveRooms.filter((r) => r.owner === owner && r.kind !== "hall");
    const x1 = Math.min(...owned.map((r) => r.x1));
    const y1 = Math.min(...owned.map((r) => r.y1));
    const left = ((ox + x1 + 0.12) / VIEW_COLS) * 100;
    const top = ((oy + y1 - 2.6) / VIEW_ROWS) * 100;
    return `left:${left}%;top:${top}%`;
  }

  function labelHot(r: Room): boolean {
    return r.owner === focusedId && r.kind === (goalHot[r.owner] || "");
  }

  function focusOwner(id: string) {
    focusedId = id;
  }

  $: if (focusedId !== PRIMARY_NAAN_ID && !$naanCrew.some((c) => c.id === focusedId)) {
    focusedId = PRIMARY_NAAN_ID;
  }

  $: hereX = (poseClock, ((ox + hero.px) / VIEW_COLS) * 100);
  $: hereY = (poseClock, ((oy + hero.py) / VIEW_ROWS) * 100);
  $: focusW = poseClock >= 0 ? ownerWalker(focusedId) : ownerWalker(focusedId);
  $: room = poseClock >= 0 ? (ownerRoom(focusW.owner) || liveRooms[0]) : liveRooms[0];
  $: poseLabel = poseClock >= 0 ? poseWord(focusW.sitting, focusW.working) : "WALK";
  $: nowLine = [room?.name || "HALL", room?.sn || "", poseLabel].filter(Boolean).join(" · ");
  $: focusedLabel = focusedId === PRIMARY_NAAN_ID
    ? agent.label
    : findAgent($naanCrew.find((c) => c.id === focusedId)?.skin || "").label;
  $: zoomPct = Math.round(z * 100);
  $: worldXf = `translate3d(${panX}px, ${panY}px, 0) scale(${z})`;
  $: heroSrc = poseClock >= 0 ? walkerSrc(hero) : walkerSrc(hero);
  $: heroXp = poseClock >= 0 ? bodyXp("primary") : bodyXp("primary");
  $: crewViews = (poseClock >= 0 ? crewWalk : crewWalk).map((w) => {
    const c = $naanCrew.find((x) => x.id === w.owner);
    const lv = bodyXp(w.owner);
    return {
      id: w.id,
      owner: w.owner,
      sit: w.sitting,
      left: ((ox + w.px) / VIEW_COLS) * 100,
      top: ((oy + w.py) / VIEW_ROWS) * 100,
      z: 80 + Math.floor(w.py),
      src: walkerSrc(w),
      bump: frameBump,
      name: findAgent(c?.skin || w.skin).label,
      pose: poseWord(w.sitting, w.working),
      level: lv.level,
      frac: lv.frac,
    };
  });

  function clamp(n: number, a: number, b: number): number {
    return Math.max(a, Math.min(b, n));
  }

  function stationPoint(clientX: number, clientY: number): { x: number; y: number } {
    const r = stationEl?.getBoundingClientRect();
    if (!r) return { x: 0, y: 0 };
    return { x: clientX - r.left, y: clientY - r.top };
  }

  function zoomAt(next: number, sx: number, sy: number) {
    const nz = clamp(next, ZMIN, ZMAX);
    const wx = (sx - tpanX) / tz;
    const wy = (sy - tpanY) / tz;
    tz = nz;
    tpanX = sx - wx * nz;
    tpanY = sy - wy * nz;
    if (reduceMotion) {
      z = tz;
      panX = tpanX;
      panY = tpanY;
    }
  }

  function zoomTowardCenter(factor: number) {
    const r = stationEl?.getBoundingClientRect();
    const cx = r ? r.width / 2 : 0;
    const cy = r ? r.height / 2 : 0;
    zoomAt(tz * factor, cx, cy);
  }

  function fitView() {
    tz = ZFIT;
    tpanX = 0;
    tpanY = 0;
    if (reduceMotion) {
      z = tz;
      panX = 0;
      panY = 0;
    }
  }

  function onWheel(ev: WheelEvent) {
    ev.preventDefault();
    const p = stationPoint(ev.clientX, ev.clientY);
    const step = ev.deltaY < 0 ? 1.12 : 1 / 1.12;
    zoomAt(tz * step, p.x, p.y);
  }

  function onPointerDown(ev: PointerEvent) {
    if (ev.pointerType === "mouse" && ev.button !== 0) return;
    (ev.currentTarget as HTMLElement).setPointerCapture(ev.pointerId);
    drag = { id: ev.pointerId, x: ev.clientX, y: ev.clientY };
  }

  function onPointerMove(ev: PointerEvent) {
    if (!drag || drag.id !== ev.pointerId) return;
    const dx = ev.clientX - drag.x;
    const dy = ev.clientY - drag.y;
    drag = { id: drag.id, x: ev.clientX, y: ev.clientY };
    tpanX += dx;
    tpanY += dy;
    if (reduceMotion) {
      panX = tpanX;
      panY = tpanY;
    }
  }

  function onPointerUp(ev: PointerEvent) {
    if (drag && drag.id === ev.pointerId) drag = null;
  }

  function touchDist(a: Touch, b: Touch): number {
    return Math.hypot(a.clientX - b.clientX, a.clientY - b.clientY);
  }

  function onTouchStart(ev: TouchEvent) {
    if (ev.touches.length === 2) {
      pinch0 = touchDist(ev.touches[0], ev.touches[1]);
      drag = null;
    }
  }

  function onTouchMove(ev: TouchEvent) {
    if (ev.touches.length !== 2 || pinch0 <= 0) return;
    ev.preventDefault();
    const d = touchDist(ev.touches[0], ev.touches[1]);
    const mid = stationPoint(
      (ev.touches[0].clientX + ev.touches[1].clientX) / 2,
      (ev.touches[0].clientY + ev.touches[1].clientY) / 2,
    );
    zoomAt(tz * (d / pinch0), mid.x, mid.y);
    pinch0 = d;
  }

  function onTouchEnd() {
    pinch0 = 0;
  }

  function stepCam(dt: number) {
    const k = reduceMotion ? 1 : 1 - Math.exp(-10 * dt);
    z += (tz - z) * k;
    panX += (tpanX - panX) * k;
    panY += (tpanY - panY) * k;
    if (Math.abs(tz - z) < 0.0008) z = tz;
    if (Math.abs(tpanX - panX) < 0.15) panX = tpanX;
    if (Math.abs(tpanY - panY) < 0.15) panY = tpanY;
  }

  function loadImg(src: string): Promise<HTMLImageElement> {
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.decoding = "async";
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error(src));
      img.src = src;
    });
  }

  function mod(n: number, m: number): number {
    return ((n % m) + m) % m;
  }

  function stampFloor(ctx: CanvasRenderingContext2D, img: HTMLImageElement, dx: number, dy: number, tx: number, ty: number) {
    const period = 8;
    ctx.save();
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = "high";
    ctx.drawImage(
      img,
      mod(tx, period) * img.width / period,
      mod(ty, period) * img.height / period,
      img.width / period,
      img.height / period,
      dx,
      dy,
      TILE,
      TILE,
    );
    ctx.restore();
  }

  function stampStrip(ctx: CanvasRenderingContext2D, img: HTMLImageElement, dx: number, dy: number, w: number, h: number, tx: number) {
    ctx.save();
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = "high";
    ctx.drawImage(img, mod(tx, 4) * img.width / 4, 0, img.width / 4, img.height, dx, dy, w, h);
    ctx.restore();
  }

  function eraseSpandrel(ctx: CanvasRenderingContext2D, kind: "tl" | "tr" | "bl" | "br", ax: number, ay: number, rad: number) {
    const A = { tl: { cx: 1, cy: 1 }, tr: { cx: 0, cy: 1 }, bl: { cx: 1, cy: 0 }, br: { cx: 0, cy: 0 } }[kind];
    const r = Math.round(rad);
    const ox0 = Math.round(A.cx ? ax - r : ax);
    const oy0 = Math.round(A.cy ? ay - r : ay);
    ctx.save();
    ctx.globalCompositeOperation = "destination-out";
    ctx.fillStyle = "#000";
    for (let py = oy0; py < oy0 + r; py++) {
      const ady = Math.abs(py + 0.5 - ay);
      const reach = ady >= r ? 0 : r - ady;
      const ex = A.cx ? Math.round(ax - reach) : Math.round(ax + reach);
      if (A.cx) {
        if (ex > ox0) ctx.fillRect(ox0, py, ex - ox0, 1);
      } else if (ex + 1 < ox0 + r) {
        ctx.fillRect(ex + 1, py, ox0 + r - ex - 1, 1);
      }
    }
    ctx.restore();
  }

  function punchHull(ctx: CanvasRenderingContext2D) {
    const r = CHAM;
    for (let y = 0; y < stRows; y++) {
      for (let x = 0; x < stCols; x++) {
        if (!inDeck(x, y)) continue;
        const x0 = (ox + x) * TILE;
        const y0 = (oy + y) * TILE;
        const x1 = x0 + TILE;
        const y1 = y0 + TILE;
        if (!inDeck(x - 1, y) && !inDeck(x, y - 1)) eraseSpandrel(ctx, "tl", x0 + r, y0 + r, r);
        if (!inDeck(x + 1, y) && !inDeck(x, y - 1)) eraseSpandrel(ctx, "tr", x1 - r, y0 + r, r);
        if (!inDeck(x - 1, y) && !inDeck(x, y + 1)) eraseSpandrel(ctx, "bl", x0 + r, y1 - r, r);
        if (!inDeck(x + 1, y) && !inDeck(x, y + 1)) eraseSpandrel(ctx, "br", x1 - r, y1 - r, r);
      }
    }
  }

  function paintAmberWindows(ctx: CanvasRenderingContext2D) {
    for (let y = 0; y < stRows; y++) {
      for (let x = 0; x < stCols; x++) {
        if (!inDeck(x, y)) continue;
        const edge = !inDeck(x - 1, y) || !inDeck(x + 1, y) || !inDeck(x, y - 1) || isWallRow(x, y);
        if (!edge) continue;
        const hsh = ((x * 73) ^ (y * 97)) & 7;
        if (hsh > 2) continue;
        const dx = (ox + x) * TILE;
        const dy = (oy + y) * TILE;
        const lit = hsh === 0;
        ctx.fillStyle = lit ? "#ffb03a" : "#c47a18";
        const wx = dx + 4 + (hsh & 1) * 4;
        const wy = isWallRow(x, y) ? dy + 3 : dy + 6;
        ctx.fillRect(wx, wy, 3, 2);
        if (lit) {
          ctx.fillStyle = "rgba(255,176,58,0.35)";
          ctx.fillRect(wx - 1, wy - 1, 5, 4);
        }
      }
    }
  }

  // Solar wings and the comms dish used to float detached in the void next to the
  // hull. Only the windows stay; anything bolted on has to sit flush on a deck tile.
  function paintHullExtras(ctx: CanvasRenderingContext2D) {
    paintAmberWindows(ctx);
  }

  function deckPath(): Path2D {
    const p = new Path2D();
    for (let y = 0; y < stRows; y++) {
      for (let x = 0; x < stCols; x++) {
        if (!inDeck(x, y)) continue;
        p.rect((ox + x) * TILE, (oy + y) * TILE, TILE, TILE);
      }
    }
    return p;
  }

  function paintLightPools(ctx: CanvasRenderingContext2D) {
    if (!deck.lights.length) return;
    ctx.save();
    ctx.clip(deckPath());
    ctx.globalCompositeOperation = "lighter";
    for (const l of deck.lights) {
      const cx = (ox + l.tx) * TILE + TILE / 2;
      const cy = (oy + l.ty) * TILE + TILE / 2;
      const g = ctx.createRadialGradient(cx, cy, 0, cx, cy, l.r);
      g.addColorStop(0, `rgba(${l.rgb},${l.a})`);
      g.addColorStop(0.55, `rgba(${l.rgb},${(l.a * 0.38).toFixed(3)})`);
      g.addColorStop(1, `rgba(${l.rgb},0)`);
      ctx.fillStyle = g;
      ctx.fillRect(cx - l.r, cy - l.r, l.r * 2, l.r * 2);
    }
    ctx.restore();
  }

  function paintDeck() {
    if (!deckCv) return;
    const ctx = deckCv.getContext("2d");
    if (!ctx) return;
    ctx.imageSmoothingEnabled = false;
    ctx.clearRect(0, 0, VW, VH);
    const rim = 11;
    const skirt = 10;
    for (let y = 0; y < stRows; y++) {
      for (let x = 0; x < stCols; x++) {
        if (!inDeck(x, y)) continue;
        const dx = (ox + x) * TILE;
        const dy = (oy + y) * TILE;
        if (floorImg) stampFloor(ctx, floorImg, dx, dy, x, y);
        else {
          ctx.fillStyle = "#2e3136";
          ctx.fillRect(dx, dy, TILE, TILE);
        }
        if (isWallRow(x, y)) {
          const wh = Math.floor(TILE * 0.5);
          if (wallImg) stampStrip(ctx, wallImg, dx, dy, TILE, wh, x);
          else {
            ctx.fillStyle = "#4a4540";
            ctx.fillRect(dx, dy, TILE, wh);
          }
        }
        // Corridor tiles get a dimmer overlay + center wear lane
        if (inCorridor(x, y)) {
          ctx.fillStyle = "rgba(0,0,0,0.12)";
          ctx.fillRect(dx, dy, TILE, TILE);
          const hallH = liveHalls.find((h) => x >= h.x1 && x <= h.x2 && y >= h.y1 && y <= h.y2);
          if (hallH) {
            const isHorizontal = (hallH.x2 - hallH.x1) >= (hallH.y2 - hallH.y1);
            if (isHorizontal) {
              const midY = Math.floor((hallH.y1 + hallH.y2) / 2);
              if (y === midY) {
                ctx.fillStyle = "rgba(0,0,0,0.08)";
                ctx.fillRect(dx, dy + 3, TILE, TILE - 6);
              }
            } else {
              const midX = Math.floor((hallH.x1 + hallH.x2) / 2);
              if (x === midX) {
                ctx.fillStyle = "rgba(0,0,0,0.08)";
                ctx.fillRect(dx + 3, dy, TILE - 6, TILE);
              }
            }
          }
        }
        ctx.fillStyle = "rgba(0,0,0,0.22)";
        ctx.fillRect(dx, dy, TILE, 1);
        ctx.fillRect(dx, dy, 1, TILE);
        if (shellImg) {
          if (!inDeck(x - 1, y)) stampStrip(ctx, shellImg, dx - rim, dy, rim, TILE, x);
          if (!inDeck(x + 1, y)) stampStrip(ctx, shellImg, dx + TILE, dy, rim, TILE, x);
          if (!inDeck(x, y - 1)) stampStrip(ctx, shellImg, dx - rim, dy - rim, TILE + rim * 2, rim, x);
          if (!inDeck(x, y + 1)) {
            stampStrip(ctx, shellImg, dx - rim, dy + TILE, TILE + rim * 2, rim + skirt, x);
            ctx.fillStyle = "rgba(0,0,0,0.28)";
            ctx.fillRect(dx - rim, dy + TILE + rim, TILE + rim * 2, skirt);
          }
        } else {
          ctx.fillStyle = "#4a4540";
          if (!inDeck(x - 1, y)) ctx.fillRect(dx - rim, dy, rim, TILE);
          if (!inDeck(x + 1, y)) ctx.fillRect(dx + TILE, dy, rim, TILE);
          if (!inDeck(x, y - 1)) ctx.fillRect(dx - rim, dy - rim, TILE + rim * 2, rim);
          if (!inDeck(x, y + 1)) ctx.fillRect(dx - rim, dy + TILE, TILE + rim * 2, rim + skirt);
        }
        ctx.fillStyle = "rgba(230, 214, 170, 0.22)";
        if (!inDeck(x, y - 1)) ctx.fillRect(dx, dy - rim, TILE, 1);
        if (!inDeck(x - 1, y)) ctx.fillRect(dx - rim, dy, 1, TILE);
        if (!inDeck(x + 1, y)) ctx.fillRect(dx + TILE + rim - 1, dy, 1, TILE);
      }
    }
    paintLightPools(ctx);
    // Kill any stray tile-sized pixels that landed off the dilated hull.
    ctx.save();
    ctx.globalCompositeOperation = "destination-in";
    const hull = new Path2D();
    for (let y = 0; y < stRows; y++) {
      for (let x = 0; x < stCols; x++) {
        if (!inDeck(x, y)) continue;
        hull.rect((ox + x) * TILE - rim, (oy + y) * TILE - rim, TILE + rim * 2, TILE + rim + skirt);
      }
    }
    ctx.fillStyle = "#fff";
    ctx.fill(hull);
    ctx.restore();
    punchHull(ctx);
    paintHullExtras(ctx);
  }

  function paintSky(now: number) {
    if (!voidCv) return;
    const ctx = voidCv.getContext("2d");
    if (!ctx) return;
    paintVoid(ctx, VW, VH, now, reduceMotion);
  }

  async function loadSkins(gen: number) {
    try {
      const [f, w, s] = await Promise.all([
        loadImg(floorSrc(look.floor)).catch(() => null),
        loadImg(wallSrc(look.wall)).catch(() => null),
        loadImg(shellSrc(look.shell)).catch(() => null),
      ]);
      if (gen !== paintGen) return;
      floorImg = f;
      wallImg = w;
      shellImg = s;
      paintDeck();
    } catch {
      if (gen !== paintGen) return;
      paintDeck();
    }
  }

  function tick(ts: number) {
    const dt = lastTs ? Math.min(0.05, (ts - lastTs) / 1000) : 0;
    lastTs = ts;
    stepCam(dt);
    stepWalker(hero, inferred, dt);
    const ROAM_INTERVAL = 8;
    for (const w of crewWalk) {
      const cm = $naanCrew.find((c) => c.id === w.owner);
      const ownRooms = cm?.rooms || [];
      let crewGoal: RoomKind;
      if (ownRooms.length) {
        w.roamTimer += dt;
        if (w.sitting && w.working && w.roamTimer >= ROAM_INTERVAL) {
          w.roamTimer = 0;
          w.roamIdx = (w.roamIdx + 1) % ownRooms.length;
        }
        crewGoal = ownRooms[w.roamIdx % ownRooms.length];
        // Idle extra: sit in HOLD stash without touching the harvest roam list.
        if (!harvestOn(w.owner) && deck.seats[w.owner]?.hold && (w.lastGoal === "hold" || (w.sitting && w.roamTimer >= ROAM_INTERVAL))) {
          crewGoal = "hold";
        }
      } else {
        crewGoal = inferred;
      }
      stepWalker(w, crewGoal, dt);
    }
    const fps = hero.sitting ? (hero.working ? 6 : 4) : 10;
    acc += dt;
    if (acc >= 1 / fps) {
      acc = 0;
      if (hero.sitting) hero.frame += 1;
      for (const w of crewWalk) if (w.sitting) w.frame += 1;
    }
    poseClock += 1;
    frameBump += 1;
    // Sky is a full-canvas starfield; skip most rAF ticks.
    skyAcc += dt;
    skyFrames += 1;
    if (skyAcc >= 0.08 || skyFrames >= 3) {
      skyAcc = 0;
      skyFrames = 0;
      paintSky(ts);
    }
    raf = requestAnimationFrame(tick);
  }

  onMount(() => {
    void loadNaanDeckFromSettings();
    const got = resolveGoal("primary", inferred);
    seize(hero, got.id, got.seat);
    paintSky(performance.now());
    paintDeck();
    raf = requestAnimationFrame(tick);
  });

  onDestroy(() => {
    if (raf) cancelAnimationFrame(raf);
  });

  $: if (deckCv) paintDeck();
  $: if (skinKey !== lastSkinKey) {
    lastSkinKey = skinKey;
    paintGen += 1;
    void loadSkins(paintGen);
  }

  function pctPlate(r: Room): string {
    const left = ((ox + r.x1 + 0.12) / VIEW_COLS) * 100;
    const top = r.y1 <= 2
      ? ((oy + r.y1 - 1.6) / VIEW_ROWS) * 100
      : ((oy + r.y2 + 1.2) / VIEW_ROWS) * 100;
    return `left:${left}%;top:${top}%`;
  }

  function owningRoom(ov: Overlay): Room | undefined {
    return liveRooms.find((r) => r.id === ov.room);
  }

  function pctProp(ov: Overlay): string {
    const v = viewOf(ov.file);
    const u = TILE / DUMP_T;
    const left = ((ox + ov.tx) * TILE + v.bx * u) / VW * 100;
    const top = ((oy + ov.ty) * TILE + v.by * u) / VH * 100;
    const w = (v.bw * u) / VW * 100;
    const h = (v.bh * u) / VH * 100;
    const zix = 5 + ov.ty + v.h;
    return `left:${left}%;top:${top}%;width:${w}%;height:${h}%;z-index:${zix}`;
  }

  // Clip box is the owning room in view percent. Sprite dump bounds can hang
  // past the tile; overflow:hidden on this box keeps walls/props in the room.
  function pctPropClip(ov: Overlay): string {
    const r = owningRoom(ov);
    const v = viewOf(ov.file);
    const zix = 5 + ov.ty + v.h;
    if (!r) return pctProp(ov);
    const left = ((ox + r.x1) * TILE) / VW * 100;
    const top = ((oy + r.y1) * TILE) / VH * 100;
    const w = ((r.x2 - r.x1 + 1) * TILE) / VW * 100;
    const h = ((r.y2 - r.y1 + 1) * TILE) / VH * 100;
    return `left:${left}%;top:${top}%;width:${w}%;height:${h}%;z-index:${zix}`;
  }

  function pctPropInRoom(ov: Overlay): string {
    const r = owningRoom(ov);
    const v = viewOf(ov.file);
    const u = TILE / DUMP_T;
    if (!r) return "left:0;top:0;width:100%;height:100%";
    const rw = Math.max(1, (r.x2 - r.x1 + 1) * TILE);
    const rh = Math.max(1, (r.y2 - r.y1 + 1) * TILE);
    const left = ((ov.tx - r.x1) * TILE + v.bx * u) / rw * 100;
    const top = ((ov.ty - r.y1) * TILE + v.by * u) / rh * 100;
    const w = (v.bw * u) / rw * 100;
    const h = (v.bh * u) / rh * 100;
    return `left:${left}%;top:${top}%;width:${w}%;height:${h}%`;
  }
</script>

<div class="station-wrap">
  <div class="station-head">
    <span class="station-title">NAAN · {focusedLabel}</span>
    <span class="station-tools">
      <button type="button" class="zbtn" on:click={() => zoomTowardCenter(1 / 1.18)} aria-label="Zoom out">-</button>
      <button type="button" class="zbtn" on:click={fitView} aria-label="Fit station">{zoomPct}%</button>
      <button type="button" class="zbtn" on:click={() => zoomTowardCenter(1.18)} aria-label="Zoom in">+</button>
    </span>
    <span class="station-where">{room?.name || "HALL"} · {room?.sn || ""} · {poseLabel}</span>
  </div>

  <div
    class="station"
    bind:this={stationEl}
    style="aspect-ratio:{VIEW_COLS}/{VIEW_ROWS}"
    role="application"
    aria-label="NAAN harvest map"
    on:wheel|preventDefault={onWheel}
    on:pointerdown={onPointerDown}
    on:pointermove={onPointerMove}
    on:pointerup={onPointerUp}
    on:pointercancel={onPointerUp}
    on:touchstart={onTouchStart}
    on:touchmove|preventDefault={onTouchMove}
    on:touchend={onTouchEnd}
    on:dblclick={fitView}
  >
    <canvas class="void" bind:this={voidCv} width={VW} height={VH} aria-hidden="true"></canvas>
    <div class="world" style="transform:{worldXf}">
      <canvas class="deck" bind:this={deckCv} width={VW} height={VH} aria-hidden="true"></canvas>

      {#each overlays as ov}
        <div class="prop-clip" style={pctPropClip(ov)}>
          <img
            class="prop"
            class:term={ov.prop === "terminal"}
            class:chair={ov.prop === "chair"}
            class:dress={ov.prop === "dress"}
            class:hot={poseClock >= 0 && ov.kind === goalHot[ov.owner]}
            src={ov.src}
            alt=""
            draggable="false"
            style={pctPropInRoom(ov)}
            on:error={(e) => { e.currentTarget.style.display = "none"; }}
          />
        </div>
      {/each}

      {#each wingOwners() as owner}
        <div class="wing-banner" style={pctBanner(owner)}>{ownerTag(owner)}</div>
      {/each}

      {#each liveRooms as r}
        {#if r.kind !== "hall"}
          <div
            class="room-label"
            class:on={poseClock >= 0 && labelHot(r)}
            class:mine={poseClock >= 0 && r.owner === focusedId && !labelHot(r)}
            style={pctPlate(r)}
          >
            <span class="rn">{r.name}</span>
            {#if poseClock >= 0 && labelHot(r)}
              <span class="rs">{r.sn}{r.owner === focusW.owner && poseLabel ? " · " + poseLabel : ""}</span>
            {/if}
          </div>
        {/if}
      {/each}

      <div
        class="walker-slot"
        class:sit={poseClock >= 0 && hero.sitting}
        style="left:{hereX}%;top:{hereY}%;z-index:{80 + Math.floor(hero.py)};--beat:{poseClock}"
      >
        <AgentLevelChip
          compact
          level={heroXp.level}
          frac={heroXp.frac}
          name={focusedId === PRIMARY_NAAN_ID ? agentShortName(look.agent) : undefined}
        />
        <div
          class="walker"
          class:sit={poseClock >= 0 && hero.sitting}
          class:focused={focusedId === PRIMARY_NAAN_ID}
          role="button"
          tabindex="0"
          on:pointerdown|stopPropagation={() => focusOwner(PRIMARY_NAAN_ID)}
          on:keydown={(e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); focusOwner(PRIMARY_NAAN_ID); } }}
        >
          <img src={heroSrc} alt="" width="48" height="48" draggable="false" />
        </div>
      </div>

      {#each crewViews as cv (cv.id)}
        <div
          class="walker-slot"
          class:sit={cv.sit}
          style="left:{cv.left}%;top:{cv.top}%;z-index:{cv.z};--beat:{cv.bump}"
        >
          <AgentLevelChip
            compact
            level={cv.level}
            frac={cv.frac}
            name={focusedId === cv.owner ? agentShortName($naanCrew.find((x) => x.id === cv.owner)?.skin || "") : undefined}
          />
          <div
            class="walker"
            class:sit={cv.sit}
            class:focused={focusedId === cv.owner}
            role="button"
            tabindex="0"
            on:pointerdown|stopPropagation={() => focusOwner(cv.owner)}
            on:keydown={(e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); focusOwner(cv.owner); } }}
          >
            <img src={cv.src} alt="" width="48" height="48" draggable="false" />
          </div>
        </div>
      {/each}
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

  .station-tools {
    display: flex;
    align-items: center;
    gap: 4px;
    margin-left: auto;
    margin-right: 10px;
  }

  .zbtn {
    font-family: var(--font);
    font-size: 8px;
    letter-spacing: 0;
    color: var(--text-secondary);
    background: none;
    border: 1px solid var(--border);
    border-radius: 0;
    min-width: 22px;
    height: 18px;
    padding: 0 6px;
    line-height: 1;
  }

  .zbtn:hover:not(:disabled) {
    color: var(--text-primary);
    border-color: rgba(255, 255, 255, 0.4);
  }

  .station-title,
  .station-where,
  .rn,
  .rs,
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
    max-height: 58vh;
    background: #040302;
    border: 1px solid var(--border);
    overflow: hidden;
    image-rendering: pixelated;
    touch-action: none;
    cursor: grab;
    user-select: none;
  }

  .station:active {
    cursor: grabbing;
  }

  .void {
    z-index: 0;
    pointer-events: none;
  }

  .void,
  .deck {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    display: block;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
  }

  .world {
    position: absolute;
    inset: 0;
    transform-origin: 0 0;
    will-change: transform;
    z-index: 1;
  }

  .deck {
    z-index: 1;
  }

  .prop-clip {
    position: absolute;
    overflow: hidden;
    overflow: clip;
    overflow-clip-margin: 0;
    clip-path: inset(0);
    contain: paint;
    pointer-events: none;
  }

  .prop {
    position: absolute;
    object-fit: fill;
    object-position: 0 0;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    pointer-events: none;
    filter: saturate(0.92);
    opacity: 0.9;
  }

  .prop.term {
    z-index: 6;
  }

  .prop.chair {
    opacity: 0.96;
  }

  .prop.dress {
    opacity: 0.88;
  }

  .prop.hot {
    opacity: 1;
    filter: saturate(1.05) drop-shadow(0 0 4px rgba(0, 229, 255, 0.35));
  }

  .room-label {
    position: absolute;
    display: flex;
    flex-direction: column;
    justify-content: flex-start;
    padding: 1px 3px 2px;
    pointer-events: none;
    border: none;
    background: rgba(4, 3, 2, 0.78);
    box-sizing: border-box;
    z-index: 22;
    white-space: nowrap;
  }

  .wing-banner {
    position: absolute;
    padding: 1px 4px 2px;
    pointer-events: none;
    background: rgba(4, 3, 2, 0.86);
    color: #ffd34a;
    font-family: var(--font);
    font-size: 7px;
    letter-spacing: 0.4px;
    z-index: 23;
    white-space: nowrap;
    box-shadow: 0 0 0 1px rgba(255, 211, 74, 0.35);
  }

  .room-label.on {
    background: rgba(0, 18, 24, 0.78);
    box-shadow: 0 0 0 1px var(--cy, #00e5ff);
  }

  .room-label.mine {
    box-shadow: 0 0 0 1px rgba(0, 229, 255, 0.28);
  }

  .rn {
    font-size: 6px;
    color: rgba(245, 245, 247, 0.78);
  }

  .rs {
    font-size: 5px;
    color: rgba(161, 161, 166, 0.9);
    margin-top: 1px;
  }

  .walker-slot {
    position: absolute;
    width: 4.6%;
    pointer-events: none;
    transform: translate(-50%, -100%);
    overflow: visible;
  }

  .walker-slot.sit {
    transform: translate(-50%, -92%);
  }

  .walker {
    position: relative;
    width: 100%;
    image-rendering: pixelated;
    pointer-events: auto;
    cursor: pointer;
    transform: none;
    overflow: visible;
  }

  .walker.focused {
    filter: drop-shadow(0 0 6px rgba(0, 229, 255, 0.55));
  }

  .walker-slot :global(.ag-lv) {
    position: absolute;
    left: 50%;
    bottom: 100%;
    top: auto;
    transform: translate(-50%, -2px);
    z-index: 2;
    background: #000;
    color: var(--cy, #00e5ff);
    border-color: #ffd34a;
    font-family: var(--font);
    backdrop-filter: none;
    -webkit-backdrop-filter: none;
  }

  .walker-slot :global(.ag-n) {
    color: #ffd34a;
  }

  .walker img {
    width: 100%;
    height: auto;
    display: block;
    image-rendering: pixelated;
    image-rendering: crisp-edges;
    pointer-events: none;
  }

  .station-task {
    margin-top: 8px;
    font-size: 8px;
    color: var(--text-secondary);
    line-height: 1.5;
    overflow-wrap: anywhere;
  }

</style>
