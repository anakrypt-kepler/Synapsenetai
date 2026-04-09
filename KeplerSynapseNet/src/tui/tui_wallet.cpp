#include "tui/tui.h"
#include <ncurses.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace synapse {
namespace tui {

struct WalletEntry {
    std::string address;
    std::string label;
    uint64_t balance;
    uint64_t pendingBalance;
    uint64_t stakedBalance;
    uint64_t rewardsEarned;
    uint64_t transactionCount;
    uint64_t createdAt;
    bool isDefault;
    bool isWatchOnly;
    bool isHardware;
};

struct TransactionEntry {
    std::string txId;
    std::string fromAddress;
    std::string toAddress;
    uint64_t amount;
    uint64_t fee;
    uint64_t timestamp;
    uint32_t confirmations;
    std::string status;
    std::string type;
    std::string memo;
};

struct WalletScreenState {
    std::vector<WalletEntry> wallets;
    std::vector<TransactionEntry> transactions;
    int selectedWallet;
    int selectedTransaction;
    int scrollOffset;
    int txScrollOffset;
    std::string currentView;
    std::string searchQuery;
    bool showPending;
    bool showStaked;
    uint64_t totalBalance;
    uint64_t totalPending;
    uint64_t totalStaked;
};

static WalletScreenState walletState;

static void initWalletState() {
    walletState.wallets.clear();
    walletState.transactions.clear();
    walletState.selectedWallet = 0;
    walletState.selectedTransaction = 0;
    walletState.scrollOffset = 0;
    walletState.txScrollOffset = 0;
    walletState.currentView = "overview";
    walletState.searchQuery = "";
    walletState.showPending = true;
    walletState.showStaked = true;
    walletState.totalBalance = 0;
    walletState.totalPending = 0;
    walletState.totalStaked = 0;
}

static std::string formatNGT(uint64_t amount) {
    std::stringstream ss;
    double ngt = static_cast<double>(amount) / 100000000.0;
    ss << std::fixed << std::setprecision(8) << ngt << " NGT";
    return ss.str();
}

static std::string formatTimestamp(uint64_t timestamp) {
    time_t t = static_cast<time_t>(timestamp);
    struct tm* tm = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buf);
}

static std::string shortenAddress(const std::string& address) {
    if (address.length() <= 16) return address;
    return address.substr(0, 8) + "..." + address.substr(address.length() - 8);
}

static void drawWalletHeader(WINDOW* win, int width) {
    wattron(win, A_BOLD | COLOR_PAIR(2));
    mvwprintw(win, 1, 2, "SYNAPSENET WALLET");
    wattroff(win, A_BOLD | COLOR_PAIR(2));
    
    mvwhline(win, 2, 1, ACS_HLINE, width - 2);
    
    std::stringstream ss;
    ss << "Total: " << formatNGT(walletState.totalBalance);
    if (walletState.totalPending > 0) {
        ss << " | Pending: " << formatNGT(walletState.totalPending);
    }
    if (walletState.totalStaked > 0) {
        ss << " | Staked: " << formatNGT(walletState.totalStaked);
    }
    
    mvwprintw(win, 3, 2, "%s", ss.str().c_str());
}

static void drawWalletList(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "WALLETS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    int y = startY + 2;
    int maxVisible = height - startY - 4;
    
    for (size_t i = walletState.scrollOffset; 
         i < walletState.wallets.size() && y < startY + maxVisible; 
         i++, y++) {
        const WalletEntry& wallet = walletState.wallets[i];
        
        if (static_cast<int>(i) == walletState.selectedWallet) {
            wattron(win, A_REVERSE);
        }
        
        std::stringstream line;
        if (wallet.isDefault) line << "[*] ";
        else line << "[ ] ";
        
        line << wallet.label;
        if (wallet.label.length() < 15) {
            line << std::string(15 - wallet.label.length(), ' ');
        }
        
        line << " " << shortenAddress(wallet.address);
        line << " " << formatNGT(wallet.balance);
        
        if (wallet.isWatchOnly) line << " [W]";
        if (wallet.isHardware) line << " [H]";
        
        mvwprintw(win, y, 2, "%-*s", width - 4, line.str().c_str());
        
        if (static_cast<int>(i) == walletState.selectedWallet) {
            wattroff(win, A_REVERSE);
        }
    }
}

