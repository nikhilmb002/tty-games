/* ascii_shooter.c
 *
 * Terminal ASCII Shooter using ncurses
 * Controls:
 *   Left/Right arrow or 'a'/'d' -> move
 *   Space                       -> shoot
 *   p / P                       -> pause
 *   q / Q                       -> quit
 *
 * Features:
 * - Player, enemies, bullets (player + enemy)
 * - Levels with increasing spawn rate & speed
 * - Score & lives
 * - Colorized ASCII graphics
 *
 * Compile:
 *   gcc ascii_shooter.c -o ascii_shooter -lncurses
 */

#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#define TICK_US           40000   /* main loop tick (40ms) ~ 25 FPS */

/* Gameplay tuning */
#define INITIAL_SPAWN_RATE 40     /* lower => more frequent spawns (ticks) */
#define MIN_SPAWN_RATE     8
#define SPAWN_DECREASE_EVERY 30   /* ticks to reduce spawn rate by 1 */
#define ENEMY_SPEED_INITIAL 8     /* enemy moves one step per N ticks */
#define BULLET_SPEED        1     /* bullets move every tick */
#define PLAYER_LIVES        3
#define PLAYER_SHIP_CHAR   "^"
#define PLAYER_COLOR       1
#define ENEMY_COLOR        2
#define BULLET_COLOR       3
#define ENEMY_BULLET_COLOR 4
#define TEXT_COLOR         5

/* Linked lists for dynamic objects */
typedef struct Bullet {
    int x, y;
    int dy;                /* direction: -1 up, +1 down */
    struct Bullet *next;
} Bullet;

typedef struct Enemy {
    int x, y;
    int tick_counter;      /* to control speed */
    int speed_ticks;       /* move every speed_ticks ticks */
    struct Enemy *next;
} Enemy;

typedef struct {
    int x, y;
    int lives;
    int score;
} Player;

/* Global state */
int max_x, max_y;
Player player;
Enemy *enemies = NULL;
Bullet *bullets = NULL;      /* player's bullets and enemy bullets combined */
int spawn_counter = 0;
int spawn_rate = INITIAL_SPAWN_RATE;
int tick_count = 0;
int game_over = 0;
int paused = 0;
int level = 1;
int enemy_speed = ENEMY_SPEED_INITIAL;

/* Function prototypes */
void init_game();
void teardown();
void draw_border();
void draw_hud();
void spawn_enemy();
void update_enemies();
void update_bullets();
void draw_entities();
void add_bullet(int x, int y, int dy);
void add_enemy(int x, int y, int speed_ticks);
void remove_enemy(Enemy *prev, Enemy *e);
void remove_bullet(Bullet *prev, Bullet *b);
void process_input();
void check_collisions();
void clear_lists();

int absdiff(int a, int b) { return (a>b)? (a-b):(b-a); }

