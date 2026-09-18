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
  export let task: string = "";
  export let lastLog: string = "";
  export let submissions: number = 0;
  export let ngt: number = 0;
  export let focusedId: string = PRIMARY_NAAN_ID;
  export let nowLine: string = "";

  const TILE = 16;
  const DUMP_T = 12;
  const PRIMARY_COLS = 26;
  const ST_ROWS = 12;
  const VIEW_COLS = 64;
  const VIEW_ROWS = 36;
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
  const CHAM = Math.max(4, Math.round(TILE * 0.35));
  const DIR_A: Record<Facing, number> = {
    east: 0,
    south: Math.PI / 2,
    west: Math.PI,
    north: -Math.PI / 2,
  };

  type RoomKind = HarvestRoomId | "hall";
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

  const BASE: Record<HarvestRoomId, Room> = {
    bed: { id: "bed", kind: "bed", owner: "primary", name: "BED", sn: "IDLE", x1: 0, y1: 4, x2: 7, y2: 11 },
    tor: { id: "tor", kind: "tor", owner: "primary", name: "TOR", sn: "FETCH", x1: 10, y1: 0, x2: 16, y2: 5 },
    lymph: { id: "lymph", kind: "lymph", owner: "primary", name: "LYMPH", sn: "GATE", x1: 10, y1: 7, x2: 16, y2: 11 },
    recipe: { id: "recipe", kind: "recipe", owner: "primary", name: "RECIPE", sn: "DRAFT", x1: 18, y1: 0, x2: 25, y2: 5 },
    poe: { id: "poe", kind: "poe", owner: "primary", name: "POE", sn: "VOTE", x1: 18, y1: 7, x2: 25, y2: 11 },
  };

  const PRIMARY_HALL: Room = {
    id: "hall",
    kind: "hall",
    owner: "primary",
    name: "HALL",
    sn: "",
    x1: 8,
    y1: 6,
    x2: 9,
    y2: 8,
  };

  const PRIMARY_HALLS: Hall[] = [
    { x1: 8, y1: 6, x2: 16, y2: 6 },
    { x1: 13, y1: 5, x2: 13, y2: 7 },
    { x1: 16, y1: 2, x2: 17, y2: 3 },
    { x1: 16, y1: 8, x2: 17, y2: 9 },
    { x1: 8, y1: 5, x2: 10, y2: 5 },
  ];

  type Seat = { tx: number; ty: number; face: Facing; work: boolean };
  const BASE_SEATS: Record<HarvestRoomId, Seat> = {
    bed: { tx: 2, ty: 10, face: "south", work: false },
    tor: { tx: 13, ty: 2, face: "north", work: true },
    lymph: { tx: 13, ty: 9, face: "north", work: true },
    recipe: { tx: 20, ty: 2, face: "north", work: true },
    poe: { tx: 21, ty: 10, face: "north", work: true },
  };
  const PRIMARY_HALL_SEAT: Seat = { tx: 8, ty: 7, face: "south", work: false };

  const ROOM_BLOCKS: Record<HarvestRoomId, Array<[number, number, number, number]>> = {
    bed: [[1, 8, 2, 2], [5, 10, 2, 1], [0, 5, 1, 1]],
    tor: [[12, 1, 2, 1], [15, 0, 2, 2], [10, 4, 1, 1]],
    lymph: [[12, 8, 1, 1], [15, 7, 1, 2], [10, 10, 2, 1]],
    recipe: [[19, 1, 3, 1], [23, 1, 2, 1], [24, 4, 1, 1]],
    poe: [[20, 8, 4, 2], [18, 8, 2, 1], [24, 10, 2, 1], [18, 10, 1, 1]],
  };

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

  function baseOverlays(kind: HarvestRoomId, owner: string, roomId: string): Overlay[] {
    const out: Overlay[] = [];
    const put = (file: string | null | undefined, tx: number, ty: number, prop: Overlay["prop"]) => {
      const src = propSrc(file);
      if (src && file) out.push({ room: roomId, kind, owner, src, file, tx, ty, prop });
    };
    const spec = stationCatalog.rooms[kind];
    if (kind === "bed") {
      put(spec.prop, 1, 8, "prop");
      put("desklamp.png", 4, 8, "dress");
      put("crate.png", 5, 10, "dress");
      put("plant.png", 0, 5, "dress");
    } else if (kind === "tor") {
      put(spec.prop, 12, 1, "prop");
      put(spec.terminal, 13, 1, "terminal");
      put(spec.chair, 13, 2, "chair");
      put("comms_uplink.png", 15, 0, "dress");
      put("plant.png", 10, 4, "dress");
    } else if (kind === "lymph") {
      put(spec.prop, 12, 8, "prop");
      put(spec.terminal, 15, 7, "terminal");
      put(spec.chair, 13, 9, "chair");
      put("research_samplecart.png", 10, 10, "dress");
    } else if (kind === "recipe") {
      put(spec.prop, 19, 1, "prop");
      put(spec.terminal, 20, 1, "terminal");
      put(spec.chair, 20, 2, "chair");
      put("bookshelf.png", 23, 1, "dress");
      put("plant.png", 24, 4, "dress");
    } else if (kind === "poe") {
      put(spec.prop, 20, 8, "prop");
      put(spec.terminal, 18, 8, "terminal");
      put(spec.chair, 21, 10, "chair");
      put("crate.png", 24, 10, "dress");
      put("plant.png", 18, 10, "dress");
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

  function packWings(crew: NaanCrewMember[]): number[] {
    const n = crew.length;
    if (!n) return [];
    const prefer = crew.map((c) => (c.rooms.includes("bed") ? 22 : 15));
    const sum = prefer.reduce((a, b) => a + b, 0);
    if (PRIMARY_COLS + sum <= VIEW_COLS - 2) return prefer;
    if (PRIMARY_COLS + 15 * n <= VIEW_COLS - 2) return crew.map(() => 15);
    return crew.map(() => 12);
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

  function shiftSeat(kind: HarvestRoomId, dest: Room): Seat {
    const src = BASE[kind];
    const base = BASE_SEATS[kind];
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
    seats: Record<string, Partial<Record<RoomKind, Seat>>>;
    walk: Record<string, Set<string>>;
    enabled: Record<string, HarvestRoomId[]>;
    walkHalls: Record<string, Hall[]>;
  };

  function markWalk(set: Set<string>, x1: number, y1: number, x2: number, y2: number) {
    for (let y = y1; y <= y2; y++) for (let x = x1; x <= x2; x++) set.add(x + "," + y);
  }

  function blockFoot(set: Set<string>, tx: number, ty: number, w: number, h: number) {
    for (let y = ty; y < ty + h; y++) for (let x = tx; x < tx + w; x++) set.delete(x + "," + y);
  }

  function placeBlocks(set: Set<string>, kind: HarvestRoomId, dest: Room) {
    const src = BASE[kind];
    const dx = dest.x1 - src.x1;
    const dy = dest.y1 - src.y1;
    for (const [x, y, w, h] of ROOM_BLOCKS[kind]) {
      const nx = x + dx;
      const ny = y + dy;
      const x0 = Math.max(nx, dest.x1);
      const y0 = Math.max(ny, dest.y1);
      const x1 = Math.min(nx + w - 1, dest.x2);
      const y1 = Math.min(ny + h - 1, dest.y2);
      if (x1 >= x0 && y1 >= y0) blockFoot(set, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
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

    for (const id of enabled.primary) {
      const r = { ...BASE[id] };
      rooms.push(r);
      seats.primary[id] = { ...BASE_SEATS[id] };
      overlays.push(...baseOverlays(id, "primary", id));
    }

    const strides = packWings(crew);
    let cursor = PRIMARY_COLS;
    crew.forEach((c, i) => {
      const w = strides[i] || 15;
      const bx = cursor;
      cursor += w;
      const leftW = w <= 12 ? 5 : 6;
      const rightW = leftW;
      const gap = w <= 12 ? 1 : 1;
      const hallW = w <= 12 ? 1 : 2;
      const leftX1 = bx + hallW;
      const leftX2 = leftX1 + leftW - 1;
      const rightX1 = leftX2 + 1 + gap;
      const rightX2 = rightX1 + rightW - 1;
      const want = c.rooms.length ? [...c.rooms] : ["tor"];
      enabled[c.id] = want;
      seats[c.id] = {};
      walk[c.id] = new Set();

      const localHalls: Hall[] = [
        { x1: bx, y1: 6, x2: Math.max(bx, rightX2), y2: 6 },
        { x1: leftX1 + 2, y1: 5, x2: leftX1 + 2, y2: 7 },
        { x1: leftX2, y1: 2, x2: leftX2 + 1, y2: 3 },
        { x1: leftX2, y1: 8, x2: leftX2 + 1, y2: 9 },
      ];
      // Paint a bridge to the wing; walk stays inside each body's own rooms.
      halls.push({ x1: 16, y1: 6, x2: bx + hallW, y2: 6 });
      for (const h of localHalls) halls.push(h);
      walkHalls[c.id] = localHalls;

      const hallRoom: Room = {
        id: c.id + ":hall",
        kind: "hall",
        owner: c.id,
        name: "HALL",
        sn: "",
        x1: bx,
        y1: 6,
        x2: bx + Math.max(0, hallW - 1),
        y2: 8,
      };
      rooms.push(hallRoom);
      seats[c.id].hall = { tx: bx, ty: 7, face: "south", work: false };

      const slot: Partial<Record<HarvestRoomId, Room>> = {
        tor: wingRoom(c.id, "tor", leftX1, 0, leftX2, 5),
        lymph: wingRoom(c.id, "lymph", leftX1, 7, leftX2, 11),
        recipe: wingRoom(c.id, "recipe", rightX1, 0, rightX2, 5),
        poe: wingRoom(c.id, "poe", rightX1, 7, rightX2, 11),
      };

      if (w >= 22 && want.includes("bed")) {
        slot.bed = wingRoom(c.id, "bed", rightX2 + 2, 4, rightX2 + 7, 11);
        const bedHall: Hall = { x1: rightX2, y1: 6, x2: rightX2 + 2, y2: 6 };
        halls.push(bedHall);
        walkHalls[c.id].push(bedHall);
      } else if (want.includes("bed") && !want.includes("lymph")) {
        slot.bed = wingRoom(c.id, "bed", leftX1, 4, leftX2, 11);
      }

      for (const kind of want) {
        const dest = slot[kind];
        if (!dest || dest.kind === "hall") {
          seats[c.id][kind] = seats[c.id].hall;
          continue;
        }
        rooms.push(dest);
        seats[c.id][kind] = shiftSeat(kind, dest);
        const shifted = baseOverlays(kind, c.id, dest.id).map((ov) => shiftInto(ov, BASE[kind], dest));
        overlays.push(...shifted);
      }
    });

    const stCols = Math.max(PRIMARY_COLS, cursor);
    let ox = Math.max(1, Math.floor((VIEW_COLS - stCols) / 2));
    if (ox + stCols > VIEW_COLS) ox = Math.max(1, VIEW_COLS - stCols);

    const paintRooms = rooms.filter((r) => r.kind === "hall" || enabled[r.owner]?.includes(r.kind as HarvestRoomId));
    for (const owner of Object.keys(walk)) {
      const set = walk[owner];
      for (const r of rooms) {
        if (r.owner !== owner) continue;
        if (r.kind !== "hall" && !enabled[owner]?.includes(r.kind as HarvestRoomId)) continue;
        markWalk(set, r.x1, r.y1, r.x2, r.y2);
      }
      for (const h of walkHalls[owner] || []) {
        markWalk(set, h.x1, h.y1, h.x2, h.y2);
      }
      for (const r of rooms) {
        if (r.owner !== owner || r.kind === "hall") continue;
        if (!enabled[owner]?.includes(r.kind as HarvestRoomId)) continue;
        placeBlocks(set, r.kind as HarvestRoomId, r);
      }
    }

    overlays.sort((a, b) => a.ty + viewOf(a.file).h - (b.ty + viewOf(b.file).h));
    return { rooms: paintRooms, halls, overlays, ox, stCols, seats, walk, enabled, walkHalls };
  }

  const reduceMotion =
    typeof matchMedia !== "undefined" &&
    matchMedia("(prefers-reduced-motion: reduce)").matches;

  $: look = $stationLook;
  $: agent = findAgent(look.agent);
  $: inferred = inferHarvestRoom(status, task, lastLog);
  $: xp = harvestLevel({ submissions, ngt });
  $: active = (status || "").toUpperCase() === "ACTIVE";

  let liveRooms: Room[] = [{ ...BASE.bed }, { ...BASE.tor }, { ...BASE.lymph }, { ...BASE.recipe }, { ...BASE.poe }];
  let liveHalls: Hall[] = PRIMARY_HALLS.map((h) => ({ ...h }));
  let overlays: Overlay[] = [];
  let ox = 19;
  const oy = 14;
  let stCols = PRIMARY_COLS;
  let deck: Deck = buildDeck(["bed", "tor", "lymph", "recipe", "poe"], []);
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
  let z = ZFIT;
  let tz = ZFIT;
  let panX = 0;
  let panY = 0;
  let tpanX = 0;
  let tpanY = 0;
  let drag: { id: number; x: number; y: number } | null = null;
  let pinch0 = 0;

  function inDeck(x: number, y: number): boolean {
    if (x < 0 || y < 0 || x >= stCols || y >= ST_ROWS) return false;
    return (
      liveRooms.some((r) => x >= r.x1 && x <= r.x2 && y >= r.y1 && y <= r.y2) ||
      liveHalls.some((h) => x >= h.x1 && x <= h.x2 && y >= h.y1 && y <= h.y2)
    );
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
    if (Math.abs(angNorm(a - DIR_A[cur])) < Math.PI / 4 + 0.13) return cur;
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

  function resolveGoal(owner: string, want: HarvestRoomId): { id: RoomKind; seat: Seat } {
    const on = deck.enabled[owner] || [];
    const bag = deck.seats[owner] || {};
    const hall = bag.hall || PRIMARY_HALL_SEAT;
    if (on.includes(want) && bag[want]) return { id: want, seat: bag[want] as Seat };
    if (!on.length) return { id: "hall", seat: hall };
    const origin = bag[want]
      || (owner === "primary" ? BASE_SEATS[want] : undefined)
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
    w.settleLeft = seat.work && active ? SETTLE : 0;
    if (reduceMotion && seat.work && active) {
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

  function stepWalker(w: Walker, inferredRoom: HarvestRoomId, dt: number) {
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
      if (w.settleLeft <= 0) w.working = seat.work && active;
      return;
    }
    w.working = seat.work && active;
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
    if (owner === PRIMARY_NAAN_ID) return "";
    const c = $naanCrew.find((x) => x.id === owner);
    return c ? agentShortName(c.skin) : "";
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
  $: crewViews = (poseClock >= 0 ? crewWalk : crewWalk).map((w) => {
    const c = $naanCrew.find((x) => x.id === w.owner);
    return {
      id: w.id,
      owner: w.owner,
      sit: w.sitting,
      left: ((ox + w.px) / VIEW_COLS) * 100,
      top: ((oy + w.py) / VIEW_ROWS) * 100,
      z: 8 + Math.floor(w.py),
      src: walkerSrc(w),
      bump: frameBump,
      name: findAgent(c?.skin || w.skin).label,
      pose: poseWord(w.sitting, w.working),
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
    for (let y = 0; y < ST_ROWS; y++) {
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

  function hullBox(): { x0: number; y0: number; x1: number; y1: number } {
    let x0 = VW;
    let y0 = VH;
    let x1 = 0;
    let y1 = 0;
    for (let y = 0; y < ST_ROWS; y++) {
      for (let x = 0; x < stCols; x++) {
        if (!inDeck(x, y)) continue;
        const dx = (ox + x) * TILE;
        const dy = (oy + y) * TILE;
        if (dx < x0) x0 = dx;
        if (dy < y0) y0 = dy;
        if (dx + TILE > x1) x1 = dx + TILE;
        if (dy + TILE > y1) y1 = dy + TILE;
      }
    }
    return { x0, y0, x1, y1 };
  }

  function paintSolar(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number) {
    ctx.fillStyle = "#c4a464";
    ctx.fillRect(x, y, w, h);
    ctx.fillStyle = "#0a1218";
    ctx.fillRect(x + 1, y + 1, w - 2, h - 2);
    const cell = 5;
    for (let py = y + 2; py < y + h - 2; py += cell) {
      for (let px = x + 2; px < x + w - 2; px += cell) {
        const on = ((px + py) / cell) & 1;
        ctx.fillStyle = on ? "#163044" : "#0f1c28";
        ctx.fillRect(px, py, cell - 1, cell - 1);
        ctx.fillStyle = "#2a5a72";
        ctx.fillRect(px, py, cell - 1, 1);
      }
    }
    ctx.fillStyle = "#8a7340";
    for (let gx = x + 1; gx < x + w; gx += cell) ctx.fillRect(gx, y + 1, 1, h - 2);
    for (let gy = y + 1; gy < y + h; gy += cell) ctx.fillRect(x + 1, gy, w - 2, 1);
  }

  function paintDish(ctx: CanvasRenderingContext2D, cx: number, cy: number) {
    ctx.fillStyle = "#1c1c1a";
    ctx.fillRect(cx - 16, cy - 12, 32, 26);
    ctx.fillStyle = "#2e2c28";
    ctx.fillRect(cx - 14, cy - 10, 28, 22);
    ctx.fillStyle = "#c4a020";
    ctx.fillRect(cx - 14, cy + 10, 28, 2);
    ctx.fillStyle = "#3a3832";
    ctx.fillRect(cx - 14, cy - 10, 28, 1);
    ctx.fillStyle = "#5a5850";
    ctx.fillRect(cx - 1, cy - 22, 2, 14);
    ctx.fillStyle = "#4a6a78";
    ctx.beginPath();
    ctx.ellipse(cx, cy - 24, 9, 5, -0.35, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = "#8ab8c8";
    ctx.beginPath();
    ctx.ellipse(cx - 1, cy - 25, 4, 2, -0.35, 0, Math.PI * 2);
    ctx.fill();
  }

  function paintAmberWindows(ctx: CanvasRenderingContext2D) {
    for (let y = 0; y < ST_ROWS; y++) {
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

  function paintHullExtras(ctx: CanvasRenderingContext2D) {
    const b = hullBox();
    if (b.x1 <= b.x0) return;
    const wingW = 52;
    const wingH = 28;
    paintSolar(ctx, Math.max(2, b.x0 - wingW - 4), Math.min(VH - wingH - 2, b.y1 - 10), wingW, wingH);
    paintSolar(ctx, Math.max(2, b.x0 - 34), Math.min(VH - 22, b.y1 + 6), 40, 20);
    paintDish(ctx, Math.min(VW - 20, b.x1 + 22), Math.floor((b.y0 + b.y1) / 2));
    paintAmberWindows(ctx);
  }

  function paintDeck() {
    if (!deckCv) return;
    const ctx = deckCv.getContext("2d");
    if (!ctx) return;
    ctx.imageSmoothingEnabled = false;
    ctx.clearRect(0, 0, VW, VH);
    const rim = 6;
    for (let y = 0; y < ST_ROWS; y++) {
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
        ctx.fillStyle = "rgba(0,0,0,0.18)";
        ctx.fillRect(dx, dy, TILE, 1);
        ctx.fillRect(dx, dy, 1, TILE);
        if (shellImg) {
          if (!inDeck(x - 1, y)) stampStrip(ctx, shellImg, dx - rim, dy, rim, TILE, x);
          if (!inDeck(x + 1, y)) stampStrip(ctx, shellImg, dx + TILE, dy, rim, TILE, x);
          if (!inDeck(x, y - 1)) stampStrip(ctx, shellImg, dx - rim, dy - rim, TILE + rim * 2, rim, x);
          if (!inDeck(x, y + 1)) stampStrip(ctx, shellImg, dx - rim, dy + TILE, TILE + rim * 2, rim, x);
        } else {
          ctx.fillStyle = "#3a3632";
          if (!inDeck(x - 1, y)) ctx.fillRect(dx - rim, dy, rim, TILE);
          if (!inDeck(x + 1, y)) ctx.fillRect(dx + TILE, dy, rim, TILE);
          if (!inDeck(x, y - 1)) ctx.fillRect(dx - rim, dy - rim, TILE + rim * 2, rim);
          if (!inDeck(x, y + 1)) ctx.fillRect(dx - rim, dy + TILE, TILE + rim * 2, rim);
        }
      }
    }
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
    for (const w of crewWalk) stepWalker(w, inferred, dt);
    const fps = hero.sitting ? (hero.working ? 6 : 4) : 10;
    acc += dt;
    if (acc >= 1 / fps) {
      acc = 0;
      if (hero.sitting) hero.frame += 1;
      for (const w of crewWalk) if (w.sitting) w.frame += 1;
    }
    poseClock += 1;
    frameBump += 1;
    paintSky(ts);
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

  function pctBox(r: Room): string {
    const left = ((ox + r.x1) / VIEW_COLS) * 100;
    const top = ((oy + r.y1) / VIEW_ROWS) * 100;
    const w = ((r.x2 - r.x1 + 1) / VIEW_COLS) * 100;
    const h = ((r.y2 - r.y1 + 1) / VIEW_ROWS) * 100;
    return `left:${left}%;top:${top}%;width:${w}%;height:${h}%`;
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
    <div class="holo-vol" aria-hidden="true">
      <div class="holo-cone"></div>
      <div class="holo-mark">SYNAPSENET</div>
    </div>
    <div class="world" style="transform:{worldXf}">
      <canvas class="deck" bind:this={deckCv} width={VW} height={VH} aria-hidden="true"></canvas>

      {#each overlays as ov}
        <img
          class="prop"
          class:term={ov.prop === "terminal"}
          class:chair={ov.prop === "chair"}
          class:dress={ov.prop === "dress"}
          class:hot={poseClock >= 0 && ov.kind === goalHot[ov.owner]}
          src={ov.src}
          alt=""
          draggable="false"
          style={pctProp(ov)}
        />
      {/each}

      {#each liveRooms as r}
        {#if r.kind !== "hall"}
          <div
            class="room-label"
            class:on={poseClock >= 0 && labelHot(r)}
            class:mine={poseClock >= 0 && r.owner === focusedId && !labelHot(r)}
            class:dim={poseClock >= 0 && !labelHot(r)}
            style={pctBox(r)}
          >
            {#if ownerTag(r.owner)}
              <span class="ro">{ownerTag(r.owner)}</span>
            {/if}
            <span class="rn">{r.name}</span>
            {#if poseClock >= 0 && labelHot(r)}
              <span class="rs">{r.sn} · {r.owner === focusW.owner ? poseLabel : ""}</span>
            {/if}
          </div>
        {/if}
      {/each}

      <div
        class="walker"
        class:sit={poseClock >= 0 && hero.sitting}
        class:focused={focusedId === PRIMARY_NAAN_ID}
        style="left:{hereX}%;top:{hereY}%;z-index:{32 + Math.floor(hero.py)};--beat:{poseClock}"
        role="button"
        tabindex="0"
        on:pointerdown|stopPropagation={() => focusOwner(PRIMARY_NAAN_ID)}
        on:keydown={(e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); focusOwner(PRIMARY_NAAN_ID); } }}
      >
        <AgentLevelChip level={xp.level} frac={xp.frac} name={agent.label} />
        <img src={heroSrc} alt="" width="48" height="48" draggable="false" />
      </div>

      {#each crewViews as cv (cv.id)}
        <div
          class="walker"
          class:sit={cv.sit}
          class:focused={focusedId === cv.owner}
          style="left:{cv.left}%;top:{cv.top}%;z-index:{32 + Math.floor(cv.z)};--beat:{cv.bump}"
          role="button"
          tabindex="0"
          on:pointerdown|stopPropagation={() => focusOwner(cv.owner)}
          on:keydown={(e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); focusOwner(cv.owner); } }}
        >
          <AgentLevelChip level={1} name={cv.name} />
          <img src={cv.src} alt="" width="48" height="48" draggable="false" />
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

  .holo-vol {
    position: absolute;
    left: 50%;
    top: 10%;
    transform: translateX(-50%);
    z-index: 2;
    pointer-events: none;
    display: flex;
    flex-direction: column;
    align-items: center;
  }

  .holo-cone {
    width: 120px;
    height: 48px;
    background: linear-gradient(180deg, rgba(0, 229, 255, 0.18), transparent 70%);
    clip-path: polygon(42% 0, 58% 0, 100% 100%, 0 100%);
    opacity: 0.7;
    animation: holo-pulse 2.8s ease-in-out infinite;
  }

  .holo-mark {
    margin: -22px 0 0;
    font-family: var(--font);
    font-size: 8px;
    line-height: 1;
    letter-spacing: 4px;
    color: var(--cy, #00e5ff);
    text-shadow: 0 0 10px rgba(0, 229, 255, 0.55), 0 0 2px rgba(0, 229, 255, 0.8);
    white-space: nowrap;
    opacity: 0.88;
  }

  @keyframes holo-pulse {
    0%, 100% { opacity: 0.45; }
    50% { opacity: 0.85; }
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

  .prop {
    position: absolute;
    object-fit: contain;
    object-position: bottom center;
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
    padding: 3px 4px;
    pointer-events: none;
    border: 1px solid rgba(255, 255, 255, 0.08);
    box-sizing: border-box;
    z-index: 30;
    border-radius: 0;
  }

  .room-label.on {
    border-color: var(--cy, #00e5ff);
  }

  .room-label.mine {
    border-color: rgba(0, 229, 255, 0.28);
  }

  .room-label.dim {
    opacity: 0.55;
  }

  .ro {
    font-family: var(--font);
    font-size: 6px;
    color: var(--cy, #00e5ff);
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

  .walker {
    position: absolute;
    width: 4.6%;
    image-rendering: pixelated;
    pointer-events: auto;
    cursor: pointer;
    transform: translate(-50%, -100%);
  }

  .walker.sit {
    transform: translate(-50%, -92%);
  }

  .walker.focused {
    filter: drop-shadow(0 0 6px rgba(0, 229, 255, 0.55));
  }

  .walker :global(.ag-lv) {
    position: absolute;
    left: 50%;
    bottom: 100%;
    transform: translate(-50%, -2px);
    z-index: 2;
    background: #000;
    color: var(--cy, #00e5ff);
    border-color: #ffd34a;
    font-family: var(--font);
    backdrop-filter: none;
    -webkit-backdrop-filter: none;
  }

  .walker :global(.ag-n) {
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

  @media (prefers-reduced-motion: reduce) {
    .holo-cone {
      animation: none;
    }
  }
</style>
