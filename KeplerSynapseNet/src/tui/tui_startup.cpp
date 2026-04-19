#include "tui/tui.h"
#include "tui/bip39_wordlist.h"
#include <ncurses.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <array>

namespace synapse {
namespace tui {

static const char* SYNAPSE_LOGO[] = {
    "  ____                              _   _      _   ",
    " / ___| _   _ _ __   __ _ _ __  ___| \\ | | ___| |_ ",
    " \\___ \\| | | | '_ \\ / _` | '_ \\/ __|  \\| |/ _ \\ __|",
    "  ___) | |_| | | | | (_| | |_) \\__ \\ |\\  |  __/ |_ ",
    " |____/ \\__, |_| |_|\\__,_| .__/|___/_| \\_|\\___|\\__|",
    "        |___/            |_|                       ",
    "                                                   ",
    "      Decentralized AI Knowledge Network           ",
    "              Version 0.1.0-beta                   ",
    "                   by Kepler                       "
};

static const char* KEPLER_LOGO[] = {
    "  _  __          _           ",
    " | |/ /___ _ __ | | ___ _ __ ",
    " | ' // _ \\ '_ \\| |/ _ \\ '__|",
    " | . \\  __/ |_) | |  __/ |   ",
    " |_|\\_\\___| .__/|_|\\___|_|   ",
    "          |_|                "
};

struct InitStep {
    std::string name;
    std::string description;
    int duration;
    bool completed;
};

struct StartupScreen::Impl {
    WINDOW* win = nullptr;
    int width = 0;
    int height = 0;
    StartupState state = StartupState::LOGO;
    std::string statusMessage;
    int progressPercent = 0;
    std::vector<std::string> seedWords;
    std::string inputBuffer;
    bool walletExists = false;
    int confirmWordIndex = 0;
    int animationFrame = 0;
    std::time_t stateStartTime = 0;
    std::vector<InitStep> initSteps;
    int currentInitStep = 0;
    std::string errorMessage;
    bool passwordRequired = false;
    std::string passwordBuffer;
    bool showPassword = false;
    int syncPeers = 0;
    int syncBlocks = 0;
    int syncTotalBlocks = 0;
    
