#include "cli/cli_parser.h"

#include <getopt.h>
#include <iostream>
#include <string>

namespace synapse {

void printHelp(const char* progName) {
    std::cout << "SynapseNet v0.1.0 - Decentralized Knowledge Network\n\n";
    std::cout << "Usage: " << progName << " [command] [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  (none)              Start node with TUI\n";
    std::cout << "  status              Show node status\n";
    std::cout << "  peers               List connected peers\n";
    std::cout << "  submit <file>       Contribute knowledge\n";
    std::cout << "  send <addr> <amt>   Transfer NGT\n";
    std::cout << "  query <text>        Search knowledge network\n";
    std::cout << "  balance             Show wallet balance\n";
    std::cout << "  address             Show wallet address\n";
    std::cout << "  naan                Show NAAN runtime/observatory data\n";
    std::cout << "  logs                Show recent activity\n";
    std::cout << "  seeds               Show bootstrap/DNS seeds\n";
    std::cout << "  discovery           Show discovery diagnostics\n";
    std::cout << "\nNGT policy:\n";
    std::cout << "  NGT cannot be purchased in-protocol.\n";
    std::cout << "  NGT is earned by protocol outcomes or transferred between addresses.\n";
    std::cout << "\nOptions:\n";
    std::cout << "  -h, --help          Show this help\n";
    std::cout << "  -v, --version       Show version\n";
    std::cout << "  -d, --daemon        Run as daemon (no TUI)\n";
    std::cout << "  -c, --config FILE   Use custom config file\n";
    std::cout << "  -D, --datadir DIR   Data directory\n";
    std::cout << "  -p, --port PORT     P2P port (default: 8333)\n";
    std::cout << "  -r, --rpcport PORT  RPC port (default: 8332)\n";
    std::cout << "  --testnet           Connect to testnet\n";
    std::cout << "  --regtest           Run in regression test mode\n";
    std::cout << "  --privacy           Enable privacy mode (Tor)\n";
    std::cout << "  --amnesia           RAM-only mode, zero traces\n";
    std::cout << "  --dev               Developer mode (fast PoE params)\n";
    std::cout << "  --reset-ngt         Clear all NGT balances (transfer DB)\n";
    std::cout << "  --poe-validators X  Comma-separated validator pubkeys (hex)\n";
    std::cout << "  --poe-validator-mode MODE  Validator mode: static|stake (default: static)\n";
    std::cout << "  --poe-min-stake NGT         Minimum stake for stake-mode validators (default: 0)\n";
    std::cout << "  --quantum           Enable quantum security\n";
    std::cout << "  --security LEVEL    Security level (standard/high/paranoid/quantum-ready)\n";
    std::cout << "  --connect HOST:PORT Connect to specific node\n";
    std::cout << "  --addnode HOST:PORT Add node to connection list\n";
    std::cout << "  --seednode HOST:PORT Add seed node\n";
    std::cout << "  --maxpeers N        Maximum peer connections\n";
    std::cout << "  --dbcache N         Database cache size in MB\n";
    std::cout << "  --loglevel LEVEL    Log level (debug/info/warn/error)\n";
}

void printVersion() {
    std::cout << "SynapseNet v0.1.0-beta\n";
    std::cout << "Protocol version: 1\n";
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << "\n";
    std::cout << "Crypto: Built-in implementation\n";
}

bool parseArgs(int argc, char* argv[], NodeConfig& config) {
    static struct option longOptions[] = {
        {"help", no_argument, nullptr, 'h'},
        {"version", no_argument, nullptr, 'v'},
        {"daemon", no_argument, nullptr, 'd'},
        {"config", required_argument, nullptr, 'c'},
        {"datadir", required_argument, nullptr, 'D'},
        {"port", required_argument, nullptr, 'p'},
        {"rpcport", required_argument, nullptr, 'r'},
        {"testnet", no_argument, nullptr, 't'},
        {"regtest", no_argument, nullptr, 'R'},
        {"privacy", no_argument, nullptr, 'P'},
        {"amnesia", no_argument, nullptr, 'A'},
        {"dev", no_argument, nullptr, 'E'},
        {"reset-ngt", no_argument, nullptr, 'Z'},
        {"poe-validators", required_argument, nullptr, 'V'},
        {"poe-validator-mode", required_argument, nullptr, 'M'},
        {"poe-min-stake", required_argument, nullptr, 'T'},
        {"quantum", no_argument, nullptr, 'Q'},
        {"security", required_argument, nullptr, 'S'},
        {"connect", required_argument, nullptr, 'C'},
        {"addnode", required_argument, nullptr, 'N'},
        {"seednode", required_argument, nullptr, 's'},
        {"maxpeers", required_argument, nullptr, 'm'},
        {"dbcache", required_argument, nullptr, 'b'},
        {"loglevel", required_argument, nullptr, 'l'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    int optionIndex = 0;

    while ((opt = getopt_long(argc, argv, "+hvdc:D:p:r:tRPAEZV:M:T:QS:C:N:s:m:b:l:",
                              longOptions, &optionIndex)) != -1) {
        switch (opt) {
            case 'h':
                config.showHelp = true;
                return true;
            case 'v':
                config.showVersion = true;
                return true;
            case 'd':
                config.daemon = true;
                config.tui = false;
                break;
            case 'c':
                config.configPath = optarg;
                break;
            case 'D':
                config.dataDir = optarg;
                break;
            case 'p':
                config.port = static_cast<uint16_t>(std::stoi(optarg));
                break;
            case 'r':
                config.rpcPort = static_cast<uint16_t>(std::stoi(optarg));
                break;
            case 't':
                config.testnet = true;
                config.networkType = "testnet";
                break;
            case 'R':
                config.regtest = true;
                config.networkType = "regtest";
                config.discovery = false;
                break;
            case 'P':
                config.privacyMode = true;
                break;
            case 'A':
                config.amnesia = true;
                break;
            case 'E':
                config.dev = true;
                config.networkType = "dev";
                break;
            case 'Z':
                config.resetNgt = true;
                break;
            case 'V':
                config.poeValidators = optarg;
                break;
            case 'M':
                config.poeValidatorMode = optarg;
                break;
            case 'T':
                config.poeMinStake = optarg;
                break;
            case 'Q':
                config.quantumSecurity = true;
                config.quantumSecuritySetByCli = true;
                break;
            case 'S':
                config.securityLevel = optarg;
                config.securityLevelSetByCli = true;
                break;
            case 'C':
                config.connectNodes.push_back(optarg);
                break;
            case 'N':
                config.addNodes.push_back(optarg);
                break;
            case 's':
                config.seedNodes.push_back(optarg);
                break;
            case 'm':
                config.maxPeers = static_cast<uint32_t>(std::stoi(optarg));
                break;
            case 'b':
                config.dbCacheSize = static_cast<uint32_t>(std::stoi(optarg));
                break;
            case 'l':
                config.logLevel = optarg;
                break;
            default:
                return false;
        }
    }

    if (optind < argc) {
        std::string command = argv[optind];
        if (command == "poe" || command == "status" || command == "peers" ||
            command == "balance" || command == "address" || command == "logs" ||
            command == "model" || command == "ai" || command == "tor") {
            config.cli = true;
            config.tui = false;
            config.daemon = false;
            config.commandArgs.clear();
            for (int i = optind; i < argc; ++i) {
                config.commandArgs.emplace_back(argv[i]);
            }
            return true;
        }
    }

    return true;
}

} // namespace synapse
