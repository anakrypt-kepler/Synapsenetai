#include "tui/tui.h"
#include <ncurses.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <functional>

namespace synapse {
namespace tui {

struct SettingItem {
    std::string key;
    std::string label;
    std::string value;
    std::string description;
    std::string category;
    bool editable;
    std::vector<std::string> options;
};

struct SettingsCategory {
    std::string name;
    std::string icon;
    std::vector<SettingItem> items;
};

class SettingsScreen {
private:
    WINDOW* mainWin;
    WINDOW* menuWin;
    WINDOW* contentWin;
    WINDOW* statusWin;
    int selectedCategory;
    int selectedItem;
    int scrollOffset;
    bool editMode;
    std::string editBuffer;
    std::vector<SettingsCategory> categories;
    std::map<std::string, std::string> pendingChanges;
    bool hasUnsavedChanges;

public:
    SettingsScreen() : mainWin(nullptr), menuWin(nullptr), contentWin(nullptr),
                       statusWin(nullptr), selectedCategory(0), selectedItem(0),
                       scrollOffset(0), editMode(false), hasUnsavedChanges(false) {
        initializeCategories();
    }

    ~SettingsScreen() {
        cleanup();
    }

    void initializeCategories() {
        SettingsCategory network;
        network.name = "Network";
        network.icon = "[NET]";
        network.items = {
            {"listen_port", "Listen Port", "9333", "TCP port for incoming connections", "network", true, {}},
            {"max_peers", "Max Peers", "125", "Maximum number of peer connections", "network", true, {}},
            {"min_peers", "Min Peers", "8", "Minimum peers before seeking more", "network", true, {}},
            {"upnp_enabled", "UPnP Enabled", "true", "Automatic port forwarding", "network", true, {"true", "false"}},
            {"nat_pmp_enabled", "NAT-PMP Enabled", "true", "Apple NAT port mapping", "network", true, {"true", "false"}},
            {"dns_seeds", "DNS Seeds", "enabled", "Use DNS for peer discovery", "network", true, {"enabled", "disabled"}},
            {"listen_ipv6", "IPv6 Enabled", "true", "Accept IPv6 connections", "network", true, {"true", "false"}},
            {"ban_time", "Ban Duration", "86400", "Seconds to ban misbehaving peers", "network", true, {}},
            {"timeout_connect", "Connect Timeout", "5000", "Connection timeout in ms", "network", true, {}},
            {"timeout_handshake", "Handshake Timeout", "10000", "Handshake timeout in ms", "network", true, {}},
            {"max_upload", "Max Upload KB/s", "0", "Upload limit (0=unlimited)", "network", true, {}},
            {"max_download", "Max Download KB/s", "0", "Download limit (0=unlimited)", "network", true, {}}
        };
        categories.push_back(network);

        SettingsCategory wallet;
        wallet.name = "Wallet";
        wallet.icon = "[WAL]";
        wallet.items = {
            {"wallet_file", "Wallet File", "wallet.dat", "Primary wallet filename", "wallet", true, {}},
            {"keypool_size", "Keypool Size", "100", "Pre-generated keys in pool", "wallet", true, {}},
            {"spend_confirm", "Spend Confirm", "true", "Confirm before spending", "wallet", true, {"true", "false"}},
            {"min_confirmations", "Min Confirms", "6", "Required confirmations", "wallet", true, {}},
            {"dust_threshold", "Dust Threshold", "0.00001", "Minimum output value", "wallet", true, {}},
            {"fee_rate", "Fee Rate", "0.0001", "Default fee per KB", "wallet", true, {}},
            {"rbf_enabled", "RBF Enabled", "true", "Replace-by-fee support", "wallet", true, {"true", "false"}},
            {"auto_backup", "Auto Backup", "true", "Automatic wallet backups", "wallet", true, {"true", "false"}},
            {"backup_interval", "Backup Interval", "3600", "Seconds between backups", "wallet", true, {}},
            {"encrypt_wallet", "Encryption", "enabled", "Wallet encryption status", "wallet", false, {}}
        };
        categories.push_back(wallet);

        SettingsCategory privacy;
        privacy.name = "Privacy";
        privacy.icon = "[PRV]";
        privacy.items = {
            {"privacy_mode", "Privacy Mode", "standard", "Privacy level", "privacy", true, {"standard", "high", "paranoid"}},
            {"tor_enabled", "Tor Enabled", "false", "Route through Tor network", "privacy", true, {"true", "false"}},
            {"tor_proxy", "Tor Proxy", "127.0.0.1:9050", "Tor SOCKS5 proxy address", "privacy", true, {}},
            {"i2p_enabled", "I2P Enabled", "false", "Route through I2P network", "privacy", true, {"true", "false"}},
            {"i2p_proxy", "I2P Proxy", "127.0.0.1:4447", "I2P SOCKS5 proxy address", "privacy", true, {}},
            {"onion_service", "Onion Service", "disabled", "Run as hidden service", "privacy", true, {"disabled", "enabled", "exclusive"}},
            {"stealth_addresses", "Stealth Addr", "true", "Use stealth addresses", "privacy", true, {"true", "false"}},
            {"dandelion", "Dandelion++", "true", "Transaction origin hiding", "privacy", true, {"true", "false"}},
            {"mix_network", "Mix Network", "false", "Route through mix nodes", "privacy", true, {"true", "false"}},
            {"decoy_traffic", "Decoy Traffic", "false", "Generate cover traffic", "privacy", true, {"true", "false"}},
            {"amnesia_mode", "Amnesia Mode", "false", "RAM-only operation", "privacy", true, {"true", "false"}},
            {"log_privacy", "Log Privacy", "standard", "Log anonymization level", "privacy", true, {"none", "standard", "paranoid"}}
        };
        categories.push_back(privacy);

        SettingsCategory security;
        security.name = "Security";
        security.icon = "[SEC]";
        security.items = {
            {"security_level", "Security Level", "standard", "Cryptographic security", "security", true, {"standard", "high", "paranoid"}},
            {"pqc_enabled", "PQC Enabled", "true", "Post-quantum cryptography", "security", true, {"true", "false"}},
            {"hybrid_crypto", "Hybrid Crypto", "true", "Classic + PQC combined", "security", true, {"true", "false"}},
            {"otp_enabled", "OTP Layer", "false", "One-time pad encryption", "security", true, {"true", "false"}},
            {"qkd_enabled", "QKD Ready", "false", "Quantum key distribution", "security", true, {"true", "false"}},
            {"key_rotation", "Key Rotation", "3600", "Seconds between rotations", "security", true, {}},
            {"session_timeout", "Session Timeout", "1800", "Idle timeout in seconds", "security", true, {}},
            {"max_auth_attempts", "Max Auth Tries", "3", "Failed attempts before lock", "security", true, {}},
            {"lockout_duration", "Lockout Time", "300", "Seconds after failed auth", "security", true, {}},
            {"require_passphrase", "Require Pass", "true", "Passphrase for operations", "security", true, {"true", "false"}},
            {"auto_lock", "Auto Lock", "true", "Lock wallet on idle", "security", true, {"true", "false"}},
            {"secure_memory", "Secure Memory", "true", "Lock sensitive memory", "security", true, {"true", "false"}}
        };
        categories.push_back(security);

        SettingsCategory model;
        model.name = "AI Model";
        model.icon = "[MDL]";
        model.items = {
            {"model_path", "Model Path", "~/.synapse/models", "Model storage directory", "model", true, {}},
            {"default_model", "Default Model", "llama-7b-q4", "Default inference model", "model", true, {}},
            {"max_context", "Max Context", "4096", "Maximum context length", "model", true, {}},
            {"gpu_layers", "GPU Layers", "0", "Layers to offload to GPU", "model", true, {}},
            {"threads", "CPU Threads", "4", "Threads for inference", "model", true, {}},
            {"batch_size", "Batch Size", "512", "Batch size for processing", "model", true, {}},
            {"temperature", "Temperature", "0.7", "Sampling temperature", "model", true, {}},
            {"top_p", "Top P", "0.9", "Nucleus sampling threshold", "model", true, {}},
            {"top_k", "Top K", "40", "Top-k sampling limit", "model", true, {}},
            {"repeat_penalty", "Repeat Penalty", "1.1", "Repetition penalty", "model", true, {}},
            {"model_access", "Access Mode", "private", "Default model access", "model", true, {"private", "shared", "public", "paid"}},
            {"inference_timeout", "Timeout", "60000", "Inference timeout in ms", "model", true, {}}
        };
        categories.push_back(model);

        SettingsCategory mining;
        mining.name = "Mining";
        mining.icon = "[MIN]";
        mining.items = {
            {"mining_enabled", "Mining Enabled", "false", "Enable mining/validation", "mining", true, {"true", "false"}},
            {"mining_threads", "Mining Threads", "1", "CPU threads for mining", "mining", true, {}},
            {"mining_intensity", "Intensity", "50", "Mining intensity (0-100)", "mining", true, {}},
            {"payout_address", "Payout Address", "", "Mining reward address", "mining", true, {}},
            {"pool_enabled", "Pool Mining", "false", "Connect to mining pool", "mining", true, {"true", "false"}},
            {"pool_url", "Pool URL", "", "Mining pool address", "mining", true, {}},
            {"pool_user", "Pool User", "", "Pool username/worker", "mining", true, {}},
            {"pool_pass", "Pool Pass", "", "Pool password", "mining", true, {}},
            {"auto_start", "Auto Start", "false", "Start mining on launch", "mining", true, {"true", "false"}},
            {"power_limit", "Power Limit", "100", "Power limit percentage", "mining", true, {}},
            {"temp_limit", "Temp Limit", "80", "Temperature limit (C)", "mining", true, {}},
            {"fan_speed", "Fan Speed", "auto", "Fan speed control", "mining", true, {"auto", "50", "75", "100"}}
        };
        categories.push_back(mining);

        SettingsCategory display;
        display.name = "Display";
        display.icon = "[DSP]";
        display.items = {
            {"color_scheme", "Color Scheme", "dark", "UI color theme", "display", true, {"dark", "light", "matrix", "amber"}},
            {"refresh_rate", "Refresh Rate", "1000", "UI refresh interval (ms)", "display", true, {}},
            {"show_logo", "Show Logo", "true", "Display ASCII logo", "display", true, {"true", "false"}},
            {"compact_mode", "Compact Mode", "false", "Compact UI layout", "display", true, {"true", "false"}},
            {"show_balance", "Show Balance", "true", "Display balance on dash", "display", true, {"true", "false"}},
            {"hide_amounts", "Hide Amounts", "false", "Mask transaction amounts", "display", true, {"true", "false"}},
            {"date_format", "Date Format", "YYYY-MM-DD", "Date display format", "display", true, {"YYYY-MM-DD", "DD/MM/YYYY", "MM/DD/YYYY"}},
            {"time_format", "Time Format", "24h", "Time display format", "display", true, {"12h", "24h"}},
            {"unicode_chars", "Unicode", "true", "Use unicode characters", "display", true, {"true", "false"}},
            {"animations", "Animations", "true", "Enable UI animations", "display", true, {"true", "false"}},
            {"sound_enabled", "Sound", "false", "Enable sound alerts", "display", true, {"true", "false"}},
            {"notifications", "Notifications", "true", "Show notifications", "display", true, {"true", "false"}}
        };
        categories.push_back(display);

        SettingsCategory advanced;
        advanced.name = "Advanced";
        advanced.icon = "[ADV]";
        advanced.items = {
            {"data_dir", "Data Directory", "~/.synapse", "Data storage location", "advanced", true, {}},
            {"debug_level", "Debug Level", "info", "Logging verbosity", "advanced", true, {"error", "warn", "info", "debug", "trace"}},
            {"log_file", "Log File", "synapse.log", "Log filename", "advanced", true, {}},
            {"max_log_size", "Max Log Size", "10485760", "Max log file size", "advanced", true, {}},
            {"db_cache", "DB Cache MB", "450", "Database cache size", "advanced", true, {}},
            {"mempool_size", "Mempool MB", "300", "Transaction pool size", "advanced", true, {}},
            {"prune_mode", "Prune Mode", "false", "Enable blockchain pruning", "advanced", true, {"true", "false"}},
            {"prune_target", "Prune Target", "550", "Prune target in MB", "advanced", true, {}},
            {"rpc_enabled", "RPC Enabled", "false", "Enable JSON-RPC server", "advanced", true, {"true", "false"}},
            {"rpc_port", "RPC Port", "9332", "JSON-RPC port", "advanced", true, {}},
            {"rpc_user", "RPC User", "", "RPC username", "advanced", true, {}},
            {"rpc_pass", "RPC Pass", "", "RPC password", "advanced", true, {}},
            {"testnet", "Testnet", "false", "Use test network", "advanced", true, {"true", "false"}},
            {"regtest", "Regtest", "false", "Regression test mode", "advanced", true, {"true", "false"}}
        };
        categories.push_back(advanced);
    }

