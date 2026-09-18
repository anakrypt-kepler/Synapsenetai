import { writable } from "svelte/store";
import { rpcCall } from "./rpc";
import catalog from "./stationCatalog.json";

// 2D station look: agent sprite + floor/wall/shell skins from public/station.
// Persisted through settings.update (settings.json). Does not change harvest.

export type Facing = "north" | "south" | "east" | "west" | "north-east" | "north-west" | "south-east" | "south-west";

export type StationAgent = {
  id: string;
  folder: string;
  label: string;
  preview: string | null;
  idle: string | null;
  walk: string[];
  type: string[];
  src: string;
  idleSrc: string | null;
  walkSrc: string[];
  typeSrc: string[];
  rot?: Record<string, string>;
  sitSrc?: Record<string, string[]>;
  walkDir?: Record<string, string[]>;
  typeDir?: Record<string, string[]>;
};

export type StationTile = {
  id: string;
  file: string;
  label: string;
  src: string;
};

export type StationLook = {
  agent: string;
  floor: string;
  wall: string;
  shell: string;
};

export type HarvestRoomId = "bed" | "tor" | "lymph" | "recipe" | "poe";

type StationCatalog = {
  agents: StationAgent[];
  floors: StationTile[];
  walls: StationTile[];
  shells: StationTile[];
  defaults: StationLook;
  rooms: Record<HarvestRoomId, {
    prop: string;
    terminal: string | null;
    chair?: string | null;
    title: string;
    sn: string;
  }>;
};

export const stationCatalog = catalog as StationCatalog;

export const DEFAULT_STATION_LOOK: StationLook = { ...stationCatalog.defaults };

export const stationLook = writable<StationLook>({ ...DEFAULT_STATION_LOOK });

function asId(v: unknown, fallback: string): string {
  return typeof v === "string" && v.trim() ? v.trim() : fallback;
}

export function findAgent(id: string): StationAgent {
  return stationCatalog.agents.find((a) => a.id === id) ||
    stationCatalog.agents.find((a) => a.id === DEFAULT_STATION_LOOK.agent) ||
    stationCatalog.agents[0];
}

export function findTile(list: StationTile[], id: string, fallbackId: string): StationTile {
  return list.find((t) => t.id === id) || list.find((t) => t.id === fallbackId) || list[0];
}

export function agentPreviewSrc(id: string): string {
  const a = findAgent(id);
  return a.src;
}

export function agentPortraitSrc(id: string): string {
  const a = findAgent(id);
  return a.rot?.south || a.src;
}

export function agentShortName(id: string): string {
  const label = findAgent(id).label.trim();
  const first = label.split(/\s+/)[0] || label;
  return first.slice(0, 8).toUpperCase();
}

export function floorSrc(id: string): string {
  return findTile(stationCatalog.floors, id, DEFAULT_STATION_LOOK.floor).src;
}

export function wallSrc(id: string): string {
  return findTile(stationCatalog.walls, id, DEFAULT_STATION_LOOK.wall).src;
}

export function shellSrc(id: string): string {
  return findTile(stationCatalog.shells, id, DEFAULT_STATION_LOOK.shell).src;
}

export function propSrc(file: string | null | undefined): string | null {
  if (!file) return null;
  return `/station/props/${file}`;
}

export function normalizeLook(raw: Partial<StationLook> | null | undefined): StationLook {
  const agent = findAgent(asId(raw?.agent, DEFAULT_STATION_LOOK.agent)).id;
  const floor = findTile(stationCatalog.floors, asId(raw?.floor, DEFAULT_STATION_LOOK.floor), DEFAULT_STATION_LOOK.floor).id;
  const wall = findTile(stationCatalog.walls, asId(raw?.wall, DEFAULT_STATION_LOOK.wall), DEFAULT_STATION_LOOK.wall).id;
  const shell = findTile(stationCatalog.shells, asId(raw?.shell, DEFAULT_STATION_LOOK.shell), DEFAULT_STATION_LOOK.shell).id;
  return { agent, floor, wall, shell };
}

