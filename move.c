#include "jsonw.h"
#include "tictac.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// XXX multithreading?
// XXX Best Reply Search? https://notpeerreviewed.com/blog/battlesnake/
// XXX run Jack's snake and compare search depth

// write diagnostics to `stderr` so cgi.conf can redirect them to a file

#define MAX_DEPTH 64 // for allocating a buffer for iterative deepening
#define MAX_SNAKES 4 // for allocating buffers to store snakes
#define T_CUTOFF 200 // cutoff to deepening the search, in milliseconds
#define N_PROPS 32   // number of propagation steps in Voronoi
#define K_OWNED 1    // reward for "owned" cells in Voronoi
#define K_LENGTH 2   // reward for snake's length
#define K_HEALTH 0   // reward for snake's health

#define TICTAC_STREAM stderr

#if defined(UINT128_MAX) // standard `uint128_t` is (probably) supported
#
#elif defined(__SIZEOF_INT128__) // `__uint128_t` GCC extension
#define uint128_t __uint128_t
#define UINT128_MAX ((uint128_t) - 1)
#else // expect user to provide definitions
#error "provide definitions for `uint128_t` and `UINT128_MAX`"
#endif

#define OUT_PARAM(TYPE, IDENT) /* taken verbatim from jsonw.c */               \
  TYPE _out_param_##IDENT;                                                     \
  if (IDENT == NULL)                                                           \
  IDENT = &_out_param_##IDENT

char *jsonw_uchar(unsigned char *num, char *json) {
  OUT_PARAM(unsigned char, num);
  double dbl;
  if ((json = jsonw_number(&dbl, json)) && dbl >= 0 && dbl <= UCHAR_MAX &&
      (unsigned char)dbl == dbl)
    return *num = (unsigned char)dbl, json;
  return NULL;
}

typedef uint128_t bb_t; // bit board

int bb_popcnt(bb_t bb) {
  // codegens into a pair of `popcnt` instructions
  int pop = 0;
  for (; bb; pop++)
    bb &= bb - 1;
  return pop;
}

bb_t bb_dump(bb_t bb) {
  fprintf(stderr, "%016" PRIxLEAST64 "%016" PRIxLEAST64 "\n",
          (uint_least64_t)(bb >> 64), (uint_least64_t)(bb & (bb_t)-1 >> 64));
  return bb;
}

struct snake {
  // making the head and tail `unsigned char`s doesn't improve performance and
  // complicates the code
  bb_t head, tail;
  bb_t axis, sign;
  unsigned char length, health;
  unsigned char taillag; // number of turns to wait before moving the tail
};

struct board {
  struct snake snakes[MAX_SNAKES];
  bb_t food, bodies, heads; // XXX doc `heads` just for this turn
  bb_t board, xmask;        // XXX doc these
  unsigned char width, height;
};

short evaluate(struct board *board) {
  if (!board->snakes->health)
    return 0;

  // use 's' to index into `board->snakes` and 'l' to index into `sort`

  // caching this sorting doesn't improve performance
  int n = 0;
  unsigned char sort[MAX_SNAKES]; // snake indices sorted by decreasing length
  for (int s = 0; s < MAX_SNAKES; s++)
    if (board->snakes[s].health)
      sort[n++] = s;
  for (int temp, hi = n; hi--;)
    for (int l = 0; l < hi; l++)
      if (board->snakes[sort[l]].length < board->snakes[sort[l + 1]].length)
        temp = sort[l], sort[l] = sort[l + 1], sort[l + 1] = temp;

  // Voronoi heuristic

  bb_t filled = board->bodies;

  bb_t owned[MAX_SNAKES];
  for (int l = 0; l < n; l++)
    owned[l] = board->snakes[sort[l]].head;

  for (int i = 0; i < N_PROPS; i++) {
    for (int l = 0; l < n; l++) {
      bb_t adjacent = 0;
      adjacent |= (owned[l] & board->xmask) >> 1;
      adjacent |= owned[l] << 1 & board->xmask;
      adjacent |= owned[l] >> board->width;
      adjacent |= owned[l] << board->width & board->board;

      owned[l] |= adjacent & ~filled;
      filled |= adjacent;
    }
  }

  // XXX computing `owned[]` for all snakes but only using the one corresponding
  // to the "you" snake. keeping it in, because in the future we may need to
  // sort opponent moves based on how good they are for them
  for (int l = 0; l < n; l++)
    if (sort[l] == 0)
      return bb_popcnt(owned[l]) * K_OWNED + board->snakes->length * K_LENGTH +
             board->snakes->health * K_HEALTH;

  abort();
}

