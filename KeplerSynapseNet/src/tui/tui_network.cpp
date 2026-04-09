#include "tui/tui.h"
#include <ncurses.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>

namespace synapse {
namespace tui {

struct PeerInfo {
    std::string peerId;
    std::string address;
    uint16_t port;
    std::string version;
    uint64_t connectedAt;
    uint64_t lastSeen;
    uint64_t bytesSent;
    uint64_t bytesReceived;
    uint64_t messagesSent;
    uint64_t messagesReceived;
    uint32_t latencyMs;
    uint64_t blockHeight;
    std::string status;
    bool isInbound;
    bool isPqcEnabled;
    bool isTrusted;
    bool isBanned;
};

struct NetworkStats {
    uint64_t totalPeers;
    uint64_t inboundPeers;
    uint64_t outboundPeers;
    uint64_t totalBytesSent;
    uint64_t totalBytesReceived;
    uint64_t totalMessages;
    uint64_t avgLatencyMs;
    uint64_t uptime;
    uint64_t blocksRelayed;
    uint64_t transactionsRelayed;
    uint64_t bannedPeers;
    std::string networkId;
    std::string nodeVersion;
    bool isListening;
    uint16_t listenPort;
};

struct BandwidthSample {
    uint64_t timestamp;
    uint64_t bytesIn;
    uint64_t bytesOut;
};

struct NetworkScreenState {
    std::vector<PeerInfo> peers;
    std::vector<BandwidthSample> bandwidthHistory;
    NetworkStats stats;
    int selectedPeer;
    int scrollOffset;
    std::string currentView;
    std::string filterQuery;
    bool showInbound;
    bool showOutbound;
    bool showBanned;
};

static NetworkScreenState networkState;

static void initNetworkState() {
    networkState.peers.clear();
    networkState.bandwidthHistory.clear();
    networkState.stats = NetworkStats{};
    networkState.stats.networkId = "synapsenet-mainnet";
    networkState.stats.nodeVersion = "0.1.0";
    networkState.stats.isListening = false;
    networkState.stats.listenPort = 8333;
    networkState.selectedPeer = 0;
    networkState.scrollOffset = 0;
    networkState.currentView = "overview";
    networkState.filterQuery = "";
    networkState.showInbound = true;
    networkState.showOutbound = true;
    networkState.showBanned = false;
}

static std::string formatBytes(uint64_t bytes) {
    std::stringstream ss;
    if (bytes >= 1024ULL * 1024 * 1024 * 1024) {
        ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024 * 1024 * 1024)) << " TB";
    } else if (bytes >= 1024ULL * 1024 * 1024) {
        ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024 * 1024)) << " GB";
    } else if (bytes >= 1024 * 1024) {
        ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024)) << " MB";
    } else if (bytes >= 1024) {
        ss << std::fixed << std::setprecision(2) << (bytes / 1024.0) << " KB";
    } else {
        ss << bytes << " B";
    }
    return ss.str();
}

static std::string formatDuration(uint64_t seconds) {
    std::stringstream ss;
    if (seconds >= 86400) {
        ss << (seconds / 86400) << "d ";
        seconds %= 86400;
    }
    if (seconds >= 3600) {
        ss << (seconds / 3600) << "h ";
        seconds %= 3600;
    }
    if (seconds >= 60) {
        ss << (seconds / 60) << "m ";
        seconds %= 60;
    }
    ss << seconds << "s";
    return ss.str();
}

static std::string formatTimestamp(uint64_t timestamp) {
    time_t t = static_cast<time_t>(timestamp);
    struct tm* tm = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buf);
}

static std::string shortenPeerId(const std::string& peerId) {
    if (peerId.length() <= 16) return peerId;
    return peerId.substr(0, 8) + "..." + peerId.substr(peerId.length() - 8);
}

static void drawNetworkHeader(WINDOW* win, int width) {
    wattron(win, A_BOLD | COLOR_PAIR(2));
    mvwprintw(win, 1, 2, "SYNAPSENET NETWORK");
    wattroff(win, A_BOLD | COLOR_PAIR(2));
    
    mvwhline(win, 2, 1, ACS_HLINE, width - 2);
    
    std::stringstream ss;
    ss << "Network: " << networkState.stats.networkId;
    ss << " | Version: " << networkState.stats.nodeVersion;
    ss << " | Peers: " << networkState.stats.totalPeers;
    
    if (networkState.stats.isListening) {
        ss << " | Listening: " << networkState.stats.listenPort;
    } else {
        ss << " | [NOT LISTENING]";
    }
    
    mvwprintw(win, 3, 2, "%s", ss.str().c_str());
}