static void drawTransactionList(WINDOW* win, int startY, int height, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "RECENT TRANSACTIONS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 2, 2, "%-12s %-20s %-20s %-18s %-10s", 
              "TYPE", "FROM", "TO", "AMOUNT", "STATUS");
    wattroff(win, A_DIM);
    
    mvwhline(win, startY + 3, 1, ACS_HLINE, width - 2);
    
    int y = startY + 4;
    int maxVisible = height - startY - 6;
    
    for (size_t i = walletState.txScrollOffset; 
         i < walletState.transactions.size() && y < startY + maxVisible; 
         i++, y++) {
        const TransactionEntry& tx = walletState.transactions[i];
        
        if (static_cast<int>(i) == walletState.selectedTransaction) {
            wattron(win, A_REVERSE);
        }
        
        int typeColor = 7;
        if (tx.type == "receive") typeColor = 2;
        else if (tx.type == "send") typeColor = 1;
        else if (tx.type == "stake") typeColor = 3;
        else if (tx.type == "reward") typeColor = 6;
        
        wattron(win, COLOR_PAIR(typeColor));
        mvwprintw(win, y, 2, "%-12s", tx.type.c_str());
        wattroff(win, COLOR_PAIR(typeColor));
        
        mvwprintw(win, y, 14, "%-20s", shortenAddress(tx.fromAddress).c_str());
        mvwprintw(win, y, 34, "%-20s", shortenAddress(tx.toAddress).c_str());
        
        if (tx.type == "receive" || tx.type == "reward") {
            wattron(win, COLOR_PAIR(2));
            mvwprintw(win, y, 54, "+%-17s", formatNGT(tx.amount).c_str());
            wattroff(win, COLOR_PAIR(2));
        } else {
            wattron(win, COLOR_PAIR(1));
            mvwprintw(win, y, 54, "-%-17s", formatNGT(tx.amount).c_str());
            wattroff(win, COLOR_PAIR(1));
        }
        
        int statusColor = 7;
        if (tx.status == "confirmed") statusColor = 2;
        else if (tx.status == "pending") statusColor = 3;
        else if (tx.status == "failed") statusColor = 1;
        
        wattron(win, COLOR_PAIR(statusColor));
        mvwprintw(win, y, 72, "%-10s", tx.status.c_str());
        wattroff(win, COLOR_PAIR(statusColor));
        
        if (static_cast<int>(i) == walletState.selectedTransaction) {
            wattroff(win, A_REVERSE);
        }
    }
    
    if (walletState.transactions.empty()) {
        wattron(win, A_DIM);
        mvwprintw(win, startY + 4, 2, "[GENESIS] No transactions yet");
        wattroff(win, A_DIM);
    }
}

static void drawWalletDetails(WINDOW* win, int startY, int width) {
    if (walletState.wallets.empty()) return;
    
    const WalletEntry& wallet = walletState.wallets[walletState.selectedWallet];
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "WALLET DETAILS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "Label:        %s", wallet.label.c_str());
    mvwprintw(win, startY + 3, 2, "Address:      %s", wallet.address.c_str());
    mvwprintw(win, startY + 4, 2, "Balance:      %s", formatNGT(wallet.balance).c_str());
    mvwprintw(win, startY + 5, 2, "Pending:      %s", formatNGT(wallet.pendingBalance).c_str());
    mvwprintw(win, startY + 6, 2, "Staked:       %s", formatNGT(wallet.stakedBalance).c_str());
    mvwprintw(win, startY + 7, 2, "Rewards:      %s", formatNGT(wallet.rewardsEarned).c_str());
    mvwprintw(win, startY + 8, 2, "Transactions: %lu", wallet.transactionCount);
    mvwprintw(win, startY + 9, 2, "Created:      %s", formatTimestamp(wallet.createdAt).c_str());
    
    std::string flags;
    if (wallet.isDefault) flags += "[DEFAULT] ";
    if (wallet.isWatchOnly) flags += "[WATCH-ONLY] ";
    if (wallet.isHardware) flags += "[HARDWARE] ";
    
    if (!flags.empty()) {
        mvwprintw(win, startY + 10, 2, "Flags:        %s", flags.c_str());
    }
}