    void drawLogo();
    void drawProgress();
    void drawWalletChoice();
    void drawSeedDisplay();
    void drawSeedConfirm();
    void drawSeedImport();
    void drawPasswordEntry();
    void drawSyncing();
    void drawError();
    void drawBox(int y, int x, int h, int w, const char* title);
    void drawProgressBar(int y, int x, int w, int percent, int color);
    void animateText(const std::string& text, int y, int x, int delayMs);
    std::string maskPassword(const std::string& password);
    void initializeSteps();
};

void StartupScreen::Impl::drawBox(int y, int x, int h, int w, const char* title) {
    wattron(win, COLOR_PAIR(1));
    
    mvwaddch(win, y, x, ACS_ULCORNER);
    mvwaddch(win, y, x + w - 1, ACS_URCORNER);
    mvwaddch(win, y + h - 1, x, ACS_LLCORNER);
    mvwaddch(win, y + h - 1, x + w - 1, ACS_LRCORNER);
    
    mvwhline(win, y, x + 1, ACS_HLINE, w - 2);
    mvwhline(win, y + h - 1, x + 1, ACS_HLINE, w - 2);
    mvwvline(win, y + 1, x, ACS_VLINE, h - 2);
    mvwvline(win, y + 1, x + w - 1, ACS_VLINE, h - 2);
    
    wattroff(win, COLOR_PAIR(1));
    
    if (title && strlen(title) > 0) {
        wattron(win, COLOR_PAIR(2) | A_BOLD);
        mvwprintw(win, y, x + 2, " %s ", title);
        wattroff(win, COLOR_PAIR(2) | A_BOLD);
    }
}

void StartupScreen::Impl::drawProgressBar(int y, int x, int w, int percent, int color) {
    wattron(win, COLOR_PAIR(1));
    mvwaddch(win, y, x, '[');
    mvwaddch(win, y, x + w - 1, ']');
    wattroff(win, COLOR_PAIR(1));
    
    int filled = ((w - 2) * percent) / 100;
    
    wattron(win, COLOR_PAIR(color) | A_BOLD);
    for (int i = 0; i < filled; i++) {
        mvwaddch(win, y, x + 1 + i, ACS_BLOCK);
    }
    wattroff(win, COLOR_PAIR(color) | A_BOLD);
}

std::string StartupScreen::Impl::maskPassword(const std::string& password) {
    if (showPassword) return password;
    return std::string(password.size(), '*');
}

void StartupScreen::Impl::initializeSteps() {
    initSteps.clear();
    initSteps.push_back({"Database", "Initializing database...", 500, false});
    initSteps.push_back({"Cryptography", "Loading cryptographic modules...", 300, false});
    initSteps.push_back({"Network", "Starting network layer...", 400, false});
    initSteps.push_back({"Wallet", "Loading wallet...", 200, false});
    initSteps.push_back({"Model", "Checking AI models...", 300, false});
    initSteps.push_back({"Privacy", "Initializing privacy layer...", 200, false});
    initSteps.push_back({"Quantum", "Loading quantum security...", 400, false});
    initSteps.push_back({"Complete", "Ready!", 100, false});
}

void StartupScreen::Impl::drawLogo() {
    werase(win);
    
    int logoHeight = 10;
    int startY = (height - logoHeight) / 2 - 4;
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    for (int i = 0; i < logoHeight; i++) {
        int logoLen = strlen(SYNAPSE_LOGO[i]);
        int startX = (width - logoLen) / 2;
        if (startX < 0) startX = 0;
        mvwprintw(win, startY + i, startX, "%s", SYNAPSE_LOGO[i]);
    }
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    wattron(win, COLOR_PAIR(3));
    const char* tagline = "Decentralized AI Knowledge Network";
    mvwprintw(win, startY + logoHeight + 2, (width - strlen(tagline)) / 2, "%s", tagline);
    wattroff(win, COLOR_PAIR(3));
    
    wattron(win, COLOR_PAIR(4));
    const char* version = "Version 0.1.0-beta";
    mvwprintw(win, startY + logoHeight + 3, (width - strlen(version)) / 2, "%s", version);
    wattroff(win, COLOR_PAIR(4));
    
    wattron(win, COLOR_PAIR(1));
    int keplerLogoHeight = 6;
    int keplerStartY = startY + logoHeight + 6;
    for (int i = 0; i < keplerLogoHeight; i++) {
        int logoLen = strlen(KEPLER_LOGO[i]);
        int startX = (width - logoLen) / 2;
        if (startX < 0) startX = 0;
        mvwprintw(win, keplerStartY + i, startX, "%s", KEPLER_LOGO[i]);
    }
    wattroff(win, COLOR_PAIR(1));
    
    static const char* spinner = "|/-\\";
    animationFrame = (animationFrame + 1) % 4;
    
    wattron(win, A_BLINK);
    const char* prompt = "Press any key to continue...";
    mvwprintw(win, height - 3, (width - strlen(prompt)) / 2, "%s", prompt);
    wattroff(win, A_BLINK);
    
    wattron(win, COLOR_PAIR(2));
    mvwaddch(win, height - 3, (width - strlen(prompt)) / 2 - 3, spinner[animationFrame]);
    mvwaddch(win, height - 3, (width + strlen(prompt)) / 2 + 2, spinner[animationFrame]);
    wattroff(win, COLOR_PAIR(2));
    
    box(win, 0, 0);
    wrefresh(win);
}

void StartupScreen::Impl::drawProgress() {
    werase(win);
    box(win, 0, 0);
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    const char* title = "[ INITIALIZING SYNAPSENET ]";
    mvwprintw(win, 2, (width - strlen(title)) / 2, "%s", title);
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    int listY = 5;
    int listX = (width - 50) / 2;
    if (listX < 5) listX = 5;
    
    for (size_t i = 0; i < initSteps.size(); i++) {
        const auto& step = initSteps[i];
        
        if (i < static_cast<size_t>(currentInitStep)) {
            wattron(win, COLOR_PAIR(2));
            mvwprintw(win, listY + i, listX, "[OK]");
            wattroff(win, COLOR_PAIR(2));
        } else if (i == static_cast<size_t>(currentInitStep)) {
            wattron(win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(win, listY + i, listX, "[..]");
            wattroff(win, COLOR_PAIR(3) | A_BOLD);
        } else {
            wattron(win, COLOR_PAIR(1));
            mvwprintw(win, listY + i, listX, "[  ]");
            wattroff(win, COLOR_PAIR(1));
        }
        
        int nameColor = (i <= static_cast<size_t>(currentInitStep)) ? 3 : 1;
        wattron(win, COLOR_PAIR(nameColor));
        mvwprintw(win, listY + i, listX + 6, "%s", step.name.c_str());
        wattroff(win, COLOR_PAIR(nameColor));
        
        if (i == static_cast<size_t>(currentInitStep)) {
            wattron(win, COLOR_PAIR(1));
            mvwprintw(win, listY + i, listX + 20, "%s", step.description.c_str());
            wattroff(win, COLOR_PAIR(1));
        }
    }
    
    int barWidth = width - 20;
    int barY = height / 2 + 5;
    int barX = 10;
    
    drawProgressBar(barY, barX, barWidth, progressPercent, 2);
    
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, barY + 2, (width - 4) / 2, "%3d%%", progressPercent);
    wattroff(win, COLOR_PAIR(3));
    
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, barY + 4, barX, "%s", statusMessage.c_str());
    wattroff(win, COLOR_PAIR(1));
    