short minimax(struct board *board, short (*evals)[4], short alpha, short beta,
              int depth);
short minimax_step(int s, struct board *board, short (*evals)[4], short alpha,
                   short beta, int depth) {
  if (depth == 0)
    return evaluate(board);

  if (!board->snakes->health)
    return 0; // prevent infinite loop below
  do
    if (++s == MAX_SNAKES)
      return minimax(board, evals, alpha, beta, depth);
  while (!board->snakes[s].health);

  struct snake *snake = board->snakes + s;

  // shifting right by 1 to leave some buffer to avoid wraparound
  short eval = s ? SHRT_MAX >> 1 : 0;
  unsigned char length = snake->length, health = snake->health;
  unsigned char taillag = snake->taillag;

  board->heads &= ~snake->head;

  // XXX bug: if a snake has no possible moves, we stop recursing
  for (int i = 0; i < 4; i++) {
    short *evalp = *evals;
    for (short *e = *evals; e < *evals + 4; e++)
      if (*evalp < 0 || *e >= 0 && *e > *evalp == !s)
        evalp = e;

    bool axis = (evalp - *evals) >> 1, sign = (evalp - *evals) & 1;

    if (!axis && !sign && !(snake->head & board->xmask) ||
        !axis && sign && !(snake->head << 1 & board->xmask) ||
        axis && !sign && !(snake->head >> board->width) ||
        axis && sign && !(snake->head << board->width & board->board))
      goto contin;

    axis ? (snake->axis |= snake->head) : (snake->axis &= ~snake->head);
    sign ? (snake->sign |= snake->head) : (snake->sign &= ~snake->head);
    sign ? (snake->head <<= axis ? board->width : 1)
         : (snake->head >>= axis ? board->width : 1);

    if (snake->head & board->bodies)
      goto abort;

    // XXX code dup with `evaluate`
    bb_t adjacent = 0;
    adjacent |= (snake->head & board->xmask) >> 1;
    adjacent |= snake->head << 1 & board->xmask;
    adjacent |= snake->head >> board->width;
    adjacent |= snake->head << board->width & board->board;

    // XXX doc. quiescence search? can't actually make a move if it's next to
    // the head of a weakly longer snake that hasn't yet moved this turn
    if (adjacent & board->heads)
      for (int r = s + 1; r < MAX_SNAKES; r++)
        if (board->snakes[r].length >= snake->length &&
            board->snakes[r].health && (adjacent & board->snakes[r].head))
          goto abort;

    board->bodies |= snake->head;
    snake->health--;
    if (snake->taillag)
      snake->taillag--;
    if (snake->head & board->food)
      snake->length++, snake->taillag++, snake->health = 100;

    *evalp = minimax_step(s, board, evals + 1, alpha, beta, depth - 1);
    ++*evalp; // XXX doc prefer committing suicide later

    if (!s && *evalp > eval)
      eval = *evalp, alpha = eval > alpha ? eval : alpha;
    // XXX comment; make sure still a problem
    // the `+1` is to assume an imperfect opponent. without it, when we're in
    // a situation where an optimal opponent could technically screw us
    // regardless of whan we do, we'd commit suicide
    if (s && *evalp < eval)
      eval = ++*evalp, beta = eval < beta ? eval : beta;

    snake->length = length, snake->health = health;
    snake->taillag = taillag;
    board->bodies &= ~snake->head;

  abort:
    sign ? (snake->head >>= axis ? board->width : 1)
         : (snake->head <<= axis ? board->width : 1);

    // when minimax is hopelessly broken and commenting out these two lines
    // fixes it, it's likely there's an eval overflowing somewhere
    if (alpha >= beta)
      break;

  contin:
    *evalp = ~*evalp;
  }

  for (short *e = *evals; e < *evals + 4; e++)
    *e = *e < 0 ? ~*e : *e;

  board->heads |= snake->head;

  return eval;
}

