# SynapseNet 0.1.0-alphaV4 — UI Verification Report

> **Note:** All screenshots use simulated mock data for visual demonstration purposes only.
> Values such as "1,847.00 NGT", "12 peers", "NAAN: mining", wallet addresses, seed phrases,
> transaction history, peer lists, and agent submissions are hardcoded placeholders.
> None of this data is real. In production the Tauri app receives all live data from
> `synapsed_get_status()` and `synapsed_rpc_call()` via the libsynapsed FFI bridge,
> polling every 2 seconds.

## Build

```
npm install        OK (84 packages)
npm run build      OK (1028 modules, 10s, no errors)
```

Warnings: a11y label/input associations (non-blocking), Monaco chunk size (expected).

## Verification Results

| Check | Result |
|-------|--------|
| Dark theme default (#0a0a0a) | PASS |
| Theme toggle dark → light | PASS |
| Theme toggle light → dark | PASS |
| Theme persists in localStorage | PASS |
| StatusBar always visible | PASS |
| StatusBar persists across all tabs | PASS |
| StatusBar content: Tor, 12 peers, NGT balance, NAAN state, version | PASS |
| All 8 tabs switch without reload | PASS |
| Monaco Editor loads in IDE tab | PASS |
| ChatPanel visible in IDE (bottom 40%) | PASS |
| JetBrains Mono font loaded | PASS |
| Setup Wizard appears on first launch | PASS |
| Step 5 "Open SynapseNet" disabled until Wallet + Connection OK | PASS |
| GPU Acceleration section in Wizard Step 4 | PASS |
| GPU Acceleration section in Settings | PASS |

## Screenshots

### Setup Wizard

| Screenshot | Description |
|-----------|-------------|
| `09_wizard_step1.png` | Step 1: Create / Restore wallet |
| `10_wizard_step1_wallet.png` | Step 1: Wallet created, seed phrase shown |
| `11_wizard_step2.png` | Step 2: Connection type (Clearnet / Tor / Tor+Bridges) |
| `12_wizard_step3.png` | Step 3: AI model (Download / Local / Skip) |
| `13_wizard_step4_cpu.png` | Step 4: Resources (CPU, RAM, Disk) |
| `13_wizard_step4_gpu.png` | Step 4: GPU Acceleration enabled (device, layers) |
| `14_wizard_step5.png` | Step 5: Ready checklist |

### Main Interface (Dark Theme)

| Screenshot | Description |
|-----------|-------------|
| `01_dashboard.png` | Dashboard: balance, peers, connection, model, quick actions |
| `02_wallet.png` | Wallet: address, QR, seed, export/import |
| `03_transfers.png` | Transfers: send form, history, filters |
| `04_knowledge.png` | Knowledge: submit, search, submissions, PoE stats |
| `05_naan_agent.png` | NAAN Agent: status, score, task, queue, config, observatory |
| `06_ide.png` | IDE: Monaco Editor + AI Chat Panel |
| `07_network.png` | Network: peer table, Tor status, bandwidth, peer map |
| `08_settings.png` | Settings: connection, model, resources, GPU, NAAN, startup |

### Light Theme

| Screenshot | Description |
|-----------|-------------|
| `15_light_theme.png` | Settings page in light theme (#f5f5f5) with GPU section |
