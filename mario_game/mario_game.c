/*
 ascii_mario.c - simple ASCII Mario-style platformer using ncurses

 Controls:
   Left  : LEFT arrow or 'a' / 'A'
   Right : RIGHT arrow or 'd' / 'D'
   Jump  : Space or 'w' / 'W' / Up arrow
   Pause : 'p' / 'P'
   Quit  : 'q' / 'Q'

 Tiles:
   ' ' : empty
   '#' : platform (collidable)
   '=' : ground (collidable)
   'o' : coin (collect)
   'E' : enemy (patrol)
   '>' : level exit (win)

 Features:
 - Gravity & jump physics (float)
 - Smooth vertical movement and integer horizontal steps
 - Camera that follows player (side-scrolling)
 - Basic enemy patrol and collisions
 - Score, lives, HUD
*/

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#define TICK_US 30000          /* ~33 FPS */
#define GRAVITY 0.60f
#define JUMP_VELO -8.0f
#define MOVE_SPEED 1           /* horizontal move steps per key press */
#define MAX_ENEMIES 128

/* Player */
typedef struct {
    float x, y;    /* world coords (x in cols, y in rows) - y increases downward */
    float vy;      /* vertical velocity */
    int facing;    /* -1 left, 1 right */
    int on_ground; /* boolean */
    int lives;
    int score;
} Player;

/* Enemy simple struct */
typedef struct {
    int alive;
    int x, y;         /* tile coords */
    int dir;          /* -1 left, 1 right */
    int left_bound;   /* tile left patrol bound */
    int right_bound;  /* tile right patrol bound */
} Enemy;

/* Map: array of strings (each string same length). Map is wider than terminal for scrolling */
const char *level_map[] = {
    "                                                                                ",
    "                                                                                ",
    "                                                                                ",
    "                                                                                ",
    "                                                                                ",
    "                                                                                ",
    "                                                                                ",
    "                           o                                                    ",
    "                #####                                                           ",
    "                                                                                ",
    "         o                                                                      ",
    "    #######                                                    >               ",
    "                                                                                ",
    "                              ###                                             ",
    "                                                                                ",
    "                o                                                               ",
    "           #####                                                                ",
    "                                                                                ",
    "                                                             E                ",
    "============================================     =============================",
    "                                                                                ",
    "                                                                                ",
    NULL
};

/* Globals */
int term_w, term_h;
int map_w = 0, map_h = 0;
char **map_data = NULL;
Player player;
Enemy enemies[MAX_ENEMIES];
int enemy_count = 0;
int cam_x = 0; /* camera top-left x (cols) */
int paused = 0;
int game_over = 0;
int win = 0;

/* Prototypes */
void init_ncurses();
void cleanup();
void load_map(const char **src);
char map_at(int mx, int my);
void set_map_char(int mx, int my, char c);
void spawn_enemies_from_map();
void draw();
void update_physics();
void handle_input();
int tile_collide(int tx, int ty); /* collidable tile check */
void update_enemies();
void check_enemy_collisions();
void reset_level();

int main() {
    srand((unsigned)time(NULL));
    init_ncurses();
    load_map(level_map);
    reset_level();

    while (!game_over) {
        int ch = getch();
        if (ch != ERR) {
            ungetch(ch);
        }

        handle_input();

        if (!paused && !win) {
            update_physics();
            update_enemies();
            check_enemy_collisions();
        }

        draw();

        if (win || game_over) {
            /* small pause to show final state */
            nodelay(stdscr, FALSE);
            mvprintw(term_h/2, term_w/2 - 6, win ? " YOU WIN! " : " GAME OVER ");
            mvprintw(term_h/2 + 1, term_w/2 - 12, "Press 'q' to quit or 'r' to restart");
            refresh();
            int c = getch();
            if (c == 'q' || c == 'Q') break;
            if (c == 'r' || c == 'R') {
                reset_level();
                nodelay(stdscr, TRUE);
                continue;
            }
            nodelay(stdscr, TRUE);
        }

        usleep(TICK_US);
    }

    cleanup();
    return 0;
}