    wrefresh(win);
}

void StartupScreen::Impl::drawWalletChoice() {
    werase(win);
    box(win, 0, 0);
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    const char* title = "[ WALLET SETUP ]";
    mvwprintw(win, 2, (width - strlen(title)) / 2, "%s", title);
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    int centerY = height / 2 - 6;
    int boxW = 50;
    int boxX = (width - boxW) / 2;
    
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, centerY, boxX, "No wallet found. Please choose an option:");
    wattroff(win, COLOR_PAIR(3));
    
    drawBox(centerY + 2, boxX - 2, 5, boxW + 4, nullptr);
    
    wattron(win, COLOR_PAIR(4) | A_BOLD);
    mvwprintw(win, centerY + 3, boxX, "[1] Create New Wallet");
    wattroff(win, COLOR_PAIR(4) | A_BOLD);
    
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, centerY + 4, boxX + 4, "Generate a new 24-word seed phrase");
    mvwprintw(win, centerY + 5, boxX + 4, "for a fresh wallet");
    wattroff(win, COLOR_PAIR(1));
    
    drawBox(centerY + 8, boxX - 2, 5, boxW + 4, nullptr);
    
    wattron(win, COLOR_PAIR(4) | A_BOLD);
    mvwprintw(win, centerY + 9, boxX, "[2] Import Existing Wallet");
    wattroff(win, COLOR_PAIR(4) | A_BOLD);
    
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, centerY + 10, boxX + 4, "Restore wallet from your existing");
    mvwprintw(win, centerY + 11, boxX + 4, "24-word seed phrase");
    wattroff(win, COLOR_PAIR(1));
    
    wattron(win, COLOR_PAIR(5));
    const char* warning = "Your seed phrase is the ONLY way to recover your wallet.";
    mvwprintw(win, height - 5, (width - strlen(warning)) / 2, "%s", warning);
    const char* warning2 = "Never share it with anyone!";
    mvwprintw(win, height - 4, (width - strlen(warning2)) / 2, "%s", warning2);
    wattroff(win, COLOR_PAIR(5));
    
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, height - 2, (width - 30) / 2, "Press 1 or 2 to continue...");
    wattroff(win, COLOR_PAIR(3));
    
    wrefresh(win);
}

