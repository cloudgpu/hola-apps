/*
 * pacman.c — A simple Pacman clone written entirely in C, using ncurses.
 *
 * Build:  cc pacman.c -o pacman -lncurses
 * Run:    ./pacman
 *
 * Controls:
 *   Arrow keys / WASD  move Pacman
 *   p                  pause
 *   q                  quit
 */

#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ROWS   21
#define COLS   28

/* Maze legend:
 *   '#'  wall
 *   '.'  pellet
 *   'o'  power pellet
 *   ' '  empty (ghost house / tunnels)
 *   'P'  pacman
 *   'G'  ghost
 */
static const char MAZE[ROWS][COLS + 1] = {
    "############################",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#o####.#####.##.#####.####o#",
    "#.####.#####.##.#####.####.#",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#......##....##....##......#",
    "######.##### ## #####.######",
    "     #.##### ## #####.#     ",
    "######.##### ## #####.######",
    "#......##...G  H..##.......#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#o..##..............##...o#",
    "###.##.######.######.##.###",
    "#......##....##....##......#",
    "#.##########.##.##########.#",
    "#..........................#",
    "############################"
};

/* Ghost house door (open/closed). We'll treat row 12 col 13/14 as door. */
#define DOOR_R 12
#define DOOR_C1 13
#define DOOR_C2 14

typedef struct {
    int y, x;
    int dir;            /* 0 up 1 down 2 left 3 right */
} Entity;

static char board[ROWS][COLS + 1];
static Entity pac;
static Entity ghosts[4];
static int ghost_mode = 0;      /* 0 scatter/chase, 1 frightened */
static int scared_timer = 0;
static int score = 0;
static int lives = 3;
static int pellets_left = 0;
static int level = 1;
static int paused = 0;
static int gameover = 0;

static void init_board(void)
{
    int y, x;
    pellets_left = 0;
    for (y = 0; y < ROWS; y++) {
        int len = (int)strlen(MAZE[y]);
        for (x = 0; x < COLS; x++) {
            char c = (x < len) ? MAZE[y][x] : ' ';
            board[y][x] = c;
            if (c == '.' || c == 'o')
                pellets_left++;
        }
        board[y][COLS] = '\0';
    }
    /* Place pacman */
    pac.y = 15; pac.x = 13; pac.dir = 3;
    /* Place ghosts in the house */
    ghosts[0].y = 12; ghosts[0].x = 12; ghosts[0].dir = 2;
    ghosts[1].y = 12; ghosts[1].x = 15; ghosts[1].dir = 3;
    ghosts[2].y = 11; ghosts[2].x = 13; ghosts[2].dir = 2;
    ghosts[3].y = 11; ghosts[3].x = 14; ghosts[3].dir = 3;
}

static int is_door(int y, int x)
{
    return (y == DOOR_R && (x == DOOR_C1 || x == DOOR_C2));
}

static int can_move(Entity *e, int dir, int is_ghost)
{
    int ny = e->y, nx = e->x;
    switch (dir) {
        case 0: ny--; break;
        case 1: ny++; break;
        case 2: nx--; break;
        case 3: nx++; break;
    }
    if (nx < 0) nx = COLS - 1;
    if (nx >= COLS) nx = 0;
    if (ny < 0 || ny >= ROWS) return 0;
    if (board[ny][nx] == '#') return 0;
    /* Ghosts may pass through the door; pacman may not */
    if (is_door(ny, nx) && !is_ghost) return 0;
    return 1;
}

static void move_entity(Entity *e, int dir)
{
    switch (dir) {
        case 0: e->y--; break;
        case 1: e->y++; break;
        case 2: e->x--; break;
        case 3: e->x++; break;
    }
    if (e->x < 0) e->x = COLS - 1;
    if (e->x >= COLS) e->x = 0;
}

static void move_pacman(int dir)
{
    if (can_move(&pac, dir, 0)) {
        pac.dir = dir;
        move_entity(&pac, dir);
    }
    /* eat pellet */
    char c = board[pac.y][pac.x];
    if (c == '.') {
        score += 10;
        board[pac.y][pac.x] = ' ';
        pellets_left--;
    } else if (c == 'o') {
        score += 50;
        board[pac.y][pac.x] = ' ';
        pellets_left--;
        ghost_mode = 1;
        scared_timer = 40;
    }
}

static int manhattan(int y1, int x1, int y2, int x2)
{
    int dx = x1 - x2;
    if (dx > COLS / 2) dx -= COLS;
    if (dx < -COLS / 2) dx += COLS;
    return abs(y1 - y2) + abs(dx);
}

static void move_ghost(Entity *g)
{
    /* If frightened, move randomly */
    if (ghost_mode == 1) {
        int tries = 0;
        while (tries < 8) {
            int d = rand() % 4;
            if (can_move(g, d, 1)) {
                g->dir = d;
                move_entity(g, d);
                return;
            }
            tries++;
        }
        return;
    }

    /* Chase: pick the direction that minimizes distance to pacman.
     * Don't reverse unless forced. */
    int best = -1, bestDist = 1 << 30;
    int dirs[4] = {0, 1, 2, 3};
    int idx = 0;
    while (idx < 4) {
        int d = dirs[idx];
        if (d == (g->dir ^ 1)) { idx++; continue; }  /* skip reverse */
        if (can_move(g, d, 1)) {
            int ny = g->y, nx = g->x;
            switch (d) { case 0: ny--; break; case 1: ny++; break;
                         case 2: nx--; break; case 3: nx++; break; }
            if (nx < 0) nx = COLS - 1;
            if (nx >= COLS) nx = 0;
            int dist = manhattan(ny, nx, pac.y, pac.x);
            if (dist < bestDist) { bestDist = dist; best = d; }
        }
        idx++;
    }
    if (best < 0) {
        /* forced to reverse */
        int d = g->dir ^ 1;
        if (can_move(g, d, 1)) { g->dir = d; move_entity(g, d); }
        return;
    }
    g->dir = best;
    move_entity(g, best);
}