    void initialize() {
        int maxY, maxX;
        getmaxyx(stdscr, maxY, maxX);

        mainWin = newwin(maxY - 2, maxX, 0, 0);
        menuWin = newwin(maxY - 4, 20, 1, 1);
        contentWin = newwin(maxY - 4, maxX - 23, 1, 22);
        statusWin = newwin(1, maxX, maxY - 1, 0);

        keypad(mainWin, TRUE);
        keypad(menuWin, TRUE);
        keypad(contentWin, TRUE);
    }

    void cleanup() {
        if (mainWin) delwin(mainWin);
        if (menuWin) delwin(menuWin);
        if (contentWin) delwin(contentWin);
        if (statusWin) delwin(statusWin);
        mainWin = menuWin = contentWin = statusWin = nullptr;
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

    void drawMenu() {
        werase(menuWin);
        drawBorder(menuWin, "Categories");

        int y = 2;
        for (size_t i = 0; i < categories.size(); i++) {
            if (static_cast<int>(i) == selectedCategory) {
                wattron(menuWin, A_REVERSE | COLOR_PAIR(3));
            }

            mvwprintw(menuWin, y, 2, "%s %s", 
                     categories[i].icon.c_str(),
                     categories[i].name.c_str());

            if (static_cast<int>(i) == selectedCategory) {
                wattroff(menuWin, A_REVERSE | COLOR_PAIR(3));
            }
            y++;
        }

        wrefresh(menuWin);
    }

    void drawContent() {
        werase(contentWin);
        drawBorder(contentWin, categories[selectedCategory].name + " Settings");

        int maxY, maxX;
        getmaxyx(contentWin, maxY, maxX);

        const auto& items = categories[selectedCategory].items;
        int visibleItems = maxY - 4;
        int y = 2;

        for (size_t i = scrollOffset; i < items.size() && y < maxY - 2; i++) {
            const auto& item = items[i];
            bool isSelected = (static_cast<int>(i) == selectedItem);

            if (isSelected) {
                wattron(contentWin, A_REVERSE);
            }

            std::string line = item.label;
            while (line.length() < 20) line += " ";
            line += ": ";

            if (editMode && isSelected) {
                line += editBuffer + "_";
            } else {
                line += item.value;
            }

            while (line.length() < static_cast<size_t>(maxX - 4)) line += " ";
            mvwprintw(contentWin, y, 2, "%s", line.substr(0, maxX - 4).c_str());

            if (isSelected) {
                wattroff(contentWin, A_REVERSE);
            }

            if (!item.editable) {
                wattron(contentWin, COLOR_PAIR(4));
                mvwprintw(contentWin, y, maxX - 10, "[locked]");
                wattroff(contentWin, COLOR_PAIR(4));
            }

            y++;
        }

        if (selectedItem >= 0 && selectedItem < static_cast<int>(items.size())) {
            wattron(contentWin, COLOR_PAIR(5));
            mvwprintw(contentWin, maxY - 2, 2, "%s", 
                     items[selectedItem].description.c_str());
            wattroff(contentWin, COLOR_PAIR(5));
        }

        wrefresh(contentWin);
    }

    void drawStatus() {
        werase(statusWin);
        wattron(statusWin, COLOR_PAIR(1));

        std::string status;
        if (editMode) {
            status = " [EDIT] Enter=Save | Esc=Cancel";
        } else {
            status = " [NAV] Arrows=Move | Enter=Edit | S=Save | R=Reset | Q=Back";
        }

        if (hasUnsavedChanges) {
            status += " | *UNSAVED*";
        }

        mvwprintw(statusWin, 0, 0, "%-*s", COLS, status.c_str());
        wattroff(statusWin, COLOR_PAIR(1));
        wrefresh(statusWin);
    }

    void handleInput(int ch) {
        if (editMode) {
            handleEditInput(ch);
            return;
        }

        switch (ch) {
            case KEY_UP:
            case 'k':
                if (selectedItem > 0) {
                    selectedItem--;
                    if (selectedItem < scrollOffset) {
                        scrollOffset = selectedItem;
                    }
                }
                break;

            case KEY_DOWN:
            case 'j':
                if (selectedItem < static_cast<int>(categories[selectedCategory].items.size()) - 1) {
                    selectedItem++;
                    int maxY, maxX;
                    getmaxyx(contentWin, maxY, maxX);
                    if (selectedItem >= scrollOffset + maxY - 4) {
                        scrollOffset++;
                    }
                }
                break;

            case KEY_LEFT:
            case 'h':
                if (selectedCategory > 0) {
                    selectedCategory--;
                    selectedItem = 0;
                    scrollOffset = 0;
                }
                break;

            case KEY_RIGHT:
            case 'l':
                if (selectedCategory < static_cast<int>(categories.size()) - 1) {
                    selectedCategory++;
                    selectedItem = 0;
                    scrollOffset = 0;
                }
                break;

            case '\n':
            case KEY_ENTER:
                startEdit();
                break;

            case 's':
            case 'S':
                saveSettings();
                break;

            case 'r':
            case 'R':
                resetToDefault();
                break;

            case ' ':
                toggleOption();
                break;
        }
    }

    void handleEditInput(int ch) {
        switch (ch) {
            case 27:
                editMode = false;
                editBuffer.clear();
                break;

            case '\n':
            case KEY_ENTER:
                applyEdit();
                break;

            case KEY_BACKSPACE:
            case 127:
            case 8:
                if (!editBuffer.empty()) {
                    editBuffer.pop_back();
                }
                break;

            default:
                if (ch >= 32 && ch < 127) {
                    editBuffer += static_cast<char>(ch);
                }
                break;
        }
    }

    void startEdit() {
        auto& item = categories[selectedCategory].items[selectedItem];
        if (!item.editable) return;

        if (!item.options.empty()) {
            toggleOption();
        } else {
            editMode = true;
            editBuffer = item.value;
        }
    }

    void applyEdit() {
        auto& item = categories[selectedCategory].items[selectedItem];
        if (editBuffer != item.value) {
            pendingChanges[item.key] = editBuffer;
            item.value = editBuffer;
            hasUnsavedChanges = true;
        }
        editMode = false;
        editBuffer.clear();
    }

    void toggleOption() {
        auto& item = categories[selectedCategory].items[selectedItem];
        if (item.options.empty()) return;

        auto it = std::find(item.options.begin(), item.options.end(), item.value);
        if (it != item.options.end()) {
            ++it;
            if (it == item.options.end()) {
                it = item.options.begin();
            }
            pendingChanges[item.key] = *it;
            item.value = *it;
            hasUnsavedChanges = true;
        }
    }

    void saveSettings() {
        if (pendingChanges.empty()) return;

        for (const auto& change : pendingChanges) {
            writeConfigValue(change.first, change.second);
        }

        pendingChanges.clear();
        hasUnsavedChanges = false;
        showMessage("Settings saved successfully");
    }

    void resetToDefault() {
        auto& item = categories[selectedCategory].items[selectedItem];
        std::string defaultValue = getDefaultValue(item.key);
        if (item.value != defaultValue) {
            pendingChanges[item.key] = defaultValue;
            item.value = defaultValue;
            hasUnsavedChanges = true;
        }
    }

    std::string getDefaultValue(const std::string& key) {
        static std::map<std::string, std::string> defaults = {
            {"listen_port", "9333"},
            {"max_peers", "125"},
            {"min_peers", "8"},
            {"upnp_enabled", "true"},
            {"privacy_mode", "standard"},
            {"security_level", "standard"},
            {"color_scheme", "dark"},
            {"debug_level", "info"}
        };

        auto it = defaults.find(key);
        return (it != defaults.end()) ? it->second : "";
    }

    void writeConfigValue(const std::string& key, const std::string& value) {
    }

    void showMessage(const std::string& msg) {
        werase(statusWin);
        wattron(statusWin, COLOR_PAIR(3));
        mvwprintw(statusWin, 0, 0, " %s", msg.c_str());
        wattroff(statusWin, COLOR_PAIR(3));
        wrefresh(statusWin);
    }

    void run() {
        initialize();
        bool running = true;

        while (running) {
            drawMenu();
            drawContent();
            drawStatus();

            int ch = wgetch(mainWin);
            if (ch == 'q' || ch == 'Q') {
                if (hasUnsavedChanges) {
                    if (confirmDiscard()) {
                        running = false;
                    }
                } else {
                    running = false;
                }
            } else {
                handleInput(ch);
            }
        }

        cleanup();
    }

    bool confirmDiscard() {
        WINDOW* dialog = newwin(7, 50, LINES/2 - 3, COLS/2 - 25);
        box(dialog, 0, 0);
        mvwprintw(dialog, 0, 2, " Unsaved Changes ");
        mvwprintw(dialog, 2, 2, "You have unsaved changes.");
        mvwprintw(dialog, 3, 2, "Discard changes and exit?");
        mvwprintw(dialog, 5, 2, "[Y] Yes  [N] No  [S] Save & Exit");
        wrefresh(dialog);

        int ch = wgetch(dialog);
        delwin(dialog);

        if (ch == 'y' || ch == 'Y') {
            return true;
        } else if (ch == 's' || ch == 'S') {
            saveSettings();
            return true;
        }
        return false;
    }
};

class ProfileManager {
private:
    std::vector<std::string> profiles;
    std::string activeProfile;
    std::map<std::string, std::map<std::string, std::string>> profileData;

public:
    ProfileManager() : activeProfile("default") {
        loadProfiles();
    }