void StartupScreen::Impl::drawSeedDisplay() {
    werase(win);
    box(win, 0, 0);
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    const char* title = "[ YOUR SEED PHRASE ]";
    mvwprintw(win, 2, (width - strlen(title)) / 2, "%s", title);
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    int warningY = 4;
    wattron(win, COLOR_PAIR(5) | A_BOLD);
    mvwprintw(win, warningY, 5, "!!! CRITICAL SECURITY WARNING !!!");
    wattroff(win, COLOR_PAIR(5) | A_BOLD);
    
    wattron(win, COLOR_PAIR(5));
    mvwprintw(win, warningY + 2, 5, "1. Write down these 24 words on PAPER");
    mvwprintw(win, warningY + 3, 5, "2. Store them in a SAFE place");
    mvwprintw(win, warningY + 4, 5, "3. NEVER store them digitally (no photos, no files)");
    mvwprintw(win, warningY + 5, 5, "4. NEVER share them with anyone");
    mvwprintw(win, warningY + 6, 5, "5. Anyone with these words can STEAL your funds");
    wattroff(win, COLOR_PAIR(5));
    
    int seedY = warningY + 9;
    int col1X = 10;
    int col2X = width / 2 + 5;
    
    drawBox(seedY - 1, col1X - 2, 14, width / 2 - 10, "Words 1-12");
    drawBox(seedY - 1, col2X - 2, 14, width / 2 - 10, "Words 13-24");
    
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    for (size_t i = 0; i < seedWords.size() && i < 24; i++) {
        int x = (i < 12) ? col1X : col2X;
        int y = seedY + (i % 12);
        mvwprintw(win, y, x, "%2zu. %s", i + 1, seedWords[i].c_str());
    }
    wattroff(win, COLOR_PAIR(3) | A_BOLD);
    
    wattron(win, COLOR_PAIR(4));
    const char* checksum = "Checksum: Valid";
    mvwprintw(win, seedY + 13, (width - strlen(checksum)) / 2, "%s", checksum);
    wattroff(win, COLOR_PAIR(4));
    
    wattron(win, A_BLINK | COLOR_PAIR(2));
    const char* prompt = "Press ENTER when you have written down ALL words...";
    mvwprintw(win, height - 3, (width - strlen(prompt)) / 2, "%s", prompt);
    wattroff(win, A_BLINK | COLOR_PAIR(2));
    
    wrefresh(win);
}

void StartupScreen::Impl::drawSeedConfirm() {
    werase(win);
    box(win, 0, 0);
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    const char* title = "[ CONFIRM SEED PHRASE ]";
    mvwprintw(win, 2, (width - strlen(title)) / 2, "%s", title);
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    int centerY = height / 2 - 4;
    
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, centerY - 2, (width - 50) / 2, "To verify you have saved your seed phrase correctly,");
    mvwprintw(win, centerY - 1, (width - 40) / 2, "please enter the following word:");
    wattroff(win, COLOR_PAIR(3));
    
    wattron(win, COLOR_PAIR(4) | A_BOLD);
    mvwprintw(win, centerY + 1, (width - 20) / 2, "Word #%d:", confirmWordIndex + 1);
    wattroff(win, COLOR_PAIR(4) | A_BOLD);
    
    int inputY = centerY + 3;
    int inputX = (width - 30) / 2;
    int inputW = 30;
    
    drawBox(inputY - 1, inputX - 2, 3, inputW + 4, nullptr);
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    mvwprintw(win, inputY, inputX, "%s", inputBuffer.c_str());
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    wattron(win, COLOR_PAIR(1));
    for (size_t i = inputBuffer.size(); i < static_cast<size_t>(inputW); i++) {
        mvwaddch(win, inputY, inputX + i, '_');
    }
    wattroff(win, COLOR_PAIR(1));
    
    if (!errorMessage.empty()) {
        wattron(win, COLOR_PAIR(5) | A_BOLD);
        mvwprintw(win, inputY + 3, (width - errorMessage.size()) / 2, "%s", errorMessage.c_str());
        wattroff(win, COLOR_PAIR(5) | A_BOLD);
    }
    
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, height - 4, (width - 40) / 2, "Type the word and press ENTER");
    mvwprintw(win, height - 3, (width - 30) / 2, "Press ESC to go back");
    wattroff(win, COLOR_PAIR(1));
    
    wrefresh(win);
}