static int collide_with_pac(Entity *g)
{
    return (g->y == pac.y && g->x == pac.x);
}

static void draw(void)
{
    int y, x;
    erase();
    attron(A_BOLD);
    mvprintw(0, 0, "SCORE: %d   LIVES: %d   LEVEL: %d   %s",
             score, lives, level, paused ? "[PAUSED]" :
             (ghost_mode ? "[FRIGHTENED]" : ""));
    attroff(A_BOLD);
    for (y = 0; y < ROWS; y++) {
        for (x = 0; x < COLS; x++) {
            char c = board[y][x];
            if (is_door(y, x)) {
                mvaddch(y + 1, x, '-');
                continue;
            }
            switch (c) {
                case '#': attron(COLOR_PAIR(1)); mvaddch(y + 1, x, '#'); attroff(COLOR_PAIR(1)); break;
                case '.': attron(COLOR_PAIR(2)); mvaddch(y + 1, x, '.'); attroff(COLOR_PAIR(2)); break;
                case 'o': attron(COLOR_PAIR(3)); mvaddch(y + 1, x, 'o'); attroff(COLOR_PAIR(3)); break;
                default:  mvaddch(y + 1, x, ' '); break;
            }
        }
    }
    /* draw ghosts */
    for (int i = 0; i < 4; i++) {
        int pair = ghost_mode ? 5 : (4 + i);
        attron(COLOR_PAIR(pair));
        mvaddch(ghosts[i].y + 1, ghosts[i].x, 'G');
        attroff(COLOR_PAIR(pair));
    }
    /* draw pacman */
    attron(COLOR_PAIR(6));
    mvaddch(pac.y + 1, pac.x, 'C');
    attroff(COLOR_PAIR(6));
    refresh();
}

static void new_level(void)
{
    level++;
    init_board();
    ghost_mode = 0;
    scared_timer = 0;
}

int main(void)
{
    srand((unsigned)time(NULL));
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    timeout(150);   /* ~6.6 updates/sec */

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_BLUE,   COLOR_BLACK);   /* walls */
        init_pair(2, COLOR_WHITE,  COLOR_BLACK);   /* pellets */
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);   /* power */
        init_pair(4, COLOR_RED,    COLOR_BLACK);   /* ghost 0 */
        init_pair(5, COLOR_CYAN,   COLOR_BLACK);   /* ghost 1 */
        init_pair(6, COLOR_MAGENTA,COLOR_BLACK);   /* ghost 2 */
        init_pair(7, COLOR_GREEN,  COLOR_BLACK);   /* ghost 3 */
        init_pair(8, COLOR_BLUE,   COLOR_WHITE);   /* frightened */
        init_pair(9, COLOR_YELLOW, COLOR_BLACK);   /* pacman */
    }

    init_board();

    while (1) {
        if (!gameover && !paused) {
            draw();
        }

        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;
        if (ch == 'p' || ch == 'P') { paused = !paused; continue; }
        if (paused) continue;

        int newdir = -1;
        switch (ch) {
            case KEY_UP:    case 'w': newdir = 0; break;
            case KEY_DOWN:  case 's': newdir = 1; break;
            case KEY_LEFT:  case 'a': newdir = 2; break;
            case KEY_RIGHT: case 'd': newdir = 3; break;
            default: break;
        }

        if (gameover) {
            if (ch == '\n' || ch == 'r' || ch == 'R') {
                score = 0; lives = 3; level = 1; gameover = 0;
                init_board();
            }
            continue;
        }

        if (newdir >= 0) move_pacman(newdir);

        /* Move ghosts */
        for (int i = 0; i < 4; i++) move_ghost(&ghosts[i]);

        if (scared_timer > 0) {
            scared_timer--;
            if (scared_timer == 0) ghost_mode = 0;
        }

        /* Collision checks */
        int hit = 0;
        for (int i = 0; i < 4; i++) {
            if (collide_with_pac(&ghosts[i])) {
                if (ghost_mode) {
                    /* eat ghost */
                    score += 200;
                    /* reset ghost to house */
                    ghosts[i].y = 12; ghosts[i].x = 12; ghosts[i].dir = 2;
                } else {
                    hit = 1;
                }
            }
        }

        if (hit) {
            lives--;
            if (lives <= 0) {
                gameover = 1;
                mvprintw(ROWS / 2 + 1, COLS / 2 - 8, "GAME OVER - press R to restart");
                refresh();
                continue;
            }
            /* reset positions */
            pac.y = 15; pac.x = 13; pac.dir = 3;
            ghosts[0].y = 12; ghosts[0].x = 12; ghosts[0].dir = 2;
            ghosts[1].y = 12; ghosts[1].x = 15; ghosts[1].dir = 3;
            ghosts[2].y = 11; ghosts[2].x = 13; ghosts[2].dir = 2;
            ghosts[3].y = 11; ghosts[3].x = 14; ghosts[3].dir = 3;
            ghost_mode = 0; scared_timer = 0;
        }

        if (pellets_left <= 0) {
            new_level();
        }
    }

    endwin();
    return 0;
}
