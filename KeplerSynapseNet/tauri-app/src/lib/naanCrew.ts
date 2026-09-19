import { writable, get } from "svelte/store";
import { rpcCall, naanControl } from "./rpc";
import { findAgent, stationCatalog, stationLook, type HarvestRoomId } from "./stationSkins";

  // Command-block rooms + extra NAAN bodies. Crew ids persist through
  // settings.update. Start Agent / Stop Agent talk to naan.control.

export const PRIMARY_ROOM_IDS: HarvestRoomId[] = ["bed", "tor", "lymph", "recipe", "poe"];

export const ROOM_META: { id: HarvestRoomId; name: string; sn: string }[] = [
  { id: "bed", name: "BED", sn: "IDLE" },
  { id: "tor", name: "TOR", sn: "FETCH" },
  { id: "lymph", name: "LYMPH", sn: "GATE" },
  { id: "recipe", name: "RECIPE", sn: "DRAFT" },
  { id: "poe", name: "POE", sn: "VOTE" },
];

export const DEFAULT_ROOMS: HarvestRoomId[] = [...PRIMARY_ROOM_IDS];
export const DEFAULT_CREW_ROOMS: HarvestRoomId[] = ["tor", "recipe", "poe"];
export const MAX_CREW = 3;

export type NaanCrewMember = {
  id: string;
  skin: string;
  rooms: HarvestRoomId[];
  modelMode: "primary" | "own";
  modelPath: string;
};

export const naanRooms = writable<HarvestRoomId[]>([...DEFAULT_ROOMS]);
export const naanCrew = writable<NaanCrewMember[]>([]);

function isRoom(v: unknown): v is HarvestRoomId {
  return v === "bed" || v === "tor" || v === "lymph" || v === "recipe" || v === "poe";
}

function parseRooms(raw: unknown, fallback: HarvestRoomId[]): HarvestRoomId[] {
  let list: unknown[] = [];
  if (Array.isArray(raw)) list = raw;
  else if (typeof raw === "string" && raw.trim()) {
    try {
      const parsed = JSON.parse(raw);
      if (Array.isArray(parsed)) list = parsed;
    } catch {
      list = raw.split(",").map((s) => s.trim());
    }
  }
  const out: HarvestRoomId[] = [];
  for (const item of list) {
    if (isRoom(item) && !out.includes(item)) out.push(item);
  }
  return out.length ? out : [...fallback];
}

function parseCrew(raw: unknown): NaanCrewMember[] {
  let list: unknown[] = [];
  if (Array.isArray(raw)) list = raw;
  else if (typeof raw === "string" && raw.trim()) {
    try {
      const parsed = JSON.parse(raw);
      if (Array.isArray(parsed)) list = parsed;
    } catch {
      list = [];
    }
  }
  const out: NaanCrewMember[] = [];
  const seen = new Set<string>();
  for (const item of list) {
    if (!item || typeof item !== "object") continue;
    const rec = item as Record<string, unknown>;
    const id = typeof rec.id === "string" && rec.id.trim() ? rec.id.trim() : "";
    if (!id || id === "primary" || seen.has(id)) continue;
    seen.add(id);
    const skin = findAgent(typeof rec.skin === "string" ? rec.skin : "").id;
    const modelMode = rec.model_mode === "own" || rec.modelMode === "own" ? "own" : "primary";
    const modelPath =
      typeof rec.model_path === "string" ? rec.model_path :
      typeof rec.modelPath === "string" ? rec.modelPath : "";
    out.push({
      id,
      skin,
      rooms: parseRooms(rec.rooms, DEFAULT_CREW_ROOMS),
      modelMode,
      modelPath: modelMode === "own" ? modelPath : "",
    });
  }
  return out.slice(0, MAX_CREW);
}

function newCrewId(): string {
  if (typeof crypto !== "undefined" && crypto.randomUUID) return crypto.randomUUID();
  return "naan-" + Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
}

export function pickCrewSkin(avoid: string[]): string {
  const taken = new Set(avoid.filter(Boolean));
  const next = stationCatalog.agents.find((a) => !taken.has(a.id));
  if (next) return next.id;
  const other = stationCatalog.agents.find((a) => a.id !== avoid[0]);
  return other?.id || stationCatalog.agents[0]?.id || "station_minion";
}