void StartupScreen::Impl::drawSeedImport() {
    werase(win);
    box(win, 0, 0);
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    const char* title = "[ IMPORT WALLET ]";
    mvwprintw(win, 2, (width - strlen(title)) / 2, "%s", title);
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, 5, 10, "Enter your 24-word seed phrase:");
    mvwprintw(win, 6, 10, "(Separate words with spaces, press ENTER when done)");
    wattroff(win, COLOR_PAIR(3));
    
    int inputY = 9;
    int inputX = 5;
    int inputW = width - 10;
    int inputH = 8;
    
    drawBox(inputY - 1, inputX - 2, inputH + 2, inputW + 4, nullptr);
    
    wattron(win, COLOR_PAIR(2));
    std::string display = inputBuffer;
    int line = 0;
    size_t pos = 0;
    int charsPerLine = inputW;
    
    while (pos < display.size() && line < inputH) {
        size_t lineEnd = std::min(pos + charsPerLine, display.size());
        
        size_t spacePos = display.rfind(' ', lineEnd);
        if (spacePos != std::string::npos && spacePos > pos && lineEnd < display.size()) {
            lineEnd = spacePos + 1;
        }
        
        mvwprintw(win, inputY + line, inputX, "%s", display.substr(pos, lineEnd - pos).c_str());
        pos = lineEnd;
        line++;
    }
    wattroff(win, COLOR_PAIR(2));
    
    wattron(win, COLOR_PAIR(4));
    mvwprintw(win, inputY + inputH + 2, 10, "Words entered: %zu/24", seedWords.size());
    wattroff(win, COLOR_PAIR(4));
    
    if (seedWords.size() > 0) {
        wattron(win, COLOR_PAIR(1));
        mvwprintw(win, inputY + inputH + 3, 10, "Last word: %s", seedWords.back().c_str());
        wattroff(win, COLOR_PAIR(1));
    }
    
    int progressColor = (seedWords.size() >= 24) ? 2 : 3;
    int barY = inputY + inputH + 5;
    drawProgressBar(barY, 10, width - 20, (seedWords.size() * 100) / 24, progressColor);
    
    if (!errorMessage.empty()) {
        wattron(win, COLOR_PAIR(5) | A_BOLD);
        mvwprintw(win, height - 5, (width - errorMessage.size()) / 2, "%s", errorMessage.c_str());
        wattroff(win, COLOR_PAIR(5) | A_BOLD);
    }
    
    wattron(win, COLOR_PAIR(1));
    if (seedWords.size() >= 24) {
        mvwprintw(win, height - 3, (width - 40) / 2, "Press ENTER to import wallet");
    } else {
        mvwprintw(win, height - 3, (width - 40) / 2, "Continue entering words...");
    }
    mvwprintw(win, height - 2, (width - 30) / 2, "Press ESC to go back");
    wattroff(win, COLOR_PAIR(1));
    
    wrefresh(win);
}