int main() {
    srand(time(NULL));
    initscr();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    getmaxyx(stdscr, max_y, max_x);

    if (!has_colors()) {
        endwin();
        fprintf(stderr, "Terminal does not support colors.\n");
        return 1;
    }

    start_color();
    init_pair(PLAYER_COLOR, COLOR_GREEN, COLOR_BLACK);
    init_pair(ENEMY_COLOR, COLOR_RED, COLOR_BLACK);
    init_pair(BULLET_COLOR, COLOR_YELLOW, COLOR_BLACK);
    init_pair(ENEMY_BULLET_COLOR, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(TEXT_COLOR, COLOR_CYAN, COLOR_BLACK);

    init_game();

    while (!game_over) {
        int ch = getch();
        if (ch != ERR) {
            ungetch(ch);  /* push back for process_input to read uniformly */
        }

        process_input();

        if (!paused) {
            tick_count++;
            /* spawn logic */
            spawn_counter++;
            if (spawn_counter >= spawn_rate) {
                spawn_enemy();
                spawn_counter = 0;
            }
            /* difficulty scaling */
            if (tick_count % SPAWN_DECREASE_EVERY == 0 && spawn_rate > MIN_SPAWN_RATE) {
                spawn_rate--;
                level++;
                /* slight speed up for enemies */
                if (enemy_speed > 2) enemy_speed--;
            }

            update_enemies();
            update_bullets();
            check_collisions();
        }

        /* render */
        clear(); /* full redraw - acceptable for ASCII ncurses games */
        draw_border();
        draw_hud();
        draw_entities();
        refresh();

        /* small sleep */
        usleep(TICK_US);
    }

    teardown();
    return 0;
}

/* Initialize player and state */
void init_game() {
    player.lives = PLAYER_LIVES;
    player.score = 0;
    player.x = max_x / 2;
    player.y = max_y - 3;  /* keep room for HUD & border */

    spawn_counter = 0;
    spawn_rate = INITIAL_SPAWN_RATE;
    tick_count = 0;
    game_over = 0;
    paused = 0;
    level = 1;
    enemy_speed = ENEMY_SPEED_INITIAL;

    enemies = NULL;
    bullets = NULL;
}

/* Free lists and end ncurses */
void teardown() {
    clear_lists();
    endwin();
    /* show final stats on stdout after ncurses ends */
    printf("Game Over! Final Score: %d\n", player.score);
}

/* Draw borders around play area */
void draw_border() {
    attron(COLOR_PAIR(TEXT_COLOR));
    for (int i = 0; i < max_x; ++i) {
        mvaddch(1, i, '-');            /* top border (below HUD) */
        mvaddch(max_y - 2, i, '-');    /* bottom border */
    }
    for (int i = 1; i < max_y - 1; ++i) {
        mvaddch(i, 0, '|');
        mvaddch(i, max_x - 1, '|');
    }
    mvaddch(1, 0, '+'); mvaddch(1, max_x-1, '+');
    mvaddch(max_y-2, 0, '+'); mvaddch(max_y-2, max_x-1, '+');
    attroff(COLOR_PAIR(TEXT_COLOR));
}

/* Heads-up display: score, lives, level, instructions */
void draw_hud() {
    attron(COLOR_PAIR(TEXT_COLOR));
    mvprintw(0, 2, " ASCII Shooter ");
    mvprintw(0, max_x - 30, "Score: %d  Lives: %d  Level: %d", player.score, player.lives, level);
    mvprintw(max_y - 1, 2, "Arrows/A-D: Move  Space: Shoot  P: Pause  Q: Quit");
    if (paused) {
        mvprintw(max_y/2, max_x/2 - 6, "== PAUSED ==");
    }
    attroff(COLOR_PAIR(TEXT_COLOR));
}

/* Spawn a new enemy at a random x at top area */
void spawn_enemy() {
    int margin = 2;
    int x = (rand() % (max_x - margin*2 - 2)) + margin + 1;
    int y = 3; /* spawn below top HUD/border */
    int speed_ticks = enemy_speed; /* move every speed_ticks ticks */
    add_enemy(x, y, speed_ticks);
}

/* Add enemy to list */
void add_enemy(int x, int y, int speed_ticks) {
    Enemy *e = malloc(sizeof(Enemy));
    if (!e) return;
    e->x = x;
    e->y = y;
    e->tick_counter = 0;
    e->speed_ticks = speed_ticks;
    e->next = enemies;
    enemies = e;
}

/* Add bullet to list */
void add_bullet(int x, int y, int dy) {
    Bullet *b = malloc(sizeof(Bullet));
    if (!b) return;
    b->x = x;
    b->y = y;
    b->dy = dy;
    b->next = bullets;
    bullets = b;
}

/* Update enemy positions and maybe fire bullets */
void update_enemies() {
    Enemy *e = enemies;
    Enemy *prev = NULL;
    while (e) {
        e->tick_counter++;
        if (e->tick_counter >= e->speed_ticks) {
            e->tick_counter = 0;
            e->y += 1;
        }

        /* very simple enemy behavior: occasionally fire a bullet downward */
        if ((rand() % 200) == 0) {
            add_bullet(e->x, e->y+1, +1); /* enemy bullet */
        }

        /* if enemy reached bottom (player zone), penalize */
        if (e->y >= max_y - 3) {
            /* enemy passed player */
            player.lives--;
            /* remove this enemy */
            Enemy *to_remove = e;
            e = e->next;
            remove_enemy(prev, to_remove);
            if (player.lives <= 0) game_over = 1;
            continue;
        }

        prev = e;
        e = e->next;
    }
}

/* Update bullets (both player and enemy bullets) */
void update_bullets() {
    Bullet *b = bullets;
    Bullet *prev = NULL;
    while (b) {
        /* Move bullet */
        b->y += b->dy * BULLET_SPEED;

        /* Remove bullet if off-screen */
        if (b->y <= 2 || b->y >= max_y - 2) {
            Bullet *to_remove = b;
            b = b->next;
            remove_bullet(prev, to_remove);
            continue;
        }

        prev = b;
        b = b->next;
    }
}

/* Draw player, enemies, bullets */
void draw_entities() {
    /* player */
    attron(COLOR_PAIR(PLAYER_COLOR));
    mvprintw(player.y, player.x - 1, "<%s>", PLAYER_SHIP_CHAR); /* ship looks like <^> */
    attroff(COLOR_PAIR(PLAYER_COLOR));

    /* enemies */
    attron(COLOR_PAIR(ENEMY_COLOR));
    Enemy *e = enemies;
    while (e) {
        mvaddch(e->y, e->x, 'W');   /* enemy ASCII char (single column) */
        e = e->next;
    }
    attroff(COLOR_PAIR(ENEMY_COLOR));

    /* bullets - differentiate by dy */
    Bullet *b = bullets;
    while (b) {
        if (b->dy < 0) {
            attron(COLOR_PAIR(BULLET_COLOR));
            mvaddch(b->y, b->x, '|');   /* player bullet */
            attroff(COLOR_PAIR(BULLET_COLOR));
        } else {
            attron(COLOR_PAIR(ENEMY_BULLET_COLOR));
            mvaddch(b->y, b->x, '!');   /* enemy bullet */
            attroff(COLOR_PAIR(ENEMY_BULLET_COLOR));
        }
        b = b->next;
    }
}

/* Remove enemy from list given previous pointer; if prev==NULL target is head */
void remove_enemy(Enemy *prev, Enemy *e) {
    if (!e) return;
    if (!prev) {
        enemies = e->next;
    } else {
        prev->next = e->next;
    }
    free(e);
}

/* Remove bullet and fix list */
void remove_bullet(Bullet *prev, Bullet *b) {
    if (!b) return;
    if (!prev) {
        bullets = b->next;
    } else {
        prev->next = b->next;
    }
    free(b);
}

/* Main input processing (reading from getch) */
void process_input() {
    int ch;
    while ((ch = getch()) != ERR) {
        switch (ch) {
            case KEY_LEFT:
            case 'a':
            case 'A':
                if (!paused) {
                    if (player.x > 2) player.x -= 2;
                }
                break;
            case KEY_RIGHT:
            case 'd':
            case 'D':
                if (!paused) {
                    if (player.x < max_x - 3) player.x += 2;
                }
                break;
            case ' ':
                if (!paused) {
                    /* add bullet slightly above the ship center */
                    add_bullet(player.x, player.y - 1, -1);
                }
                break;
            case 'p':
            case 'P':
                paused = !paused;
                break;
            case 'q':
            case 'Q':
                game_over = 1;
                break;
            default:
                break;
        }
    }
}

/* Collision detection:
 * - Player bullets vs enemies (tolerant in x by ±1)
 * - Enemy bullets vs player
 * - Enemy touching player (tolerant in x by ±1)
 */
void check_collisions() {
    /* Player bullets hitting enemies */
    Bullet *pb = bullets;
    Bullet *pb_prev = NULL;
    while (pb) {
        int removed_bullet = 0;
        if (pb->dy < 0) { /* player bullet */
            Enemy *e = enemies;
            Enemy *e_prev = NULL;
            while (e) {
                /* tolerant collision: if bullet row matches enemy row and x within 1 column */
                if (e->y == pb->y && absdiff(e->x, pb->x) <= 1) {
                    /* hit */
                    player.score += 10;
                    /* remove enemy e and bullet pb */
                    Enemy *to_remove_e = e;
                    e = e->next;
                    remove_enemy(e_prev, to_remove_e);
                    /* remove pb */
                    Bullet *to_remove_b = pb;
                    pb = pb->next;
                    remove_bullet(pb_prev, to_remove_b);
                    removed_bullet = 1;
                    break;
                }
                e_prev = e;
                e = e->next;
            }
            if (removed_bullet) continue;
        } else {
            /* Enemy bullet - check collision with player ship area (player spans x-1..x+1) */
            if (pb->y == player.y && (pb->x >= player.x - 1 && pb->x <= player.x + 1)) {
                player.lives--;
                /* remove this bullet */
                Bullet *to_remove_b = pb;
                pb = pb->next;
                remove_bullet(pb_prev, to_remove_b);
                if (player.lives <= 0) game_over = 1;
                continue;
            }
        }
        pb_prev = pb;
        if (pb) pb = pb->next;
    }

    /* Enemies colliding into player ship (touching) - tolerant in x by ±1 */
    Enemy *e = enemies;
    Enemy *e_prev = NULL;
    while (e) {
        if (e->y == player.y && absdiff(e->x, player.x) <= 1) {
            /* collision */
            player.lives--;
            Enemy *to_remove_e = e;
            e = e->next;
            remove_enemy(e_prev, to_remove_e);
            if (player.lives <= 0) {
                game_over = 1;
                return;
            }
            continue;
        }
        e_prev = e;
        e = e->next;
    }
}

/* Free enemy and bullet lists */
void clear_lists() {
    Enemy *e = enemies;
    while (e) {
        Enemy *n = e->next;
        free(e);
        e = n;
    }
    enemies = NULL;

    Bullet *b = bullets;
    while (b) {
        Bullet *n = b->next;
        free(b);
        b = n;
    }
    bullets = NULL;
}