static void drawSendForm(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "SEND NGT");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "From:    [Select wallet...]");
    mvwprintw(win, startY + 3, 2, "To:      [Enter recipient address...]");
    mvwprintw(win, startY + 4, 2, "Amount:  [0.00000000 NGT]");
    mvwprintw(win, startY + 5, 2, "Fee:     [Auto: 0.00001000 NGT]");
    mvwprintw(win, startY + 6, 2, "Memo:    [Optional message...]");
    
    mvwhline(win, startY + 7, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 8, 2, "Available: 0.00000000 NGT");
    mvwprintw(win, startY + 9, 2, "Total:     0.00001000 NGT (including fee)");
    wattroff(win, A_DIM);
    
    mvwprintw(win, startY + 11, 2, "[GENESIS] Wallet not initialized - cannot send");
}

static void drawReceiveForm(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "RECEIVE NGT");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    if (walletState.wallets.empty()) {
        mvwprintw(win, startY + 2, 2, "[GENESIS] No wallet available");
        return;
    }
    
    const WalletEntry& wallet = walletState.wallets[walletState.selectedWallet];
    
    mvwprintw(win, startY + 2, 2, "Wallet:  %s", wallet.label.c_str());
    mvwprintw(win, startY + 3, 2, "Address: %s", wallet.address.c_str());
    
    mvwhline(win, startY + 5, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 6, 2, "QR CODE:");
    wattroff(win, A_BOLD);
    
    int qrY = startY + 7;
    int qrX = 4;
    int qrSize = 21;
    
    for (int y = 0; y < qrSize; y++) {
        for (int x = 0; x < qrSize; x++) {
            bool isBlack = ((x + y) % 3 == 0) || (x < 7 && y < 7) || 
                          (x >= qrSize - 7 && y < 7) || (x < 7 && y >= qrSize - 7);
            if (isBlack) {
                mvwaddch(win, qrY + y, qrX + x * 2, ACS_CKBOARD);
                mvwaddch(win, qrY + y, qrX + x * 2 + 1, ACS_CKBOARD);
            } else {
                mvwaddch(win, qrY + y, qrX + x * 2, ' ');
                mvwaddch(win, qrY + y, qrX + x * 2 + 1, ' ');
            }
        }
    }
    
    mvwprintw(win, qrY + qrSize + 1, 2, "Scan to receive NGT to this address");
}

static void drawStakeForm(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "STAKE NGT");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "Wallet:      [Select wallet...]");
    mvwprintw(win, startY + 3, 2, "Amount:      [0.00000000 NGT]");
    mvwprintw(win, startY + 4, 2, "Duration:    [30 days]");
    mvwprintw(win, startY + 5, 2, "Est. Reward: [0.00000000 NGT]");
    
    mvwhline(win, startY + 6, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 7, 2, "STAKING TIERS:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 8, 2, "  30 days:  5%% APY");
    mvwprintw(win, startY + 9, 2, "  90 days:  8%% APY");
    mvwprintw(win, startY + 10, 2, " 180 days: 12%% APY");
    mvwprintw(win, startY + 11, 2, " 365 days: 18%% APY");
    
    mvwhline(win, startY + 12, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_DIM);
    mvwprintw(win, startY + 13, 2, "Currently Staked: 0.00000000 NGT");
    mvwprintw(win, startY + 14, 2, "Pending Rewards:  0.00000000 NGT");
    wattroff(win, A_DIM);
    
    mvwprintw(win, startY + 16, 2, "[GENESIS] Staking not available yet");
}