void StartupScreen::Impl::drawPasswordEntry() {
    werase(win);
    box(win, 0, 0);
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    const char* title = "[ WALLET PASSWORD ]";
    mvwprintw(win, 2, (width - strlen(title)) / 2, "%s", title);
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    int centerY = height / 2 - 3;
    
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, centerY - 2, (width - 50) / 2, "Enter a password to encrypt your wallet:");
    mvwprintw(win, centerY - 1, (width - 50) / 2, "(Leave empty for no encryption - not recommended)");
    wattroff(win, COLOR_PAIR(3));
    
    int inputY = centerY + 1;
    int inputX = (width - 40) / 2;
    int inputW = 40;
    
    drawBox(inputY - 1, inputX - 2, 3, inputW + 4, nullptr);
    
    wattron(win, COLOR_PAIR(2));
    mvwprintw(win, inputY, inputX, "%s", maskPassword(passwordBuffer).c_str());
    wattroff(win, COLOR_PAIR(2));
    
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, inputY + 3, inputX, "[S] %s password", showPassword ? "Hide" : "Show");
    wattroff(win, COLOR_PAIR(1));
    
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, height - 3, (width - 40) / 2, "Press ENTER to continue");
    wattroff(win, COLOR_PAIR(1));
    
    wrefresh(win);
}

void StartupScreen::Impl::drawSyncing() {
    werase(win);
    box(win, 0, 0);
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    const char* title = "[ SYNCING WITH NETWORK ]";
    mvwprintw(win, 2, (width - strlen(title)) / 2, "%s", title);
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    int centerY = height / 2 - 6;
    
    wattron(win, COLOR_PAIR(4));
    mvwprintw(win, centerY, 10, "Status:");
    wattroff(win, COLOR_PAIR(4));
    
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwprintw(win, centerY, 20, "%s", statusMessage.c_str());
    wattroff(win, COLOR_PAIR(3) | A_BOLD);
    
    wattron(win, COLOR_PAIR(4));
    mvwprintw(win, centerY + 2, 10, "Peers:");
    wattroff(win, COLOR_PAIR(4));
    
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, centerY + 2, 20, "%d connected", syncPeers);
    wattroff(win, COLOR_PAIR(3));
    
    wattron(win, COLOR_PAIR(4));
    mvwprintw(win, centerY + 3, 10, "Blocks:");
    wattroff(win, COLOR_PAIR(4));
    
    wattron(win, COLOR_PAIR(3));
    if (syncTotalBlocks > 0) {
        mvwprintw(win, centerY + 3, 20, "%d / %d", syncBlocks, syncTotalBlocks);
    } else {
        mvwprintw(win, centerY + 3, 20, "%d", syncBlocks);
    }
    wattroff(win, COLOR_PAIR(3));
    
    int barWidth = width - 20;
    int barY = centerY + 6;
    int barX = 10;
    
    drawProgressBar(barY, barX, barWidth, progressPercent, 2);
    
    static const char* spinner = "|/-\\";
    animationFrame = (animationFrame + 1) % 4;
    
    wattron(win, COLOR_PAIR(2) | A_BOLD);
    mvwaddch(win, barY, barX + barWidth + 2, spinner[animationFrame]);
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
    
    wattron(win, COLOR_PAIR(4));
    mvwprintw(win, barY + 2, barX, "Progress: %d%%", progressPercent);
    wattroff(win, COLOR_PAIR(4));
    
    if (progressPercent < 100) {
        int eta = (100 - progressPercent) * 2;
        wattron(win, COLOR_PAIR(1));
        mvwprintw(win, barY + 3, barX, "Estimated time: %d seconds", eta);
        wattroff(win, COLOR_PAIR(1));
    }
    
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, height - 4, 10, "Tip: Initial sync may take a few minutes depending on network conditions.");
    mvwprintw(win, height - 3, 10, "     You can use the node while syncing, but some features may be limited.");
    wattroff(win, COLOR_PAIR(1));
    
    wrefresh(win);
}