    void loadProfiles() {
        profiles = {"default", "privacy", "performance", "minimal"};
    }

    void saveProfile(const std::string& name) {
        profiles.push_back(name);
    }

    void deleteProfile(const std::string& name) {
        if (name == "default") return;
        auto it = std::find(profiles.begin(), profiles.end(), name);
        if (it != profiles.end()) {
            profiles.erase(it);
        }
    }

    void switchProfile(const std::string& name) {
        auto it = std::find(profiles.begin(), profiles.end(), name);
        if (it != profiles.end()) {
            activeProfile = name;
            applyProfile(name);
        }
    }

    void applyProfile(const std::string& name) {
        if (name == "privacy") {
            applyPrivacyProfile();
        } else if (name == "performance") {
            applyPerformanceProfile();
        } else if (name == "minimal") {
            applyMinimalProfile();
        }
    }

    void applyPrivacyProfile() {
    }

    void applyPerformanceProfile() {
    }

    void applyMinimalProfile() {
    }

    const std::vector<std::string>& getProfiles() const {
        return profiles;
    }

    const std::string& getActiveProfile() const {
        return activeProfile;
    }
};

class ImportExportManager {
private:
    std::string exportPath;
    std::string importPath;

public:
    bool exportSettings(const std::string& path) {
        std::stringstream ss;
        ss << "# SynapseNet Configuration Export\n";
        ss << "# Generated: " << getCurrentTimestamp() << "\n\n";

        return writeToFile(path, ss.str());
    }

