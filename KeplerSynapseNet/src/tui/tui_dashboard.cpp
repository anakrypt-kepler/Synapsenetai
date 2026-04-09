#include "tui/tui.h"
#include "utils/utils.h"
#include <ncurses.h>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace synapse {
namespace tui {

struct TransactionEntry {
    std::string txId;
    std::string type;
    double amount;
    std::string address;
    std::time_t timestamp;
    int confirmations;
    std::string status;
};

struct DashboardScreen::Impl {
    WINDOW* win = nullptr;
    int width = 0;
    int height = 0;
    int selectedPanel = 0;
    int scrollOffset = 0;
    
    WalletInfo wallet;
    NetworkInfo network;
    AIModelInfo model;
    std::vector<NodeInfo> peers;
    std::vector<ContributionInfo> contributions;
    std::vector<TransactionEntry> transactions;
    
    void drawHeader();
    void drawBox(int y, int x, int h, int w, const char* title);
    void drawProgressBar(int y, int x, int w, double progress, int color);
    void drawSparkline(int y, int x, int w, const std::vector<uint64_t>& data, int color);
    std::string formatBytes(uint64_t bytes);
    std::string formatDuration(uint64_t seconds);
    std::string formatTime(std::time_t t);
    std::string formatAmount(double amount);
    std::string formatCurrency(uint64_t amount);
    std::string truncateString(const std::string& str, size_t maxLen);
    void drawBalancePanel();
    void drawNetworkPanel();
    void drawModelPanel();
    void drawStatsPanel();
    void drawActivityPanel();
    void drawTransactionsPanel();
    void drawKnowledgePanel();
    void drawFooter();
};

void DashboardScreen::Impl::drawHeader() {
    wattron(win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(win, 1, 2, "SYNAPSENET DASHBOARD");
    wattroff(win, A_BOLD | COLOR_PAIR(4));
    
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm);
    
    mvwprintw(win, 1, width - 22, "%s", timeBuf);
    mvwhline(win, 2, 1, ACS_HLINE, width - 2);
}

void DashboardScreen::Impl::drawBox(int y, int x, int h, int w, const char* title) {
    mvwaddch(win, y, x, ACS_ULCORNER);
    mvwaddch(win, y, x + w - 1, ACS_URCORNER);
    mvwaddch(win, y + h - 1, x, ACS_LLCORNER);
    mvwaddch(win, y + h - 1, x + w - 1, ACS_LRCORNER);
    
    for (int i = 1; i < w - 1; i++) {
        mvwaddch(win, y, x + i, ACS_HLINE);
        mvwaddch(win, y + h - 1, x + i, ACS_HLINE);
    }
    for (int i = 1; i < h - 1; i++) {
        mvwaddch(win, y + i, x, ACS_VLINE);
        mvwaddch(win, y + i, x + w - 1, ACS_VLINE);
    }
    
    if (title && strlen(title) > 0) {
        wattron(win, A_BOLD);
        mvwprintw(win, y, x + 2, "[%s]", title);
        wattroff(win, A_BOLD);
    }
}

void DashboardScreen::Impl::drawProgressBar(int y, int x, int w, double progress, int color) {
    int filled = static_cast<int>(progress * (w - 2));
    mvwaddch(win, y, x, '[');
    wattron(win, COLOR_PAIR(color));
    for (int i = 0; i < w - 2; i++) {
        if (i < filled) {
            mvwaddch(win, y, x + 1 + i, '#');
        } else {
            mvwaddch(win, y, x + 1 + i, ' ');
        }
    }
    wattroff(win, COLOR_PAIR(color));
    mvwaddch(win, y, x + w - 1, ']');
}

void DashboardScreen::Impl::drawSparkline(int y, int x, int w,
                                           const std::vector<uint64_t>& data, int color) {
    if (data.empty()) return;

    uint64_t maxVal = *std::max_element(data.begin(), data.end());
    if (maxVal == 0) maxVal = 1;

    const char* blocks = " _.-=*#";
    int numBlocks = 7;

    wattron(win, COLOR_PAIR(color));
    for (size_t i = 0; i < data.size() && static_cast<int>(i) < w; i++) {
        int level = static_cast<int>((static_cast<double>(data[i]) / static_cast<double>(maxVal)) * (numBlocks - 1));
        mvwaddch(win, y, x + i, blocks[level]);
    }
    wattroff(win, COLOR_PAIR(color));
}

std::string DashboardScreen::Impl::formatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return oss.str();
}