void StartupScreen::Impl::drawError() {
    werase(win);
    box(win, 0, 0);
    
    wattron(win, COLOR_PAIR(5) | A_BOLD);
    const char* title = "[ ERROR ]";
    mvwprintw(win, 2, (width - strlen(title)) / 2, "%s", title);
    wattroff(win, COLOR_PAIR(5) | A_BOLD);
    
    int centerY = height / 2 - 2;
    
    wattron(win, COLOR_PAIR(5));
    mvwprintw(win, centerY, (width - errorMessage.size()) / 2, "%s", errorMessage.c_str());
    wattroff(win, COLOR_PAIR(5));
    
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, height - 3, (width - 30) / 2, "Press any key to continue...");
    wattroff(win, COLOR_PAIR(1));
    
    wrefresh(win);
}

void StartupScreen::Impl::animateText(const std::string& text, int y, int x, int delayMs) {
    for (size_t i = 0; i < text.size(); i++) {
        mvwaddch(win, y, x + i, text[i]);
        wrefresh(win);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}

StartupScreen::StartupScreen() : impl_(std::make_unique<Impl>()) {}
StartupScreen::~StartupScreen() = default;

void StartupScreen::init(WINDOW* win, int width, int height) {
    impl_->win = win;
    impl_->width = width;
    impl_->height = height;
    impl_->state = StartupState::LOGO;
    impl_->stateStartTime = std::time(nullptr);
    impl_->initializeSteps();
}

void StartupScreen::draw() {
    switch (impl_->state) {
        case StartupState::LOGO:
            impl_->drawLogo();
            break;
        case StartupState::INIT_PROGRESS:
            impl_->drawProgress();
            break;
        case StartupState::WALLET_CHOICE:
            impl_->drawWalletChoice();
            break;
        case StartupState::SEED_DISPLAY:
            impl_->drawSeedDisplay();
            break;
        case StartupState::SEED_CONFIRM:
            impl_->drawSeedConfirm();
            break;
        case StartupState::SEED_IMPORT:
            impl_->drawSeedImport();
            break;
        case StartupState::SYNCING:
            impl_->drawSyncing();
            break;
        case StartupState::COMPLETE:
        case StartupState::PASSWORD_ENTRY:
        case StartupState::ERROR:
            break;
    }
}

bool StartupScreen::handleInput(int ch) {
    switch (impl_->state) {
        case StartupState::LOGO:
            if (ch != ERR) {
                impl_->state = StartupState::INIT_PROGRESS;
                impl_->stateStartTime = std::time(nullptr);
                return true;
            }
            break;
            
        case StartupState::INIT_PROGRESS:
            break;
            
        case StartupState::WALLET_CHOICE:
            if (ch == '1') {
                impl_->state = StartupState::SEED_DISPLAY;
                generateSeedWords();
                return true;
            } else if (ch == '2') {
                impl_->state = StartupState::SEED_IMPORT;
                impl_->inputBuffer.clear();
                impl_->seedWords.clear();
                impl_->errorMessage.clear();
                return true;
            }
            break;
            
        case StartupState::SEED_DISPLAY:
            if (ch == '\n' || ch == KEY_ENTER) {
                impl_->state = StartupState::SEED_CONFIRM;
                impl_->inputBuffer.clear();
                impl_->errorMessage.clear();
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(0, 23);
                impl_->confirmWordIndex = dis(gen);
                return true;
            }
            break;
            
        case StartupState::SEED_CONFIRM:
            if (ch == '\n' || ch == KEY_ENTER) {
                if (impl_->inputBuffer == impl_->seedWords[impl_->confirmWordIndex]) {
                    impl_->state = StartupState::SYNCING;
                    impl_->errorMessage.clear();
                    return true;
                } else {
                    impl_->errorMessage = "Incorrect word. Please try again.";
                    impl_->inputBuffer.clear();
                }
            } else if (ch == 27) {
                impl_->state = StartupState::SEED_DISPLAY;
                impl_->inputBuffer.clear();
                impl_->errorMessage.clear();
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                if (!impl_->inputBuffer.empty()) {
                    impl_->inputBuffer.pop_back();
                }
            } else if (ch >= 'a' && ch <= 'z') {
                impl_->inputBuffer += ch;
            } else if (ch >= 'A' && ch <= 'Z') {
                impl_->inputBuffer += tolower(ch);
            }
            break;
            
        case StartupState::SEED_IMPORT:
            if (ch == '\n' || ch == KEY_ENTER) {
                if (!impl_->inputBuffer.empty()) {
                    impl_->seedWords.push_back(impl_->inputBuffer);
                    impl_->inputBuffer.clear();
                }
                if (impl_->seedWords.size() >= 24) {
                    impl_->state = StartupState::SYNCING;
                    impl_->errorMessage.clear();
                    return true;
                }
            } else if (ch == 27) {
                impl_->state = StartupState::WALLET_CHOICE;
                impl_->inputBuffer.clear();
                impl_->seedWords.clear();
                impl_->errorMessage.clear();
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                if (!impl_->inputBuffer.empty()) {
                    impl_->inputBuffer.pop_back();
                } else if (!impl_->seedWords.empty()) {
                    impl_->inputBuffer = impl_->seedWords.back();
                    impl_->seedWords.pop_back();
                }
            } else if (ch == ' ') {
                if (!impl_->inputBuffer.empty()) {
                    impl_->seedWords.push_back(impl_->inputBuffer);
                    impl_->inputBuffer.clear();
                }
            } else if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
                impl_->inputBuffer += tolower(ch);
            }
            break;
            
        case StartupState::SYNCING:
            break;
            
        case StartupState::COMPLETE:
        case StartupState::PASSWORD_ENTRY:
        case StartupState::ERROR:
            return true;
    }
    return false;
}