    bool importSettings(const std::string& path) {
        std::string content = readFromFile(path);
        if (content.empty()) return false;

        return parseAndApply(content);
    }

    std::string getCurrentTimestamp() {
        time_t now = time(nullptr);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        return std::string(buf);
    }

    bool writeToFile(const std::string& path, const std::string& content) {
        return true;
    }

    std::string readFromFile(const std::string& path) {
        return "";
    }

    bool parseAndApply(const std::string& content) {
        return true;
    }
};

class KeybindManager {
private:
    std::map<int, std::string> keybinds;
    std::map<std::string, std::function<void()>> actions;

public:
    KeybindManager() {
        initializeDefaults();
    }

    void initializeDefaults() {
        keybinds[KEY_UP] = "navigate_up";
        keybinds[KEY_DOWN] = "navigate_down";
        keybinds[KEY_LEFT] = "navigate_left";
        keybinds[KEY_RIGHT] = "navigate_right";
        keybinds['\n'] = "select";
        keybinds['q'] = "quit";
        keybinds['s'] = "save";
        keybinds['r'] = "reset";
        keybinds['h'] = "help";
        keybinds['/'] = "search";
        keybinds['1'] = "tab_1";
        keybinds['2'] = "tab_2";
        keybinds['3'] = "tab_3";
        keybinds['4'] = "tab_4";
        keybinds['5'] = "tab_5";
        keybinds['6'] = "tab_6";
        keybinds['7'] = "tab_7";
        keybinds['8'] = "tab_8";
    }

