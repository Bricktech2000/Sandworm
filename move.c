#include "jsonw.h"
#include "tictac.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICTAC_STREAM stderr

// {
//   "game": {
//     "id": "1772f349-0175-4eeb-968d-84382a1d4709",
//     "ruleset": {
//       "name": "standard",
//       "version": "v1.2.3",
//       "settings": {
//         "foodSpawnChance": 15,
//         "minimumFood": 1,
//         "hazardDamagePerTurn": 0,
//         "hazardMap": "",
//         "hazardMapAuthor": "",
//         "royale": {
//           "shrinkEveryNTurns": 0
//         },
//         "squad": {
//           "allowBodyCollisions": false,
//           "sharedElimination": false,
//           "sharedHealth": false,
//           "sharedLength": false
//         }
//       }
//     },
//     "map": "standard",
//     "timeout": 500,
//     "source": "custom"
//   },
//   "turn": 0,
//   "board": {
//     "height": 11,
//     "width": 11,
//     "snakes": [
//       {
//         "id": "gs_VT7pR9Q8fBRVCXbBQSDgDBxR",
//         "name": "Raptor",
//         "latency": "",
//         "health": 100,
//         "body": [
//           {
//             "x": 5,
//             "y": 9
//           },
//           {
//             "x": 5,
//             "y": 9
//           },
//           {
//             "x": 5,
//             "y": 9
//           }
//         ],
//         "head": {
//           "x": 5,
//           "y": 9
//         },
//         "length": 3,
//         "shout": "",
//         "squad": "",
//         "customizations": {
//           "color": "#32a844",
//           "head": "sand-worm",
//           "tail": "round-bum"
//         }
//       },
//       {
//         "id": "gs_TvCvpJySTgJJBc4TkHw4YyxG",
//         "name": "Raptor",
//         "latency": "",
//         "health": 100,
//         "body": [
//           {
//             "x": 1,
//             "y": 5
//           },
//           {
//             "x": 1,
//             "y": 5
//           },
//           {
//             "x": 1,
//             "y": 5
//           }
//         ],
//         "head": {
//           "x": 1,
//           "y": 5
//         },
//         "length": 3,
//         "shout": "",
//         "squad": "",
//         "customizations": {
//           "color": "#32a844",
//           "head": "sand-worm",
//           "tail": "round-bum"
//         }
//       }
//     ],
//     "food": [
//       {
//         "x": 6,
//         "y": 10
//       },
//       {
//         "x": 0,
//         "y": 6
//       },
//       {
//         "x": 5,
//         "y": 5
//       }
//     ],
//     "hazards": []
//   },
//   "you": {
//     "id": "gs_VT7pR9Q8fBRVCXbBQSDgDBxR",
//     "name": "Raptor",
//     "latency": "",
//     "health": 100,
//     "body": [
//       {
//         "x": 5,
//         "y": 9
//       },
//       {
//         "x": 5,
//         "y": 9
//       },
//       {
//         "x": 5,
//         "y": 9
//       }
//     ],
//     "head": {
//       "x": 5,
//       "y": 9
//     },
//     "length": 3,
//     "shout": "",
//     "squad": "",
//     "customizations": {
//       "color": "#32a844",
//       "head": "sand-worm",
//       "tail": "round-bum"
//     }
//   }
// }

enum { WALL = -2, EMPTY = -1 };

struct game {
  int width;
  int height;
  int you; // XXX move to idx 0
  struct snake {
    bool alive;
    int hx, hy, tx, ty;
  } snakes[4];
  signed char *board;
};

int eval(struct game game) {
  signed char(*board_)[game.width + 4] = (signed char(*)[])game.board;

  int eval = 0;
  // XXX unroll this outer loop too, so that no cond jmp on `sign`
  // TICTAC(eval)
  for (int s = 0; s < sizeof(game.snakes) / sizeof(*game.snakes); s++) {
    if (!game.snakes[s].alive)
      continue;
    // XXX loop unroll
    // XXX doc this is neighborhood
    for (int dx = -2; dx <= 2; dx++) {
      for (int dy = -2; dy <= 2; dy++) {
        int weight = dx < -1 || dx > 1 || dy < -1 || dy > 1 ? 1 : 2;
        int sign = s == game.you ? 1 : -1;
        int val =
            board_[game.snakes[s].hx + 2 + dx][game.snakes[s].hy + 2 + dy];
        if (val > 0 && !game.snakes[val >> 2].alive)
          continue;
        eval += (val == EMPTY) * weight * sign;
      }
    }
  }

  return eval;
}

// void make_move(struct game game, int s, int dir, int *last_val, int
// *tail_dir) {
//   signed char(*board_)[game.width + 4] = (signed char(*)[])game.board;

//   int lasv = board_[game.snakes[s].hx][game.snakes[s].hy];
//   board_[game.snakes[s].hx][game.snakes[s].hy] = s << 2 | dir;