static void drawWalletMenu(WINDOW* win, int y, int width) {
    mvwhline(win, y, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, y + 1, 2, "[O]verview  [S]end  [R]eceive  [T]ransactions  S[t]ake  [N]ew Wallet  [B]ackup  [Q]uit");
    wattroff(win, A_BOLD);
}

static void drawNewWalletForm(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "CREATE NEW WALLET");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "Label:    [Enter wallet name...]");
    mvwprintw(win, startY + 3, 2, "Type:     [Standard / Hardware / Watch-Only]");
    mvwprintw(win, startY + 4, 2, "Password: [Enter encryption password...]");
    mvwprintw(win, startY + 5, 2, "Confirm:  [Confirm password...]");
    
    mvwhline(win, startY + 6, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 7, 2, "SECURITY OPTIONS:");
    wattroff(win, A_BOLD);
    
    mvwprintw(win, startY + 8, 2, "[ ] Enable 2FA");
    mvwprintw(win, startY + 9, 2, "[ ] Hardware key backup");
    mvwprintw(win, startY + 10, 2, "[X] Generate recovery phrase");
    
    mvwhline(win, startY + 11, 1, ACS_HLINE, width - 2);
    
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, startY + 12, 2, "WARNING: Write down your recovery phrase!");
    mvwprintw(win, startY + 13, 2, "If you lose it, you cannot recover your wallet.");
    wattroff(win, COLOR_PAIR(3));
}

static void drawBackupForm(WINDOW* win, int startY, int width) {
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "BACKUP WALLET");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "Select backup method:");
    mvwprintw(win, startY + 3, 2, "");
    mvwprintw(win, startY + 4, 2, "  [1] Export encrypted file");
    mvwprintw(win, startY + 5, 2, "  [2] Show recovery phrase");
    mvwprintw(win, startY + 6, 2, "  [3] Export to hardware wallet");
    mvwprintw(win, startY + 7, 2, "  [4] Generate paper wallet");
    
    mvwhline(win, startY + 8, 1, ACS_HLINE, width - 2);
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY + 9, 2, "RECOVERY PHRASE (24 words):");
    wattroff(win, A_BOLD);
    
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, startY + 10, 2, "[HIDDEN - Enter password to reveal]");
    wattroff(win, COLOR_PAIR(1));
    
    mvwhline(win, startY + 12, 1, ACS_HLINE, width - 2);
    
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, startY + 13, 2, "SECURITY: Never share your recovery phrase!");
    mvwprintw(win, startY + 14, 2, "Anyone with this phrase can access your funds.");
    wattroff(win, COLOR_PAIR(3));
}

static void drawTransactionDetails(WINDOW* win, int startY, int width) {
    if (walletState.transactions.empty()) return;
    
    const TransactionEntry& tx = walletState.transactions[walletState.selectedTransaction];
    
    wattron(win, A_BOLD);
    mvwprintw(win, startY, 2, "TRANSACTION DETAILS");
    wattroff(win, A_BOLD);
    
    mvwhline(win, startY + 1, 1, ACS_HLINE, width - 2);
    
    mvwprintw(win, startY + 2, 2, "TX ID:         %s", tx.txId.c_str());
    mvwprintw(win, startY + 3, 2, "Type:          %s", tx.type.c_str());
    mvwprintw(win, startY + 4, 2, "From:          %s", tx.fromAddress.c_str());
    mvwprintw(win, startY + 5, 2, "To:            %s", tx.toAddress.c_str());
    mvwprintw(win, startY + 6, 2, "Amount:        %s", formatNGT(tx.amount).c_str());
    mvwprintw(win, startY + 7, 2, "Fee:           %s", formatNGT(tx.fee).c_str());
    mvwprintw(win, startY + 8, 2, "Timestamp:     %s", formatTimestamp(tx.timestamp).c_str());
    mvwprintw(win, startY + 9, 2, "Confirmations: %u", tx.confirmations);
    mvwprintw(win, startY + 10, 2, "Status:        %s", tx.status.c_str());
    
    if (!tx.memo.empty()) {
        mvwprintw(win, startY + 11, 2, "Memo:          %s", tx.memo.c_str());
    }
}