    void setKeybind(int key, const std::string& action) {
        keybinds[key] = action;
    }

    std::string getAction(int key) {
        auto it = keybinds.find(key);
        return (it != keybinds.end()) ? it->second : "";
    }

    void registerAction(const std::string& name, std::function<void()> func) {
        actions[name] = func;
    }

    void executeAction(const std::string& name) {
        auto it = actions.find(name);
        if (it != actions.end()) {
            it->second();
        }
    }

    void processKey(int key) {
        std::string action = getAction(key);
        if (!action.empty()) {
            executeAction(action);
        }
    }
};

class SearchFilter {
private:
    std::string searchQuery;
    std::vector<std::pair<int, int>> matchedItems;
    int currentMatch;

public:
    SearchFilter() : currentMatch(-1) {}

    void setQuery(const std::string& query) {
        searchQuery = query;
        matchedItems.clear();
        currentMatch = -1;
    }

    void search(const std::vector<SettingsCategory>& categories) {
        matchedItems.clear();
        std::string lowerQuery = toLower(searchQuery);

        for (size_t cat = 0; cat < categories.size(); cat++) {
            for (size_t item = 0; item < categories[cat].items.size(); item++) {
                const auto& setting = categories[cat].items[item];
                if (matches(setting, lowerQuery)) {
                    matchedItems.push_back({cat, item});
                }
            }
        }

        if (!matchedItems.empty()) {
            currentMatch = 0;
        }
    }

    bool matches(const SettingItem& item, const std::string& query) {
        return toLower(item.key).find(query) != std::string::npos ||
               toLower(item.label).find(query) != std::string::npos ||
               toLower(item.description).find(query) != std::string::npos;
    }