//   dir = board_[game.snakes[s].tx][game.snakes[s].ty];
//   board_[game.snakes[s].tx][game.snakes[s].ty] = EMPTY;
//   int dx = (dir == 0) - (dir == 2);
//   int dy = (dir == 1) - (dir == 3);
//   game.snakes[s].tx += dx, game.snakes[s].ty += dy;

//   *last_val = lasv;
//   *tail_dir = dir;
// }

// void undo_move(struct game game, int s, int last_val, int tail_dir, int *dir)
// {
//   signed char(*board_)[game.width + 4] = (signed char(*)[])game.board;

//   int lasv = board_[game.snakes[s].hx][game.snakes[s].hy];
//   board_[game.snakes[s].hx][game.snakes[s].hy] = s << 2 | dir;

//   dir = board_[game.snakes[s].tx][game.snakes[s].ty];
//   board_[game.snakes[s].tx][game.snakes[s].ty] = EMPTY;
//   int dx = (dir == 0) - (dir == 2);
//   int dy = (dir == 1) - (dir == 3);
//   game.snakes[s].tx += dx, game.snakes[s].ty += dy;

//   *last_val = lasv;
//   *tail_dir = dir;
// }

int minimax(struct game game, int depth, int s, int best_dir[4]) {
  if (depth == 0 /* || XXX game over */) {
    return eval(game);
  }

  if (!game.snakes[s].alive /* || XXX game over */)
    return minimax(game, depth - 1, (s + 1) % 4, best_dir);

  signed char(*board_)[game.width + 4] = (signed char(*)[])game.board;

  int eval_ = s == game.you ? -99999 : 99999;
  // TICTAC(minimax)
  for (int dir = 0; dir < 4; dir++) {
    // XXX bounds check

    board_[game.snakes[s].hx + 2][game.snakes[s].hy + 2] = s << 2 | dir;
    int dx = (dir == 0) - (dir == 2);
    int dy = (dir == 1) - (dir == 3);
    game.snakes[s].hx += dx, game.snakes[s].hy += dy;
    int val = board_[game.snakes[s].hx + 2][game.snakes[s].hy + 2];
    if (val != EMPTY) {
      game.snakes[s].hx -= dx, game.snakes[s].hy -= dy;
      // int mm = s == game.you ? -9999 : 9999;
      // eval_ = (s == game.you) == (mm > eval_) ? *best_dir = dir, mm : eval_;
      continue;
    }
    board_[game.snakes[s].hx + 2][game.snakes[s].hy + 2] = s << 2;

    int tail_val = board_[game.snakes[s].tx + 2][game.snakes[s].ty + 2];
    board_[game.snakes[s].tx + 2][game.snakes[s].ty + 2] = EMPTY;
    int tail_dx = ((tail_val & 3) == 0) - ((tail_val & 3) == 2);
    int tail_dy = ((tail_val & 3) == 1) - ((tail_val & 3) == 3);
    game.snakes[s].tx += tail_dx, game.snakes[s].ty += tail_dy;

    // char *str;
    //
    // // TODO arr
    // switch (dir) {
    // case 0:
    //   str = "right";
    //   break;
    // case 1:
    //   str = "up";
    //   break;
    // case 2:
    //   str = "left";
    //   break;
    // case 3:
    //   str = "down";
    //   break;
    // default:
    //   str = "invalid";
    //   break;
    // }
    //
    // fputs("TEST POS FOR DIR:\n", stderr);
    // fputs(str, stderr);
    // fputs("\n", stderr);
    // for (int x = 0; x < game.width + 4; x++) {
    //   for (int y = 0; y < game.height + 4; y++)
    //     fprintf(stderr, "%02hhx ", board_[x][y]);
    //   fputc('\n', stderr);
    // }
    // fprintf(stderr, "eval: %d\n", eval(game));

    int dummy[4]; // XXX hacky
    int mm =
        minimax(game, depth - 1, (s + 1) % 4, depth > 12 ? best_dir : dummy);
    if (mm > eval_)
      best_dir[s] = dir;
    eval_ = (s == game.you) == (mm > eval_) ? mm : eval_;

    game.snakes[s].tx -= tail_dx, game.snakes[s].ty -= tail_dy;
    board_[game.snakes[s].tx + 2][game.snakes[s].ty + 2] = tail_val;

    board_[game.snakes[s].hx + 2][game.snakes[s].hy + 2] = EMPTY;
    game.snakes[s].hx -= dx, game.snakes[s].hy -= dy;
  }

  return eval_;
}