// XXX pay closer attention to ABI?
short minimax(struct board *board, short (*evals)[4], short alpha, short beta,
              int depth) {
#if MAX_SNAKES <= 8
  uint_fast8_t
#elif MAX_SNAKES <= 16
  uint_fast16_t
#elif MAX_SNAKES <= 32
  uint_fast32_t
#elif MAX_SNAKES <= 64
  uint_fast64_t
#endif
      axes = 0,
      sgns = 0;

  board->heads = 0;

  for (int s = 0; s < MAX_SNAKES; s++) {
    struct snake *snake = board->snakes + s;

    if (!snake->health)
      continue;

    board->heads |= snake->head;

    if (snake->taillag)
      continue;

    board->bodies &= ~snake->tail;
    axes <<= 1, sgns <<= 1;
    axes |= !!(snake->axis & snake->tail);
    sgns |= !!(snake->sign & snake->tail);
    sgns & 1 ? (snake->tail <<= axes & 1 ? board->width : 1)
             : (snake->tail >>= axes & 1 ? board->width : 1);
  }

  short eval = minimax_step(-1, board, evals, alpha, beta, depth);

  for (int s = MAX_SNAKES; s--;) {
    struct snake *snake = board->snakes + s;

    if (!snake->health || snake->taillag)
      continue;

    sgns & 1 ? (snake->tail >>= axes & 1 ? board->width : 1)
             : (snake->tail <<= axes & 1 ? board->width : 1);
    axes & 1 ? (snake->axis |= snake->tail) : (snake->axis &= ~snake->tail);
    sgns & 1 ? (snake->sign |= snake->tail) : (snake->sign &= ~snake->tail);
    axes >>= 1, sgns >>= 1;
    board->bodies |= snake->tail;
  }

  return eval;
}