static void drawNetworkStats(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "NETWORK STATISTICS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    int col1 = 2;
    int col2 = width / 2;
    
    mvwprintw(win, startY + 2, col1, "Total Peers:      %lu", networkState.stats.totalPeers);
    mvwprintw(win, startY + 2, col2, "Inbound:          %lu", networkState.stats.inboundPeers);
    
    mvwprintw(win, startY + 3, col1, "Outbound:         %lu", networkState.stats.outboundPeers);
    mvwprintw(win, startY + 3, col2, "Banned:           %lu", networkState.stats.bannedPeers);
    
    mvwprintw(win, startY + 4, col1, "Bytes Sent:       %s", formatBytes(networkState.stats.totalBytesSent).c_str());
    mvwprintw(win, startY + 4, col2, "Bytes Received:   %s", formatBytes(networkState.stats.totalBytesReceived).c_str());
    
    mvwprintw(win, startY + 5, col1, "Messages:         %lu", networkState.stats.totalMessages);
    mvwprintw(win, startY + 5, col2, "Avg Latency:      %lu ms", networkState.stats.avgLatencyMs);
    
    mvwprintw(win, startY + 6, col1, "Blocks Relayed:   %lu", networkState.stats.blocksRelayed);
    mvwprintw(win, startY + 6, col2, "TXs Relayed:      %lu", networkState.stats.transactionsRelayed);
    
    mvwprintw(win, startY + 7, col1, "Uptime:           %s", formatDuration(networkState.stats.uptime).c_str());
}

static void drawPeerList(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "CONNECTED PEERS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 2, 2, "%-20s %-22s %-8s %-10s %-12s %-12s %-8s", 
              "PEER ID", "ADDRESS", "VERSION", "LATENCY", "SENT", "RECV", "STATUS");
    wattroff(win, A_DIM);
    
    mvwhline(win, startY + 3, 1, ACS_HLINE, width - 2);
    
    int y = startY + 4;
    int maxVisible = height - startY - 6;
    
    std::vector<PeerInfo> filteredPeers;
    for (const auto& peer : networkState.peers) {
        if (!networkState.showInbound && peer.isInbound) continue;
        if (!networkState.showOutbound && !peer.isInbound) continue;
        if (!networkState.showBanned && peer.isBanned) continue;
        if (!networkState.filterQuery.empty()) {
            if (peer.peerId.find(networkState.filterQuery) == std::string::npos &&
                peer.address.find(networkState.filterQuery) == std::string::npos) {
                continue;
            }
        }
        filteredPeers.push_back(peer);
    }
    
    for (size_t i = networkState.scrollOffset; 
         i < filteredPeers.size() && y < startY + maxVisible; 
         i++, y++) {
        const PeerInfo& peer = filteredPeers[i];
        
        if (static_cast<int>(i) == networkState.selectedPeer) {
            wattron(win, A_REVERSE);
        }
        
        int statusColor = 7;
        if (peer.status == "connected") statusColor = 2;
        else if (peer.status == "connecting") statusColor = 3;
        else if (peer.status == "disconnected") statusColor = 1;
        else if (peer.isBanned) statusColor = 1;
        
        std::string direction = peer.isInbound ? "<-" : "->";
        std::string addrStr = peer.address + ":" + std::to_string(peer.port);
        
        mvwprintw(win, y, 2, "%-20s", shortenPeerId(peer.peerId).c_str());
        mvwprintw(win, y, 22, "%s %-19s", direction.c_str(), addrStr.c_str());
        mvwprintw(win, y, 44, "%-8s", peer.version.c_str());
        mvwprintw(win, y, 52, "%4u ms", peer.latencyMs);
        mvwprintw(win, y, 62, "%-12s", formatBytes(peer.bytesSent).c_str());
        mvwprintw(win, y, 74, "%-12s", formatBytes(peer.bytesReceived).c_str());
        
        wattron(win, COLOR_PAIR(statusColor));
        mvwprintw(win, y, 86, "%-8s", peer.status.c_str());
        wattroff(win, COLOR_PAIR(statusColor));
        
        if (peer.isPqcEnabled) {
            wattron(win, COLOR_PAIR(6));
            mvwprintw(win, y, 94, "[PQC]");
            wattroff(win, COLOR_PAIR(6));
        }
        
        if (static_cast<int>(i) == networkState.selectedPeer) {
            wattroff(win, A_REVERSE);
        }
    }
    
    if (filteredPeers.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, startY + 4, 2, "[GENESIS] No peers connected");
        wattroff(win, A_DIM);
    }
}