    std::string toLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    std::pair<int, int> getCurrentMatch() {
        if (currentMatch >= 0 && currentMatch < static_cast<int>(matchedItems.size())) {
            return matchedItems[currentMatch];
        }
        return {-1, -1};
    }

    void nextMatch() {
        if (!matchedItems.empty()) {
            currentMatch = (currentMatch + 1) % matchedItems.size();
        }
    }

    void prevMatch() {
        if (!matchedItems.empty()) {
            currentMatch = (currentMatch - 1 + matchedItems.size()) % matchedItems.size();
        }
    }

    int getMatchCount() const {
        return matchedItems.size();
    }

    void clear() {
        searchQuery.clear();
        matchedItems.clear();
        currentMatch = -1;
    }
};

class ValidationEngine {
private:
    std::map<std::string, std::function<bool(const std::string&)>> validators;

public:
    ValidationEngine() {
        initializeValidators();
    }

    void initializeValidators() {
        validators["port"] = [](const std::string& val) {
            try {
                int port = std::stoi(val);
                return port > 0 && port < 65536;
            } catch (...) {
                return false;
            }
        };

        validators["positive_int"] = [](const std::string& val) {
            try {
                return std::stoi(val) > 0;
            } catch (...) {
                return false;
            }
        };

        validators["non_negative_int"] = [](const std::string& val) {
            try {
                return std::stoi(val) >= 0;
            } catch (...) {
                return false;
            }
        };

        validators["float_0_1"] = [](const std::string& val) {
            try {
                float f = std::stof(val);
                return f >= 0.0f && f <= 1.0f;
            } catch (...) {
                return false;
            }
        };

        validators["boolean"] = [](const std::string& val) {
            return val == "true" || val == "false";
        };

        validators["ip_address"] = [](const std::string& val) {
            int dots = 0;
            for (char c : val) {
                if (c == '.') dots++;
                else if (!isdigit(c) && c != ':') return false;
            }
            return dots == 3 || val.find(':') != std::string::npos;
        };

        validators["path"] = [](const std::string& val) {
            return !val.empty() && val[0] != ' ';
        };
    }

    bool validate(const std::string& type, const std::string& value) {
        auto it = validators.find(type);
        if (it != validators.end()) {
            return it->second(value);
        }
        return true;
    }

    void addValidator(const std::string& type, std::function<bool(const std::string&)> func) {
        validators[type] = func;
    }
};

class ThemeManager {
private:
    std::string currentTheme;
    std::map<std::string, std::map<std::string, int>> themes;

public:
    ThemeManager() : currentTheme("dark") {
        initializeThemes();
    }

    void initializeThemes() {
        themes["dark"] = {
            {"background", COLOR_BLACK},
            {"foreground", COLOR_WHITE},
            {"accent", COLOR_CYAN},
            {"highlight", COLOR_YELLOW},
            {"error", COLOR_RED},
            {"success", COLOR_GREEN},
            {"warning", COLOR_YELLOW},
            {"border", COLOR_BLUE}
        };

        themes["light"] = {
            {"background", COLOR_WHITE},
            {"foreground", COLOR_BLACK},
            {"accent", COLOR_BLUE},
            {"highlight", COLOR_MAGENTA},
            {"error", COLOR_RED},
            {"success", COLOR_GREEN},
            {"warning", COLOR_YELLOW},
            {"border", COLOR_CYAN}
        };

        themes["matrix"] = {
            {"background", COLOR_BLACK},
            {"foreground", COLOR_GREEN},
            {"accent", COLOR_GREEN},
            {"highlight", COLOR_WHITE},
            {"error", COLOR_RED},
            {"success", COLOR_GREEN},
            {"warning", COLOR_YELLOW},
            {"border", COLOR_GREEN}
        };

        themes["amber"] = {
            {"background", COLOR_BLACK},
            {"foreground", COLOR_YELLOW},
            {"accent", COLOR_YELLOW},
            {"highlight", COLOR_WHITE},
            {"error", COLOR_RED},
            {"success", COLOR_GREEN},
            {"warning", COLOR_YELLOW},
            {"border", COLOR_YELLOW}
        };
    }

    void setTheme(const std::string& name) {
        if (themes.find(name) != themes.end()) {
            currentTheme = name;
            applyTheme();
        }
    }

    void applyTheme() {
        const auto& theme = themes[currentTheme];
        init_pair(1, theme.at("foreground"), theme.at("background"));
        init_pair(2, theme.at("border"), theme.at("background"));
        init_pair(3, theme.at("accent"), theme.at("background"));
        init_pair(4, theme.at("error"), theme.at("background"));
        init_pair(5, theme.at("success"), theme.at("background"));
        init_pair(6, theme.at("warning"), theme.at("background"));
        init_pair(7, theme.at("highlight"), theme.at("background"));
    }

    int getColor(const std::string& name) {
        const auto& theme = themes[currentTheme];
        auto it = theme.find(name);
        return (it != theme.end()) ? it->second : COLOR_WHITE;
    }

    const std::string& getCurrentTheme() const {
        return currentTheme;
    }

    std::vector<std::string> getAvailableThemes() const {
        std::vector<std::string> result;
        for (const auto& pair : themes) {
            result.push_back(pair.first);
        }
        return result;
    }
};

class HelpSystem {
private:
    std::map<std::string, std::string> helpTexts;
    std::map<std::string, std::vector<std::string>> contextHelp;

public:
    HelpSystem() {
        initializeHelp();
    }

