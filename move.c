#include "jsonw.h"
#include "tictac.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// write diagnostics to `stderr` so cgi.conf can redirect them to a file

#define TICTAC_STREAM stderr

#define OUT_PARAM(TYPE, IDENT)                                                 \
  TYPE _out_param_##IDENT;                                                     \
  if (IDENT == NULL)                                                           \
  IDENT = &_out_param_##IDENT

char *jsonw_short(short *num, char *json) {
  OUT_PARAM(short, num);
  double dbl;
  if ((json = jsonw_number(&dbl, json)) && dbl < SHRT_MAX && (short)dbl == dbl)
    return *num = (short)dbl, json;
  return NULL;
}

#define MIN_SEARCH 0.175 // seconds. determined experimentally
#define K_LENGTH 16.0    // reward for length
#define K_HEALTH 0.0     // reward for health
#define K_OTHERS 4.0     // reward for screwing others

#define BUFFER 2 // number of `WALL` tiles to buffer the edge of the playfield

// TODO:
// - [x] make all snakes move at the same time.
// - [x] put our snake at index 0 instead of using `game.you`
// - [x] add food & health to our game model
// - [x] add snake length to our game model
// - [x] add snakes dying to game model
// - [x] make a BOARD(X, Y) macro to query the board
// - [x] don't store `snakes` in `game`, rather make it a param
//       of `minimax`; this way, only need to revert `board`
// - [x] why do we keep dying in head-to-heads?
// - [x] stop committing suicide; assume imperfect opponent

enum { WALL = -3, FOOD = -2, EMPTY = -1 };

#define AT(BOARD, Y, X)                                                        \
  BOARD.cells[(Y + BUFFER) * (BOARD.w + 2 * BUFFER) + (X + BUFFER)]

struct snake {
  short length;
  unsigned char health;
  unsigned char taillag; // number of turns to wait before moving the tail
  short hx, hy, tx, ty;
};

struct board {
  // width and height of the actual playfield, excludes the `BUFFER`
  short w, h;
  // when `cell` is not one of `WALL, FOOD, EMPTY`, `cell >> 2` is the index of
  // the snake in the cell and `cell & 3` is the direction toward the head
  signed char *cells;
};

int boardeval(struct board board, struct snake snakes[8]) {
  if (!(*snakes).health)
    return INT_MIN;

  int eval = 0;

  for (int s = 0; s < 8; s++) {
    if (!snakes[s].health)
      continue;

    // TODO use floodfill instead:
    // memcpy board.
    // sort snakes by len.
    // for sorted snakes:
    //   fill cardinals around head.
    // for 0 to 5:
    //   for each empty/food cell:
    //     if next to filled, fill with snake of max len.

    // TODO rewrite everything to bitboards

    int e = 0;
    // XXX this check is expensive
#define ISFREE(V)                                                              \
  (V == FOOD || V == EMPTY || V != WALL && !snakes[V >> 2].health)
#define E(DY, DX) ISFREE(AT(board, snakes[s].hy DY, snakes[s].hx DX))

    // don't exceed `BUFFER`, with these deltas!

    e += E(-1, -1) + E(-1, +0) + E(-1, +1);
    e += E(+0, -1) + E(+0, +0) + E(+0, +1);
    e += E(+1, -1) + E(+1, +0) + E(+1, +1);

    e += E(-2, -2) + E(-2, -1) + E(-2, +0) + E(-2, +1) + E(-2, +2);
    e += E(-1, -2) + E(-1, -1) + E(-1, +0) + E(-1, +1) + E(-1, +2);
    e += E(+0, -2) + E(+0, -1) + E(+0, +0) + E(+0, +1) + E(+0, +2);
    e += E(+1, -2) + E(+1, -1) + E(+1, +1) + E(+1, +1) + E(+1, +2);
    e += E(+2, -2) + E(+2, -1) + E(+2, +2) + E(+2, +1) + E(+2, +2);

    // for (short dy = -BUFFER; dy <= BUFFER; dy++) {
    //   for (short dx = -BUFFER; dx <= BUFFER; dx++) {
    //     signed char v = AT(board, snakes[s].hy + dy, snakes[s].hx + dx);
    //     e += v == FOOD || v == EMPTY || v != WALL && !snakes[v >> 2].health;
    //   }
    // }

    e += snakes[s].length * K_LENGTH;
    e += snakes[s].health * K_HEALTH;
    eval += s ? -e * K_OTHERS : e;
  }

  return (unsigned)eval << 2;
}

// XXX pay closer attention to ABI?

