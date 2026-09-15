# Pacman (C + ncurses)

A complete Pacman clone written **entirely in C**, using only the standard
library and ncurses for terminal rendering.

## Build

```sh
cc -Wall -Wextra pacman.c -o pacman -lncurses
```

## Run

```sh
./pacman
```

## Controls

| Key            | Action            |
|----------------|-------------------|
| Arrows / WASD  | Move Pacman       |
| `p`            | Pause / resume    |
| `r`            | Restart (game over) |
| `q`            | Quit              |

## Features

- 21×28 maze with pellets and power pellets
- 4 chasing ghosts (greedy chase AI + frightened mode)
- Tunnel wrapping on the side corridors
- Scoring (10/pellet, 50/power, 200/ghost)
- 3 lives, level progression, game over / restart
- Color rendering (falls back gracefully without color)
