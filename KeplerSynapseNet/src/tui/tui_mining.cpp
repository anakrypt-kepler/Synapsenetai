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
#include <deque>

namespace synapse {
namespace tui {

struct MiningStats {
    uint64_t hashRate;
    uint64_t sharesAccepted;
    uint64_t sharesRejected;
    uint64_t blocksFound;
    uint64_t totalRewards;
    uint64_t pendingRewards;
    uint64_t uptime;
    double efficiency;
    double powerConsumption;
    double temperature;
    std::string status;
    std::string algorithm;
    uint32_t difficulty;
};

struct MiningDevice {
    std::string deviceId;
    std::string name;
    std::string type;
    uint64_t hashRate;
    double temperature;
    double fanSpeed;
    double powerDraw;
    double efficiency;
    uint64_t sharesAccepted;
    uint64_t sharesRejected;
    std::string status;
    bool isEnabled;
};

struct MiningPool {
    std::string poolId;
    std::string name;
    std::string url;
    uint16_t port;
    std::string worker;
    uint64_t hashRate;
    uint64_t sharesSubmitted;
    uint64_t sharesAccepted;
    double fee;
    uint64_t lastBlockTime;
    std::string status;
    bool isActive;
};

struct HashRateSample {
    uint64_t timestamp;
    uint64_t hashRate;
};

struct MiningScreenState {
    MiningStats stats;
    std::vector<MiningDevice> devices;
    std::vector<MiningPool> pools;
    std::deque<HashRateSample> hashRateHistory;
    int selectedDevice;
    int selectedPool;
    int scrollOffset;
    std::string currentView;
    bool isMining;
    uint64_t miningStartTime;
    uint64_t poePendingSubmissions = 0;
    uint64_t poeActiveVotes = 0;
    std::string poeLastFinalizedId;
    uint64_t poeLastFinalizedRewardNgt = 0;
    uint32_t poePowDifficulty = 0;
    uint64_t poeEstSecondsToFinalize = 0;
};

static MiningScreenState miningState;

static void initMiningState() {
    miningState.stats = MiningStats{};
    miningState.stats.status = "stopped";
    miningState.stats.algorithm = "SHA256d";
    miningState.stats.difficulty = 1;
    miningState.devices.clear();
    miningState.pools.clear();
    miningState.hashRateHistory.clear();
    miningState.selectedDevice = 0;
    miningState.selectedPool = 0;
    miningState.scrollOffset = 0;
    miningState.currentView = "overview";
    miningState.isMining = false;
    miningState.miningStartTime = 0;
}

static std::string formatHashRate(uint64_t hashRate) {
    std::stringstream ss;
    if (hashRate >= 1000000000000ULL) {
        ss << std::fixed << std::setprecision(2) << (hashRate / 1000000000000.0) << " TH/s";
    } else if (hashRate >= 1000000000) {
        ss << std::fixed << std::setprecision(2) << (hashRate / 1000000000.0) << " GH/s";
    } else if (hashRate >= 1000000) {
        ss << std::fixed << std::setprecision(2) << (hashRate / 1000000.0) << " MH/s";
    } else if (hashRate >= 1000) {
        ss << std::fixed << std::setprecision(2) << (hashRate / 1000.0) << " KH/s";
    } else {
        ss << hashRate << " H/s";
    }
    return ss.str();
}

static std::string formatNGT(uint64_t amount) {
    std::stringstream ss;
    double ngt = static_cast<double>(amount) / 100000000.0;
    ss << std::fixed << std::setprecision(8) << ngt << " NGT";
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

static void drawMiningHeader(WINDOW* win, int width) {
    wattron(win, A_BOLD | COLOR_PAIR(2));
    mvwprintw(win, 1, 2, "NGT MINING");
    wattroff(win, A_BOLD | COLOR_PAIR(2));
    
    mvwhline(win, 2, 1, ACS_HLINE, width - 2);
    
    std::stringstream ss;
    ss << "Status: ";
    
    int statusColor = 7;
    if (miningState.stats.status == "mining") statusColor = 2;
    else if (miningState.stats.status == "starting") statusColor = 3;
    else if (miningState.stats.status == "stopped") statusColor = 1;
    
    wattron(win, COLOR_PAIR(statusColor));
    ss << miningState.stats.status;
    wattroff(win, COLOR_PAIR(statusColor));
    
    ss << " | Algorithm: " << miningState.stats.algorithm;
    ss << " | Difficulty: " << miningState.stats.difficulty;
    ss << " | Hashrate: " << formatHashRate(miningState.stats.hashRate);
    
    mvwprintw(win, 3, 2, "%s", ss.str().c_str());
}

static void drawMiningStats(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "MINING STATISTICS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    int col1 = 2;
    int col2 = width / 3;
    int col3 = 2 * width / 3;
    
    mvwprintw(win, startY + 2, col1, "Hash Rate:      %s", formatHashRate(miningState.stats.hashRate).c_str());
    mvwprintw(win, startY + 2, col2, "Shares:         %lu / %lu", 
              miningState.stats.sharesAccepted, 
              miningState.stats.sharesAccepted + miningState.stats.sharesRejected);
    mvwprintw(win, startY + 2, col3, "Blocks Found:   %lu", miningState.stats.blocksFound);
    
    mvwprintw(win, startY + 3, col1, "Efficiency:     %.2f%%", miningState.stats.efficiency);
    mvwprintw(win, startY + 3, col2, "Power:          %.1f W", miningState.stats.powerConsumption);
    mvwprintw(win, startY + 3, col3, "Temperature:    %.1f C", miningState.stats.temperature);
    
    mvwprintw(win, startY + 4, col1, "Total Rewards:  %s", formatNGT(miningState.stats.totalRewards).c_str());
    mvwprintw(win, startY + 4, col2, "Pending:        %s", formatNGT(miningState.stats.pendingRewards).c_str());
    mvwprintw(win, startY + 4, col3, "Uptime:         %s", formatDuration(miningState.stats.uptime).c_str());

    wattron(win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(win, startY + 6, col1, "PoE STATUS");
    wattroff(win, A_BOLD | COLOR_PAIR(4));

    mvwprintw(win, startY + 7, col1, "Pending Submissions: %lu", miningState.poePendingSubmissions);
    mvwprintw(win, startY + 7, col2, "Active Votes:        %lu", miningState.poeActiveVotes);

    mvwprintw(win, startY + 8, col1, "Last Finalized ID:   %s", miningState.poeLastFinalizedId.c_str());
    mvwprintw(win, startY + 8, col2, "Last Finalized Rwd:  %s", formatNGT(miningState.poeLastFinalizedRewardNgt).c_str());

    mvwprintw(win, startY + 9, col1, "PoW Difficulty:      %u", miningState.poePowDifficulty);
    mvwprintw(win, startY + 9, col2, "Est Secs to Finalize: %s", formatDuration(miningState.poeEstSecondsToFinalize).c_str());
}

static void drawDeviceList(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "MINING DEVICES");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 2, 2, "%-20s %-10s %-12s %-8s %-8s %-8s %-10s %-8s", 
              "DEVICE", "TYPE", "HASHRATE", "TEMP", "FAN", "POWER", "SHARES", "STATUS");
    wattroff(win, A_DIM);
    
    mvwhline(win, startY + 3, 1, ACS_HLINE, width - 2);
    
    int y = startY + 4;
    int maxVisible = height - startY - 6;
    
    for (size_t i = miningState.scrollOffset; 
         i < miningState.devices.size() && y < startY + maxVisible; 
         i++, y++) {
        const MiningDevice& device = miningState.devices[i];
        
        if (static_cast<int>(i) == miningState.selectedDevice) {
            wattron(win, A_REVERSE);
        }
        
        std::string enabledMark = device.isEnabled ? "[X]" : "[ ]";
        mvwprintw(win, y, 2, "%s %-17s", enabledMark.c_str(), device.name.c_str());
        mvwprintw(win, y, 24, "%-10s", device.type.c_str());
        mvwprintw(win, y, 34, "%-12s", formatHashRate(device.hashRate).c_str());
        
        int tempColor = 2;
        if (device.temperature > 80) tempColor = 1;
        else if (device.temperature > 70) tempColor = 3;
        
        wattron(win, COLOR_PAIR(tempColor));
        mvwprintw(win, y, 46, "%.0f C", device.temperature);
        wattroff(win, COLOR_PAIR(tempColor));
        
        mvwprintw(win, y, 54, "%.0f%%", device.fanSpeed);
        mvwprintw(win, y, 62, "%.0f W", device.powerDraw);
        mvwprintw(win, y, 70, "%lu/%lu", device.sharesAccepted, 
                 device.sharesAccepted + device.sharesRejected);
        
        int statusColor = 7;
        if (device.status == "mining") statusColor = 2;
        else if (device.status == "idle") statusColor = 3;
        else if (device.status == "error") statusColor = 1;
        
        wattron(win, COLOR_PAIR(statusColor));
        mvwprintw(win, y, 80, "%-8s", device.status.c_str());
        wattroff(win, COLOR_PAIR(statusColor));
        
        if (static_cast<int>(i) == miningState.selectedDevice) {
            wattroff(win, A_REVERSE);
        }
    }
    
    if (miningState.devices.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, startY + 4, 2, "[GENESIS] No mining devices detected");
        wattroff(win, A_DIM);
    }
}

static void drawPoolList(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "MINING POOLS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 2, 2, "%-20s %-30s %-12s %-10s %-8s %-8s", 
              "POOL", "URL", "HASHRATE", "SHARES", "FEE", "STATUS");
    wattroff(win, A_DIM);
    
