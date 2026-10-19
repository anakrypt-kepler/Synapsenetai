// Right-edge plaques. Faults and security warnings stay until the
// condition clears or the operator dismisses them. NAAN start/stop
// notes expire on their own.
import { writable } from "svelte/store";
import type { NodeStatus } from "./rpc";

export type NoticeKind = "fault" | "security" | "naan";

export type Notice = {
  id: string;
  kind: NoticeKind;
  title: string;
  body: string;
};

const dismissed = new Set<string>();
let seq = 0;
let seenStatus = false;
let lastNaan = "";

export const notices = writable<Notice[]>([]);

function upsert(id: string, kind: NoticeKind, title: string, body: string) {
  notices.update((list) => {
    const rest = list.filter((n) => n.id !== id);
    return [{ id, kind, title, body }, ...rest].slice(0, 6);
  });
}

export function dismissNotice(id: string) {
  dismissed.add(id);
  notices.update((list) => list.filter((n) => n.id !== id));
}

export function syncSticky(id: string, active: boolean, kind: NoticeKind, title: string, body: string) {
  if (!active) {
    dismissed.delete(id);
    notices.update((list) => list.filter((n) => n.id !== id));
    return;
  }
  if (dismissed.has(id)) return;
  upsert(id, kind, title, body);
}

export function pushNotice(kind: NoticeKind, title: string, body: string, ttlMs = 0) {
  const id = "n" + String(++seq);
  upsert(id, kind, title, body);
  if (ttlMs > 0) {
    setTimeout(() => {
      notices.update((list) => list.filter((n) => n.id !== id));
    }, ttlMs);
  }
}

const NAAN_LIVE = new Set(["active", "cooldown", "budget_exhausted", "stopping"]);

function noteNaan(state: string) {
  const next = (state || "off").toLowerCase();
  if (!seenStatus) {
    lastNaan = next;
    return;
  }
  if (next === lastNaan) return;
  const prev = lastNaan;
  lastNaan = next;
  const wasLive = NAAN_LIVE.has(prev);
  const nowLive = next === "active" || next === "cooldown" || next === "budget_exhausted";
  if (!wasLive && nowLive) {
    pushNotice(
      "naan",
      "NAAN agent started",
      "The harvest loop is running. Stop it on the NAAN tab when you want it idle.",
      8000,
    );
    return;
  }
  if (next === "stopping") {
    pushNotice("naan", "NAAN agent stopping", "Letting the current page finish.", 8000);
    return;
  }
  if (wasLive && (next === "off" || next === "idle")) {
    pushNotice("naan", "NAAN agent stopped", "The harvest loop is off until you start it again.", 8000);
  }
}

export function noteStatus(status: NodeStatus | null) {
  if (!status) {
    syncSticky(
      "status",
      true,
      "fault",
      "Node status failed",
      "The engine did not answer. Bars stay stale until the next good poll.",
    );
    return;
  }
  syncSticky("status", false, "fault", "", "");

  const torUp = status.connection === "tor";
  const boot = (status.tor_bootstrap || "").trim();
  syncSticky(
    "tor",
    !torUp,
    "fault",
    "Tor is not connected",
    boot
      ? `Bootstrap is ${boot}. The mesh stays off until session Tor finishes.`
      : "Session Tor is down. The mesh stays off until it connects. Leave SET on Tor.",
  );

  if (typeof status.pqc_backend === "string" && status.pqc_backend.length > 0) {
    const simulated = status.pqc_backend !== "liboqs";
    syncSticky(
      "pqc",
      simulated,
      "security",
      "Quantum protection is simulated",
      "Handshake and signatures are not liboqs. Rebuild the node with the real library before you trust that panel.",
    );
  }

  const naanState = status.naan_state || "off";
  syncSticky(
    "naan-error",
    naanState.toLowerCase() === "error",
    "fault",
    "NAAN agent hit an error",
    "Open the NAAN tab and read the status line before you start it again.",
  );
  noteNaan(naanState);
  seenStatus = true;
}