namespace wallet_screen {

void drawScreen() {
    int maxY, maxX;
    getmaxyx(stdscr, maxY, maxX);
    
    WINDOW* win = newwin(maxY - 2, maxX - 2, 1, 1);
    box(win, 0, 0);
    
    drawWalletHeader(win, maxX - 2);
    
    if (walletState.currentView == "overview") {
        drawWalletList(win, 5, maxY / 2, maxX - 2);
        drawTransactionList(win, maxY / 2, maxY - 4, maxX - 2);
    } else if (walletState.currentView == "send") {
        drawSendForm(win, 5, maxX - 2);
    } else if (walletState.currentView == "receive") {
        drawReceiveForm(win, 5, maxX - 2);
    } else if (walletState.currentView == "transactions") {
        drawTransactionList(win, 5, maxY - 6, maxX - 2);
        if (!walletState.transactions.empty()) {
            drawTransactionDetails(win, maxY - 20, maxX - 2);
        }
    } else if (walletState.currentView == "stake") {
        drawStakeForm(win, 5, maxX - 2);
    } else if (walletState.currentView == "new") {
        drawNewWalletForm(win, 5, maxX - 2);
    } else if (walletState.currentView == "backup") {
        drawBackupForm(win, 5, maxX - 2);
    } else if (walletState.currentView == "details") {
        drawWalletDetails(win, 5, maxX - 2);
    }
    
    drawWalletMenu(win, maxY - 5, maxX - 2);
    
    wrefresh(win);
    delwin(win);
}

int handleInput(int ch) {
    switch (ch) {
        case 'o':
        case 'O':
            walletState.currentView = "overview";
            break;
        case 's':
        case 'S':
            walletState.currentView = "send";
            break;
        case 'r':
        case 'R':
            walletState.currentView = "receive";
            break;
        case 'x':
        case 'X':
            walletState.currentView = "transactions";
            break;
        case 't':
        case 'T':
            walletState.currentView = "stake";
            break;
        case 'n':
        case 'N':
            walletState.currentView = "new";
            break;
        case 'b':
        case 'B':
            walletState.currentView = "backup";
            break;
        case 'd':
        case 'D':
            walletState.currentView = "details";
            break;
        case KEY_UP:
            if (walletState.currentView == "overview" || walletState.currentView == "details") {
                if (walletState.selectedWallet > 0) {
                    walletState.selectedWallet--;
                    if (walletState.selectedWallet < walletState.scrollOffset) {
                        walletState.scrollOffset = walletState.selectedWallet;
                    }
                }
            } else if (walletState.currentView == "transactions") {
                if (walletState.selectedTransaction > 0) {
                    walletState.selectedTransaction--;
                    if (walletState.selectedTransaction < walletState.txScrollOffset) {
                        walletState.txScrollOffset = walletState.selectedTransaction;
                    }
                }
            }
            break;
        case KEY_DOWN:
            if (walletState.currentView == "overview" || walletState.currentView == "details") {
                if (walletState.selectedWallet < static_cast<int>(walletState.wallets.size()) - 1) {
                    walletState.selectedWallet++;
                }
            } else if (walletState.currentView == "transactions") {
                if (walletState.selectedTransaction < static_cast<int>(walletState.transactions.size()) - 1) {
                    walletState.selectedTransaction++;
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
    initWalletState();
}

}

}
}
