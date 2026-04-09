#include "tui/tui.h"
#include <ncurses.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

namespace synapse {
namespace tui {

struct HelpTopic {
    std::string title;
    std::string shortcut;
    std::vector<std::string> content;
    std::vector<std::string> relatedTopics;
};

struct KeyBinding {
    std::string key;
    std::string action;
    std::string context;
};

class HelpScreen {
private:
    WINDOW* mainWin;
    WINDOW* topicWin;
    WINDOW* contentWin;
    WINDOW* statusWin;
    std::vector<HelpTopic> topics;
    std::vector<KeyBinding> keyBindings;
    int selectedTopic;
    int contentScroll;
    std::string searchQuery;
    std::vector<int> searchResults;
    int currentSearchResult;

public:
    HelpScreen() : mainWin(nullptr), topicWin(nullptr), contentWin(nullptr),
                   statusWin(nullptr), selectedTopic(0), contentScroll(0),
                   currentSearchResult(-1) {
        initializeTopics();
        initializeKeyBindings();
    }

    ~HelpScreen() {
        cleanup();
    }

    void initializeTopics() {
        topics.push_back({
            "Getting Started",
            "F1",
            {
                "Welcome to SynapseNet!",
                "",
                "SynapseNet is a decentralized knowledge network that combines",
                "blockchain technology with AI capabilities.",
                "",
                "FIRST STEPS:",
                "1. Create or restore a wallet",
                "2. Wait for the network to sync",
                "3. Start exploring the knowledge base",
                "",
                "Use the arrow keys to navigate between screens.",
                "Press 'h' or '?' at any time for context-sensitive help."
            },
            {"Wallet", "Network", "Navigation"}
        });

        topics.push_back({
            "Wallet",
            "F2",
            {
                "WALLET MANAGEMENT",
                "",
                "Your wallet stores your NGT tokens and manages your identity",
                "on the SynapseNet network.",
                "",
                "CREATING A WALLET:",
                "- Choose 'Create New Wallet' from the startup menu",
                "- Write down your 12-word seed phrase",
                "- Set a strong passphrase",
                "",
                "RESTORING A WALLET:",
                "- Choose 'Restore Wallet' from the startup menu",
                "- Enter your 12-word seed phrase",
                "- Set a new passphrase",
                "",
                "SECURITY TIPS:",
                "- Never share your seed phrase",
                "- Use a strong, unique passphrase",
                "- Enable encryption for maximum security"
            },
            {"Security", "Transactions"}
        });

        topics.push_back({
            "Network",
            "F3",
            {
                "NETWORK OVERVIEW",
                "",
                "SynapseNet uses a peer-to-peer network for communication.",
                "",
                "PEER CONNECTIONS:",
                "- Outbound: Connections you initiate",
                "- Inbound: Connections from other nodes",
                "- Maximum peers: 125 (configurable)",
                "",
                "SYNC STATUS:",
                "- Syncing: Downloading blockchain data",
                "- Synced: Up to date with the network",
                "- Stale: Connection issues detected",
                "",
                "BANDWIDTH:",
                "- Upload and download rates are shown",
                "- Limits can be set in Settings"
            },
            {"Peers", "Settings"}
        });

        topics.push_back({
            "Knowledge",
            "F4",
            {
                "KNOWLEDGE SYSTEM",
                "",
                "Submit and discover knowledge on the network.",
                "",
                "SUBMITTING KNOWLEDGE:",
                "1. Press 'S' to open submission form",
                "2. Enter title and content",
                "3. Select category and tags",
                "4. Stake required NGT (minimum 10)",
                "5. Submit for validation",
                "",
                "VALIDATION PROCESS:",
                "- 5 random validators review your submission",
                "- 3 of 5 must approve for acceptance",
                "- Rewards distributed based on quality score",
                "",
                "SEARCHING:",
                "- Press '/' to search",
                "- Filter by category, type, or tags",
                "- Sort by score, date, or views"
            },
            {"Validation", "Rewards"}
        });

        topics.push_back({
            "AI Models",
            "F5",
            {
                "LOCAL AI MODEL HOSTING",
                "",
                "Run AI models locally on your machine.",
                "",
                "LOADING A MODEL:",
                "1. Go to Model screen (press 'M')",
                "2. Select 'Load Model'",
                "3. Choose from available models",
                "4. Wait for model to load",
                "",
                "INFERENCE:",
                "- Type your query in the input box",
                "- Press Enter to generate response",
                "- Adjust temperature and other settings",
                "",
                "ACCESS MODES:",
                "- Private: Only you can use",
                "- Shared: Invite specific users",
                "- Public: Anyone can use",
                "- Paid: Charge NGT per query"
            },
            {"Marketplace", "Settings"}
        });

        topics.push_back({
            "Mining",
            "F6",
            {
                "MINING AND VALIDATION",
                "",
                "Earn NGT by validating knowledge submissions.",
                "",
                "REQUIREMENTS:",
                "- Minimum stake: 100 NGT",
                "- Active network connection",
                "- Sufficient hardware resources",
                "",
                "STARTING MINING:",
                "1. Go to Mining screen (press 'I')",
                "2. Configure mining settings",
                "3. Press 'Start' to begin",
                "",
                "REWARDS:",
                "- Validation rewards for correct votes",
                "- Penalties for incorrect or malicious votes",
                "- Bonus for consistent participation"
            },
            {"Rewards", "Staking"}
        });

        topics.push_back({
            "Privacy",
            "F7",
            {
                "PRIVACY FEATURES",
                "",
                "SynapseNet offers multiple privacy layers.",
                "",
                "PRIVACY MODES:",
                "- Standard: Basic privacy",
                "- High: Tor routing enabled",
                "- Paranoid: Maximum anonymity",
                "",
                "FEATURES:",
                "- Stealth addresses",
                "- Dandelion++ transaction routing",
                "- Mix network for AI queries",
                "- Cover traffic generation",
                "",
                "ENABLING PRIVACY:",
                "1. Go to Settings",
                "2. Select Privacy section",
                "3. Choose privacy mode",
                "4. Configure individual features"
            },
            {"Security", "Settings"}
        });

        topics.push_back({
            "Security",
            "F8",
            {
                "SECURITY FEATURES",
                "",
                "Multi-layer cryptographic protection.",
                "",
                "SECURITY LEVELS:",
                "- Standard: Classic cryptography",
                "- High: Hybrid classic + PQC",
                "- Paranoid: Full quantum security",
                "",
                "CRYPTOGRAPHIC LAYERS:",
                "- Layer 0: Ed25519 + X25519 + AES-256",
                "- Layer 1: Kyber + Dilithium (PQC)",
                "- Layer 2: SPHINCS+ (hash-based)",
                "- Layer 3: One-Time Pad (optional)",
                "- Layer 4: QKD ready (future)",
                "",
                "KEY MANAGEMENT:",
                "- Automatic key rotation",
                "- Secure key storage",
                "- Hardware wallet support (future)"
            },
            {"Privacy", "Wallet"}
        });

        topics.push_back({
            "Navigation",
            "F9",
            {
                "KEYBOARD NAVIGATION",
                "",
                "GLOBAL SHORTCUTS:",
                "  Tab       - Switch between panels",
                "  F1-F10    - Quick access to screens",
                "  ?         - Show help",
                "  q         - Quit / Go back",
                "  /         - Search",
                "",
                "NAVIGATION:",
                "  Up/Down   - Move selection",
                "  Left/Right- Switch tabs/categories",
                "  PgUp/PgDn - Scroll page",
                "  Home/End  - Jump to start/end",
                "  Enter     - Select / Confirm",
                "  Esc       - Cancel / Close",
                "",
                "VIM-STYLE (optional):",
                "  h/j/k/l   - Left/Down/Up/Right",
                "  gg        - Go to top",
                "  G         - Go to bottom"
            },
            {"Settings"}
        });

        topics.push_back({
            "Settings",
            "F10",
            {
                "CONFIGURATION",
                "",
                "Access settings with 'S' from main menu.",
                "",
                "CATEGORIES:",
                "- Network: Ports, peers, bandwidth",
                "- Wallet: Fees, confirmations, backup",
                "- Privacy: Tor, stealth, mixing",
                "- Security: Crypto level, timeouts",
                "- Model: Paths, threads, GPU",
                "- Mining: Intensity, pool settings",
                "- Display: Theme, refresh, animations",
                "- Advanced: Debug, RPC, testnet",
                "",
                "SAVING CHANGES:",
                "- Press 'S' to save",
                "- Press 'R' to reset to default",
                "- Changes take effect immediately"
            },
            {"Navigation"}
        });
    }