int main(void) {
  static char req[65535]; // XXX fixed buf hacky
  size_t size = fread(req, 1, sizeof(req) - 1, stdin);
  if (ferror(stdin))
    perror("fread"), exit(EXIT_FAILURE);
  if (!feof(stdin))
    fputs("buffer exhausted\n", stderr), exit(EXIT_FAILURE);
  req[size] = '\0';

  // parse

  struct game game;

  char *you_id = jsonw_lookup(
      "id", jsonw_beginobj(jsonw_lookup("you", jsonw_beginobj(req))));
  if (!you_id)
    fputs("you_id\n", stderr), exit(EXIT_FAILURE);
  size_t id_len = 0; // XXX
  jsonw_string(&id_len, you_id);

  game.you = -1; // XXX actually populate

  double width = NAN, height = NAN;
  char *board = jsonw_lookup("board", jsonw_beginobj(req));
  jsonw_number(&width, jsonw_lookup("width", jsonw_beginobj(board)));
  if (isnan(width) || (game.width = width) != width)
    fputs("no width\n", stderr), exit(EXIT_FAILURE);
  jsonw_number(&height, jsonw_lookup("height", jsonw_beginobj(board)));
  if (isnan(height) || (game.height = height) != height)
    fputs("no height\n", stderr), exit(EXIT_FAILURE);
  // XXX +4 is neighborhood
  game.board = malloc(sizeof(*game.board) * (height + 4) * (width + 4));
  signed char(*board_)[game.width + 4] = (signed char(*)[])game.board;
  for (int x = 0; x < game.width + 4; x++)
    for (int y = 0; y < game.height + 4; y++)
      board_[x][y] = WALL;
  for (int x = 2; x < game.width + 2; x++)
    for (int y = 2; y < game.height + 2; y++)
      board_[x][y] = EMPTY;

  for (int s = 0; s < sizeof(game.snakes) / sizeof(*game.snakes); s++)
    game.snakes[s].alive = false;

  char *snakes = jsonw_lookup("snakes", jsonw_beginobj(board));
  int s = 0;
  TICTAC(parse)
  for (char *snake = jsonw_beginarr(snakes); snake;
       snake = jsonw_element(snake)) {

    game.snakes[s].tx = -1, game.snakes[s].tx = -1;
    game.snakes[s].alive = true;

    char *id = jsonw_lookup("id", jsonw_beginobj(snake));
    if (!id)
      fputs("id\n", stderr), exit(EXIT_FAILURE);
    if (strncmp(you_id, id, id_len) == 0) {
      game.you = s;
      fprintf(stderr, "you: %d\n", game.you);
    }

    char *body = jsonw_lookup("body", jsonw_beginobj(snake));
    for (char *point = jsonw_beginarr(body); point;
         point = jsonw_element(point)) {
      int x_, y_;
      double x = NAN, y = NAN;
      jsonw_number(&x, jsonw_lookup("x", jsonw_beginobj(point)));
      if (isnan(x) || (x_ = x) != x)
        fputs("no x\n", stderr), exit(EXIT_FAILURE);
      jsonw_number(&y, jsonw_lookup("y", jsonw_beginobj(point)));
      if (isnan(y) || (y_ = y) != y)
        fputs("no y\n", stderr), exit(EXIT_FAILURE);

      // num << 4
      // wall = -1
      // empty = 0

      if (game.snakes[s].tx == -1 || game.snakes[s].ty == -1)
        game.snakes[s].hx = x_, game.snakes[s].hy = y_;
      game.snakes[s].tx = x_, game.snakes[s].ty = y_;
      int dx = game.snakes[s].tx - x_, dy = game.snakes[s].ty - y_;
      // XXX doc chatgpt. why works?
      int dir = (dy == 1) + 2 * (dx == -1) + 3 * (dy == -1);

      if (x_ > width || y_ > height)
        fputs("out of bounds\n", stderr), exit(EXIT_FAILURE);
      board_[x_ + 2][y_ + 2] = s << 4 | dir;
    }

    s++;
  }

  fputs("board:\n", stderr);
  for (int x = 0; x < game.width + 4; x++) {
    for (int y = 0; y < game.height + 4; y++)
      fprintf(stderr, "%02hhx ", board_[x][y]);
    fputc('\n', stderr);
  }
  fprintf(stderr, "eval: %d\n", eval(game));

  int best_dir[4] = {0};
  TICTAC(search)
  minimax(game, 16, (game.you + 1) % 4, best_dir);
  char *str;

  // TODO arr
  switch (best_dir[game.you]) {
  case 0:
    str = "right";
    break;
  case 1:
    str = "up";
    break;
  case 2:
    str = "left";
    break;
  case 3:
    str = "down";
    break;
  default:
    str = "invalid";
    break;
  }

  fprintf(stderr, "{\"move\":\"%s\"}\n", str);

  printf("Status: 200 OK\nContent-Type: application/json\n\n");
  printf("{\"move\":\"%s\"}\n", str);

  // fprintf(stderr, "%s\n", req);
}