static void drawPeerDetails(WINDOW* win, int startY, int width) {
    if (networkState.peers.empty()) return;
    if (networkState.selectedPeer >= static_cast<int>(networkState.peers.size())) return;
    
    const PeerInfo& peer = networkState.peers[networkState.selectedPeer];
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "PEER DETAILS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    int col1 = 2;
    int col2 = width / 2;
    
    mvwprintw(win, startY + 2, col1, "Peer ID:       %s", peer.peerId.c_str());
    mvwprintw(win, startY + 3, col1, "Address:       %s:%u", peer.address.c_str(), peer.port);
    mvwprintw(win, startY + 4, col1, "Version:       %s", peer.version.c_str());
    mvwprintw(win, startY + 5, col1, "Direction:     %s", peer.isInbound ? "Inbound" : "Outbound");
    
    mvwprintw(win, startY + 2, col2, "Connected:     %s", formatTimestamp(peer.connectedAt).c_str());
    mvwprintw(win, startY + 3, col2, "Last Seen:     %s", formatTimestamp(peer.lastSeen).c_str());
    mvwprintw(win, startY + 4, col2, "Block Height:  %lu", peer.blockHeight);
    mvwprintw(win, startY + 5, col2, "Latency:       %u ms", peer.latencyMs);
    
    mvwhline(win, startY + 6, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 7, col1, "Bytes Sent:    %s", formatBytes(peer.bytesSent).c_str());
    mvwprintw(win, startY + 7, col2, "Bytes Recv:    %s", formatBytes(peer.bytesReceived).c_str());
    mvwprintw(win, startY + 8, col1, "Msgs Sent:     %lu", peer.messagesSent);
    mvwprintw(win, startY + 8, col2, "Msgs Recv:     %lu", peer.messagesReceived);
    
    mvwhline(win, startY + 9, 1, ACS_HLINE, width - 2);
    
    std::string flags;
    if (peer.isPqcEnabled) flags += "[PQC] ";
    if (peer.isTrusted) flags += "[TRUSTED] ";
    if (peer.isBanned) flags += "[BANNED] ";
    
    if (!flags.empty()) {
        mvwprintw(win, startY + 10, col1, "Flags:         %s", flags.c_str());
    }
}

static void drawBandwidthGraph(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "BANDWIDTH (last 60 seconds)");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    int graphHeight = height - startY - 4;
    int graphWidth = width - 10;
    
    if (networkState.bandwidthHistory.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, startY + 2, 2, "[GENESIS] No bandwidth data");
        wattroff(win, A_DIM);
        return;
    }
    
    uint64_t maxBandwidth = 1;
    for (const auto& sample : networkState.bandwidthHistory) {
        maxBandwidth = std::max(maxBandwidth, std::max(sample.bytesIn, sample.bytesOut));
    }
    
    for (int y = 0; y < graphHeight; y++) {
        uint64_t threshold = maxBandwidth * (graphHeight - y) / graphHeight;
        mvwprintw(win, startY + 2 + y, 2, "%6s|", formatBytes(threshold).c_str());
    }
    
    mvwhline(win, startY + 2 + graphHeight, 8, ACS_HLINE, graphWidth);
    
    size_t startIdx = networkState.bandwidthHistory.size() > static_cast<size_t>(graphWidth) ?
                      networkState.bandwidthHistory.size() - graphWidth : 0;
    
    for (size_t i = startIdx; i < networkState.bandwidthHistory.size(); i++) {
        int x = 9 + (i - startIdx);
        const auto& sample = networkState.bandwidthHistory[i];
        
        int inHeight = (sample.bytesIn * graphHeight) / maxBandwidth;
        int outHeight = (sample.bytesOut * graphHeight) / maxBandwidth;
        
        for (int y = 0; y < inHeight; y++) {
            wattron(win, COLOR_PAIR(2));
            mvwaddch(win, startY + 2 + graphHeight - 1 - y, x, ACS_BLOCK);
            wattroff(win, COLOR_PAIR(2));
        }
        
        for (int y = 0; y < outHeight; y++) {
            wattron(win, COLOR_PAIR(1));
            mvwaddch(win, startY + 2 + graphHeight - 1 - y, x, ACS_CKBOARD);
            wattroff(win, COLOR_PAIR(1));
        }
    }
    
    wattron(win, COLOR_PAIR(2));
    mvwprintw(win, startY + 3 + graphHeight, 2, "IN");
    wattroff(win, COLOR_PAIR(2));
    
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, startY + 3 + graphHeight, 6, "OUT");
    wattroff(win, COLOR_PAIR(1));
}