export function lookFromSettings(parsed: Record<string, unknown>): StationLook {
  return normalizeLook({
    agent: asId(parsed.naan_agent_skin, DEFAULT_STATION_LOOK.agent),
    floor: asId(parsed.naan_floor_skin, DEFAULT_STATION_LOOK.floor),
    wall: asId(parsed.naan_wall_skin, DEFAULT_STATION_LOOK.wall),
    shell: asId(parsed.naan_shell_skin, DEFAULT_STATION_LOOK.shell),
  });
}

export async function loadStationLookFromSettings(): Promise<StationLook> {
  let look = { ...DEFAULT_STATION_LOOK };
  try {
    const raw = await rpcCall("settings.get", "{}");
    const parsed = JSON.parse(raw);
    if (parsed && typeof parsed === "object" && !parsed.error) {
      look = lookFromSettings(parsed as Record<string, unknown>);
    }
  } catch {}
  stationLook.set(look);
  try {
    const { loadNaanDeckFromSettings } = await import("./naanCrew");
    await loadNaanDeckFromSettings();
  } catch {}
  return look;
}

export async function saveStationLook(next: StationLook): Promise<string> {
  const look = normalizeLook(next);
  stationLook.set(look);
  try {
    const raw = await rpcCall("settings.update", JSON.stringify({
      naan_agent_skin: look.agent,
      naan_floor_skin: look.floor,
      naan_wall_skin: look.wall,
      naan_shell_skin: look.shell,
    }));
    const parsed = JSON.parse(raw);
    if (parsed && parsed.error) return String(parsed.error);
    if (parsed && parsed.ok === false) return "SAVE FAILED";
    return "";
  } catch (e) {
    return e instanceof Error ? e.message : "SAVE FAILED";
  }
}

// Rooms follow the engine harvest path: fetch (Tor) -> extract/draft (RECIPE) -> PoE submit.
export function inferHarvestRoom(status: string, task: string, lastLog: string): HarvestRoomId {
  const st = (status || "").toUpperCase();
  const blob = `${task} ${lastLog}`.toLowerCase();
  if (st === "QUARANTINE") return "lymph";
  if (st !== "ACTIVE") return "bed";
  if (/poe reject|poe submit|poe pending|\bpoe\b.*ok|vote|final/.test(blob)) return "poe";
  if (/extract|recipe|draft|submit/.test(blob)) return "recipe";
  if (/lymph|replay|hash|gate|captcha|ocr/.test(blob)) return "lymph";
  if (/fetch fail|failed|fetch|tor|onion|harvest|http|clearnet/.test(blob)) return "tor";
  return "tor";
}

export function harvestPose(sitting: boolean, working: boolean): "walk" | "sit" | "type" {
  if (!sitting) return "walk";
  return working ? "type" : "sit";
}

const DIAG_FALLBACK: Record<string, Facing> = {
  "north-east": "east",
  "north-west": "west",
  "south-east": "east",
  "south-west": "west",
};

function dirSeq(map: Record<string, string[]> | undefined, dir: Facing, fallback: Facing): string[] {
  if (!map) return [];
  return map[dir] || map[DIAG_FALLBACK[dir] || ""] || map[fallback] || [];
}

export function agentPoseSrc(
  agent: StationAgent,
  pose: "walk" | "sit" | "type" | "idle",
  dir: Facing,
  frame: number,
): string {
  if (pose === "walk") {
    const seq = dirSeq(agent.walkDir, dir, "south") || agent.walkSrc || [];
    if (seq.length) return seq[Math.abs(frame) % seq.length];
  }
  if (pose === "type") {
    const seq = dirSeq(agent.typeDir, dir, "north") || agent.typeSrc || [];
    if (seq.length) return seq[Math.abs(frame) % seq.length];
    pose = "sit";
  }
  if (pose === "sit") {
    const seq = dirSeq(agent.sitSrc, dir, "south");
    if (seq.length) return seq[Math.abs(frame) % seq.length];
  }
  return agent.rot?.[dir] || agent.rot?.[DIAG_FALLBACK[dir] || "south"] || agent.rot?.south || agent.idleSrc || agent.src;
}
