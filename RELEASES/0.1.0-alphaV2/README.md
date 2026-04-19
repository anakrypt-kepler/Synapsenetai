<h1 align="center">SynapseNet 0.1.0-alphaV2</h1>

<p align="center"><strong>3-Node Devnet Over Tor — Full Mesh Discovery Through Hidden Services</strong></p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-0.1.0--alphaV2-000000?style=for-the-badge&labelColor=000000" alt="Version" />
  <img src="https://img.shields.io/badge/Transport-Tor_Hidden_Services-000000?style=for-the-badge&labelColor=000000" alt="Tor" />
  <img src="https://img.shields.io/badge/Nodes-3%2F3_Connected-000000?style=for-the-badge&labelColor=000000" alt="Nodes" />
</p>

<p align="center">
  <a href="https://github.com/anakrypt"><img src="https://img.shields.io/badge/Kepler-000000?style=for-the-badge&logo=github&logoColor=white" alt="Profile" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai"><img src="https://img.shields.io/badge/Source_Code-000000?style=for-the-badge&logo=github&logoColor=white" alt="Source" /></a>
  <a href="https://github.com/anakrypt/SynapseNet"><img src="https://img.shields.io/badge/Documentation-000000?style=for-the-badge&logo=gitbook&logoColor=white" alt="Docs" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/blob/main/launch_tor_devnet.sh"><img src="https://img.shields.io/badge/Launch_Script-000000?style=for-the-badge&logo=gnubash&logoColor=white" alt="Launch Script" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES/0.1.0-alpha"><img src="https://img.shields.io/badge/←_0.1.0--alpha-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="0.1.0-alpha" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES/0.1.0-alphaV3"><img src="https://img.shields.io/badge/→_0.1.0--alphaV3-000000?style=for-the-badge&logo=torproject&logoColor=white" alt="0.1.0-alphaV3" /></a>
  <a href="https://github.com/anakrypt/Synapsenetai/tree/main/RELEASES"><img src="https://img.shields.io/badge/All_Releases-000000?style=for-the-badge&logo=github&logoColor=white" alt="All Releases" /></a>
</p>

---

> Three SynapseNet nodes built from source, connected exclusively through Tor hidden services. Every peer address is a `.onion`. Transport is `tor-onion`. Zero clearnet. All output below is real — captured from a live test run on March 25, 2026.

---

## What Changed from 0.1.0-alpha

| 0.1.0-alpha | 0.1.0-alphaV2 |
|-------------|---------------|
| 3 nodes on localhost (127.0.0.1) | 3 nodes over Tor hidden services |
| Direct TCP connections | All P2P routed through Tor SOCKS5 |
| Peers see each other's IP | Peers see only `.onion` addresses |
| `--seednode 127.0.0.1:PORT` | `--seednode ONION.onion:PORT` |
| No Tor requirement | `agent.tor.required=true`, clearnet fallback disabled |

---

## Tor Bootstrap

<p align="center">
  <img src="tor_boot.png" width="700" alt="Tor Bootstrap" />
</p>

Three separate Tor instances started, each with its own `HiddenServiceDir`. Onion addresses generated:

```
Node 1: xfno53a6lu6mmrolixjdw6kbjyz7m5h5rtxb3tswvcso6i6age55awad.onion
Node 2: noczlkxzpswg6h75jpgzlscmgxluxjeqglwzrjcenrzoup2jrclac2yd.onion
Node 3: mwv6bmzprktfgac7ge5tqxul3jq7s4kqqan47s7h7zpjvcq52dkhhiqd.onion
```

---

## Node Boot

<p align="center">
  <img src="tor_node_boot.png" width="700" alt="Node 1 Boot Log" />
</p>

Real log output from Node 1. Key lines:

- `Privacy mode enabled: xqberuyoav4nula3zqb5prjb76ba7xd4kfic6cx7qsn3h2qu5fggpjyd.onion`
- `Tor control reachable: yes`
- `Onion service active: yes`
- `Peer connected: mwv6bmzprktfgac7ge5tqxul3jq7s4kqqan47s7h7zpjvcq52dkhhiqd.onion:8336`
- `Peer connected: noczlkxzpswg6h75jpgzlscmgxluxjeqglwzrjcenrzoup2jrclac2yd.onion:8334`

Each node creates its own onion service via Tor's control port (`ADD_ONION`), then connects outbound to the other two nodes' `.onion` addresses through SOCKS5.

---

## Peers — Full Mesh Over Tor

<p align="center">
  <img src="tor_peers.png" width="700" alt="All 3 Nodes Connected" />
</p>

All three nodes found each other exclusively through `.onion` addresses:

- Node 1 → Node 2 (`.onion:8334`) + Node 3 (`.onion:8336`)
- Node 2 → Node 1 (`.onion:8333`) + Node 3 (`.onion:8336`)
- Node 3 → Node 1 (`.onion:8333`) + Node 2 (`.onion:8334`)

Full mesh. Six `.onion` peer connections total across the network.

---

## RPC Peer Info

<p align="center">
  <img src="tor_rpc_peers.png" width="700" alt="RPC Peer Info" />
</p>

RPC output from Node 1 showing 4 connected peers — 2 outbound, 2 inbound. Every peer shows:

- `transport=tor-onion`
- `state=CONNECTED`
- `.onion` display addresses

---

## Tor Status

<p align="center">
  <img src="tor_status.png" width="700" alt="Tor Status All Nodes" />
</p>

All three nodes confirm:

- `Privacy mode enabled` with unique `.onion` addresses
- `Tor control reachable: yes`
- `Onion service active: yes`

---

## How to Run

```bash
# 1. Install Tor
sudo apt-get install tor    # Ubuntu/Debian
brew install tor            # macOS

# 2. Build SynapseNet
git clone https://github.com/anakrypt/Synapsenetai.git
cd Synapsenetai
cmake -S KeplerSynapseNet -B KeplerSynapseNet/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DUSE_LLAMA_CPP=ON -DUSE_SECP256K1=ON
cmake --build KeplerSynapseNet/build --parallel $(nproc)

# 3. Launch 3-node Tor devnet
chmod +x launch_tor_devnet.sh
./launch_tor_devnet.sh
```

<p align="center">
  <a href="https://github.com/anakrypt/Synapsenetai/blob/main/launch_tor_devnet.sh"><img src="https://img.shields.io/badge/View_Launch_Script-000000?style=for-the-badge&logo=gnubash&logoColor=white" alt="View Script" /></a>
</p>

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