// `minimax(...) >> 2` is the actual eval, and `minimax(...) & 3` is the
// direction that led to that eval
int minimax(struct board board, struct snake snakes[8], int s, int alpha,
            int beta, int depth) {
  if (depth == 0)
    return boardeval(board, snakes);

  if (!(*snakes).health)
    return INT_MIN; // prevent infinite loop below
  while (!snakes[s++, s %= 8].health)
    ;
  struct snake orig = snakes[s];

  int eval = s ? INT_MAX : INT_MIN;

  // TODO sort dirs based on previous runs so we get better pruning
  for (int dir = 0; dir < 4; dir++) {
    snakes[s].health--;

    AT(board, snakes[s].hy, snakes[s].hx) = s << 2 | dir;
    snakes[s].hx += (dir == 0) - (dir == 2);
    snakes[s].hy += (dir == 1) - (dir == 3);
    signed char hv = AT(board, snakes[s].hy, snakes[s].hx);
    signed char tv = AT(board, snakes[s].ty, snakes[s].tx);
    if (snakes[s].taillag)
      snakes[s].taillag--;
    else {
      // before erasing our tail, make sure no other snake put their head there
      if (tv >> 2 == s)
        AT(board, snakes[s].ty, snakes[s].tx) = EMPTY;
      int dir = tv & 3;
      snakes[s].tx += (dir == 0) - (dir == 2);
      snakes[s].ty += (dir == 1) - (dir == 3);
    }

    int o = hv >> 2; // index of the snake where we're walking over, if any
    unsigned char o_health = snakes[o].health;
    // if wall, die
    if (hv == WALL)
      snakes[s].health = 0;
    // if food or empty or dead snake's body, ok
    else if (hv == FOOD || hv == EMPTY || !snakes[o].health)
      ;
    // if tail of a snake that hasn't yet moved in this turn, ok
    else if (o > s && !snakes[o].taillag && snakes[o].tx == snakes[s].hx &&
             snakes[o].ty == snakes[s].hy)
      ;
    // if head of a shorter snake that's already moved in this turn, kill it, ok
    else if (o < s && snakes[o].length <= snakes[s].length &&
             snakes[o].hx == snakes[s].hx && snakes[o].hy == snakes[s].hy)
      snakes[o].health = 0; // health saved in `hv_health` to be restored later
    // otherwise, die
    else
      snakes[s].health = 0;

    if (snakes[s].health)
      AT(board, snakes[s].hy, snakes[s].hx) = s << 2;

    if (hv == FOOD)
      snakes[s].length++, snakes[s].taillag++, snakes[s].health = 100;

    // the bit fiddling is to stuff into the eval value the direction we took
    int mm = minimax(board, snakes, s, alpha, beta, depth - 1) & ~3 | dir;
    if (!s && mm > eval)
      eval = mm, alpha = eval > alpha ? eval : alpha;
    // the `+8` is to assume am imperfect opponent. without it, when we're in a
    // situation where an optimal opponent could technically screw us regardless
    // of whan we do, we'd commit suicide
    if (s && mm < eval)
      eval = mm + 8, beta = eval < beta ? eval : beta;

    // recursive call done; restore everything
    AT(board, orig.ty, orig.tx) = tv;
    AT(board, snakes[s].hy, snakes[s].hx) = hv;
    snakes[o].health = o_health;
    snakes[s] = orig;

    // alpha--beta pruning
    if (alpha >= beta)
      break;
  }

  return eval;
}

int main(void) {
  static char req[1 << 16];
  size_t size = fread(req, 1, sizeof(req) - 1, stdin);
  if (ferror(stdin))
    perror("fread"), exit(EXIT_FAILURE);
  if (!feof(stdin))
    fputs("buffer exhausted\n", stderr), exit(EXIT_FAILURE);
  req[size] = '\0';

  TIC(parse);
  // see example-move.json

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

  board.cells = malloc((board.w + 2 * BUFFER) * (board.h + 2 * BUFFER));
  memset(board.cells, WALL, (board.w + 2 * BUFFER) * (board.h + 2 * BUFFER));
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
  for (char *j_snake = jsonw_beginarr(j_snakes); j_snake;
       j_snake = jsonw_element(j_snake)) {

    char *j_id = jsonw_lookup("id", jsonw_beginobj(j_snake));
    char *j_id_end = jsonw_string(NULL, j_id);
    if (!j_id_end)
      fputs("bad id\n", stderr), exit(EXIT_FAILURE);
    ptrdiff_t j_id_sz = j_id_end - j_id;

    bool is_you = j_yid_sz == j_id_sz && memcmp(j_yid, j_id, j_yid_sz) == 0;
    // XXX bounds check
    struct snake *snake = is_you ? snakes : snakes + s++;

    (*snake).hx = -1, (*snake).hy = -1;
    (*snake).tx = -1, (*snake).ty = -1;

    short length, health;
    if (!jsonw_short(&length, jsonw_lookup("length", jsonw_beginobj(j_snake))))
      fputs("bad length\n", stderr), exit(EXIT_FAILURE);
    if (!jsonw_short(&health, jsonw_lookup("health", jsonw_beginobj(j_snake))))
      fputs("bad health\n", stderr), exit(EXIT_FAILURE);
    (*snake).length = length, (*snake).health = health;

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
      int dir = 0 * (dx == 1) + 1 * (dy == 1) + 2 * (dx == -1) + 3 * (dy == -1);

      if ((*snake).hx == -1 && (*snake).hy == -1)
        (*snake).hx = x, (*snake).hy = y, dir = 0;
      if ((*snake).tx == x && (*snake).ty == y)
        // several body parts stacked on top of eachother represents tail lag
        (*snake).taillag++;
      (*snake).tx = x, (*snake).ty = y;

      AT(board, y, x) = (snake - snakes) << 2 | dir;
    }
  }

  TAC(parse);

  // fprintf(stderr, "board (with eval %d):\n", boardeval(board, snakes));
  // for (short y = board.h + BUFFER; --y >= -BUFFER;) {
  //   for (short x = -BUFFER; x < board.w + BUFFER; x++)
  //     fprintf(stderr, "%02hhx ", AT(board, y, x));
  //   fputc('\n', stderr);
  // }

  // fprintf(stderr, "%s\n", req);

  int eval = 0;
  int depth = 0;
  clock_t start = clock();
  while ((clock() - start) * 1.0 / CLOCKS_PER_SEC < MIN_SEARCH)
    TICTAC(search)
  eval = minimax(board, snakes, -1, INT_MIN, INT_MAX, ++depth);

  // extract the direction that was stuffed into the eval value
  char *move = (char *[]){"right", "up", "left", "down"}[eval & 3];

  fprintf(stderr, "searched to depth %d\n", depth);
  fprintf(stderr, "moving %s with eval %d\n", move, eval);

  printf("Status: 200 OK\nContent-Type: application/json\n\n");
  printf("{\"move\":\"%s\"}\n", move);

  free(board.cells);
}
