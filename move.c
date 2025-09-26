#include "jsonw.h"
#include "tictac.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICTAC_STREAM stderr

#define OUT_PARAM(TYPE, IDENT)                                                 \
  TYPE _out_param_##IDENT;                                                     \
  if (IDENT == NULL)                                                           \
  IDENT = &_out_param_##IDENT

char *jsonw_short(short *num, char *json) {
  OUT_PARAM(short, num);
  double dbl;
  // XXX jsonw readme: not checking against _MAX, so UB
  if ((json = jsonw_number(&dbl, json)) && dbl < SHRT_MAX && (short)dbl == dbl)
    return *num = (short)dbl, json;
  return NULL;
}

// TODO:
// - [ ] make all snakes move at the same time.
//       or actually, make the longest snake move first
// - [x] put our snake at index 0 instead of using `game.you`
// - [x] add food & health to our game model
// - [x] add snake length to our game model
// - [x] add snakes dying to game model
// - [x] make a BOARD(X, Y) macro to query the board
// - [x] don't store `snakes` in `game`, rather make it a param
//       of `minimax`; this way, only need to revert `board`
// - [ ] why do we keep dying in head-to-heads?
// - [ ] stop committing suicide; assume imperfect opponent

enum { WALL = -3, FOOD = -2, EMPTY = -1 };

#define AT(BOARD, Y, X) BOARD.cells[(Y + 2) * (BOARD.w + 4) + (X + 2)]

struct snake {
  short step;
  short length, health;
  short hx, hy, tx, ty;
};

struct board {
  short w, h;
  signed char *cells;
};

int boardeval(struct board board, struct snake snakes[8]) {
  if (!(*snakes).health)
    return INT_MIN;

  int eval = 0;

  // TICTAC(eval)
  for (int s = 0; s < 8; s++) {
    if (!snakes[s].health)
      continue;

    int e = 0;
    // XXX this check is expensive (dup)
#define ISFREE(V)                                                              \
  (V == FOOD || V == EMPTY || V != WALL && !snakes[V >> 2].health)
#define E(DY, DX) ISFREE(AT(board, snakes[s].hy DY, snakes[s].hx DX))

    e += E(-1, -1) + E(-1, +0) + E(-1, +1);
    e += E(+0, -1) + E(+0, +0) + E(+0, +1);
    e += E(+1, -1) + E(+1, +0) + E(+1, +1);

    e += E(-2, -2) + E(-2, -1) + E(-2, +0) + E(-2, +1) + E(-2, +2);
    e += E(-1, -2) + E(-1, -1) + E(-1, +0) + E(-1, +1) + E(-1, +2);
    e += E(+0, -2) + E(+0, -1) + E(+0, +0) + E(+0, +1) + E(+0, +2);
    e += E(+1, -2) + E(+1, -1) + E(+1, +1) + E(+1, +1) + E(+1, +2);
    e += E(+2, -2) + E(+2, -1) + E(+2, +2) + E(+2, +1) + E(+2, +2);

    // XXX loop unroll
    // XXX doc this is neighborhood
    // for (short dy = -2; dy <= 2; dy++) {
    //   for (short dx = -2; dx <= 2; dx++) {
    //     signed char v = AT(board, snakes[s].hy + dy, snakes[s].hx + dx);
    //     e += v == FOOD || v == EMPTY || v != WALL && !snakes[v >> 2].health;
    //   }
    // }

    // XXX doc magic 4 is weight to lengths
    e += snakes[s].length * 16;
    // XXX doc magic 1 is weight to healths
    // e += snakes[s].health * 1;
    // XXX doc magic 4 is weight to self
    eval += s ? -e : e * 4;
  }

  return (unsigned)eval << 2;
}

