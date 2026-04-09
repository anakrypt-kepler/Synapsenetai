<h1 align="center">SynapseNet 0.1.0-alphaV3</h1>

<p align="center"><strong>Hybrid Mesh — Tor Nodes and Clearnet Nodes See Each Other</strong></p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.1.0--alphaV3-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <img src="https://img.shields.io/badge/Transport-Hybrid_Tor_%2B_Clearnet-000000?style=for-the-badge&labelColor=000000" alt="Hybrid" />
  <img src="https://img.shields.io/badge/Tests-267%2F267_passed-000000?style=for-the-badge&labelColor=000000" alt="Tests" />
</p>

<p align="center">
  <a href="https://github.com/anakrypt"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" /></a>
  <a href="https://github.com/anakrypt/SynapseNet"><img src="https://img.shields.io/badge/Documentation-000000?style=for-the-badge&logo=gitbook&logoColor=white" alt="Docs" /></a>
  <a href="https://github.com/anakrypt/SynapseNet/blob/main/SynapseNet_Whitepaper.pdf"><img src="https://img.shields.io/badge/Whitepaper-000000?style=for-the-badge&logo=adobeacrobatreader&logoColor=white" alt="Whitepaper" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES/0.1.0-alpha"><img src="https://img.shields.io/badge/←_0.1.0--alpha-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="0.1.0-alpha" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV2"><img src="https://img.shields.io/badge/←_0.1.0--alphaV2-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="V2" /></a>
  <a href="ARCHITECTURE.md"><img src="https://img.shields.io/badge/Architecture-000000?style=for-the-badge&logo=blueprint&logoColor=white" alt="Architecture" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES"><img src="https://img.shields.io/badge/All_Releases-000000?style=for-the-badge&logo=github&logoColor=white" alt="All Releases" /></a>
</p>

---

> Tor nodes and clearnet nodes in the same network, seeing each other. A clearnet node connects to a Tor node via `.onion` through SOCKS5 and simultaneously to other clearnet nodes via direct TCP. A Tor node accepts inbound connections from both. NAAN agent follows the same routing as the node. All output below is real — captured from a live test run on March 25, 2026.

---

## Hybrid Network Topology

```
SynapseNet Hybrid Mesh

  Node 1 (Tor)                    Node 2 (Clearnet)
    .onion service  <-- Tor SOCKS -->  direct TCP
    port 8333                          port 8334
    SOCKS5: 9050                       hybridMode: on
        |                                  |
        |  Tor circuit (inbound)           |  direct TCP
        |  + direct TCP (hybrid)           |
        |                                  |
        v                                  v
                  Node 3 (Clearnet)
                    direct TCP to Node 2
                    Tor SOCKS5 to Node 1 .onion
                    port 8336
                    hybridMode: on
```

---

## Connection Routing

```
                  connect()
                     |
                Is address .onion?
                  /        \
                YES         NO
                 |           |
             SOCKS5      Direct TCP
             via Tor     connection
                 |           |
                 |       (if SOCKS5 available
                 |        and hybridMode=true,
                 |        can also try SOCKS5
                 |        as fallback)
                 |           |
                  \         /
                  CONNECTED
                  transport=tor-onion
                  or clearnet
```

---

## Peer Exchange Flow

```
Node 1 (Tor)              Node 2 (Clearnet)          Node 3 (Clearnet)
.onion:8333               IP:8334                    IP:8336
    |                         |                          |
    |<-- getpeers ------------|                          |
    |                         |                          |
    |--- peers -------------->|                          |
    |    [.onion:8336,        |                          |
    |     IP:8336]            |                          |
    |                         |                          |
    |                         |--- getpeers ------------>|
    |                         |                          |
    |                         |<-- peers ----------------|
    |                         |    [.onion:8333,         |
    |                         |     IP:8334]             |
    |                         |                          |
    |<-- getpeers ----------------------------------------|
    |                                                    |
    |--- peers ----------------------------------------->|
    |    [IP:8334,                                       |
    |     .onion:8334]                                   |
    |                         |                          |
    v                         v                          v
Full mesh                 Full mesh                  Full mesh
via peer exchange         via peer exchange          via peer exchange
```

