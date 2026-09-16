#include <ncurses.h>
#include <unistd.h>
#include <iostream>

int main() {
    // Check if we're in a proper terminal
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        std::cout << "Not running in a proper terminal" << std::endl;
        return 1;
    }
    
    // Initialize ncurses
    WINDOW* w = initscr();
    if (!w) {
        std::cout << "Failed to initialize ncurses" << std::endl;
        return 1;
    }
    
    // Check terminal size
    if (LINES < 24 || COLS < 80) {
        endwin();
        std::cout << "Terminal too small: " << COLS << "x" << LINES << " (need 80x24)" << std::endl;
        return 1;
    }
    
    // Setup ncurses
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE);
    
    // Test colors
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN, -1);
        init_pair(2, COLOR_YELLOW, -1);
        init_pair(3, COLOR_RED, -1);
        init_pair(4, COLOR_CYAN, -1);
    }
    
    bool running = true;
    int counter = 0;
    
    while (running) {
        clear();
        
        // Draw header
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(0, 0, "SynapseNet TUI Test - Terminal: %dx%d", COLS, LINES);
        attroff(COLOR_PAIR(4) | A_BOLD);
        
        // Draw instructions
        mvprintw(2, 0, "Counter: %d", counter++);
        mvprintw(4, 0, "Press 'q' to quit, SPACE to continue, Ctrl+C to force exit");
        
        // Draw a simple box
        mvprintw(6, 0, "+------------------+");
        mvprintw(7, 0, "| TUI Test Working |");
        mvprintw(8, 0, "+------------------+");
        
        refresh();
        
        // Handle input
        int ch = getch();
        if (ch != ERR) {
            if (ch == 'q' || ch == 'Q' || ch == 3) { // q or Ctrl+C
                running = false;
            }
        }
        
        // Small delay to prevent 100% CPU usage
        usleep(50000); // 50ms
    }
    
    endwin();
    std::cout << "TUI test completed successfully!" << std::endl;
    return 0;
}