/* Initialize ncurses and colors */
void init_ncurses() {
    initscr();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    getmaxyx(stdscr, term_h, term_w);

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_YELLOW, COLOR_BLACK); /* player */
        init_pair(2, COLOR_WHITE, COLOR_BLACK);  /* tiles */
        init_pair(3, COLOR_CYAN, COLOR_BLACK);   /* hud */
        init_pair(4, COLOR_MAGENTA, COLOR_BLACK);/* coins */
        init_pair(5, COLOR_RED, COLOR_BLACK);    /* enemy */
        init_pair(6, COLOR_GREEN, COLOR_BLACK);  /* exit */
    }
}

/* Free and end ncurses */
void cleanup() {
    if (map_data) {
        for (int i=0;i<map_h;i++) free(map_data[i]);
        free(map_data);
        map_data = NULL;
    }
    endwin();
}

/* Copy map strings into mutable 2D char array */
void load_map(const char **src) {
    /* count height and width */
    map_h = 0;
    map_w = 0;
    while (src[map_h]) {
        int len = strlen(src[map_h]);
        if (len > map_w) map_w = len;
        map_h++;
    }
    if (map_h == 0) return;

    map_data = malloc(sizeof(char*) * map_h);
    for (int r=0;r<map_h;r++) {
        map_data[r] = malloc(map_w + 1);
        int len = strlen(src[r]);
        for (int c=0;c<map_w;c++) {
            if (c < len) map_data[r][c] = src[r][c];
            else map_data[r][c] = ' ';
        }
        map_data[r][map_w] = '\0';
    }
}

/* Helper: safe get char from map */
char map_at(int mx, int my) {
    if (mx < 0 || my < 0 || my >= map_h || mx >= map_w) return ' ';
    return map_data[my][mx];
}

/* Set map char (for coins removal, etc.) */
void set_map_char(int mx, int my, char c) {
    if (mx < 0 || my < 0 || my >= map_h || mx >= map_w) return;
    map_data[my][mx] = c;
}

/* Place player and parse enemies */
void reset_level() {
    /* find nice spawn position: first row from top where ground appears */
    int px = 2, py = 2;
    for (int r=0;r<map_h;r++) {
        for (int c=0;c<map_w;c++) {
            char ch = map_at(c, r);
            if (ch == '=' || ch == '#') {
                /* place player a few tiles above first ground found */
                px = c;
                py = r - 2;
                if (py < 1) py = 1;
                goto placed;
            }
        }
    }
placed:
    player.x = (float)px + 0.5f;
    player.y = (float)py;
    player.vy = 0;
    player.facing = 1;
    player.on_ground = 0;
    player.lives = 3;
    player.score = 0;
    cam_x = 0;
    paused = 0;
    game_over = 0;
    win = 0;

    /* init enemies */
    enemy_count = 0;
    for (int i=0;i<MAX_ENEMIES;i++) enemies[i].alive = 0;
    spawn_enemies_from_map();
}

/* Scan map and create enemy structs for 'E' characters */
void spawn_enemies_from_map() {
    for (int r=0;r<map_h;r++) {
        for (int c=0;c<map_w;c++) {
            if (map_at(c,r) == 'E') {
                if (enemy_count < MAX_ENEMIES) {
                    enemies[enemy_count].alive = 1;
                    enemies[enemy_count].x = c;
                    enemies[enemy_count].y = r;
                    enemies[enemy_count].dir = (rand()%2)?1:-1;
                    /* set patrol bounds: walk until hits non-empty tile below or wall */
                    int L=c, R=c;
                    /* expand left */
                    while (L-1 >=0 && map_at(L-1, r) == ' ') L--;
                    while (R+1 < map_w && map_at(R+1, r) == ' ') R++;
                    enemies[enemy_count].left_bound = L;
                    enemies[enemy_count].right_bound = R;
                    enemy_count++;
                }
                /* clear the map marker; enemy tracked separately */
                set_map_char(c,r,' ');
            }
        }
    }
}

