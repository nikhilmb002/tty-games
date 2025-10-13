#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define EASY_DELAY   150000
#define MEDIUM_DELAY 100000
#define HARD_DELAY   60000

typedef struct SnakeSegment {
    int x, y;
    struct SnakeSegment *next;
} SnakeSegment;

typedef struct {
    SnakeSegment *head;
    SnakeSegment *tail;
    int dir_x, dir_y;
} Snake;

typedef struct {
    int x, y;
} Food;

int max_x, max_y;
Snake snake;
Food food;
int score = 0;
int paused = 0;
int delay_time;

void init_game();
void draw_borders();
void draw_snake();
void move_snake();
int check_collision();
void spawn_food();
void end_game();
void erase_tail();
int show_menu();

int main() {
    initscr();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    getmaxyx(stdscr, max_y, max_x);

    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK);   // Snake
    init_pair(2, COLOR_RED, COLOR_BLACK);     // Food
    init_pair(3, COLOR_CYAN, COLOR_BLACK);    // Borders
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);  // Score / Text
    init_pair(5, COLOR_MAGENTA, COLOR_BLACK); // Menu highlight

    srand(time(NULL));

    // --- Show Level Menu ---
    int level = show_menu();
    switch (level) {
        case 1: delay_time = EASY_DELAY; break;
        case 2: delay_time = MEDIUM_DELAY; break;
        case 3: delay_time = HARD_DELAY; break;
        default: delay_time = MEDIUM_DELAY;
    }

    clear();
    init_game();
    draw_borders();

    while (1) {
        attron(COLOR_PAIR(4));
        mvprintw(0, 2, "Score: %d | Level: %s", score,
                 (delay_time == EASY_DELAY) ? "Easy" :
                 (delay_time == MEDIUM_DELAY) ? "Medium" : "Hard");
        attroff(COLOR_PAIR(4));

        draw_snake();
        attron(COLOR_PAIR(2));
        mvaddch(food.y, food.x, '@');
        attroff(COLOR_PAIR(2));

        if (paused) {
            attron(COLOR_PAIR(4));
            mvprintw(max_y / 2, max_x / 2 - 5, "--- PAUSED ---");
            attroff(COLOR_PAIR(4));
        } else {
            mvprintw(max_y / 2, max_x / 2 - 5, "             ");
        }

        refresh();
        usleep(delay_time);

        int ch = getch();
        switch (ch) {
            case KEY_UP:    if (!paused && snake.dir_y != 1) { snake.dir_x = 0; snake.dir_y = -1; } break;
            case KEY_DOWN:  if (!paused && snake.dir_y != -1) { snake.dir_x = 0; snake.dir_y = 1; } break;
            case KEY_LEFT:  if (!paused && snake.dir_x != 1) { snake.dir_x = -1; snake.dir_y = 0; } break;
            case KEY_RIGHT: if (!paused && snake.dir_x != -1) { snake.dir_x = 1; snake.dir_y = 0; } break;
            case 'p': case 'P': paused = !paused; break;
            case 'q': case 'Q': end_game(); return 0;
        }

        if (!paused) {
            move_snake();
            if (check_collision()) {
                end_game();
                return 0;
            }
        }
    }

    endwin();
    return 0;
}

