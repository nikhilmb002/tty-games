#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define EASY_DELAY   150000
#define MEDIUM_DELAY 100000
#define HARD_DELAY   60000

/* Start length (change this) */
#define INITIAL_SNAKE_LEN 12

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
/* Play area (top-left origin and size) */
int play_x0, play_y0, play_w, play_h;

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
void free_snake();

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

    /***** Compute a smaller centered playable area *****/
    /* Use 50% of terminal for a tighter play area */
    play_w = max_x * 50 / 100;
    play_h = max_y * 50 / 100;
    /* Fallback minimum sizes */
    if (play_w < 20) play_w = max_x - 4;
    if (play_h < 10) play_h = max_y - 4;
    play_x0 = (max_x - play_w) / 2;
    play_y0 = (max_y - play_h) / 2;

    init_game();
    draw_borders();

    while (1) {
        /* Score and level displayed above play area */
        attron(COLOR_PAIR(4));
        mvprintw(play_y0 - 1, play_x0, " Score: %d | Level: %s ",
                 score,
                 (delay_time == EASY_DELAY) ? "Easy" :
                 (delay_time == MEDIUM_DELAY) ? "Medium" : "Hard");
        attroff(COLOR_PAIR(4));

        draw_snake();
        attron(COLOR_PAIR(2));
        mvaddch(food.y, food.x, '@');
        attroff(COLOR_PAIR(2));

        if (paused) {
            attron(COLOR_PAIR(4));
            mvprintw(play_y0 + play_h / 2, play_x0 + play_w / 2 - 6, "--- PAUSED ---");
            attroff(COLOR_PAIR(4));
        } else {
            /* Clear paused message area */
            mvprintw(play_y0 + play_h / 2, play_x0 + play_w / 2 - 6, "               ");
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
    /* Initialize direction */
    snake.dir_x = 1;
    snake.dir_y = 0;
    snake.head = snake.tail = NULL;

    /* Create initial snake centered in play area */
    int start_x = play_x0 + play_w / 2;
    int start_y = play_y0 + play_h / 2;

    SnakeSegment *prev = NULL;
    for (int i = 0; i < INITIAL_SNAKE_LEN; ++i) {
        SnakeSegment *seg = malloc(sizeof(SnakeSegment));
        seg->x = start_x - i;   /* grow leftwards from head */
        seg->y = start_y;
        seg->next = NULL;
        if (prev) prev->next = seg;
        else snake.head = seg;
        prev = seg;
    }
    snake.tail = prev;

    score = 0;
    spawn_food();
}

void draw_borders() {
    attron(COLOR_PAIR(3));
    /* top and bottom */
    for (int i = play_x0; i < play_x0 + play_w; ++i) {
        mvaddch(play_y0, i, '#');
        mvaddch(play_y0 + play_h - 1, i, '#');
    }
    /* left and right */
    for (int i = play_y0; i < play_y0 + play_h; ++i) {
        mvaddch(i, play_x0, '#');
        mvaddch(i, play_x0 + play_w - 1, '#');
    }
    attroff(COLOR_PAIR(3));
}

void draw_snake() {
    attron(COLOR_PAIR(1));
    /* Draw full snake: head as 'O', body as 'o' */
    SnakeSegment *cur = snake.head;
    int first = 1;
    while (cur) {
        if (first) {
            mvaddch(cur->y, cur->x, 'O');
            first = 0;
        } else {
            mvaddch(cur->y, cur->x, 'o');
        }
        cur = cur->next;
    }
    attroff(COLOR_PAIR(1));
}

/* Remove last segment (tail) from screen and list */
void erase_tail() {
    if (!snake.head) return;
    if (!snake.head->next) {
        /* single segment: nothing to erase (we keep at least head) */
        return;
    }
    SnakeSegment *curr = snake.head;
    /* find second last */
    while (curr->next && curr->next->next) {
        curr = curr->next;
    }
    /* curr->next is tail */
    mvaddch(curr->next->y, curr->next->x, ' ');
    free(curr->next);
    curr->next = NULL;
    snake.tail = curr;
}

void move_snake() {
    int new_x = snake.head->x + snake.dir_x;
    int new_y = snake.head->y + snake.dir_y;

    SnakeSegment *new_head = malloc(sizeof(SnakeSegment));
    new_head->x = new_x;
    new_head->y = new_y;
    new_head->next = snake.head;
    snake.head = new_head;

    /* If eaten food, grow and respawn food; otherwise drop tail */
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

    /* colliding with borders of play area */
    if (x <= play_x0 || x >= play_x0 + play_w - 1 || y <= play_y0 || y >= play_y0 + play_h - 1)
        return 1;

    /* self-collision */
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
        int fx = (rand() % (play_w - 2)) + play_x0 + 1;
        int fy = (rand() % (play_h - 2)) + play_y0 + 1;

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

void free_snake() {
    SnakeSegment *cur = snake.head;
    while (cur) {
        SnakeSegment *n = cur->next;
        free(cur);
        cur = n;
    }
    snake.head = snake.tail = NULL;
}

void end_game() {
    nodelay(stdscr, FALSE);
    attron(COLOR_PAIR(4));
    mvprintw(play_y0 + play_h / 2 - 1, play_x0 + play_w / 2 - 5, "Game Over!");
    mvprintw(play_y0 + play_h / 2, play_x0 + play_w / 2 - 8, "Final Score: %d", score);
    mvprintw(play_y0 + play_h / 2 + 1, play_x0 + play_w / 2 - 12, "Press any key to exit...");
    attroff(COLOR_PAIR(4));
    refresh();
    getch();

    free_snake();
    endwin();
}