    void initializeHelp() {
        helpTexts["navigation"] = 
            "Use arrow keys or hjkl to navigate.\n"
            "Press Enter to edit a setting.\n"
            "Press Space to toggle options.\n"
            "Press S to save changes.\n"
            "Press Q to exit.";

        helpTexts["editing"] = 
            "Type to modify the value.\n"
            "Press Enter to confirm.\n"
            "Press Escape to cancel.\n"
            "Press Backspace to delete.";

        helpTexts["search"] = 
            "Press / to start searching.\n"
            "Type your search query.\n"
            "Press Enter to find.\n"
            "Press N for next match.\n"
            "Press P for previous match.";

        contextHelp["network"] = {
            "Network settings control how your node connects to peers.",
            "Adjust ports, limits, and discovery options here.",
            "UPnP and NAT-PMP help with automatic port forwarding."
        };

        contextHelp["wallet"] = {
            "Wallet settings manage your funds and keys.",
            "Set confirmation requirements and fee rates.",
            "Enable automatic backups for safety."
        };

        contextHelp["privacy"] = {
            "Privacy settings control anonymity features.",
            "Enable Tor or I2P for network privacy.",
            "Use stealth addresses and Dandelion++ for transaction privacy."
        };

        contextHelp["security"] = {
            "Security settings control cryptographic protection.",
            "Enable post-quantum cryptography for future-proofing.",
            "Set session timeouts and authentication requirements."
        };

        contextHelp["model"] = {
            "AI Model settings configure local inference.",
            "Set model paths, context sizes, and sampling parameters.",
            "Adjust GPU offloading and thread counts for performance."
        };

        contextHelp["mining"] = {
            "Mining settings control validation and rewards.",
            "Set thread counts and intensity levels.",
            "Configure pool mining if desired."
        };

        contextHelp["display"] = {
            "Display settings customize the interface.",
            "Choose color schemes and refresh rates.",
            "Toggle animations and notifications."
        };

        contextHelp["advanced"] = {
            "Advanced settings for power users.",
            "Configure data directories and logging.",
            "Enable RPC server and test networks."
        };
    }

    std::string getHelp(const std::string& topic) {
        auto it = helpTexts.find(topic);
        return (it != helpTexts.end()) ? it->second : "";
    }

    std::vector<std::string> getContextHelp(const std::string& category) {
        auto it = contextHelp.find(category);
        return (it != contextHelp.end()) ? it->second : std::vector<std::string>();
    }

    void showHelpDialog(WINDOW* parent) {
        int maxY, maxX;
        getmaxyx(parent, maxY, maxX);

        WINDOW* helpWin = newwin(maxY - 4, maxX - 4, 2, 2);
        box(helpWin, 0, 0);
        mvwprintw(helpWin, 0, 2, " Help ");

        int y = 2;
        mvwprintw(helpWin, y++, 2, "KEYBOARD SHORTCUTS");
        mvwprintw(helpWin, y++, 2, "==================");
        y++;
        mvwprintw(helpWin, y++, 2, "Navigation:");
        mvwprintw(helpWin, y++, 4, "Up/Down/k/j    - Move selection");
        mvwprintw(helpWin, y++, 4, "Left/Right/h/l - Change category");
        mvwprintw(helpWin, y++, 4, "PgUp/PgDn      - Scroll page");
        mvwprintw(helpWin, y++, 4, "Home/End       - Jump to start/end");
        y++;
        mvwprintw(helpWin, y++, 2, "Actions:");
        mvwprintw(helpWin, y++, 4, "Enter          - Edit setting");
        mvwprintw(helpWin, y++, 4, "Space          - Toggle option");
        mvwprintw(helpWin, y++, 4, "S              - Save changes");
        mvwprintw(helpWin, y++, 4, "R              - Reset to default");
        mvwprintw(helpWin, y++, 4, "/              - Search settings");
        mvwprintw(helpWin, y++, 4, "?              - Show this help");
        mvwprintw(helpWin, y++, 4, "Q              - Exit settings");

        mvwprintw(helpWin, maxY - 6, 2, "Press any key to close...");
        wrefresh(helpWin);
        wgetch(helpWin);
        delwin(helpWin);
    }
};

class ChangeHistory {
private:
    struct Change {
        std::string key;
        std::string oldValue;
        std::string newValue;
        time_t timestamp;
    };

    std::vector<Change> history;
    int currentIndex;
    static const int MAX_HISTORY = 100;

public:
    ChangeHistory() : currentIndex(-1) {}

    void recordChange(const std::string& key, const std::string& oldVal, const std::string& newVal) {
        if (currentIndex < static_cast<int>(history.size()) - 1) {
            history.erase(history.begin() + currentIndex + 1, history.end());
        }

        Change change;
        change.key = key;
        change.oldValue = oldVal;
        change.newValue = newVal;
        change.timestamp = time(nullptr);

        history.push_back(change);
        currentIndex = history.size() - 1;

        if (history.size() > MAX_HISTORY) {
            history.erase(history.begin());
            currentIndex--;
        }
    }

    bool canUndo() const {
        return currentIndex >= 0;
    }

    bool canRedo() const {
        return currentIndex < static_cast<int>(history.size()) - 1;
    }

    std::pair<std::string, std::string> undo() {
        if (!canUndo()) return {"", ""};

        const Change& change = history[currentIndex];
        currentIndex--;
        return {change.key, change.oldValue};
    }