    mvwhline(win, startY + 3, 1, ACS_HLINE, width - 2);
    
    int y = startY + 4;
    int maxVisible = height - startY - 6;
    
    for (size_t i = 0; i < miningState.pools.size() && y < startY + maxVisible; i++, y++) {
        const MiningPool& pool = miningState.pools[i];
        
        if (static_cast<int>(i) == miningState.selectedPool) {
            wattron(win, A_REVERSE);
        }
        
        std::string activeMark = pool.isActive ? "[*]" : "[ ]";
        mvwprintw(win, y, 2, "%s %-17s", activeMark.c_str(), pool.name.c_str());
        
        std::string urlStr = pool.url + ":" + std::to_string(pool.port);
        if (urlStr.length() > 28) urlStr = urlStr.substr(0, 25) + "...";
        mvwprintw(win, y, 24, "%-30s", urlStr.c_str());
        
        mvwprintw(win, y, 54, "%-12s", formatHashRate(pool.hashRate).c_str());
        mvwprintw(win, y, 66, "%lu/%lu", pool.sharesAccepted, pool.sharesSubmitted);
        mvwprintw(win, y, 76, "%.1f%%", pool.fee);
        
        int statusColor = 7;
        if (pool.status == "connected") statusColor = 2;
        else if (pool.status == "connecting") statusColor = 3;
        else if (pool.status == "disconnected") statusColor = 1;
        
        wattron(win, COLOR_PAIR(statusColor));
        mvwprintw(win, y, 84, "%-8s", pool.status.c_str());
        wattroff(win, COLOR_PAIR(statusColor));
        
        if (static_cast<int>(i) == miningState.selectedPool) {
            wattroff(win, A_REVERSE);
        }
    }
    