int minimax(struct board board, struct snake snakes[8], int alpha, int beta,
            int depth) {
  if (depth == 0)
    // XXX not setting dir here
    return boardeval(board, snakes);

  // XXX moving the longest snake like this doesn't actually work. for gameplay
  // yes it works, but for minimax it doesn't. minimax has to be a max of mins.
  // mins and maxes don't distribute. can probably fix by instead iterating
  // through snakes in order, and retroactively killing snakes that have already
  // moved if they're shorter than us

  // XXX doc find best snake
  struct snake *snake = snakes;
  for (int s = 0; s < 8; s++)
    // XXX >= is important; when len equal, both snakes die, so make other snake
    // move first
    if (snakes[s].step > (*snake).step ||
        snakes[s].step == (*snake).step && snakes[s].length >= (*snake).length)
      snake = snakes + s;
  struct snake orig = *snake;

  if (!(*snake).step)
    return INT_MIN; // XXX all snakes marked
  if (!(*snake).health)
    abort();

  int eval = snake == snakes ? INT_MIN : INT_MAX;

  for (int dir = 0; dir < 4; dir++) {
    (*snake).step--;
    (*snake).health--;
    if ((*snake).health == 0)
      (*snake).step = 0;

    AT(board, (*snake).hy, (*snake).hx) = (snake - snakes) << 2 | dir;
    (*snake).hx += (dir == 0) - (dir == 2);
    (*snake).hy += (dir == 1) - (dir == 3);
    signed char hv = AT(board, (*snake).hy, (*snake).hx);
    // XXX this check is expensive (dup)
    if (hv != FOOD && hv != EMPTY && (hv == WALL || snakes[hv >> 2].health))
      // XXX doc step=0 so won't be chosen
      (*snake).step = 0, (*snake).health = 0;
    else
      AT(board, (*snake).hy, (*snake).hx) = (snake - snakes) << 2;

    signed char tv = AT(board, (*snake).ty, (*snake).tx);
    if (hv == FOOD)
      (*snake).length++, (*snake).health = 100;
    else {
      AT(board, (*snake).ty, (*snake).tx) = EMPTY;
      (*snake).tx += ((tv & 3) == 0) - ((tv & 3) == 2);
      (*snake).ty += ((tv & 3) == 1) - ((tv & 3) == 3);
    }

    int mm = minimax(board, snakes, alpha, beta, depth - 1);
    if (snake == snakes && mm > eval) {
      // XXX doc max & save dir
      eval = mm & ~3 | dir;
      alpha = eval > alpha ? eval : alpha;
    }
    if (snake != snakes && mm < eval) {
      // XXX doc min & passthru dir
      eval = mm + 8; // XXX doc 8 is for imperfect opponent
      beta = eval < beta ? eval : beta;
    }

    AT(board, orig.ty, orig.tx) = tv;
    AT(board, (*snake).hy, (*snake).hx) = hv;
    *snake = orig;

    if (alpha >= beta)
      break;
  }

  return eval;
}