    void initializeKeyBindings() {
        keyBindings = {
            {"Tab", "Switch panel", "Global"},
            {"F1", "Help", "Global"},
            {"F2", "Wallet", "Global"},
            {"F3", "Network", "Global"},
            {"F4", "Knowledge", "Global"},
            {"F5", "Models", "Global"},
            {"F6", "Mining", "Global"},
            {"?", "Context help", "Global"},
            {"q", "Quit/Back", "Global"},
            {"/", "Search", "Global"},
            {"Esc", "Cancel", "Global"},
            {"Up/k", "Move up", "Lists"},
            {"Down/j", "Move down", "Lists"},
            {"Enter", "Select", "Lists"},
            {"PgUp", "Page up", "Lists"},
            {"PgDn", "Page down", "Lists"},
            {"Home", "Go to top", "Lists"},
            {"End", "Go to bottom", "Lists"},
            {"s", "Send", "Wallet"},
            {"r", "Receive", "Wallet"},
            {"S", "Submit", "Knowledge"},
            {"v", "Verify", "Knowledge"},
            {"L", "Load model", "Models"},
            {"U", "Unload model", "Models"},
            {"Space", "Start/Stop", "Mining"}
        };
    }

    void initialize() {
        int maxY, maxX;
        getmaxyx(stdscr, maxY, maxX);

        mainWin = newwin(maxY - 2, maxX, 0, 0);
        topicWin = newwin(maxY - 4, 25, 1, 1);
        contentWin = newwin(maxY - 4, maxX - 28, 1, 27);
        statusWin = newwin(1, maxX, maxY - 1, 0);

        keypad(mainWin, TRUE);
        keypad(topicWin, TRUE);
    }