    if (miningState.pools.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, startY + 4, 2, "[GENESIS] No pools configured");
        wattroff(win, A_DIM);
    }
}

static void drawHashRateGraph(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "HASHRATE (last 60 minutes)");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    int graphHeight = height - startY - 4;
    int graphWidth = width - 14;
    
    if (miningState.hashRateHistory.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, startY + 2, 2, "[GENESIS] No hashrate data");
        wattroff(win, A_DIM);
        return;
    }
    
    uint64_t maxHashRate = 1;
    for (const auto& sample : miningState.hashRateHistory) {
        maxHashRate = std::max(maxHashRate, sample.hashRate);
    }
    
    for (int y = 0; y < graphHeight; y++) {
        uint64_t threshold = maxHashRate * (graphHeight - y) / graphHeight;
        mvwprintw(win, startY + 2 + y, 2, "%10s|", formatHashRate(threshold).c_str());
    }
    
    mvwhline(win, startY + 2 + graphHeight, 12, ACS_HLINE, graphWidth);
    
    size_t startIdx = miningState.hashRateHistory.size() > static_cast<size_t>(graphWidth) ?
                      miningState.hashRateHistory.size() - graphWidth : 0;
    
    for (size_t i = startIdx; i < miningState.hashRateHistory.size(); i++) {
        int x = 13 + (i - startIdx);
        const auto& sample = miningState.hashRateHistory[i];
        
        int barHeight = (sample.hashRate * graphHeight) / maxHashRate;
        
        for (int y = 0; y < barHeight; y++) {
            wattron(win, COLOR_PAIR(2));
            mvwaddch(win, startY + 2 + graphHeight - 1 - y, x, ACS_BLOCK);
            wattroff(win, COLOR_PAIR(2));
        }
    }
}