static void drawAddPeerForm(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "ADD PEER");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "Address: [Enter IP or hostname...]");
    mvwprintw(win, startY + 3, 2, "Port:    [8333]");
    
    mvwhline(win, startY + 4, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 5, 2, "BOOTSTRAP NODES:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 6, 2, "  seed1.synapsenet.io:8333");
    mvwprintw(win, startY + 7, 2, "  seed2.synapsenet.io:8333");
    mvwprintw(win, startY + 8, 2, "  seed3.synapsenet.io:8333");
    
    mvwhline(win, startY + 9, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 10, 2, "Press [C] to connect to bootstrap nodes");
    wattroff(win, A_DIM);
}

static void drawBanListForm(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "BANNED PEERS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 2, 2, "%-30s %-20s %-30s", "ADDRESS", "BANNED AT", "REASON");
    wattroff(win, A_DIM);
    
    mvwhline(win, startY + 3, 1, ACS_HLINE, width - 2);
    
    int y = startY + 4;
    bool hasBanned = false;
    
    for (const auto& peer : networkState.peers) {
        if (peer.isBanned && y < height - 4) {
            hasBanned = true;
            mvwprintw(win, y, 2, "%-30s %-20s %-30s", 
                     peer.address.c_str(),
                     formatTimestamp(peer.lastSeen).c_str(),
                     "Protocol violation");
            y++;
        }
    }
    
    if (!hasBanned) {
        wattron(win, A_DIM);
        mvwprintw(win, startY + 4, 2, "[GENESIS] No banned peers");
        wattroff(win, A_DIM);
    }
}

static void drawNetworkMenu(WINDOW* win, int y, int width) {
    mvwhline(win, y, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, y + 1, 2, "[O]verview  [P]eers  [D]etails  [B]andwidth  [A]dd Peer  Ba[n] List  [Q]uit");
    wattroff(win, A_BOLD);
}

namespace network_screen { void drawScreen() {
    int maxY, maxX;
    getmaxyx(stdscr, maxY, maxX);
    
    WINDOW* win = newwin(maxY - 2, maxX - 2, 1, 1);
    box(win, 0, 0);
    
    drawNetworkHeader(win, maxX - 2);
    
    if (networkState.currentView == "overview") {
        drawNetworkStats(win, 5, maxX - 2);
        drawPeerList(win, 14, maxY - 4, maxX - 2);
    } else if (networkState.currentView == "peers") {
        drawPeerList(win, 5, maxY - 4, maxX - 2);
    } else if (networkState.currentView == "details") {
        drawPeerDetails(win, 5, maxX - 2);
    } else if (networkState.currentView == "bandwidth") {
        drawBandwidthGraph(win, 5, maxY - 8, maxX - 2);
    } else if (networkState.currentView == "add") {
        drawAddPeerForm(win, 5, maxX - 2);
    } else if (networkState.currentView == "ban") {
        drawBanListForm(win, 5, maxY - 4, maxX - 2);
    }
    
    drawNetworkMenu(win, maxY - 5, maxX - 2);
    
    wrefresh(win);
    delwin(win);
}

int handleInput(int ch) {
    switch (ch) {
        case 'o':
        case 'O':
            networkState.currentView = "overview";
            break;
        case 'p':
        case 'P':
            networkState.currentView = "peers";
            break;
        case 'd':
        case 'D':
            networkState.currentView = "details";
            break;
        case 'b':
        case 'B':
            networkState.currentView = "bandwidth";
            break;
        case 'a':
        case 'A':
            networkState.currentView = "add";
            break;
        case 'n':
        case 'N':
            networkState.currentView = "ban";
            break;
        case KEY_UP:
            if (networkState.selectedPeer > 0) {
                networkState.selectedPeer--;
                if (networkState.selectedPeer < networkState.scrollOffset) {
                    networkState.scrollOffset = networkState.selectedPeer;
                }
            }
            break;
        case KEY_DOWN:
            if (networkState.selectedPeer < static_cast<int>(networkState.peers.size()) - 1) {
                networkState.selectedPeer++;
            }
            break;
        case 'i':
        case 'I':
            networkState.showInbound = !networkState.showInbound;
            break;
        case 'u':
        case 'U':
            networkState.showOutbound = !networkState.showOutbound;
            break;
        case 'q':
        case 'Q':
            return -1;
        case KEY_F(1):
            return 1;
    }
    
    return 0;
}

void init() {
    initNetworkState();
}

}

}
}
