#include <ncurses.h>
#include <iostream>
#include <unistd.h>

int main() {
    std::cout << "Testing ncurses initialization..." << std::endl;
    
    WINDOW* w = initscr();
    if (!w) {
        std::cout << "Failed to initialize ncurses!" << std::endl;
        return 1;
    }
    
    std::cout << "ncurses initialized successfully!" << std::endl;
    
    // Test basic functionality
    clear();
    mvprintw(0, 0, "Hello from ncurses!");
    mvprintw(1, 0, "Press any key to exit...");
    refresh();
    
    // Wait for input
    getch();
    
    endwin();
    std::cout << "ncurses test completed successfully!" << std::endl;
    return 0;
}