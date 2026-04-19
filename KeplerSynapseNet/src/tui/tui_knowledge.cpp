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
#include <chrono>

namespace synapse {
namespace tui {

enum class KnowledgeType {
    Text,
    Code,
    Data,
    Research,
    Tutorial,
    Documentation,
    Analysis,
    Other
};

struct KnowledgeEntry {
    std::string id;
    std::string title;
    std::string content;
    std::string author;
    std::string category;
    KnowledgeType type;
    uint64_t timestamp;
    uint64_t size;
    uint64_t score;
    uint64_t views;
    uint64_t citations;
    bool verified;
    bool encrypted;
    std::vector<std::string> tags;
    std::string hash;
};

struct KnowledgeStats {
    uint64_t totalEntries;
    uint64_t totalSize;
    uint64_t totalViews;
    uint64_t totalCitations;
    uint64_t verifiedCount;
    uint64_t pendingCount;
    uint64_t rejectedCount;
    double averageScore;
    std::map<KnowledgeType, uint64_t> byType;
    std::map<std::string, uint64_t> byCategory;
};

class KnowledgeScreen {
private:
    WINDOW* mainWin;
    WINDOW* listWin;
    WINDOW* detailWin;
    WINDOW* statusWin;
    WINDOW* searchWin;
    std::vector<KnowledgeEntry> entries;
    std::vector<KnowledgeEntry> filteredEntries;
    int selectedIndex;
    int scrollOffset;
    int viewMode;
    std::string searchQuery;
    std::string filterCategory;
    KnowledgeType filterType;
    bool showVerifiedOnly;
    KnowledgeStats stats;

public:
    KnowledgeScreen() : mainWin(nullptr), listWin(nullptr), detailWin(nullptr),
                        statusWin(nullptr), searchWin(nullptr), selectedIndex(0),
                        scrollOffset(0), viewMode(0), filterType(KnowledgeType::Other),
                        showVerifiedOnly(false) {
        initializeTestData();
        calculateStats();
    }

    ~KnowledgeScreen() {
        cleanup();
    }

    void initializeTestData() {
        entries.clear();
    }

    void calculateStats() {
        stats.totalEntries = 0;
        stats.totalSize = 0;
        stats.totalViews = 0;
        stats.totalCitations = 0;
        stats.verifiedCount = 0;
        stats.pendingCount = 0;
        stats.rejectedCount = 0;
        stats.averageScore = 0.0;
        stats.byType.clear();
        stats.byCategory.clear();

        if (entries.empty()) return;

        double totalScore = 0.0;
        for (const auto& entry : entries) {
            stats.totalEntries++;
            stats.totalSize += entry.size;
            stats.totalViews += entry.views;
            stats.totalCitations += entry.citations;
            totalScore += entry.score;

            if (entry.verified) {
                stats.verifiedCount++;
            }

            stats.byType[entry.type]++;
            stats.byCategory[entry.category]++;
        }

        stats.averageScore = totalScore / entries.size();
    }

    void initialize() {
        int maxY, maxX;
        getmaxyx(stdscr, maxY, maxX);

        mainWin = newwin(maxY - 2, maxX, 0, 0);
        listWin = newwin(maxY - 6, maxX / 2 - 1, 2, 1);
        detailWin = newwin(maxY - 6, maxX / 2 - 1, 2, maxX / 2);
        statusWin = newwin(1, maxX, maxY - 1, 0);
        searchWin = newwin(3, maxX - 2, 1, 1);

        keypad(mainWin, TRUE);
        keypad(listWin, TRUE);
    }