let loadP: Promise<void> | null = null;

export function loadNaanDeckFromSettings(): Promise<void> {
  if (!loadP) {
    loadP = doLoad().then((ok) => {
      if (!ok) loadP = null;
    });
  }
  return loadP;
}

async function doLoad(): Promise<boolean> {
  try {
    const raw = await rpcCall("settings.get", "{}");
    const parsed = JSON.parse(raw);
    if (parsed && typeof parsed === "object" && !parsed.error) {
      naanRooms.set(parseRooms(parsed.naan_rooms, DEFAULT_ROOMS));
      naanCrew.set(parseCrew(parsed.naan_crew));
      return true;
    }
  } catch {}
  return false;
}

export async function persistNaanDeck(): Promise<string> {
  try {
    const raw = await rpcCall(
      "settings.update",
      JSON.stringify({
        naan_rooms: get(naanRooms),
        naan_crew: get(naanCrew).map((c) => ({
          id: c.id,
          skin: c.skin,
          rooms: c.rooms,
          model_mode: c.modelMode,
          model_path: c.modelPath,
        })),
      }),
    );
    const parsed = JSON.parse(raw);
    if (parsed && parsed.error) return String(parsed.error);
    if (parsed && parsed.ok === false) return "SAVE FAILED";
    return "";
  } catch (e) {
    return e instanceof Error ? e.message : "SAVE FAILED";
  }
}

export async function togglePrimaryRoom(id: HarvestRoomId): Promise<string> {
  if (!isRoom(id)) return "";
  const cur = get(naanRooms);
  const on = cur.includes(id);
  if (on) {
    if (cur.length <= 1) return "";
    naanRooms.set(cur.filter((r) => r !== id));
  } else {
    naanRooms.set(PRIMARY_ROOM_IDS.filter((r) => r === id || cur.includes(r)));
  }
  return persistNaanDeck();
}

export async function addCrewMember(): Promise<string> {
  const crew = get(naanCrew);
  if (crew.length >= MAX_CREW) return "";
  const primary = get(stationLook).agent;
  const skin = pickCrewSkin([primary, ...crew.map((c) => c.skin)]);
  naanCrew.set([
    ...crew,
    { id: newCrewId(), skin, rooms: [...DEFAULT_CREW_ROOMS], modelMode: "primary", modelPath: "" },
  ]);
  return persistNaanDeck();
}

export async function removeCrewMember(id: string): Promise<string> {
  try {
    await naanControl("stop", id);
  } catch {
    // Persist the roster even if the engine is already off.
  }
  naanCrew.set(get(naanCrew).filter((c) => c.id !== id));
  return persistNaanDeck();
}

export async function toggleCrewRoom(id: string, room: HarvestRoomId): Promise<string> {
  if (!isRoom(room)) return "";
  const crew = get(naanCrew);
  const next = crew.map((c) => {
    if (c.id !== id) return c;
    const on = c.rooms.includes(room);
    if (on) {
      if (c.rooms.length <= 1) return c;
      return { ...c, rooms: c.rooms.filter((r) => r !== room) };
    }
    return {
      ...c,
      rooms: PRIMARY_ROOM_IDS.filter((r) => r === room || c.rooms.includes(r)),
    };
  });
  naanCrew.set(next);
  return persistNaanDeck();
}

export async function setCrewSkin(id: string, skin: string): Promise<string> {
  const resolved = findAgent(skin).id;
  naanCrew.set(
    get(naanCrew).map((c) => (c.id === id ? { ...c, skin: resolved } : c)),
  );
  return persistNaanDeck();
}

export async function setCrewPrimaryModel(id: string): Promise<string> {
  naanCrew.set(
    get(naanCrew).map((c) => (c.id === id ? { ...c, modelMode: "primary", modelPath: "" } : c)),
  );
  return persistNaanDeck();
}

export async function setCrewOwnModel(id: string, path: string): Promise<string> {
  const modelPath = path.trim();
  if (!modelPath) return "NO PATH SELECTED";
  naanCrew.set(
    get(naanCrew).map((c) =>
      c.id === id ? { ...c, modelMode: "own", modelPath } : c,
    ),
  );
  return persistNaanDeck();
}
