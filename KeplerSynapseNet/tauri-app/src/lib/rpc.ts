import { invoke } from "@tauri-apps/api/core";

export async function checkFirstLaunch(): Promise<boolean> {
  return invoke<boolean>("check_first_launch");
}

export async function initEngine(): Promise<void> {
  return invoke<void>("synapsed_init");
}

export async function shutdownEngine(): Promise<void> {
  return invoke<void>("synapsed_shutdown");
}

export async function rpcCall(method: string, params: string = "{}"): Promise<string> {
  return invoke<string>("synapsed_rpc_call", { method, params });
}

export async function getStatus(): Promise<string> {
  return invoke<string>("synapsed_get_status");
}

export async function subscribeEvent(eventType: string): Promise<void> {
  return invoke<void>("synapsed_subscribe", { eventType });
}

export async function saveSetupConfig(config: SetupConfig): Promise<void> {
  return invoke<void>("save_setup_config", { config });
}

export async function getSystemInfo(): Promise<SystemInfo> {
  return invoke<SystemInfo>("get_system_info");
}

export async function walletCreate(): Promise<string> {
  return invoke<string>("wallet_create");
}

export async function walletRestore(seedPhrase: string): Promise<string> {
  return invoke<string>("wallet_restore", { seedPhrase });
}

export async function sendNgt(recipient: string, amount: string, memo?: string): Promise<string> {
  return invoke<string>("send_ngt", { recipient, amount, memo });
}

export async function getTransactions(filter?: string): Promise<string> {
  return invoke<string>("get_transactions", { filter });
}

export async function submitKnowledge(title: string, content: string, citations: string): Promise<string> {
  return invoke<string>("submit_knowledge", { title, content, citations });
}

export async function searchKnowledge(query: string): Promise<string> {
  return invoke<string>("search_knowledge", { query });
}

export async function naanControl(action: string): Promise<string> {
  return invoke<string>("naan_control", { action });
}

export async function naanConfigUpdate(config: string): Promise<string> {
  return invoke<string>("naan_config_update", { config });
}

export async function aiComplete(prompt: string): Promise<string> {
  return invoke<string>("ai_complete", { prompt });
}

export async function modelLoad(path: string): Promise<string> {
  return invoke<string>("model_load", { path });
}

export async function modelUnload(): Promise<string> {
  return invoke<string>("model_unload");
}

export async function updateSettings(settings: string): Promise<string> {
  return invoke<string>("update_settings", { settings });
}

export async function checkUpdates(): Promise<string> {
  return invoke<string>("check_updates");
}

export async function poeSubmitCode(patch: string): Promise<string> {
  return invoke<string>("poe_submit_code", { patch });
}

export interface SetupConfig {
  wallet_mode: string;
  seed_phrase: string | null;
  password: string | null;
  connection_type: string;
  bridge_lines: string | null;
  ai_model: string;
  model_path: string | null;
  cpu_threads: number;
  ram_limit_mb: number;
  disk_limit_mb: number;
  gpu_enabled: boolean;
  gpu_device: string | null;
  gpu_layers: number;
  launch_at_startup: boolean;
  mine_background: boolean;
}

export interface GpuDevice {
  id: string;
  name: string;
  vram_mb: number;
}

export interface SystemInfo {
  cpu_cores: number;
  ram_total_mb: number;
  gpu_devices: GpuDevice[];
}

export interface NodeStatus {
  connection: string;
  peers: number;
  balance: string;
  naan_state: string;
  last_block: number;
  model_loaded: boolean;
  model_name: string;
  tor_bootstrap: string;
  tor_circuits: number;
  bandwidth_in: number;
  bandwidth_out: number;
  version: string;
}

export function parseStatus(raw: string): NodeStatus {
  try {
    return JSON.parse(raw);
  } catch {
    return {
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
    };
  }
}