int main(void) {
  static char req[65535]; // XXX fixed buf hacky
  size_t size = fread(req, 1, sizeof(req) - 1, stdin);
  if (ferror(stdin))
    perror("fread"), exit(EXIT_FAILURE);
  if (!feof(stdin))
    fputs("buffer exhausted\n", stderr), exit(EXIT_FAILURE);
  req[size] = '\0';

  char *j_yid = jsonw_lookup(
      "id", jsonw_beginobj(jsonw_lookup("you", jsonw_beginobj(req))));
  char *j_yid_end = jsonw_string(NULL, j_yid);
  if (!j_yid_end)
    fputs("bad you\n", stderr), exit(EXIT_FAILURE);
  ptrdiff_t j_yid_sz = j_yid_end - j_yid;

  struct board board;

  char *j_board = jsonw_lookup("board", jsonw_beginobj(req));
  if (!jsonw_short(&board.w, jsonw_lookup("width", jsonw_beginobj(j_board))))
    fputs("bad width\n", stderr), exit(EXIT_FAILURE);
  if (!jsonw_short(&board.h, jsonw_lookup("height", jsonw_beginobj(j_board))))
    fputs("bad height\n", stderr), exit(EXIT_FAILURE);

  // XXX +4 is neighborhood
  board.cells = malloc((board.w + 4) * (board.h + 4));
  memset(board.cells, WALL, (board.w + 4) * (board.h + 4));
  for (short y = 0; y < board.h; y++)
    for (short x = 0; x < board.w; x++)
      AT(board, y, x) = EMPTY;

  struct snake snakes[8] = {0};

  char *j_food = jsonw_lookup("food", jsonw_beginobj(j_board));
  for (char *j_point = jsonw_beginarr(j_food); j_point;
       j_point = jsonw_element(j_point)) {
    short x, y;
    if (!jsonw_short(&x, jsonw_lookup("x", jsonw_beginobj(j_point))))
      fputs("bad x\n", stderr), exit(EXIT_FAILURE);
    if (!jsonw_short(&y, jsonw_lookup("y", jsonw_beginobj(j_point))))
      fputs("bad y\n", stderr), exit(EXIT_FAILURE);
    if (x < 0 || x > board.w || y < 0 || y > board.h)
      fputs("bad point\n", stderr), exit(EXIT_FAILURE);

    AT(board, y, x) = FOOD;
  }

  int s = 1;
  char *j_snakes = jsonw_lookup("snakes", jsonw_beginobj(j_board));
  TICTAC(parse)
  for (char *j_snake = jsonw_beginarr(j_snakes); j_snake;
       j_snake = jsonw_element(j_snake)) {

    char *j_id = jsonw_lookup("id", jsonw_beginobj(j_snake));
    char *j_id_end = jsonw_string(NULL, j_id);
    if (!j_id_end)
      fputs("bad id\n", stderr), exit(EXIT_FAILURE);
    ptrdiff_t j_id_sz = j_id_end - j_id;

    bool is_you = j_yid_sz == j_id_sz && memcmp(j_yid, j_id, j_yid_sz) == 0;
    struct snake *snake = is_you ? snakes : snakes + s++;

    (*snake).step = SHRT_MAX;
    (*snake).hx = -1, (*snake).hy = -1;
    (*snake).tx = -1, (*snake).ty = -1;

    if (!jsonw_short(&(*snake).length,
                     jsonw_lookup("length", jsonw_beginobj(j_snake))))
      fputs("bad length\n", stderr), exit(EXIT_FAILURE);
    if (!jsonw_short(&(*snake).health,
                     jsonw_lookup("health", jsonw_beginobj(j_snake))))
      fputs("bad health\n", stderr), exit(EXIT_FAILURE);

    char *j_body = jsonw_lookup("body", jsonw_beginobj(j_snake));
    for (char *j_point = jsonw_beginarr(j_body); j_point;
         j_point = jsonw_element(j_point)) {
      short x, y;
      if (!jsonw_short(&x, jsonw_lookup("x", jsonw_beginobj(j_point))))
        fputs("bad x\n", stderr), exit(EXIT_FAILURE);
      if (!jsonw_short(&y, jsonw_lookup("y", jsonw_beginobj(j_point))))
        fputs("bad y\n", stderr), exit(EXIT_FAILURE);
      if (x < 0 || x > board.w || y < 0 || y > board.h)
        fputs("bad point\n", stderr), exit(EXIT_FAILURE);

      short dx = (*snake).tx - x, dy = (*snake).ty - y;
      // XXX chatgpt. understand.
      int dir = (dy == 1) + 2 * (dx == -1) + 3 * (dy == -1);

      if ((*snake).hx == -1 || (*snake).hy == -1)
        (*snake).hx = x, (*snake).hy = y, dir = 0;
      (*snake).tx = x, (*snake).ty = y;

      AT(board, y, x) = (snake - snakes) << 2 | dir;
    }
  }

  for (short y = board.h + 2; --y >= -2;) {
    for (short x = -2; x < board.w + 2; x++)
      fprintf(stderr, "%02hhx ", AT(board, y, x));
    fputc('\n', stderr);
  }
  fprintf(stderr, "eval: %d\n", boardeval(board, snakes));

  // fprintf(stderr, "%s\n", req);

  int eval;
  TICTAC(search)
  eval = minimax(board, snakes, INT_MIN, INT_MAX, 13);
  char *move = (char *[]){"right", "up", "left", "down"}[eval & 3];

  fprintf(stderr, "{\"move\":\"%s\"}\n", move);
  fprintf(stderr, "move eval: %d\n", eval);

  printf("Status: 200 OK\nContent-Type: application/json\n\n");
  printf("{\"move\":\"%s\"}\n", move);

  free(board.cells);
}