int show_menu() {
    int choice = 1;
    int ch;
    nodelay(stdscr, FALSE);

    while (1) {
        clear();
        attron(COLOR_PAIR(4));
        mvprintw(max_y / 2 - 4, max_x / 2 - 6, " SNAKE GAME ");
        attroff(COLOR_PAIR(4));

        mvprintw(max_y / 2 - 1, max_x / 2 - 10, "Select Difficulty Level:");

        if (choice == 1) attron(COLOR_PAIR(5));
        mvprintw(max_y / 2 + 1, max_x / 2 - 4, "1. Easy");
        if (choice == 1) attroff(COLOR_PAIR(5));

        if (choice == 2) attron(COLOR_PAIR(5));
        mvprintw(max_y / 2 + 2, max_x / 2 - 4, "2. Medium");
        if (choice == 2) attroff(COLOR_PAIR(5));

        if (choice == 3) attron(COLOR_PAIR(5));
        mvprintw(max_y / 2 + 3, max_x / 2 - 4, "3. Hard");
        if (choice == 3) attroff(COLOR_PAIR(5));

        mvprintw(max_y / 2 + 5, max_x / 2 - 11, "Use UP/DOWN and ENTER to select");
        refresh();

        ch = getch();
        if (ch == KEY_UP && choice > 1) choice--;
        else if (ch == KEY_DOWN && choice < 3) choice++;
        else if (ch == '\n' || ch == KEY_ENTER) break;
    }

    nodelay(stdscr, TRUE);
    return choice;
}

void init_game() {
    snake.dir_x = 1;
    snake.dir_y = 0;

    SnakeSegment *prev = NULL;
    for (int i = 0; i < 35; ++i) {
        SnakeSegment *seg = malloc(sizeof(SnakeSegment));
        seg->x = max_x / 2 - i;
        seg->y = max_y / 2;
        seg->next = NULL;
        if (prev) prev->next = seg;
        else snake.head = seg;
        prev = seg;
    }
    snake.tail = prev;
    spawn_food();
}

void draw_borders() {
    attron(COLOR_PAIR(3));
    for (int i = 0; i < max_x; ++i) {
        mvaddch(1, i, '#');
        mvaddch(max_y - 1, i, '#');
    }
    for (int i = 1; i < max_y; ++i) {
        mvaddch(i, 0, '#');
        mvaddch(i, max_x - 1, '#');
    }
    attroff(COLOR_PAIR(3));
}

void draw_snake() {
    attron(COLOR_PAIR(1));
    mvaddch(snake.head->y, snake.head->x, 'O');
    attroff(COLOR_PAIR(1));
}

void erase_tail() {
    SnakeSegment *curr = snake.head;
    while (curr->next && curr->next->next) {
        curr = curr->next;
    }
    mvaddch(curr->next->y, curr->next->x, ' ');
    free(curr->next);
    curr->next = NULL;
}

void move_snake() {
    int new_x = snake.head->x + snake.dir_x;
    int new_y = snake.head->y + snake.dir_y;

    SnakeSegment *new_head = malloc(sizeof(SnakeSegment));
    new_head->x = new_x;
    new_head->y = new_y;
    new_head->next = snake.head;
    snake.head = new_head;

    if (new_x == food.x && new_y == food.y) {
        score += 10;
        spawn_food();
    } else {
        erase_tail();
    }
}

int check_collision() {
    int x = snake.head->x;
    int y = snake.head->y;

    if (x <= 0 || x >= max_x - 1 || y <= 1 || y >= max_y - 1)
        return 1;

    SnakeSegment *curr = snake.head->next;
    while (curr) {
        if (curr->x == x && curr->y == y)
            return 1;
        curr = curr->next;
    }
    return 0;
}

void spawn_food() {
    while (1) {
        int fx = rand() % (max_x - 2) + 1;
        int fy = rand() % (max_y - 3) + 2;

        SnakeSegment *curr = snake.head;
        int conflict = 0;
        while (curr) {
            if (curr->x == fx && curr->y == fy) {
                conflict = 1;
                break;
            }
            curr = curr->next;
        }

        if (!conflict) {
            food.x = fx;
            food.y = fy;
            break;
        }
    }
}

void end_game() {
    nodelay(stdscr, FALSE);
    attron(COLOR_PAIR(4));
    mvprintw(max_y / 2, max_x / 2 - 5, "Game Over!");
    mvprintw(max_y / 2 + 1, max_x / 2 - 8, "Final Score: %d", score);
    mvprintw(max_y / 2 + 2, max_x / 2 - 12, "Press any key to exit...");
    attroff(COLOR_PAIR(4));
    refresh();
    getch();
    endwin();
}