std::string DashboardScreen::Impl::formatDuration(uint64_t seconds) {
    std::ostringstream oss;
    if (seconds >= 86400) {
        oss << (seconds / 86400) << "d ";
        seconds %= 86400;
    }
    if (seconds >= 3600) {
        oss << (seconds / 3600) << "h ";
        seconds %= 3600;
    }
    oss << (seconds / 60) << "m";
    return oss.str();
}

std::string DashboardScreen::Impl::formatTime(std::time_t t) {
    std::tm* tm = std::localtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    return std::string(buf);
}

std::string DashboardScreen::Impl::formatAmount(double amount) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8) << amount << " NGT";
    return oss.str();
}

std::string DashboardScreen::Impl::formatCurrency(uint64_t amount) {
    return utils::Formatter::formatCurrency(amount);
}

std::string DashboardScreen::Impl::truncateString(const std::string& str, size_t maxLen) {
    if (str.length() <= maxLen) return str;
    return str.substr(0, maxLen - 3) + "...";
}

void DashboardScreen::Impl::drawBalancePanel() {
    int panelW = width / 3 - 2;
    int panelH = 11;
    int panelX = 1;
    int panelY = 3;
    
    drawBox(panelY, panelX, panelH, panelW, "BALANCE");
    
    int row = panelY + 2;
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    mvwprintw(win, row++, panelX + 2, "Available:");
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    wattron(win, A_BOLD);
    mvwprintw(win, row++, panelX + 4, "%s", formatCurrency(wallet.balance).c_str());
    wattroff(win, A_BOLD);
    
    row++;
    mvwprintw(win, row++, panelX + 2, "Pending:  %s", formatCurrency(wallet.pending).c_str());
    mvwprintw(win, row++, panelX + 2, "Staked:   %s", formatCurrency(wallet.staked).c_str());
    
    row++;
    uint64_t total = wallet.balance + wallet.pending + wallet.staked;
    wattron(win, A_BOLD);
    mvwprintw(win, row++, panelX + 2, "Total:    %s", formatCurrency(total).c_str());
    wattroff(win, A_BOLD);
}

void DashboardScreen::Impl::drawNetworkPanel() {
    int panelW = width / 3 - 2;
    int panelH = 10;
    int panelX = width / 3;
    int panelY = 3;
    
    drawBox(panelY, panelX, panelH, panelW, "NETWORK");
    
    int row = panelY + 2;
    
    mvwprintw(win, row++, panelX + 2, "Nodes:    %lu", network.totalNodes);
    mvwprintw(win, row++, panelX + 2, "Knowledge: %lu entries", network.knowledgeEntries);
    mvwprintw(win, row++, panelX + 2, "Net Size: %s", formatBytes(static_cast<uint64_t>(network.networkSize * 1024 * 1024 * 1024)).c_str());
    mvwprintw(win, row++, panelX + 2, "Your Data: %s", formatBytes(static_cast<uint64_t>(network.yourStorage * 1024 * 1024)).c_str());

    mvwprintw(win, row++, panelX + 2, "Route: %s", network.routeMode.empty() ? "n/a" : network.routeMode.c_str());
    mvwprintw(win, row++, panelX + 2, "Tor: %s  Proxy: %s", network.torHealth >= 0.5 ? "healthy" : "degraded", network.proxyStatus.c_str());
    mvwprintw(win, row++, panelX + 2, "Consensus: height=%lu lag=%lus syncRate=%.3f/s", network.consensusHeight, network.consensusLag, network.syncRate);
    mvwprintw(win, row++, panelX + 2, "PoE: mode=%s validators=%lu self=%s",
              network.poeBootstrapMode.empty() ? "disabled" : network.poeBootstrapMode.c_str(),
              network.poeValidatorCount,
              network.poeSelfBootstrapActive ? "on" : "off");
    
    row++;
    mvwprintw(win, row++, panelX + 2, "Sync:");
    drawProgressBar(row++, panelX + 2, panelW - 4, network.syncProgress, 1);
    
    if (network.synced) {
        wattron(win, COLOR_PAIR(1));
        mvwprintw(win, row, panelX + 2, "SYNCHRONIZED");
        wattroff(win, COLOR_PAIR(1));
    } else {
        wattron(win, COLOR_PAIR(2));
        mvwprintw(win, row, panelX + 2, "SYNCING %.1f%%", network.syncProgress * 100);
        wattroff(win, COLOR_PAIR(2));
    }
}