/* Draw visible slice of map + entities + HUD */
void draw() {
    clear();
    getmaxyx(stdscr, term_h, term_w);

    /* camera: center horizontally on player (try to keep player near 1/3 from left) */
    int margin = term_w / 3;
    int target_cam = (int)player.x - margin;
    if (target_cam < 0) target_cam = 0;
    if (target_cam > map_w - term_w) target_cam = map_w - term_w;
    if (map_w <= term_w) target_cam = 0;
    cam_x = target_cam;

    /* draw map */
    for (int r=0;r<term_h-2 && r < map_h; r++) {
        for (int c=0;c<term_w && (cam_x + c) < map_w; c++) {
            char ch = map_at(cam_x + c, r);
            if (ch == '#') {
                attron(COLOR_PAIR(2));
                mvaddch(r, c, '#');
                attroff(COLOR_PAIR(2));
            } else if (ch == '=') {
                attron(COLOR_PAIR(2));
                mvaddch(r, c, '=');
                attroff(COLOR_PAIR(2));
            } else if (ch == 'o') {
                attron(COLOR_PAIR(4));
                mvaddch(r, c, 'o');
                attroff(COLOR_PAIR(4));
            } else if (ch == '>') {
                attron(COLOR_PAIR(6));
                mvaddch(r, c, '>');
                attroff(COLOR_PAIR(6));
            } else {
                mvaddch(r, c, ' ');
            }
        }
    }

    /* draw enemies */
    for (int i=0;i<enemy_count;i++) {
        if (!enemies[i].alive) continue;
        int sx = enemies[i].x - cam_x;
        int sy = enemies[i].y;
        if (sx >=0 && sx < term_w && sy >=0 && sy < term_h-2) {
            attron(COLOR_PAIR(5));
            mvaddch(sy, sx, 'E');
            attroff(COLOR_PAIR(5));
        }
    }

    /* draw player - player displayed as 3-char: <^> */
    int px = (int)floor(player.x) - cam_x;
    int py = (int)floor(player.y);
    if (px >= -2 && px < term_w+2 && py >=0 && py < term_h-2) {
        attron(COLOR_PAIR(1));
        mvprintw(py, px, "<^>");
        attroff(COLOR_PAIR(1));
    }

    /* HUD */
    attron(COLOR_PAIR(3));
    mvprintw(term_h-2, 0, " Score: %d  Lives: %d  Pos: (%.1f,%.1f)  Press P to pause, Q to quit ",
             player.score, player.lives, player.x, player.y);
    attroff(COLOR_PAIR(3));

    if (paused) {
        mvprintw(term_h/2, term_w/2 - 6, "== PAUSED ==");
    }

    refresh();
}

/* Check collidable tiles; returns 1 if tile is solid */
int tile_collide(int tx, int ty) {
    char ch = map_at(tx, ty);
    if (ch == '#' || ch == '=') return 1;
    return 0;
}

/* Physics: horizontal movement is instant tile steps; vertical uses float vy */
void update_physics() {
    /* horizontal input handled directly in handle_input by moving x by MOVE_SPEED steps,
       but we still must ensure player doesn't walk into tiles. */

    /* apply gravity */
    player.vy += GRAVITY;
    float new_y = player.y + player.vy * 0.1f; /* scale down for smoother motion */

    /* We'll test collisions at future y. Player occupies 3 columns wide centered at player.x (we print "<^>" at floor(x) ) */
    int left_tile = (int)floor(player.x - 1.0f);
    int right_tile = (int)floor(player.x + 1.0f);
    int foot_tile = (int)floor(new_y + 0.9f); /* foot position */

    /* vertical collision detection (downwards) */
    if (player.vy >= 0) {
        if (tile_collide(left_tile, foot_tile) || tile_collide(right_tile, foot_tile)) {
            /* land on ground: snap to just above tile */
            player.on_ground = 1;
            player.vy = 0;
            player.y = (float)foot_tile - 1.0f; /* stand on top */
        } else {
            player.on_ground = 0;
            player.y = new_y;
        }
    } else { /* moving up */
        int head_tile = (int)floor(new_y);
        if (tile_collide(left_tile, head_tile) || tile_collide(right_tile, head_tile)) {
            /* hit head */
            player.vy = 0;
            /* push player down a bit */
            player.y = (float)head_tile + 1.0f;
        } else {
            player.y = new_y;
            player.on_ground = 0;
        }
    }

    /* Prevent falling below map */
    if (player.y > map_h - 1) {
        player.lives--;
        if (player.lives <= 0) game_over = 1;
        else {
            /* reset to spawn */
            reset_level();
        }
    }

    /* coin pickup & exit detection: check tile under player's feet and current tile */
    int px_floor = (int)floor(player.x);
    int py_floor = (int)floor(player.y + 0.9f);
    char under = map_at(px_floor, py_floor);
    if (under == 'o') {
        player.score += 5;
        set_map_char(px_floor, py_floor, ' ');
    } else if (under == '>') {
        win = 1;
    }
}