    void cleanup() {
        if (mainWin) delwin(mainWin);
        if (listWin) delwin(listWin);
        if (detailWin) delwin(detailWin);
        if (statusWin) delwin(statusWin);
        if (searchWin) delwin(searchWin);
        mainWin = listWin = detailWin = statusWin = searchWin = nullptr;
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

    void drawHeader() {
        werase(mainWin);
        drawBorder(mainWin, "Knowledge Base");

        int maxX = getmaxx(mainWin);
        std::string header = "SYNAPSENET KNOWLEDGE EXPLORER";
        mvwprintw(mainWin, 1, (maxX - header.length()) / 2, "%s", header.c_str());

        wrefresh(mainWin);
    }

    void drawSearchBar() {
        werase(searchWin);
        drawBorder(searchWin, "Search");

        std::string prompt = "Query: " + searchQuery;
        if (searchQuery.empty()) {
            prompt = "Query: (type to search...)";
        }
        mvwprintw(searchWin, 1, 2, "%s", prompt.c_str());

        std::string filters;
        if (!filterCategory.empty()) {
            filters += "[Cat:" + filterCategory + "] ";
        }
        if (showVerifiedOnly) {
            filters += "[Verified] ";
        }

        if (!filters.empty()) {
            int maxX = getmaxx(searchWin);
            mvwprintw(searchWin, 1, maxX - filters.length() - 2, "%s", filters.c_str());
        }

        wrefresh(searchWin);
    }

    void drawList() {
        werase(listWin);
        drawBorder(listWin, "Entries (" + std::to_string(filteredEntries.size()) + ")");

        int maxY, maxX;
        getmaxyx(listWin, maxY, maxX);
        int visibleItems = maxY - 2;

        if (filteredEntries.empty()) {
            wattron(listWin, COLOR_PAIR(4));
            mvwprintw(listWin, maxY / 2, 2, "No entries found");
            wattroff(listWin, COLOR_PAIR(4));
            wrefresh(listWin);
            return;
        }

        int y = 1;
        for (int i = scrollOffset; i < static_cast<int>(filteredEntries.size()) && y < visibleItems + 1; i++) {
            const auto& entry = filteredEntries[i];
            bool isSelected = (i == selectedIndex);

            if (isSelected) {
                wattron(listWin, A_REVERSE);
            }

            std::string line = entry.title;
            if (line.length() > static_cast<size_t>(maxX - 12)) {
                line = line.substr(0, maxX - 15) + "...";
            }

            std::string status = entry.verified ? "[V]" : "[ ]";
            mvwprintw(listWin, y, 2, "%s %s", status.c_str(), line.c_str());

            if (isSelected) {
                wattroff(listWin, A_REVERSE);
            }

            y++;
        }

        int scrollbarHeight = std::max(1, visibleItems * visibleItems / static_cast<int>(filteredEntries.size()));
        int scrollbarPos = 1 + (scrollOffset * (visibleItems - scrollbarHeight)) / 
                           std::max(1, static_cast<int>(filteredEntries.size()) - visibleItems);

        wattron(listWin, COLOR_PAIR(2));
        for (int i = 1; i <= visibleItems; i++) {
            if (i >= scrollbarPos && i < scrollbarPos + scrollbarHeight) {
                mvwaddch(listWin, i, maxX - 2, ACS_BLOCK);
            } else {
                mvwaddch(listWin, i, maxX - 2, ACS_VLINE);
            }
        }
        wattroff(listWin, COLOR_PAIR(2));

        wrefresh(listWin);
    }

    void drawDetail() {
        werase(detailWin);
        drawBorder(detailWin, "Details");

        if (filteredEntries.empty() || selectedIndex >= static_cast<int>(filteredEntries.size())) {
            mvwprintw(detailWin, 2, 2, "Select an entry to view details");
            wrefresh(detailWin);
            return;
        }

        const auto& entry = filteredEntries[selectedIndex];
        int y = 2;
        int maxX = getmaxx(detailWin);

        wattron(detailWin, A_BOLD);
        mvwprintw(detailWin, y++, 2, "%s", entry.title.c_str());
        wattroff(detailWin, A_BOLD);
        y++;

        mvwprintw(detailWin, y++, 2, "ID: %s", entry.id.c_str());
        mvwprintw(detailWin, y++, 2, "Author: %s", entry.author.c_str());
        mvwprintw(detailWin, y++, 2, "Category: %s", entry.category.c_str());
        mvwprintw(detailWin, y++, 2, "Type: %s", getTypeName(entry.type).c_str());
        y++;

        mvwprintw(detailWin, y++, 2, "Size: %s", formatSize(entry.size).c_str());
        mvwprintw(detailWin, y++, 2, "Score: %lu", entry.score);
        mvwprintw(detailWin, y++, 2, "Views: %lu", entry.views);
        mvwprintw(detailWin, y++, 2, "Citations: %lu", entry.citations);
        y++;

        std::string status = entry.verified ? "Verified" : "Pending";
        int statusColor = entry.verified ? 5 : 6;
        mvwprintw(detailWin, y, 2, "Status: ");
        wattron(detailWin, COLOR_PAIR(statusColor));
        wprintw(detailWin, "%s", status.c_str());
        wattroff(detailWin, COLOR_PAIR(statusColor));
        y++;

        if (entry.encrypted) {
            wattron(detailWin, COLOR_PAIR(3));
            mvwprintw(detailWin, y++, 2, "[ENCRYPTED]");
            wattroff(detailWin, COLOR_PAIR(3));
        }
        y++;

        if (!entry.tags.empty()) {
            mvwprintw(detailWin, y++, 2, "Tags:");
            std::string tagLine = "  ";
            for (const auto& tag : entry.tags) {
                if (tagLine.length() + tag.length() + 3 > static_cast<size_t>(maxX - 4)) {
                    mvwprintw(detailWin, y++, 2, "%s", tagLine.c_str());
                    tagLine = "  ";
                }
                tagLine += "#" + tag + " ";
            }
            if (tagLine.length() > 2) {
                mvwprintw(detailWin, y++, 2, "%s", tagLine.c_str());
            }
        }
        y++;

        mvwprintw(detailWin, y++, 2, "Hash: %s", entry.hash.substr(0, 16).c_str());

        time_t ts = entry.timestamp;
        char timeBuf[64];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", localtime(&ts));
        mvwprintw(detailWin, y++, 2, "Created: %s", timeBuf);

        wrefresh(detailWin);
    }

    std::string getTypeName(KnowledgeType type) {
        switch (type) {
            case KnowledgeType::Text: return "Text";
            case KnowledgeType::Code: return "Code";
            case KnowledgeType::Data: return "Data";
            case KnowledgeType::Research: return "Research";
            case KnowledgeType::Tutorial: return "Tutorial";
            case KnowledgeType::Documentation: return "Documentation";
            case KnowledgeType::Analysis: return "Analysis";
            default: return "Other";
        }
    }

    std::string formatSize(uint64_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unit = 0;
        double size = bytes;

        while (size >= 1024 && unit < 3) {
            size /= 1024;
            unit++;
        }

        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << size << " " << units[unit];
        return ss.str();
    }

    void drawStatus() {
        werase(statusWin);
        wattron(statusWin, COLOR_PAIR(1));

        std::string status = " [NAV] j/k=Move | Enter=View | /=Search | f=Filter | s=Submit | v=Verify | q=Back";
        mvwprintw(statusWin, 0, 0, "%-*s", COLS, status.c_str());

        wattroff(statusWin, COLOR_PAIR(1));
        wrefresh(statusWin);
    }

    void applyFilters() {
        filteredEntries.clear();

        for (const auto& entry : entries) {
            if (showVerifiedOnly && !entry.verified) continue;

            if (!filterCategory.empty() && entry.category != filterCategory) continue;

            if (!searchQuery.empty()) {
                std::string lowerTitle = entry.title;
                std::string lowerQuery = searchQuery;
                std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
                std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

                if (lowerTitle.find(lowerQuery) == std::string::npos) {
                    bool foundInTags = false;
                    for (const auto& tag : entry.tags) {
                        std::string lowerTag = tag;
                        std::transform(lowerTag.begin(), lowerTag.end(), lowerTag.begin(), ::tolower);
                        if (lowerTag.find(lowerQuery) != std::string::npos) {
                            foundInTags = true;
                            break;
                        }
                    }
                    if (!foundInTags) continue;
                }
            }

            filteredEntries.push_back(entry);
        }

        selectedIndex = 0;
        scrollOffset = 0;
    }

    void handleInput(int ch) {
        int maxY = getmaxy(listWin) - 2;

        switch (ch) {
            case KEY_UP:
            case 'k':
                if (selectedIndex > 0) {
                    selectedIndex--;
                    if (selectedIndex < scrollOffset) {
                        scrollOffset = selectedIndex;
                    }
                }
                break;

            case KEY_DOWN:
            case 'j':
                if (selectedIndex < static_cast<int>(filteredEntries.size()) - 1) {
                    selectedIndex++;
                    if (selectedIndex >= scrollOffset + maxY) {
                        scrollOffset++;
                    }
                }
                break;

            case KEY_PPAGE:
                selectedIndex = std::max(0, selectedIndex - maxY);
                scrollOffset = std::max(0, scrollOffset - maxY);
                break;

            case KEY_NPAGE:
                selectedIndex = std::min(static_cast<int>(filteredEntries.size()) - 1, selectedIndex + maxY);
                scrollOffset = std::min(static_cast<int>(filteredEntries.size()) - maxY, scrollOffset + maxY);
                if (scrollOffset < 0) scrollOffset = 0;
                break;

            case KEY_HOME:
                selectedIndex = 0;
                scrollOffset = 0;
                break;

            case KEY_END:
                selectedIndex = filteredEntries.size() - 1;
                scrollOffset = std::max(0, static_cast<int>(filteredEntries.size()) - maxY);
                break;

            case '/':
                startSearch();
                break;

            case 'f':
            case 'F':
                showFilterMenu();
                break;

            case 's':
            case 'S':
                showSubmitDialog();
                break;

            case 'v':
            case 'V':
                toggleVerifiedFilter();
                break;

            case '\n':
            case KEY_ENTER:
                viewEntry();
                break;

            case 'c':
            case 'C':
                clearFilters();
                break;

            case 't':
            case 'T':
                showStatsDialog();
                break;
        }
    }

    void startSearch() {
        echo();
        curs_set(1);

        char query[256];
        mvwgetnstr(searchWin, 1, 9, query, 255);

        noecho();
        curs_set(0);

        searchQuery = query;
        applyFilters();
    }

    void showFilterMenu() {
        WINDOW* filterWin = newwin(15, 40, LINES/2 - 7, COLS/2 - 20);
        box(filterWin, 0, 0);
        mvwprintw(filterWin, 0, 2, " Filter Options ");

        int y = 2;
        mvwprintw(filterWin, y++, 2, "[1] By Category");
        mvwprintw(filterWin, y++, 2, "[2] By Type");
        mvwprintw(filterWin, y++, 2, "[3] Verified Only: %s", showVerifiedOnly ? "ON" : "OFF");
        mvwprintw(filterWin, y++, 2, "[4] By Score (High to Low)");
        mvwprintw(filterWin, y++, 2, "[5] By Date (Newest)");
        mvwprintw(filterWin, y++, 2, "[6] By Views");
        y++;
        mvwprintw(filterWin, y++, 2, "[C] Clear All Filters");
        mvwprintw(filterWin, y++, 2, "[Q] Close");

        wrefresh(filterWin);

        int ch = wgetch(filterWin);
        switch (ch) {
            case '1':
                selectCategory();
                break;
            case '2':
                selectType();
                break;
            case '3':
                showVerifiedOnly = !showVerifiedOnly;
                applyFilters();
                break;
            case '4':
                sortByScore();
                break;
            case '5':
                sortByDate();
                break;
            case '6':
                sortByViews();
                break;
            case 'c':
            case 'C':
                clearFilters();
                break;
        }

        delwin(filterWin);
    }

    void selectCategory() {
        std::vector<std::string> categories;
        for (const auto& entry : entries) {
            if (std::find(categories.begin(), categories.end(), entry.category) == categories.end()) {
                categories.push_back(entry.category);
            }
        }

        if (categories.empty()) return;

        WINDOW* catWin = newwin(categories.size() + 4, 40, LINES/2 - 5, COLS/2 - 20);
        box(catWin, 0, 0);
        mvwprintw(catWin, 0, 2, " Select Category ");

        int y = 2;
        for (size_t i = 0; i < categories.size(); i++) {
            mvwprintw(catWin, y++, 2, "[%zu] %s", i + 1, categories[i].c_str());
        }

        wrefresh(catWin);

        int ch = wgetch(catWin);
        int idx = ch - '1';
        if (idx >= 0 && idx < static_cast<int>(categories.size())) {
            filterCategory = categories[idx];
            applyFilters();
        }

        delwin(catWin);
    }

    void selectType() {
    }

    void sortByScore() {
        std::sort(filteredEntries.begin(), filteredEntries.end(),
            [](const KnowledgeEntry& a, const KnowledgeEntry& b) {
                return a.score > b.score;
            });
    }

    void sortByDate() {
        std::sort(filteredEntries.begin(), filteredEntries.end(),
            [](const KnowledgeEntry& a, const KnowledgeEntry& b) {
                return a.timestamp > b.timestamp;
            });
    }

    void sortByViews() {
        std::sort(filteredEntries.begin(), filteredEntries.end(),
            [](const KnowledgeEntry& a, const KnowledgeEntry& b) {
                return a.views > b.views;
            });
    }

    void toggleVerifiedFilter() {
        showVerifiedOnly = !showVerifiedOnly;
        applyFilters();
    }

    void clearFilters() {
        searchQuery.clear();
        filterCategory.clear();
        showVerifiedOnly = false;
        applyFilters();
    }

    void viewEntry() {
        if (filteredEntries.empty()) return;
        showEntryViewer(filteredEntries[selectedIndex]);
    }

    void showEntryViewer(const KnowledgeEntry& entry) {
        WINDOW* viewWin = newwin(LINES - 4, COLS - 4, 2, 2);
        box(viewWin, 0, 0);
        mvwprintw(viewWin, 0, 2, " %s ", entry.title.c_str());

        int y = 2;
        int maxX = getmaxx(viewWin) - 4;

        std::string content = entry.content;
        size_t pos = 0;
        while (pos < content.length() && y < LINES - 8) {
            std::string line = content.substr(pos, maxX);
            size_t newline = line.find('\n');
            if (newline != std::string::npos) {
                line = line.substr(0, newline);
                pos += newline + 1;
            } else {
                pos += maxX;
            }
            mvwprintw(viewWin, y++, 2, "%s", line.c_str());
        }

        mvwprintw(viewWin, LINES - 6, 2, "Press any key to close...");
        wrefresh(viewWin);
        wgetch(viewWin);
        delwin(viewWin);
    }

    void showSubmitDialog() {
        WINDOW* submitWin = newwin(20, 60, LINES/2 - 10, COLS/2 - 30);
        box(submitWin, 0, 0);
        mvwprintw(submitWin, 0, 2, " Submit Knowledge ");

        int y = 2;
        mvwprintw(submitWin, y++, 2, "Title:");
        mvwprintw(submitWin, y++, 2, "[                                        ]");
        y++;
        mvwprintw(submitWin, y++, 2, "Category:");
        mvwprintw(submitWin, y++, 2, "[                                        ]");
        y++;
        mvwprintw(submitWin, y++, 2, "Content (or file path):");
        mvwprintw(submitWin, y++, 2, "[                                        ]");
        y++;
        mvwprintw(submitWin, y++, 2, "Tags (comma separated):");
        mvwprintw(submitWin, y++, 2, "[                                        ]");
        y += 2;
        mvwprintw(submitWin, y++, 2, "Stake Required: 10 NGT");
        mvwprintw(submitWin, y++, 2, "[S] Submit  [C] Cancel");

        wrefresh(submitWin);
        wgetch(submitWin);
        delwin(submitWin);
    }

    void showStatsDialog() {
        WINDOW* statsWin = newwin(18, 50, LINES/2 - 9, COLS/2 - 25);
        box(statsWin, 0, 0);
        mvwprintw(statsWin, 0, 2, " Knowledge Statistics ");

        int y = 2;
        mvwprintw(statsWin, y++, 2, "Total Entries: %lu", stats.totalEntries);
        mvwprintw(statsWin, y++, 2, "Total Size: %s", formatSize(stats.totalSize).c_str());
        mvwprintw(statsWin, y++, 2, "Total Views: %lu", stats.totalViews);
        mvwprintw(statsWin, y++, 2, "Total Citations: %lu", stats.totalCitations);
        y++;
        mvwprintw(statsWin, y++, 2, "Verified: %lu", stats.verifiedCount);
        mvwprintw(statsWin, y++, 2, "Pending: %lu", stats.pendingCount);
        mvwprintw(statsWin, y++, 2, "Average Score: %.1f", stats.averageScore);
        y++;
        mvwprintw(statsWin, y++, 2, "By Type:");
        for (const auto& pair : stats.byType) {
            mvwprintw(statsWin, y++, 4, "%s: %lu", getTypeName(pair.first).c_str(), pair.second);
        }

        mvwprintw(statsWin, 16, 2, "Press any key to close...");
        wrefresh(statsWin);
        wgetch(statsWin);
        delwin(statsWin);
    }

    void run() {
        initialize();
        applyFilters();
        bool running = true;

        while (running) {
            drawHeader();
            drawSearchBar();
            drawList();
            drawDetail();
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

void showKnowledgeScreen() {
    KnowledgeScreen screen;
    screen.run();
}

}
}