void DashboardScreen::Impl::drawModelPanel() {
    int panelW = width / 3 - 1;
    int panelH = 10;
    int panelX = 2 * width / 3;
    int panelY = 3;
    
    drawBox(panelY, panelX, panelH, panelW, "AI MODEL");
    
    int row = panelY + 2;
    
    mvwprintw(win, row++, panelX + 2, "Model: %s", model.name.empty() ? "None" : model.name.c_str());
    
    int statusColor = 7;
    if (model.status == "RUNNING") statusColor = 1;
    else if (model.status == "LOADING") statusColor = 2;
    else if (model.status == "ERROR") statusColor = 3;
    
    wattron(win, COLOR_PAIR(statusColor));
    mvwprintw(win, row++, panelX + 2, "Status: %s", model.status.c_str());
    wattroff(win, COLOR_PAIR(statusColor));
    
    mvwprintw(win, row++, panelX + 2, "Mode: %s", model.mode.c_str());
    mvwprintw(win, row++, panelX + 2, "Slots: %d/%d", model.slotsUsed, model.slotsMax);
    
    if (model.progress > 0 && model.progress < 1.0) {
        row++;
        mvwprintw(win, row++, panelX + 2, "Progress:");
        drawProgressBar(row++, panelX + 2, panelW - 4, model.progress, 4);
    }
}

void DashboardScreen::Impl::drawStatsPanel() {
    int panelW = width / 2 - 2;
    int panelH = 8;
    int panelX = 1;
    int panelY = 14;
    
    drawBox(panelY, panelX, panelH, panelW, "EARNINGS");
    
    int row = panelY + 2;
    
    mvwprintw(win, row++, panelX + 2, "Today:    %.8f NGT", static_cast<double>(model.earningsTodayAtoms) / 100000000.0);
    mvwprintw(win, row++, panelX + 2, "This Week: %.8f NGT", static_cast<double>(model.earningsWeekAtoms) / 100000000.0);
    mvwprintw(win, row++, panelX + 2, "Total:    %.8f NGT", static_cast<double>(model.earningsTotalAtoms) / 100000000.0);
    
    row++;
    mvwprintw(win, row++, panelX + 2, "Uptime:   %s", formatDuration(static_cast<uint64_t>(model.uptime)).c_str());
}

void DashboardScreen::Impl::drawActivityPanel() {
    int panelW = width / 2 - 1;
    int panelH = 8;
    int panelX = width / 2;
    int panelY = 14;
    
    drawBox(panelY, panelX, panelH, panelW, "RECENT ACTIVITY");
    
    int row = panelY + 2;
    int maxRows = panelH - 3;
    
    if (contributions.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, row, panelX + 2, "No recent activity");
        wattroff(win, A_DIM);
        return;
    }
    
    for (size_t i = 0; i < contributions.size() && static_cast<int>(i) < maxRows; i++) {
        const auto& c = contributions[i];
        
        int typeColor = 7;
        if (c.type == "KNOWLEDGE") typeColor = 4;
        else if (c.type == "INFERENCE") typeColor = 5;
        else if (c.type == "VALIDATION") typeColor = 6;
        
        wattron(win, COLOR_PAIR(typeColor));
        mvwprintw(win, row, panelX + 2, "%-10s", c.type.c_str());
        wattroff(win, COLOR_PAIR(typeColor));
        
        std::string desc = truncateString(c.description, 20);
        mvwprintw(win, row, panelX + 14, "%-20s", desc.c_str());
        
        wattron(win, COLOR_PAIR(1));
        mvwprintw(win, row, panelX + 36, "+%.4f", static_cast<double>(c.rewardAtoms) / 100000000.0);
        wattroff(win, COLOR_PAIR(1));
        
        row++;
    }
}