/* Input processed separately (horizontal movement & jump commands) */
void handle_input() {
    int ch;
    while ((ch = getch()) != ERR) {
        if (ch == 'p' || ch == 'P') {
            paused = !paused;
            continue;
        }
        if (ch == 'q' || ch == 'Q') {
            game_over = 1;
            return;
        }
        if (paused || win) continue;
        switch (ch) {
            case KEY_LEFT:
            case 'a': case 'A': {
                /* attempt move left by MOVE_SPEED */
                float nx = player.x - MOVE_SPEED;
                int left_tile = (int)floor(nx - 1.0f);
                int y_top = (int)floor(player.y);
                int y_bot = (int)floor(player.y + 0.9f);
                if (!tile_collide(left_tile, y_top) && !tile_collide(left_tile, y_bot)) {
                    player.x = nx;
                } else {
                    /* blocked - align to right of tile */
                    player.x = (float)(left_tile + 1) + 1.0f;
                }
                player.facing = -1;
            } break;
            case KEY_RIGHT:
            case 'd': case 'D': {
                float nx = player.x + MOVE_SPEED;
                int right_tile = (int)floor(nx + 1.0f);
                int y_top = (int)floor(player.y);
                int y_bot = (int)floor(player.y + 0.9f);
                if (!tile_collide(right_tile, y_top) && !tile_collide(right_tile, y_bot)) {
                    player.x = nx;
                } else {
                    /* blocked - align to left of tile */
                    player.x = (float)(right_tile - 1) - 1.0f;
                }
                player.facing = 1;
            } break;
            case ' ':
            case 'w': case 'W':
            case KEY_UP: {
                if (player.on_ground) {
                    player.vy = JUMP_VELO;
                    player.on_ground = 0;
                }
            } break;
            default:
                break;
        }
    }
}

/* Move enemies simply inside their patrol bound and check collisions with tiles */
void update_enemies() {
    for (int i=0;i<enemy_count;i++) {
        if (!enemies[i].alive) continue;
        Enemy *e = &enemies[i];
        /* simple patrol: move if next tile is free and within bounds */
        int nx = e->x + e->dir;
        if (nx < e->left_bound || nx > e->right_bound) {
            e->dir = -e->dir;
            nx = e->x + e->dir;
        }
        /* if tile below next position is empty but tile at next pos is empty, it's ok.
           we keep enemies on same row (they don't fall in this simple engine) */
        if (!tile_collide(nx, e->y)) e->x = nx;
        else e->dir = -e->dir;
    }
}

/* Check collisions between player and enemies (tolerant) */
void check_enemy_collisions() {
    int ptx = (int)floor(player.x);
    int pty = (int)floor(player.y + 0.5f);
    for (int i=0;i<enemy_count;i++) {
        if (!enemies[i].alive) continue;
        int ex = enemies[i].x;
        int ey = enemies[i].y;
        /* if player's foot is on same tile as enemy, treat as collision */
        if (abs(ex - ptx) <= 1 && abs(ey - pty) <= 1) {
            /* if player is falling and hitting from above, kill enemy; else player hurt */
            if (player.vy > 0 && (player.y + 0.9f) - ey < 0.75f) {
                /* stomp */
                enemies[i].alive = 0;
                player.score += 20;
                /* bounce player up slightly */
                player.vy = JUMP_VELO * 0.6f;
                player.on_ground = 0;
            } else {
                /* hurt player */
                player.lives--;
                if (player.lives <= 0) game_over = 1;
                else {
                    /* respawn player to start */
                    player.x = 2.5f;
                    player.y = 2.0f;
                    player.vy = 0;
                }
            }
        }
    }
}