static void drawAddPoolForm(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "ADD MINING POOL");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "Pool Name:   [Enter pool name...]");
    mvwprintw(win, startY + 3, 2, "URL:         [stratum+tcp://pool.example.com]");
    mvwprintw(win, startY + 4, 2, "Port:        [3333]");
    mvwprintw(win, startY + 5, 2, "Worker:      [Enter worker name...]");
    mvwprintw(win, startY + 6, 2, "Password:    [x]");
    
    mvwhline(win, startY + 7, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 8, 2, "RECOMMENDED POOLS:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 9, 2, "  pool1.synapsenet.io:3333  (Fee: 1%%)");
    mvwprintw(win, startY + 10, 2, "  pool2.synapsenet.io:3333  (Fee: 1%%)");
    mvwprintw(win, startY + 11, 2, "  pool3.synapsenet.io:3333  (Fee: 0.5%%)");
    
    mvwhline(win, startY + 12, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 13, 2, "Press [Enter] to add pool");
    wattroff(win, A_DIM);
}

static void drawMiningSettings(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "MINING SETTINGS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 2, 2, "ALGORITHM:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 3, 2, "  [X] SHA256d (Bitcoin-compatible)");
    mvwprintw(win, startY + 4, 2, "  [ ] Scrypt");
    mvwprintw(win, startY + 5, 2, "  [ ] Ethash");
    mvwprintw(win, startY + 6, 2, "  [ ] RandomX");
    
    mvwhline(win, startY + 7, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 8, 2, "PERFORMANCE:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 9, 2, "  Intensity:     [Auto]");
    mvwprintw(win, startY + 10, 2, "  Thread Count:  [Auto]");
    mvwprintw(win, startY + 11, 2, "  Power Limit:   [100%%]");
    mvwprintw(win, startY + 12, 2, "  Temp Limit:    [85 C]");
    
    mvwhline(win, startY + 13, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 14, 2, "OPTIONS:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 15, 2, "  [X] Auto-start on boot");
    mvwprintw(win, startY + 16, 2, "  [ ] Solo mining mode");
    mvwprintw(win, startY + 17, 2, "  [X] Auto-switch pools on failure");
    mvwprintw(win, startY + 18, 2, "  [ ] Enable overclocking");
}

static void drawMiningMenu(WINDOW* win, int y, int width) {
    mvwhline(win, y, 1, ACS_HLINE, width - 2);
    
    std::string startStop = miningState.isMining ? "[S]top" : "[S]tart";
    
    wattron(win, A_BOLD);
    mvwprintw(win, y + 1, 2, "[O]verview  [D]evices  [P]ools  [G]raph  [A]dd Pool  Se[t]tings  %s  [Q]uit",
              startStop.c_str());
    wattroff(win, A_BOLD);
}

namespace mining {

void drawScreen() {
    int maxY, maxX;
    getmaxyx(stdscr, maxY, maxX);
    
    WINDOW* win = newwin(maxY - 2, maxX - 2, 1, 1);
    box(win, 0, 0);
    
    drawMiningHeader(win, maxX - 2);
    
    if (miningState.currentView == "overview") {
        drawMiningStats(win, 5, maxX - 2);
        drawDeviceList(win, 10, maxY / 2, maxX - 2);
        drawPoolList(win, maxY / 2, maxY - 4, maxX - 2);
    } else if (miningState.currentView == "devices") {
        drawDeviceList(win, 5, maxY - 4, maxX - 2);
    } else if (miningState.currentView == "pools") {
        drawPoolList(win, 5, maxY - 4, maxX - 2);
    } else if (miningState.currentView == "graph") {
        drawHashRateGraph(win, 5, maxY - 8, maxX - 2);
    } else if (miningState.currentView == "add") {
        drawAddPoolForm(win, 5, maxX - 2);
    } else if (miningState.currentView == "settings") {
        drawMiningSettings(win, 5, maxX - 2);
    }
    
    drawMiningMenu(win, maxY - 5, maxX - 2);
    
    wrefresh(win);
    delwin(win);
}

int handleInput(int ch) {
    switch (ch) {
        case 'o':
        case 'O':
            miningState.currentView = "overview";
            break;
        case 'd':
        case 'D':
            miningState.currentView = "devices";
            break;
        case 'p':
        case 'P':
            miningState.currentView = "pools";
            break;
        case 'g':
        case 'G':
            miningState.currentView = "graph";
            break;
        case 'a':
        case 'A':
            miningState.currentView = "add";
            break;
        case 't':
        case 'T':
            miningState.currentView = "settings";
            break;
        case 's':
        case 'S':
            miningState.isMining = !miningState.isMining;
            miningState.stats.status = miningState.isMining ? "mining" : "stopped";
            break;
        case KEY_UP:
            if (miningState.currentView == "devices") {
                if (miningState.selectedDevice > 0) miningState.selectedDevice--;
            } else if (miningState.currentView == "pools") {
                if (miningState.selectedPool > 0) miningState.selectedPool--;
            }
            break;
        case KEY_DOWN:
            if (miningState.currentView == "devices") {
                if (miningState.selectedDevice < static_cast<int>(miningState.devices.size()) - 1) {
                    miningState.selectedDevice++;
                }
            } else if (miningState.currentView == "pools") {
                if (miningState.selectedPool < static_cast<int>(miningState.pools.size()) - 1) {
                    miningState.selectedPool++;
                }
            }
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
    initMiningState();
}

void setPoeStatus(uint64_t poePendingSubmissions, uint64_t poeActiveVotes, const std::string& poeLastFinalizedId, uint64_t poeLastFinalizedRewardNgt, uint32_t poePowDifficulty, uint64_t poeEstSecondsToFinalize) {
    miningState.poePendingSubmissions = poePendingSubmissions;
    miningState.poeActiveVotes = poeActiveVotes;
    miningState.poeLastFinalizedId = poeLastFinalizedId;
    miningState.poeLastFinalizedRewardNgt = poeLastFinalizedRewardNgt;
    miningState.poePowDifficulty = poePowDifficulty;
    miningState.poeEstSecondsToFinalize = poeEstSecondsToFinalize;
}

}

}
}