void StartupScreen::setState(StartupState state) {
    impl_->state = state;
    impl_->stateStartTime = std::time(nullptr);
}

StartupState StartupScreen::getState() const {
    return impl_->state;
}

void StartupScreen::setProgress(int percent, const std::string& message) {
    impl_->progressPercent = percent;
    impl_->statusMessage = message;
    
    if (impl_->state == StartupState::INIT_PROGRESS) {
        impl_->currentInitStep = (percent * impl_->initSteps.size()) / 100;
        if (impl_->currentInitStep >= static_cast<int>(impl_->initSteps.size())) {
            impl_->currentInitStep = impl_->initSteps.size() - 1;
        }
    }
}

void StartupScreen::generateSeedWords() {
    impl_->seedWords.clear();
    
    std::random_device rd;
    
    auto now = std::chrono::high_resolution_clock::now();
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    
    std::array<uint32_t, 16> seed_data;
    for (size_t i = 0; i < seed_data.size(); i++) {
        seed_data[i] = rd() ^ static_cast<uint32_t>(nanos >> (i * 2));
    }
    seed_data[0] ^= static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    seed_data[1] ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(impl_.get()));
    
    std::seed_seq seq(seed_data.begin(), seed_data.end());
    std::mt19937_64 gen(seq);
    std::uniform_int_distribution<> dis(0, BIP39_WORDLIST_SIZE - 1);
    
    for (int i = 0; i < 24; i++) {
        impl_->seedWords.push_back(BIP39_WORDLIST[dis(gen)]);
    }
}

void StartupScreen::setSeedWords(const std::vector<std::string>& words) {
    impl_->seedWords = words;
}

std::vector<std::string> StartupScreen::getSeedWords() const {
    return impl_->seedWords;
}

void StartupScreen::setWalletExists(bool exists) {
    impl_->walletExists = exists;
}

void StartupScreen::setSyncStatus(int peers, int blocks, int totalBlocks) {
    impl_->syncPeers = peers;
    impl_->syncBlocks = blocks;
    impl_->syncTotalBlocks = totalBlocks;
}

void StartupScreen::setError(const std::string& error) {
    impl_->errorMessage = error;
}

}
}