void DashboardScreen::Impl::drawTransactionsPanel() {
    int panelW = width - 2;
    int panelH = height - 24;
    int panelX = 1;
    int panelY = 23;
    
    if (panelH < 5) return;
    
    drawBox(panelY, panelX, panelH, panelW, "RECENT TRANSACTIONS");
    
    wattron(win, A_DIM);
    mvwprintw(win, panelY + 1, panelX + 2, "%-12s %-8s %-20s %-42s %-6s %-10s",
              "TIME", "TYPE", "AMOUNT", "ADDRESS", "CONF", "STATUS");
    wattroff(win, A_DIM);
    
    mvwhline(win, panelY + 2, panelX + 1, ACS_HLINE, panelW - 2);
    
    int row = panelY + 3;
    int maxRows = panelH - 4;
    
    if (transactions.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, row, panelX + 2, "No transactions yet");
        wattroff(win, A_DIM);
        return;
    }
    
    for (size_t i = 0; i < transactions.size() && static_cast<int>(i) < maxRows; i++) {
        const auto& tx = transactions[i];
        
        mvwprintw(win, row, panelX + 2, "%-12s", formatTime(tx.timestamp).c_str());
        
        int typeColor = tx.type == "IN" ? 1 : 3;
        wattron(win, COLOR_PAIR(typeColor));
        mvwprintw(win, row, panelX + 15, "%-8s", tx.type.c_str());
        wattroff(win, COLOR_PAIR(typeColor));
        
        mvwprintw(win, row, panelX + 24, "%-20s", formatAmount(tx.amount).c_str());
        mvwprintw(win, row, panelX + 45, "%-42s", truncateString(tx.address, 40).c_str());
        mvwprintw(win, row, panelX + 88, "%-6d", tx.confirmations);
        
        int statusColor = 7;
        if (tx.status == "confirmed") statusColor = 1;
        else if (tx.status == "pending") statusColor = 2;
        
        wattron(win, COLOR_PAIR(statusColor));
        mvwprintw(win, row, panelX + 95, "%-10s", tx.status.c_str());
        wattroff(win, COLOR_PAIR(statusColor));
        
        row++;
    }
}

void DashboardScreen::Impl::drawKnowledgePanel() {
    int panelW = width / 3 - 1;
    int panelH = 10;
    int panelX = 2 * width / 3;
    int panelY = 14;
    drawBox(panelY, panelX, panelH, panelW, "PoE / KNOWLEDGE");
    int row = panelY + 2;

    mvwprintw(win, row++, panelX + 2, "Total Entries: %lu", network.knowledgeEntries);
    mvwprintw(win, row++, panelX + 2, "Finalized:     %lu", network.poeFinalized);
    mvwprintw(win, row++, panelX + 2, "Pending:       %lu", network.poePending);
    mvwprintw(win, row++, panelX + 2, "Total PoE:     %lu", network.poeTotal);

    double last = static_cast<double>(network.lastReward) / 100000000.0;
    wattron(win, A_BOLD);
    mvwprintw(win, row++, panelX + 2, "Last Reward:   %.8f NGT", last);
    wattroff(win, A_BOLD);

    if (!network.rewardHistory.empty()) {
        drawSparkline(panelY + panelH - 2, panelX + 2, panelW - 4, network.rewardHistory, 1);
    }
}

void DashboardScreen::Impl::drawFooter() {
    mvwhline(win, height - 3, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, height - 2, 2, 
              "[W]allet  [N]etwork  [K]nowledge  [M]odel  [S]ettings  [H]elp  [Q]uit");
    wattroff(win, A_BOLD);
}

DashboardScreen::DashboardScreen() : impl_(std::make_unique<Impl>()) {}
DashboardScreen::~DashboardScreen() = default;

void DashboardScreen::init(WINDOW* win, int width, int height) {
    impl_->win = win;
    impl_->width = width;
    impl_->height = height;
}

void DashboardScreen::draw() {
    werase(impl_->win);
    box(impl_->win, 0, 0);
    
    impl_->drawHeader();
    impl_->drawBalancePanel();
    impl_->drawNetworkPanel();
    impl_->drawModelPanel();
    impl_->drawStatsPanel();
    impl_->drawActivityPanel();
    impl_->drawTransactionsPanel();
    impl_->drawFooter();
    
    wrefresh(impl_->win);
}

bool DashboardScreen::handleInput(int ch) {
    switch (ch) {
        case 'w':
        case 'W':
            return true;
        case 'n':
        case 'N':
            return true;
        case 'k':
        case 'K':
            return true;
        case 'm':
        case 'M':
            return true;
        case 's':
        case 'S':
            return true;
        case 'h':
        case 'H':
            return true;
        case 'q':
        case 'Q':
            return true;
        case KEY_UP:
            if (impl_->selectedPanel > 0) impl_->selectedPanel--;
            break;
        case KEY_DOWN:
            impl_->selectedPanel++;
            break;
    }
    return false;
}

void DashboardScreen::setWalletInfo(const WalletInfo& info) {
    impl_->wallet = info;
}

void DashboardScreen::setNetworkInfo(const NetworkInfo& info) {
    impl_->network = info;
}

void DashboardScreen::setModelInfo(const AIModelInfo& info) {
    impl_->model = info;
}

void DashboardScreen::setPeers(const std::vector<NodeInfo>& peers) {
    impl_->peers = peers;
}

void DashboardScreen::setContributions(const std::vector<ContributionInfo>& contributions) {
    impl_->contributions = contributions;
}

}
}
