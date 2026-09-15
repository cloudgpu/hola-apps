/*
 * pacman_web.c — Pacman game engine compiled to WebAssembly.
 *
 * Game logic runs entirely in C (compiled to WASM); the browser JS layer
 * polls the exported state each frame and renders it to an HTML canvas.
 *
 * Build (with emsdk):
 *   emcc -O2 -s WASM=1 -s EXPORTED_FUNCTIONS=_game_init,_game_step,_game_get_state,_game_set_dir \
 *        -s EXPORTED_RUNTIME_METHODS=ccall,cwrap -o pacman.js pacman_web.c
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ROWS 21
#define COLS 28

/* Maze: '#' wall, '.' pellet, 'o' power pellet, ' ' empty */
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

#define DOOR_R 12
#define DOOR_C1 13
#define DOOR_C2 14

typedef struct { int y, x, dir; } Entity;

static char   board[ROWS][COLS + 1];
static Entity pac;
static Entity ghosts[4];
static int    ghost_mode = 0;    /* 0 chase, 1 frightened */
static int    scared_timer = 0;
static int    score = 0;
static int    lives = 3;
static int    pellets_left = 0;
static int    level = 1;
static int    gameover = 0;
static int    started = 0;       /* 1 after first move */
static int    last_dir = 3;      /* pending direction */

/* Exported state buffer (read by JS). Layout:
 *   [0] = ROWS, [1] = COLS
 *   [2..2+ROWS*COLS-1] = board chars
 *   then: pac.y, pac.x, pac.dir
 *         ghost0.y,x,dir ... ghost3.y,x,dir
 *         score, lives, level, ghost_mode, gameover, started
 */
static int st[2 + ROWS * COLS + 4 * 3 + 6];

static int is_door(int y, int x)
{
    return (y == DOOR_R && (x == DOOR_C1 || x == DOOR_C2));
}

void game_init(void)
{
    int y, x;
    pellets_left = 0;
    for (y = 0; y < ROWS; y++) {
        int len = (int)strlen(MAZE[y]);
        for (x = 0; x < COLS; x++) {
            char c = (x < len) ? MAZE[y][x] : ' ';
            board[y][x] = c;
            if (c == '.' || c == 'o') pellets_left++;
        }
        board[y][COLS] = '\0';
    }
    pac.y = 15; pac.x = 13; pac.dir = 3; last_dir = 3;
    ghosts[0].y = 12; ghosts[0].x = 12; ghosts[0].dir = 2;
    ghosts[1].y = 12; ghosts[1].x = 15; ghosts[1].dir = 3;
    ghosts[2].y = 11; ghosts[2].x = 13; ghosts[2].dir = 2;
    ghosts[3].y = 11; ghosts[3].x = 14; ghosts[3].dir = 3;
    ghost_mode = 0; scared_timer = 0;
    score = 0; lives = 3; level = 1; gameover = 0; started = 0;
}

void game_set_dir(int dir)
{
    if (dir >= 0 && dir <= 3) { last_dir = dir; started = 1; }
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
    char c = board[pac.y][pac.x];
    if (c == '.') {
        score += 10; board[pac.y][pac.x] = ' '; pellets_left--;
    } else if (c == 'o') {
        score += 50; board[pac.y][pac.x] = ' '; pellets_left--;
        ghost_mode = 1; scared_timer = 40;
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
    if (ghost_mode == 1) {
        int tries = 0;
        while (tries < 8) {
            int d = rand() % 4;
            if (can_move(g, d, 1)) { g->dir = d; move_entity(g, d); return; }
            tries++;
        }
        return;
    }
    int best = -1, bestDist = 1 << 30;
    for (int d = 0; d < 4; d++) {
        if (d == (g->dir ^ 1)) continue;
        if (can_move(g, d, 1)) {
            int ny = g->y, nx = g->x;
            switch (d) { case 0: ny--; break; case 1: ny++; break;
                         case 2: nx--; break; case 3: nx++; break; }
            if (nx < 0) nx = COLS - 1;
            if (nx >= COLS) nx = 0;
            int dist = manhattan(ny, nx, pac.y, pac.x);
            if (dist < bestDist) { bestDist = dist; best = d; }
        }
    }
    if (best < 0) {
        int d = g->dir ^ 1;
        if (can_move(g, d, 1)) { g->dir = d; move_entity(g, d); }
        return;
    }
    g->dir = best;
    move_entity(g, best);
}

void game_step(void)
{
    if (gameover) return;

    /* Apply pending direction */
    if (started) move_pacman(last_dir);

    for (int i = 0; i < 4; i++) move_ghost(&ghosts[i]);

    if (scared_timer > 0) {
        scared_timer--;
        if (scared_timer == 0) ghost_mode = 0;
    }

    int hit = 0;
    for (int i = 0; i < 4; i++) {
        if (ghosts[i].y == pac.y && ghosts[i].x == pac.x) {
            if (ghost_mode) {
                score += 200;
                ghosts[i].y = 12; ghosts[i].x = 12; ghosts[i].dir = 2;
            } else hit = 1;
        }
    }

    if (hit) {
        lives--;
        if (lives <= 0) { gameover = 1; return; }
        pac.y = 15; pac.x = 13; pac.dir = 3; last_dir = 3;
        ghosts[0].y = 12; ghosts[0].x = 12; ghosts[0].dir = 2;
        ghosts[1].y = 12; ghosts[1].x = 15; ghosts[1].dir = 3;
        ghosts[2].y = 11; ghosts[2].x = 13; ghosts[2].dir = 2;
        ghosts[3].y = 11; ghosts[3].x = 14; ghosts[3].dir = 3;
        ghost_mode = 0; scared_timer = 0;
    }

    if (pellets_left <= 0) {
        level++;
        /* rebuild the maze but keep the new level number */
        int lv = level;
        game_init();
        level = lv;
    }
}

int *game_get_state(void)
{
    st[0] = ROWS;
    st[1] = COLS;
    int idx = 2;
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            st[idx++] = (unsigned char)board[y][x];
    st[idx++] = pac.y; st[idx++] = pac.x; st[idx++] = pac.dir;
    for (int i = 0; i < 4; i++) {
        st[idx++] = ghosts[i].y;
        st[idx++] = ghosts[i].x;
        st[idx++] = ghosts[i].dir;
    }
    st[idx++] = score;
    st[idx++] = lives;
    st[idx++] = level;
    st[idx++] = ghost_mode;
    st[idx++] = gameover;
    st[idx++] = started;
    return st;
}
