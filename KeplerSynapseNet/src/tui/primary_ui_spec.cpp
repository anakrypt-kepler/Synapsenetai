#include "tui/primary_ui_spec.h"

#include <algorithm>
#include <cctype>
#include <array>

namespace synapse::tui::primary_ui {

namespace {

std::string truncLine(const std::string& in, int width) {
    if (width <= 0) return {};
    if (static_cast<int>(in.size()) <= width) return in;
    if (width <= 3) return in.substr(0, static_cast<size_t>(width));
    return in.substr(0, static_cast<size_t>(width - 3)) + "...";
}

std::string uppercase(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return v;
}

std::string lowercase(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return v;
}

std::string capitalizeWords(std::string value) {
    bool upperNext = true;
    for (char& c : value) {
        if (c == '_' || c == '-' || c == ' ') {
            c = ' ';
            upperNext = true;
            continue;
        }
        if (upperNext) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            upperNext = false;
        } else {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return value;
}

bool looksLikeSecret(const std::string& value) {
    const std::string l = lowercase(value);
    static const std::array<const char*, 11> kSecretMarkers = {
        "api_key",
        "apikey",
        "token",
        "authorization",
        "bearer",
        "private key",
        "mnemonic",
        "seed phrase",
        "password",
        "secret",
        "wallet_key"
    };
    for (const char* marker : kSecretMarkers) {
        if (l.find(marker) != std::string::npos) return true;
    }
    return false;
}

MiningPage cyclePage(MiningPage current, int delta) {
    int idx = static_cast<int>(current);
    idx += delta;
    if (idx < 0) idx = 8;
    if (idx > 8) idx = 0;
    return static_cast<MiningPage>(idx);
}

} 

DashboardRoute routeDashboardKey(int ch) {
    if (ch == 'a' || ch == 'A') return DashboardRoute::AI_MINING;
    if (ch == '3') return DashboardRoute::AI_QUERY;
    if (ch == 'q' || ch == 'Q') return DashboardRoute::QUIT;
    return DashboardRoute::NONE;
}

MiningPage routeMiningPageKey(MiningPage current, int ch, bool* handled) {
    bool localHandled = true;
    MiningPage out = current;

    switch (ch) {
        case '1':
            out = MiningPage::OVERVIEW;
            break;
        case '2':
            out = MiningPage::NETWORK_MINING;
            break;
        case '3':
            out = MiningPage::WALLET_EARNINGS;
            break;
        case '4':
            out = MiningPage::CONTRIBUTIONS;
            break;
        case '5':
            out = MiningPage::SECURITY_POLICY;
            break;
        case '6':
            out = MiningPage::LOGS_DIAGNOSTICS;
            break;
        case '7':
            out = MiningPage::AGENT_NETWORK;
            break;
        case '8':
            out = MiningPage::IDE_CODE;
            break;
        case '9':
            out = MiningPage::STORAGE_RECOVERY;
            break;
        case 'h':
        case 'H':
            out = cyclePage(current, -1);
            break;
        case 'l':
        case 'L':
            out = cyclePage(current, 1);
            break;
        case ']':
            out = cyclePage(current, 1);
            break;
        case '[':
            out = cyclePage(current, -1);
            break;
        default:
            localHandled = false;
            break;
    }

    if (handled) *handled = localHandled;
    return out;
}

std::string renderChatStatusStrip(const ChatStatusStripInput& input, int width) {
    std::string tor;
    if (input.torRequired) {
        if (input.torReachable) tor = "Tor:CONNECTED";
        else if (input.torDegraded) tor = "Tor:OFFLINE";
        else tor = "Tor:BOOTSTRAP";
    } else {
        tor = input.torReachable ? "Tor:OPTIONAL" : "Tor:DISABLED";
    }

    std::string onion;
    if (input.torRequired && !input.torReachable) {
        onion = "Onion:DEGRADED";
    } else {
        onion = input.onionServiceActive ? "Onion:ONLINE" : "Onion:DISABLED";
    }

    const std::string clearnet = input.clearnetFallbackAllowed ? "Clearnet:OPT-IN" : "Clearnet:BLOCKED";
    std::string activity = input.agentActivity.empty() ? "IDLE" : input.agentActivity;
    if (looksLikeSecret(activity)) activity = "REDACTED";
    else activity = uppercase(activity);

    const std::string line = tor + " | " + onion + " | " + clearnet + " | Agent:" + activity;
    return truncLine(line, width);
}

std::vector<std::string> renderMiningOverviewSnapshot(const AttachedAgentStatusInfo& info, int width) {
    std::vector<std::string> lines;
    lines.reserve(16);

    lines.push_back(truncLine("AI MINING (NAAN) OVERVIEW [ALPHA DEVNET]", width));
    lines.push_back(truncLine("Runtime: " + info.runtimeState + " init=" + (info.runtimeInitialized ? "yes" : "no"), width));
    lines.push_back(truncLine("Score: " + std::to_string(info.agentScore) + " band=" + info.agentScoreBand, width));
    lines.push_back(truncLine("Scheduler: " + info.schedulerState + " tick=" + std::to_string(info.schedulerTick) + " epoch=" + std::to_string(info.schedulerEpochIndex), width));
    lines.push_back(truncLine("Budget cpu/ram/net: " + std::to_string(info.schedulerBudgetCpu) + "/" + std::to_string(info.schedulerBudgetRam) + "/" + std::to_string(info.schedulerBudgetNetwork), width));
    lines.push_back(truncLine("Route: " + info.routeMode + " tor_required=" + (info.torRequired ? "yes" : "no") + " tor_reachable=" + (info.torReachable ? "yes" : "no"), width));
    lines.push_back(truncLine("Network health overload=" + std::string(info.networkOverloadMode ? "yes" : "no") +
        " peer/in/out=" + std::to_string(info.networkPeerPressurePercent) + "/" +
        std::to_string(info.networkInboundPressurePercent) + "/" +
        std::to_string(info.networkOutboundPressurePercent), width));
    lines.push_back(truncLine("Network drops inv/getdata: " +
        std::to_string(info.networkInvBackpressureDrops) + "/" +
        std::to_string(info.networkGetDataBackpressureDrops), width));
    lines.push_back(truncLine("Network gossip suppr/subset: " +
        std::to_string(info.networkGossipSuppressed) + "/" +
        std::to_string(info.networkGossipSubsetRouted), width));
    lines.push_back(truncLine("Hashing: " + std::string(info.miningActive ? "active" : "idle") + " target=" + info.miningWorkTarget, width));
    lines.push_back(truncLine("Hash attempts last/total: " + std::to_string(info.miningHashAttemptsLast) + "/" + std::to_string(info.miningHashAttemptsTotal), width));
    lines.push_back(truncLine("Hash candidate: " + info.miningCandidateHash, width));
    lines.push_back(truncLine("Quarantine: " + std::string(info.quarantined ? "yes" : "no") + " reason=" + info.quarantineReason, width));
    lines.push_back(truncLine("Config path: " + info.configPath, width));
    lines.push_back(truncLine("Web config: " + info.webConfigPath, width));
    lines.push_back(truncLine("Storage dir: " + info.dataDir, width));

    return lines;
}

std::vector<std::string> renderSecurityStatusSnapshot(const StatusInfo& info, int width) {
    std::vector<std::string> lines;
    lines.reserve(7);

    const std::string capabilityMode =
        info.quantumCapabilityMode.empty() ? "Unknown" : capitalizeWords(info.quantumCapabilityMode);
    const std::string backend = info.liboqsEnabled ? "liboqs compiled" : "simulation only";

    lines.push_back(truncLine("PQC configured: " + std::string(info.quantumEnabled ? "enabled" : "disabled"), width));
    lines.push_back(truncLine("Capability mode: " + capabilityMode, width));
    lines.push_back(truncLine("Backend availability: " + backend, width));
    lines.push_back(truncLine(
        "Kyber ML-KEM-768: " + std::string(info.kyberReal ? "real" : "simulated") +
        " (" + (info.kyberImplementation.empty() ? std::string("unknown") : info.kyberImplementation) + ")",
        width));
    lines.push_back(truncLine(
        "Dilithium ML-DSA-65: " + std::string(info.dilithiumReal ? "real" : "simulated") +
        " (" + (info.dilithiumImplementation.empty() ? std::string("unknown") : info.dilithiumImplementation) + ")",
        width));
    lines.push_back(truncLine(
        "SPHINCS+ SLH-DSA-128s: " + std::string(info.sphincsReal ? "real" : "simulated") +
        " (" + (info.sphincsImplementation.empty() ? std::string("unknown") : info.sphincsImplementation) + ")",
        width));

    if (!info.quantumEnabled) {
        lines.push_back(truncLine("Operator note: PQC is disabled in config.", width));
    } else if (info.kyberReal && info.dilithiumReal && info.sphincsReal) {
        lines.push_back(truncLine("Operator note: all PQC backends are real.", width));
    } else if (info.kyberReal || info.dilithiumReal || info.sphincsReal) {
        lines.push_back(truncLine("Operator note: this node is running mixed real and simulated PQC.", width));
    } else {
        lines.push_back(truncLine("Operator note: this node is running simulated PQC only.", width));
    }

    return lines;
}

}