int main(void) {
  static char req[1 << 16];
  size_t size = fread(req, 1, sizeof(req) - 1, stdin);
  if (ferror(stdin))
    perror("fread"), exit(EXIT_FAILURE);
  if (!feof(stdin))
    fputs("request buffer exhausted\n", stderr), exit(EXIT_FAILURE);
  req[size] = '\0';

  // XXX shout
  // printf("Status: 200 OK\nContent-Type: application/json\n\n");
  // printf("{\"move\":\"right\",\"shout\":\"foobar\"}\n");
  // fprintf(stderr, "%s\n", req);
  // return 0;

  // see example-move.json
  TIC(parse);

  char *j_yid = jsonw_lookup(
      "id", jsonw_beginobj(jsonw_lookup("you", jsonw_beginobj(req))));
  char *j_yid_end = jsonw_string(NULL, j_yid);
  if (!j_yid_end)
    fputs("bad you id\n", stderr), exit(EXIT_FAILURE);
  ptrdiff_t j_yid_sz = j_yid_end - j_yid;

  struct board board = {0};

  char *j_board = jsonw_lookup("board", jsonw_beginobj(req));
  if (!jsonw_uchar(&board.width,
                   jsonw_lookup("width", jsonw_beginobj(j_board))))
    fputs("bad board width\n", stderr), exit(EXIT_FAILURE);
  if (!jsonw_uchar(&board.height,
                   jsonw_lookup("height", jsonw_beginobj(j_board))))
    fputs("bad board height\n", stderr), exit(EXIT_FAILURE);
  if (board.width * board.height > 128)
    fputs("board too large\n", stderr), exit(EXIT_FAILURE);

  board.board = ((bb_t)1 << board.width * board.height) - 1;
  for (unsigned char y = 0; y < board.height; y++)
    board.xmask <<= board.width, board.xmask |= 1;
  board.xmask = board.board & ~board.xmask;

  char *j_food = jsonw_lookup("food", jsonw_beginobj(j_board));
  for (char *j_point = jsonw_beginarr(j_food); j_point;
       j_point = jsonw_element(j_point)) {
    unsigned char x, y;
    if (!jsonw_uchar(&x, jsonw_lookup("x", jsonw_beginobj(j_point))))
      fputs("bad food x\n", stderr), exit(EXIT_FAILURE);
    if (!jsonw_uchar(&y, jsonw_lookup("y", jsonw_beginobj(j_point))))
      fputs("bad food y\n", stderr), exit(EXIT_FAILURE);
    if (x > board.width || y > board.height)
      fputs("bad food point\n", stderr), exit(EXIT_FAILURE);

    board.food |= (bb_t)1 << x + y * board.width;
  }

  int s = 1;
  char *j_snakes = jsonw_lookup("snakes", jsonw_beginobj(j_board));
  for (char *j_snake = jsonw_beginarr(j_snakes); j_snake;
       j_snake = jsonw_element(j_snake)) {

    char *j_id = jsonw_lookup("id", jsonw_beginobj(j_snake));
    char *j_id_end = jsonw_string(NULL, j_id);
    if (!j_id_end)
      fputs("bad snake id\n", stderr), exit(EXIT_FAILURE);
    ptrdiff_t j_id_sz = j_id_end - j_id;

    bool is_you = j_yid_sz == j_id_sz && memcmp(j_yid, j_id, j_yid_sz) == 0;
    // XXX bounds check
    struct snake *snake = is_you ? board.snakes : board.snakes + s++;
    if (s > MAX_SNAKES)
      fputs("too many snakes\n", stderr), exit(EXIT_FAILURE);

    if (!jsonw_uchar(&snake->length,
                     jsonw_lookup("length", jsonw_beginobj(j_snake))))
      fputs("bad snake length\n", stderr), exit(EXIT_FAILURE);
    if (!jsonw_uchar(&snake->health,
                     jsonw_lookup("health", jsonw_beginobj(j_snake))))
      fputs("bad snake health\n", stderr), exit(EXIT_FAILURE);

    signed char hx = -1, hy = -1, tx = -1, ty = -1;

    char *j_body = jsonw_lookup("body", jsonw_beginobj(j_snake));
    for (char *j_point = jsonw_beginarr(j_body); j_point;
         j_point = jsonw_element(j_point)) {
      unsigned char x, y;
      if (!jsonw_uchar(&x, jsonw_lookup("x", jsonw_beginobj(j_point))))
        fputs("bad body x\n", stderr), exit(EXIT_FAILURE);
      if (!jsonw_uchar(&y, jsonw_lookup("y", jsonw_beginobj(j_point))))
        fputs("bad body y\n", stderr), exit(EXIT_FAILURE);
      if (x > board.width || y > board.height)
        fputs("bad body point\n", stderr), exit(EXIT_FAILURE);

      signed char dx = tx - x, dy = ty - y;

      if (hx == -1 && hy == -1)
        hx = x, hy = y;
      if (tx == x && ty == y)
        // several body parts stacked on top of eachother represents tail lag
        snake->taillag++;
      tx = x, ty = y;

      // XXX doc
      // - X axis is 0, Y axis is 1
      // - - sign is 0, + sign is 1
      // XXX aka
      // - "one" axis is 0, "width" axis is 1
      // - "left shift" sign is 1, "right shift" sign is 0
      snake->axis |= (bb_t)(dy != 0) << x + y * board.width;
      snake->sign |= (bb_t)(dx > 0 || dy > 0) << x + y * board.width;
      board.bodies |= (bb_t)1 << x + y * board.width;
    }

    snake->head = (bb_t)1 << hx + hy * board.width;
    snake->tail = (bb_t)1 << tx + ty * board.width;
  }

  TAC(parse);

  // fprintf(stderr, "eval %hd\n", evaluate(&board));

  // fprintf(stderr, "%s\n", req);

  short evals[MAX_DEPTH][4] = {0};

  // XXX doc: iterative deepening: cache best moves and try them first
  // on next search to increase number of pruned branches
  int depth = 0;
  clock_t start = clock();
  while (++depth < MAX_DEPTH &&
         (clock() - start) * 1000 / CLOCKS_PER_SEC < T_CUTOFF)
    TICTAC(search)
  minimax(&board, evals, 0, SCHAR_MAX >> 1, depth);

  // int depth = 20;
  // TICTAC(search)
  // minimax(&board, evals, 0, SCHAR_MAX >> 1, depth);

  int m = 0;
  for (int i = 0; i < 4; i++)
    m = (*evals)[i] > (*evals)[m] ? i : m;

  char *moves[] = {"left", "right", "down", "up"};

  for (int i = 0; i < 4; i++)
    fprintf(stderr, "%s:\t%hd\n", moves[i], (*evals)[i]);
  fprintf(stderr, "searched to depth %d\n", depth);
  fprintf(stderr, "moving %s with eval %hd\n", moves[m], (*evals)[m]);

  printf("Status: 200 OK\nContent-Type: application/json\n\n");
  printf("{\"move\":\"%s\"}\n", moves[m]);
}
