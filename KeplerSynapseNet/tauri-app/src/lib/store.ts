import { writable, derived } from "svelte/store";
import { getStatus, parseStatus, type NodeStatus } from "./rpc";

// Shared UI state. Tabs match the desktop nav. nodeStatus is polled from
// synapsed_get_status; if polling dies the bars freeze, they do not crash.

export type TabId =
  | "dashboard"
  | "wallet"
  | "transfers"
  | "blocks"
  | "knowledge"
  | "naan"
  | "harvest"
  | "exploits"
  | "messages"
  | "ide"
  | "network"
  | "rental"
  | "settings";

export const activeTab = writable<TabId>("dashboard");
// 1 = new tab is to the right (slide in from right). -1 = from the left.
export const tabSlideDir = writable<1 | -1>(1);
export const showSetupWizard = writable<boolean>(false);
export const myWalletAddress = writable<string>("");

export const nodeStatus = writable<NodeStatus>({
  connection: "disconnected",
  peers: 0,
  balance: "0.00",
  naan_state: "off",
  last_block: 0,
  model_loaded: false,
  model_name: "",
  tor_bootstrap: "",
  tor_circuits: 0,
  onion: "",
  bandwidth_in: 0,
  bandwidth_out: 0,
  version: "v0.1.0-V9",
});

export const connectionColor = derived(nodeStatus, ($s) => {
  if ($s.connection === "tor") return "green";
  return "red";
});

export const connectionLabel = derived(nodeStatus, ($s) => {
  if ($s.connection === "tor") return "TOR";
  return "OFF";
});

let pollInterval: ReturnType<typeof setInterval> | null = null;

export function startStatusPolling() {
  if (pollInterval) return;
  const tick = async () => {
    try {
      const raw = await getStatus();
      nodeStatus.set(parseStatus(raw));
    } catch {}
  };
  tick();
  pollInterval = setInterval(tick, 2000);
}

export function stopStatusPolling() {
  if (pollInterval) {
    clearInterval(pollInterval);
    pollInterval = null;
  }
}

export const tabs: { id: TabId; label: string }[] = [
  { id: "dashboard", label: "MAIN" },
  { id: "wallet", label: "WALLET" },
  { id: "transfers", label: "SEND" },
  { id: "blocks", label: "BLOCKS" },
  { id: "knowledge", label: "KNOW" },
  { id: "naan", label: "NAAN" },
  { id: "harvest", label: "HARVEST" },
  { id: "exploits", label: "INTEL" },
  { id: "messages", label: "MSG" },
  { id: "ide", label: "IDE" },
  { id: "network", label: "NET" },
  { id: "rental", label: "RENT" },
  { id: "settings", label: "SET" },
];
