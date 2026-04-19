import { writable, derived } from "svelte/store";
import { getStatus, parseStatus, type NodeStatus } from "./rpc";

export type TabId =
  | "dashboard"
  | "wallet"
  | "transfers"
  | "knowledge"
  | "naan"
  | "ide"
  | "network"
  | "settings";

export const activeTab = writable<TabId>("dashboard");
export const showSetupWizard = writable<boolean>(false);

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
  bandwidth_in: 0,
  bandwidth_out: 0,
  version: "v0.1.0-V4",
});

export const connectionColor = derived(nodeStatus, ($s) => {
  if ($s.connection === "tor") return "green";
  if ($s.connection === "clearnet") return "yellow";
  return "red";
});

export const connectionLabel = derived(nodeStatus, ($s) => {
  if ($s.connection === "tor") return "Tor";
  if ($s.connection === "clearnet") return "Clearnet";
  return "Disconnected";
});

let pollInterval: ReturnType<typeof setInterval> | null = null;

export function startStatusPolling() {
  if (pollInterval) return;
  pollInterval = setInterval(async () => {
    try {
      const raw = await getStatus();
      const parsed = parseStatus(raw);
      nodeStatus.set(parsed);
    } catch {
      // engine not ready
    }
  }, 2000);
}

export function stopStatusPolling() {
  if (pollInterval) {
    clearInterval(pollInterval);
    pollInterval = null;
  }
}

export const tabs: { id: TabId; label: string }[] = [
  { id: "dashboard", label: "Dashboard" },
  { id: "wallet", label: "Wallet" },
  { id: "transfers", label: "Transfers" },
  { id: "knowledge", label: "Knowledge" },
  { id: "naan", label: "NAAN Agent" },
  { id: "ide", label: "IDE" },
  { id: "network", label: "Network" },
  { id: "settings", label: "Settings" },
];
