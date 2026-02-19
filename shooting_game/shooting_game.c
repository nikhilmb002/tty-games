#include <ncurses.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#define TICK_US 40000

#define PLAYER_LIVES 3
#define PLAYER_COLOR 1
#define ENEMY_COLOR 2
#define BULLET_COLOR 3
#define ENEMY_BULLET_COLOR 4
#define TEXT_COLOR 5
#define MENU_COLOR 6

typedef struct Bullet {
    int x, y;
    int dy;
    struct Bullet *next;
} Bullet;

typedef struct Enemy {
    int x, y;
    int tick_counter;
    int speed_ticks;
    struct Enemy *next;
} Enemy;

typedef struct {
    int x, y;
    int lives;
    int score;
} Player;

/* GLOBALS */
int max_x, max_y;
Player player;
Enemy *enemies = NULL;
Bullet *bullets = NULL;

int game_over = 0;
int paused = 0;

/* Difficulty variables */
int spawn_rate;
int enemy_speed;
int enemy_fire_chance;

int spawn_counter = 0;

/* ----------- PROTOTYPES ----------- */
void init_game();
void draw_border();
void draw_hud();
void draw_entities();
void update_enemies();
void update_bullets();
void check_collisions();
void process_input();
void spawn_enemy();
void add_enemy(int x,int y,int speed);
void add_bullet(int x,int y,int dy);
void remove_enemy(Enemy *prev, Enemy *e);
void remove_bullet(Bullet *prev, Bullet *b);
void clear_lists();
int show_menu();