    void cleanup() {
        if (mainWin) delwin(mainWin);
        if (topicWin) delwin(topicWin);
        if (contentWin) delwin(contentWin);
        if (statusWin) delwin(statusWin);
        mainWin = topicWin = contentWin = statusWin = nullptr;
    }

    void drawBorder(WINDOW* win, const std::string& title) {
        int maxY, maxX;
        getmaxyx(win, maxY, maxX);

        wattron(win, COLOR_PAIR(2));
        box(win, 0, 0);

        if (!title.empty()) {
            mvwprintw(win, 0, 2, " %s ", title.c_str());
        }
        wattroff(win, COLOR_PAIR(2));
    }

    void drawTopics() {
        werase(topicWin);
        drawBorder(topicWin, "Topics");

        int y = 1;
        for (size_t i = 0; i < topics.size() && y < getmaxy(topicWin) - 1; i++) {
            bool isSelected = (static_cast<int>(i) == selectedTopic);
            bool isSearchResult = std::find(searchResults.begin(), searchResults.end(), i) 
                                  != searchResults.end();

            if (isSelected) {
                wattron(topicWin, A_REVERSE);
            }
            if (isSearchResult) {
                wattron(topicWin, COLOR_PAIR(3));
            }

            std::string line = topics[i].shortcut + " " + topics[i].title;
            if (line.length() > 22) {
                line = line.substr(0, 19) + "...";
            }
            mvwprintw(topicWin, y, 1, " %-21s", line.c_str());

            if (isSelected) {
                wattroff(topicWin, A_REVERSE);
            }
            if (isSearchResult) {
                wattroff(topicWin, COLOR_PAIR(3));
            }

            y++;
        }

        wrefresh(topicWin);
    }

    void drawContent() {
        werase(contentWin);
        drawBorder(contentWin, topics[selectedTopic].title);

        int maxY, maxX;
        getmaxyx(contentWin, maxY, maxX);

        const auto& content = topics[selectedTopic].content;
        int y = 1;

        for (size_t i = contentScroll; i < content.size() && y < maxY - 3; i++) {
            std::string line = content[i];
            if (line.length() > static_cast<size_t>(maxX - 4)) {
                line = line.substr(0, maxX - 7) + "...";
            }

            if (!searchQuery.empty() && 
                line.find(searchQuery) != std::string::npos) {
                wattron(contentWin, COLOR_PAIR(3));
            }

            mvwprintw(contentWin, y, 2, "%s", line.c_str());

            if (!searchQuery.empty()) {
                wattroff(contentWin, COLOR_PAIR(3));
            }

            y++;
        }

        if (!topics[selectedTopic].relatedTopics.empty()) {
            y = maxY - 2;
            wattron(contentWin, COLOR_PAIR(5));
            std::string related = "Related: ";
            for (size_t i = 0; i < topics[selectedTopic].relatedTopics.size(); i++) {
                if (i > 0) related += ", ";
                related += topics[selectedTopic].relatedTopics[i];
            }
            mvwprintw(contentWin, y, 2, "%s", related.c_str());
            wattroff(contentWin, COLOR_PAIR(5));
        }

        wrefresh(contentWin);
    }

    void drawStatus() {
        werase(statusWin);
        wattron(statusWin, COLOR_PAIR(1));

        std::string status;
        if (!searchQuery.empty()) {
            status = " Search: " + searchQuery;
            if (!searchResults.empty()) {
                status += " (" + std::to_string(currentSearchResult + 1) + "/" +
                          std::to_string(searchResults.size()) + ")";
            }
            status += " | n=Next | N=Prev | Esc=Clear";
        } else {
            status = " [HELP] Arrows=Navigate | Enter=Select | /=Search | k=Keys | q=Close";
        }

        mvwprintw(statusWin, 0, 0, "%-*s", COLS, status.c_str());
        wattroff(statusWin, COLOR_PAIR(1));
        wrefresh(statusWin);
    }