---

## What Changed from V2

| V2 (Tor-only) | V3 (Hybrid) |
|-------------|------------|
| Tor-only or clearnet-only | Both in the same network |
| Clearnet nodes cannot reach `.onion` | Clearnet nodes connect to `.onion` via SOCKS5 |
| Tor nodes only connect through SOCKS5 | Tor nodes accept direct inbound from clearnet |
| Separate networks | One unified mesh |

---

## Code Changes

Three files changed:

**`include/network/network.h`** — added `hybridMode` flag to `NetworkConfig`

**`src/network/network.cpp`** — rewrote `connect()`:
- Detects `.onion` addresses and routes them through SOCKS5
- Non-`.onion` addresses use direct TCP
- If SOCKS5 fails for non-`.onion`, falls back to direct

**`src/node/node_runtime.cpp`** — network loop now enables `hybridMode` automatically when SOCKS proxy is detected, regardless of Tor-required mode

267/267 tests pass.

---

## Node 1 Boot (Tor)

<p align="center">
  <img src="hybrid_node1_boot.png" width="700" alt="Node 1 Tor Boot" />
</p>

Node 1 starts with `agent.tor.required=true`, creates an onion service, then connects to both clearnet peers through `hybridMode`.

---

## Node 2 Boot (Clearnet)

<p align="center">
  <img src="hybrid_node2_boot.png" width="700" alt="Node 2 Clearnet Boot" />
</p>

Node 2 starts with `agent.tor.required=false`. Connects to Node 3 via direct TCP and to Node 1 via `.onion` through SOCKS5.

---

## Peer Connections

<p align="center">
  <img src="hybrid_peers.png" width="700" alt="Hybrid Peer Connections" />
</p>

All three nodes connected — Node 1 (Tor) sees clearnet peers, Nodes 2 and 3 (clearnet) see each other directly and see Node 1 through `.onion`.

---

## RPC Transport Summary

<p align="center">
  <img src="hybrid_rpc.png" width="700" alt="RPC Hybrid Transport" />
</p>

RPC output showing mixed transport:

- **Node 1 (Tor):** 4 peers, `transport=clearnet` (accepted via hidden service + hybrid outbound)
- **Node 2 (clearnet):** 2 `transport=clearnet` + 2 `transport=tor-onion`
- **Node 3 (clearnet):** 2 `transport=clearnet` + 2 `transport=tor-onion`

---

## NAAN Agent Routing

```
User starts node
       |
  Tor required?
    /       \
  YES        NO
   |          |
NAAN agent   NAAN agent
routes all   routes all
through Tor  via clearnet
SOCKS5       (+ .onion via SOCKS5
              if Tor is available)
.onion
search       clearnet
engines:     search:
Ahmia,Torch  DuckDuckGo
DarkSearch
```

The NAAN agent follows whatever routing the user selected at startup. If the user chose Tor, the agent's web research, knowledge gathering, and submission pipeline all go through Tor. If clearnet, everything goes direct. In hybrid mode, the agent can reach both `.onion` and clearnet sources.

---

<p align="center">
  <a href="https://github.com/anakrypt"><img src="https://img.shields.io/badge/Built_by_Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Kepler" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source Code" /></a>
</p>

<p align="center">
  If you find this project worth watching — even if you can't contribute code — you can help keep it going.<br>
  Donations go directly toward VPS hosting for seed nodes, build infrastructure, and development time.
</p>

<p align="center">
  <a href="https://www.blockchain.com/btc/address/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4"><img src="https://img.shields.io/badge/bc1q5pkemq7q84ld4rf5kwtafp7jfl9dlf3pc4z9d4-000000?style=for-the-badge&logo=bitcoin&logoColor=white" alt="BTC" /></a>
</p>