/* ----------- MAIN ----------- */
int main() {
    srand(time(NULL));
    initscr();
    noecho();
    curs_set(FALSE);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    getmaxyx(stdscr, max_y, max_x);

    start_color();
    init_pair(PLAYER_COLOR, COLOR_GREEN, COLOR_BLACK);
    init_pair(ENEMY_COLOR, COLOR_RED, COLOR_BLACK);
    init_pair(BULLET_COLOR, COLOR_YELLOW, COLOR_BLACK);
    init_pair(ENEMY_BULLET_COLOR, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(TEXT_COLOR, COLOR_CYAN, COLOR_BLACK);
    init_pair(MENU_COLOR, COLOR_MAGENTA, COLOR_BLACK);

    /* SHOW DIFFICULTY MENU */
    int level = show_menu();

    switch(level) {
        case 1: /* EASY */
            spawn_rate = 80;
            enemy_speed = 12;
            enemy_fire_chance = 400;
            break;
        case 2: /* MEDIUM */
            spawn_rate = 50;
            enemy_speed = 8;
            enemy_fire_chance = 200;
            break;
        case 3: /* HARD */
            spawn_rate = 25;
            enemy_speed = 5;
            enemy_fire_chance = 80;
            break;
    }

    init_game();

    while(!game_over) {

        process_input();

        if(!paused) {
            spawn_counter++;
            if(spawn_counter >= spawn_rate) {
                spawn_enemy();
                spawn_counter = 0;
            }

            update_enemies();
            update_bullets();
            check_collisions();
        }

        clear();
        draw_border();
        draw_hud();
        draw_entities();
        refresh();

        usleep(TICK_US);
    }

    clear_lists();
    endwin();
    printf("Final Score: %d\n", player.score);
    return 0;
}

/* -------- MENU -------- */
int show_menu() {
    int choice = 1;
    int ch;
    nodelay(stdscr, FALSE);

    while(1) {
        clear();
        attron(COLOR_PAIR(TEXT_COLOR));
        mvprintw(max_y/2 - 4, max_x/2 - 6, "ASCII SHOOTER");
        attroff(COLOR_PAIR(TEXT_COLOR));

        mvprintw(max_y/2 - 1, max_x/2 - 8, "Select Difficulty:");

        if(choice==1) attron(COLOR_PAIR(MENU_COLOR));
        mvprintw(max_y/2 + 1, max_x/2 - 4, "1. Easy");
        if(choice==1) attroff(COLOR_PAIR(MENU_COLOR));

        if(choice==2) attron(COLOR_PAIR(MENU_COLOR));
        mvprintw(max_y/2 + 2, max_x/2 - 4, "2. Medium");
        if(choice==2) attroff(COLOR_PAIR(MENU_COLOR));

        if(choice==3) attron(COLOR_PAIR(MENU_COLOR));
        mvprintw(max_y/2 + 3, max_x/2 - 4, "3. Hard");
        if(choice==3) attroff(COLOR_PAIR(MENU_COLOR));

        mvprintw(max_y/2 + 5, max_x/2 - 12, "Use UP/DOWN + ENTER");
        refresh();

        ch = getch();
        if(ch==KEY_UP && choice>1) choice--;
        else if(ch==KEY_DOWN && choice<3) choice++;
        else if(ch=='\n') break;
    }

    nodelay(stdscr, TRUE);
    return choice;
}

/* -------- GAME LOGIC -------- */
void init_game() {
    player.x = max_x/2;
    player.y = max_y - 3;
    player.lives = PLAYER_LIVES;
    player.score = 0;
}

void draw_border() {
    attron(COLOR_PAIR(TEXT_COLOR));
    for(int i=0;i<max_x;i++){
        mvaddch(1,i,'-');
        mvaddch(max_y-2,i,'-');
    }
    attroff(COLOR_PAIR(TEXT_COLOR));
}

void draw_hud() {
    attron(COLOR_PAIR(TEXT_COLOR));
    mvprintw(0,2,"Score:%d Lives:%d",player.score,player.lives);
    mvprintw(max_y-1,2,"Arrows Move | Space Shoot | P Pause | Q Quit");
    if(paused) mvprintw(max_y/2,max_x/2-5,"PAUSED");
    attroff(COLOR_PAIR(TEXT_COLOR));
}

void spawn_enemy() {
    int x = rand()%(max_x-4)+2;
    add_enemy(x,3,enemy_speed);
}

void add_enemy(int x,int y,int speed) {
    Enemy *e = malloc(sizeof(Enemy));
    e->x=x; e->y=y;
    e->tick_counter=0;
    e->speed_ticks=speed;
    e->next=enemies;
    enemies=e;
}

void add_bullet(int x,int y,int dy){
    Bullet *b=malloc(sizeof(Bullet));
    b->x=x; b->y=y; b->dy=dy;
    b->next=bullets;
    bullets=b;
}

void update_enemies(){
    Enemy *e=enemies,*prev=NULL;
    while(e){
        e->tick_counter++;
        if(e->tick_counter>=e->speed_ticks){
            e->tick_counter=0;
            e->y++;
        }

        if(rand()%enemy_fire_chance==0)
            add_bullet(e->x,e->y+1,1);

        if(e->y>=max_y-3){
            player.lives--;
            Enemy *tmp=e;
            e=e->next;
            remove_enemy(prev,tmp);
            if(player.lives<=0) game_over=1;
            continue;
        }
        prev=e;
        e=e->next;
    }
}

void update_bullets(){
    Bullet *b=bullets,*prev=NULL;
    while(b){
        b->y+=b->dy;
        if(b->y<=2||b->y>=max_y-2){
            Bullet *tmp=b;
            b=b->next;
            remove_bullet(prev,tmp);
            continue;
        }
        prev=b;
        b=b->next;
    }
}

void draw_entities(){
    attron(COLOR_PAIR(PLAYER_COLOR));
    mvprintw(player.y,player.x-1,"<^>");
    attroff(COLOR_PAIR(PLAYER_COLOR));

    Enemy *e=enemies;
    attron(COLOR_PAIR(ENEMY_COLOR));
    while(e){
        mvaddch(e->y,e->x,'W');
        e=e->next;
    }
    attroff(COLOR_PAIR(ENEMY_COLOR));

    Bullet *b=bullets;
    while(b){
        if(b->dy<0){
            attron(COLOR_PAIR(BULLET_COLOR));
            mvaddch(b->y,b->x,'|');
            attroff(COLOR_PAIR(BULLET_COLOR));
        } else {
            attron(COLOR_PAIR(ENEMY_BULLET_COLOR));
            mvaddch(b->y,b->x,'!');
            attroff(COLOR_PAIR(ENEMY_BULLET_COLOR));
        }
        b=b->next;
    }
}

void process_input(){
    int ch;
    while((ch=getch())!=ERR){
        if(ch==KEY_LEFT && player.x>2) player.x-=2;
        else if(ch==KEY_RIGHT && player.x<max_x-3) player.x+=2;
        else if(ch==' ') add_bullet(player.x,player.y-1,-1);
        else if(ch=='p'||ch=='P') paused=!paused;
        else if(ch=='q'||ch=='Q') game_over=1;
    }
}

void check_collisions(){
    Bullet *b=bullets,*bprev=NULL;
    while(b){
        if(b->dy<0){
            Enemy *e=enemies,*eprev=NULL;
            while(e){
                if(e->y==b->y && abs(e->x-b->x)<=1){
                    player.score+=10;
                    Enemy *etmp=e;
                    e=e->next;
                    remove_enemy(eprev,etmp);
                    Bullet *btmp=b;
                    b=b->next;
                    remove_bullet(bprev,btmp);
                    goto nextbullet;
                }
                eprev=e;
                e=e->next;
            }
        } else {
            if(b->y==player.y && abs(b->x-player.x)<=1){
                player.lives--;
                Bullet *btmp=b;
                b=b->next;
                remove_bullet(bprev,btmp);
                if(player.lives<=0) game_over=1;
                continue;
            }
        }
        bprev=b;
        b=b->next;
        nextbullet:;
    }
}

void remove_enemy(Enemy *prev, Enemy *e){
    if(!prev) enemies=e->next;
    else prev->next=e->next;
    free(e);
}

void remove_bullet(Bullet *prev, Bullet *b){
    if(!prev) bullets=b->next;
    else prev->next=b->next;
    free(b);
}

void clear_lists(){
    Enemy *e=enemies;
    while(e){Enemy *n=e->next;free(e);e=n;}
    Bullet *b=bullets;
    while(b){Bullet *n=b->next;free(b);b=n;}
}