    void handleInput(int ch) {
        switch (ch) {
            case KEY_UP:
            case 'k':
                if (selectedTopic > 0) {
                    selectedTopic--;
                    contentScroll = 0;
                }
                break;

            case KEY_DOWN:
            case 'j':
                if (selectedTopic < static_cast<int>(topics.size()) - 1) {
                    selectedTopic++;
                    contentScroll = 0;
                }
                break;

            case KEY_PPAGE:
                contentScroll = std::max(0, contentScroll - 10);
                break;

            case KEY_NPAGE:
                contentScroll = std::min(
                    static_cast<int>(topics[selectedTopic].content.size()) - 10,
                    contentScroll + 10);
                if (contentScroll < 0) contentScroll = 0;
                break;

            case '/':
                startSearch();
                break;

            case 'n':
                nextSearchResult();
                break;

            case 'N':
                prevSearchResult();
                break;

            case 27:
                clearSearch();
                break;

            case 'K':
                showKeyBindings();
                break;

            case KEY_F(1):
            case KEY_F(2):
            case KEY_F(3):
            case KEY_F(4):
            case KEY_F(5):
            case KEY_F(6):
            case KEY_F(7):
            case KEY_F(8):
            case KEY_F(9):
            case KEY_F(10):
                selectedTopic = ch - KEY_F(1);
                if (selectedTopic >= static_cast<int>(topics.size())) {
                    selectedTopic = topics.size() - 1;
                }
                contentScroll = 0;
                break;
        }
    }

    void startSearch() {
        echo();
        curs_set(1);

        char query[64];
        mvwgetnstr(statusWin, 0, 9, query, 63);

        noecho();
        curs_set(0);

        searchQuery = query;
        performSearch();
    }

    void performSearch() {
        searchResults.clear();
        currentSearchResult = -1;

        if (searchQuery.empty()) return;

        std::string lowerQuery = searchQuery;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

        for (size_t i = 0; i < topics.size(); i++) {
            std::string lowerTitle = topics[i].title;
            std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);

            if (lowerTitle.find(lowerQuery) != std::string::npos) {
                searchResults.push_back(i);
                continue;
            }

            for (const auto& line : topics[i].content) {
                std::string lowerLine = line;
                std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), ::tolower);
                if (lowerLine.find(lowerQuery) != std::string::npos) {
                    searchResults.push_back(i);
                    break;
                }
            }
        }

        if (!searchResults.empty()) {
            currentSearchResult = 0;
            selectedTopic = searchResults[0];
            contentScroll = 0;
        }
    }

    void nextSearchResult() {
        if (searchResults.empty()) return;
        currentSearchResult = (currentSearchResult + 1) % searchResults.size();
        selectedTopic = searchResults[currentSearchResult];
        contentScroll = 0;
    }

    void prevSearchResult() {
        if (searchResults.empty()) return;
        currentSearchResult = (currentSearchResult - 1 + searchResults.size()) % searchResults.size();
        selectedTopic = searchResults[currentSearchResult];
        contentScroll = 0;
    }

    void clearSearch() {
        searchQuery.clear();
        searchResults.clear();
        currentSearchResult = -1;
    }

    void showKeyBindings() {
        WINDOW* keysWin = newwin(LINES - 4, COLS - 4, 2, 2);
        box(keysWin, 0, 0);
        mvwprintw(keysWin, 0, 2, " Keyboard Shortcuts ");

        int y = 2;
        std::string currentContext;

        for (const auto& kb : keyBindings) {
            if (kb.context != currentContext) {
                if (!currentContext.empty()) y++;
                currentContext = kb.context;
                wattron(keysWin, A_BOLD);
                mvwprintw(keysWin, y++, 2, "%s:", currentContext.c_str());
                wattroff(keysWin, A_BOLD);
            }

            mvwprintw(keysWin, y, 4, "%-12s %s", kb.key.c_str(), kb.action.c_str());
            y++;

            if (y >= LINES - 8) {
                mvwprintw(keysWin, y, 2, "... (press any key to continue)");
                wrefresh(keysWin);
                wgetch(keysWin);
                werase(keysWin);
                box(keysWin, 0, 0);
                mvwprintw(keysWin, 0, 2, " Keyboard Shortcuts (continued) ");
                y = 2;
            }
        }

        mvwprintw(keysWin, LINES - 6, 2, "Press any key to close...");
        wrefresh(keysWin);
        wgetch(keysWin);
        delwin(keysWin);
    }

    void run() {
        initialize();
        bool running = true;

        while (running) {
            drawTopics();
            drawContent();
            drawStatus();

            int ch = wgetch(mainWin);
            if (ch == 'q' || ch == 'Q') {
                running = false;
            } else {
                handleInput(ch);
            }
        }

        cleanup();
    }
};

void showHelpScreen() {
    HelpScreen screen;
    screen.run();
}

void showContextHelp(const std::string& context) {
    HelpScreen screen;
    screen.run();
}

}
}