    std::pair<std::string, std::string> redo() {
        if (!canRedo()) return {"", ""};

        currentIndex++;
        const Change& change = history[currentIndex];
        return {change.key, change.newValue};
    }

    void clear() {
        history.clear();
        currentIndex = -1;
    }

    std::vector<std::string> getRecentChanges(int count) {
        std::vector<std::string> result;
        int start = std::max(0, static_cast<int>(history.size()) - count);

        for (int i = start; i < static_cast<int>(history.size()); i++) {
            const Change& c = history[i];
            char buf[256];
            strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&c.timestamp));
            result.push_back(std::string(buf) + " " + c.key + ": " + c.oldValue + " -> " + c.newValue);
        }

        return result;
    }
};

class ConfigFileManager {
private:
    std::string configPath;
    std::map<std::string, std::string> configValues;
    bool isDirty;

public:
    ConfigFileManager() : isDirty(false) {
        configPath = getDefaultConfigPath();
    }

    std::string getDefaultConfigPath() {
        return "~/.synapse/synapse.conf";
    }

    bool load() {
        return true;
    }

    bool save() {
        if (!isDirty) return true;
        isDirty = false;
        return true;
    }

    std::string getValue(const std::string& key, const std::string& defaultVal = "") {
        auto it = configValues.find(key);
        return (it != configValues.end()) ? it->second : defaultVal;
    }

    void setValue(const std::string& key, const std::string& value) {
        configValues[key] = value;
        isDirty = true;
    }

    bool hasKey(const std::string& key) const {
        return configValues.find(key) != configValues.end();
    }

    void removeKey(const std::string& key) {
        configValues.erase(key);
        isDirty = true;
    }

    std::vector<std::string> getAllKeys() const {
        std::vector<std::string> keys;
        for (const auto& pair : configValues) {
            keys.push_back(pair.first);
        }
        return keys;
    }

    bool isDirtyState() const {
        return isDirty;
    }

    void setConfigPath(const std::string& path) {
        configPath = path;
    }

    const std::string& getConfigPath() const {
        return configPath;
    }
};

class NotificationManager {
private:
    struct Notification {
        std::string message;
        int type;
        time_t timestamp;
        int duration;
    };

    std::vector<Notification> notifications;
    static const int TYPE_INFO = 0;
    static const int TYPE_SUCCESS = 1;
    static const int TYPE_WARNING = 2;
    static const int TYPE_ERROR = 3;

public:
    void addInfo(const std::string& msg, int duration = 3) {
        add(msg, TYPE_INFO, duration);
    }

    void addSuccess(const std::string& msg, int duration = 3) {
        add(msg, TYPE_SUCCESS, duration);
    }

    void addWarning(const std::string& msg, int duration = 5) {
        add(msg, TYPE_WARNING, duration);
    }

    void addError(const std::string& msg, int duration = 10) {
        add(msg, TYPE_ERROR, duration);
    }

    void add(const std::string& msg, int type, int duration) {
        Notification n;
        n.message = msg;
        n.type = type;
        n.timestamp = time(nullptr);
        n.duration = duration;
        notifications.push_back(n);
    }

    void update() {
        time_t now = time(nullptr);
        notifications.erase(
            std::remove_if(notifications.begin(), notifications.end(),
                [now](const Notification& n) {
                    return (now - n.timestamp) > n.duration;
                }),
            notifications.end()
        );
    }

    void draw(WINDOW* win, int y, int x) {
        update();
        for (const auto& n : notifications) {
            int colorPair = 1;
            switch (n.type) {
                case TYPE_SUCCESS: colorPair = 5; break;
                case TYPE_WARNING: colorPair = 6; break;
                case TYPE_ERROR: colorPair = 4; break;
            }
            wattron(win, COLOR_PAIR(colorPair));
            mvwprintw(win, y++, x, "%s", n.message.c_str());
            wattroff(win, COLOR_PAIR(colorPair));
        }
    }

    bool hasNotifications() const {
        return !notifications.empty();
    }

    void clear() {
        notifications.clear();
    }
};

void showSettingsScreen() {
    SettingsScreen screen;
    screen.run();
}

void showQuickSettings(WINDOW* parent) {
    int maxY, maxX;
    getmaxyx(parent, maxY, maxX);

    WINDOW* quickWin = newwin(15, 50, maxY/2 - 7, maxX/2 - 25);
    box(quickWin, 0, 0);
    mvwprintw(quickWin, 0, 2, " Quick Settings ");

    int y = 2;
    mvwprintw(quickWin, y++, 2, "[1] Toggle Privacy Mode");
    mvwprintw(quickWin, y++, 2, "[2] Toggle Tor");
    mvwprintw(quickWin, y++, 2, "[3] Toggle Mining");
    mvwprintw(quickWin, y++, 2, "[4] Change Theme");
    mvwprintw(quickWin, y++, 2, "[5] Network Settings");
    mvwprintw(quickWin, y++, 2, "[6] Security Level");
    y++;
    mvwprintw(quickWin, y++, 2, "[S] Full Settings");
    mvwprintw(quickWin, y++, 2, "[Q] Close");

    wrefresh(quickWin);

    bool running = true;
    while (running) {
        int ch = wgetch(quickWin);
        switch (ch) {
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
                break;
            case 's':
            case 'S':
                delwin(quickWin);
                showSettingsScreen();
                return;
            case 'q':
            case 'Q':
            case 27:
                running = false;
                break;
        }
    }

    delwin(quickWin);
}

}
}
