#include "jsonw.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// XXX multithreading?
// XXX Best Reply Search? https://notpeerreviewed.com/blog/battlesnake/

// write diagnostics to `stderr` so cgi.conf can redirect them to a file

#define MAX_DEPTH 64        // max search depth, for allocating a buffer
#define MAX_SNAKES 4        // max number of snakes; the smaller the faster
#define T_CUTOFF 200 / 1000 // cutoff to deepening the search, in seconds
#define N_VORONOI 32        // number of Voronoi propagation steps
#define K_OWNED 1           // reward for number of "owned" cells
#define K_FOOD 0            // reward for number of "owned" food cells
#define K_LENGTH 2          // reward for being longer than others
#define K_HEALTH 0 / 100    // reward for having more health than others

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

#define EVAL_MIN (SHRT_MIN / 2)
#define EVAL_MAX (SHRT_MAX / 2)
#define EVAL_ZERO 0

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
  // these two bitboards contain the path to the head, so we know how to move
  // the tail. axis=0 means X axis (bitboard shift by `1`); axis=1 means Y axis
  // (bitboard shift by `board.width`). sign=0 means right/down (bitboard right
  // shift); sign=1 means left/up (bitboard left shift)
  bb_t axis, sign;
  unsigned char length, health;
  unsigned char taillag; // number of turns to wait before moving the tail
};

struct board {
  struct snake snakes[MAX_SNAKES];
  // `food` holds the cells with food, `bodies` holds the union of all snake
  // bodies, and `heads` holds the heads of all snakes that haven't yet moved
  // on the current turn
  bb_t food, bodies, heads;
  // these two bitboards they are initialized once and never modified again.
  // `board` is a bit mask that contains every cell of the board, so we can mask
  // out excess bits in a `bb_t` that are outside the board. `xmask` is the same
  // but excludes cells that are on the right edge of the board, so we can mask
  // out bits that would wrap around horizontally when shifting by 1
  bb_t board, xmask;
  unsigned char width, height;
};

short evaluate(struct board *board) {
  // Voronoi heuristic. a cell is "owned" by a snake if that snake can get to it
  // before the other snakes. we make sure longer snakes move first because they
  // would win out in a head-to-head collision

  // use name 's' to index into `board->snakes` and 'l' to index into `sort`

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

  bb_t filled = board->bodies;

  bb_t owned[MAX_SNAKES];
  for (int l = 0; l < n; l++)
    owned[l] = board->snakes[sort[l]].head;

  for (int i = 0; i < N_VORONOI; i++) {
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
  short eval = EVAL_ZERO;
  for (int l = 0; l < n; l++) {
    if (sort[l])
      eval -= board->snakes[sort[l]].length * K_LENGTH +
              board->snakes[sort[l]].health * K_HEALTH;
    else
      eval += bb_popcnt(owned[l]) * K_OWNED +
              bb_popcnt(owned[l] & board->food) * K_FOOD +
              board->snakes->length * K_LENGTH +
              board->snakes->health * K_HEALTH;
  }

  return eval;
}

short minimax(struct board *board, int *best, short (*evals)[4], short alpha,
              short beta, int depth);
short minimax_step(int s, struct board *board, int *best, short (*evals)[4],
                   short alpha, short beta, int depth) {
  // perform one minimax step. in one "step", one snake moves once

  if (!board->snakes->health)
    return EVAL_MIN;

  if (depth == 0)
    // XXX doc least significant bit for mark
    return evaluate(board) * 2;

  // skip over dead snakes and find the next live snake. if we iterate past the
  // last snake, then all live snakes have moved this turn, so call `minimax` to
  // begin the next turn
  do
    if (++s == MAX_SNAKES)
      return minimax(board, best, evals, alpha, beta, depth);
  while (!board->snakes[s].health);

  struct snake *snake = board->snakes + s;

  // shifting right by 1 to leave some buffer to avoid wraparound
  short eval = s ? EVAL_MAX : EVAL_MIN;
  unsigned char length = snake->length, health = snake->health;
  unsigned char taillag = snake->taillag;
  bool did_recurse = false;

  // about to move, so remove our head from the bitboard containing the heads of
  // snakes that haven't yet moved this turn
  board->heads &= ~snake->head;

  // TODO need a tie breaker for moving adjacent to a longer snake's head -->
  // should be fixed. make sure actually fixed

  // XXX doc: minimax invariants. these impact performance but should have no
  // effect on evals:
  //   - disabling alpha-beta pruning --> not true
  //   - changing move ordering (ignoring the cached evals)
  //   - going to a given depth right away instead of iteratively

  for (int i = 0; i < 4; i++) {
    // iterative deepening: explore more promising moves first (as given
    // by cached evals in `evals`) to maximize the number of pruned
    // branches. cached evals are marked as "explored" by setting their
    // least significant bit
    short *evalp = *evals;
    for (short *e = *evals; e < *evals + 4; e++)
      evalp = (*evalp & 1) || !(*e & 1) && *e > *evalp == !s ? e : evalp;

    // XXX invariant
    // if (depth == 20)
    //   printf("%zd\n", evalp - *evals);

    // XXX invariants
    // short *evalp = *evals + i;
    // short *evalp = *evals + 3 - i;

    // default to the worst possible eval, for `continue`s and `goto contin`s
    *evalp = (s ? EVAL_MAX : EVAL_MIN) | 1;

    // when minimax is hopelessly broken and commenting out these two lines
    // fixes it, it's likely there's an eval overflowing somewhere
    // XXX doc because of alpha--beta pruning, root *evals are not the actual
    // evals, they're upper bounds on evals. so we need *best so we can store
    // the move that leads to the actual that eval
    if (alpha >= beta)
      continue;

    bool axis = (evalp - *evals) >> 1, sign = (evalp - *evals) & 1;

    if (!axis && !sign && !(snake->head & board->xmask) ||
        !axis && sign && !(snake->head << 1 & board->xmask) ||
        axis && !sign && !(snake->head >> board->width) ||
        axis && sign && !(snake->head << board->width & board->board))
      continue; // would move out of bounds

    axis ? (snake->axis |= snake->head) : (snake->axis &= ~snake->head);
    sign ? (snake->sign |= snake->head) : (snake->sign &= ~snake->head);
    sign ? (snake->head <<= axis ? board->width : 1)
         : (snake->head >>= axis ? board->width : 1);

    if (snake->head & board->bodies)
      goto contin; // would collide with another snake

    bb_t adjacent = 0;
    adjacent |= (snake->head & board->xmask) >> 1;
    adjacent |= snake->head << 1 & board->xmask;
    adjacent |= snake->head >> board->width;
    adjacent |= snake->head << board->width & board->board;

    // can't move adjacent to the head of a longer snake that hasn't yet moved
    // this turn because they could kill us by moving onto our head
    if (adjacent & board->heads)
      for (int r = s + 1; r < MAX_SNAKES; r++)
        if (board->snakes[r].length >= snake->length &&
            board->snakes[r].health && (adjacent & board->snakes[r].head)) {
          // tie breaker: prefer a probable head-to-head death to certain death
          *evalp += 4;
          goto update;
        }

    board->bodies |= snake->head;
    snake->health--;
    if (snake->taillag)
      snake->taillag--;
    if (snake->head & board->food) {
      snake->length++, snake->taillag++, snake->health = 100;
      board->food &= ~snake->head;
    }

    // tie breaker: even when certain death is coming, survive as long as we can
    int tiebreak = 2;
    // tie breaker: certain death is better if it's contingent on an opponent
    tiebreak += s ? 2 : 0;

    // XXX is it right to subtract tiebreak from α and β like this?
    did_recurse = true;
    *evalp = minimax_step(s, board, &(int){0}, evals + 1, alpha - tiebreak,
                          beta - tiebreak, depth - 1) +
             tiebreak;

  update:
    if (s && *evalp < eval) {
      eval = *evalp, *best = evalp - *evals;
      beta = eval < beta ? eval : beta;
    }
    if (!s && *evalp > eval) {
      eval = *evalp, *best = evalp - *evals;
      alpha = eval > alpha ? eval : alpha;
    }

    // mark the cached eval as explored
    *evalp |= 1;

    if (snake->length > length)
      board->food |= snake->head;
    snake->length = length, snake->health = health;
    snake->taillag = taillag;
    board->bodies &= ~snake->head;

  contin:
    sign ? (snake->head >>= axis ? board->width : 1)
         : (snake->head <<= axis ? board->width : 1);
  }

  // unmark the evals we've just cached, for the next iterative deepening step
  for (short *e = *evals; e < *evals + 4; e++)
    *e &= ~1;

  // if an opponent has no available moves, mark them as dead and keep on
  // searching deeper. doing this manually is only necessary because we prune
  // branches that lead to immediate death
  if (s && !did_recurse) {
    snake->health = 0;
    eval = minimax_step(s, board, &(int){0}, evals + 1, alpha, beta, depth - 1);
    snake->health = health;
  }

  board->heads |= snake->head;

  return eval;
}

short minimax(struct board *board, int *best, short (*evals)[4], short alpha,
              short beta, int depth) {
  // perform one minimax turn. in one "turn", each snake moves once

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

  for (int s = 0; s < MAX_SNAKES; s++) {
    struct snake *snake = board->snakes + s;

    if (!snake->health)
      continue;

    board->heads |= snake->head;

    if (snake->taillag)
      continue;

    // move the tail toward the head according to `snake.axis` and `snake.sign`
    board->bodies &= ~snake->tail;
    axes <<= 1, sgns <<= 1;
    axes |= !!(snake->axis & snake->tail);
    sgns |= !!(snake->sign & snake->tail);
    sgns & 1 ? (snake->tail <<= axes & 1 ? board->width : 1)
             : (snake->tail >>= axes & 1 ? board->width : 1);
  }

  short eval = minimax_step(-1, board, best, evals, alpha, beta, depth);

  for (int s = MAX_SNAKES; s--;) {
    struct snake *snake = board->snakes + s;

    if (!snake->health || snake->taillag)
      continue;

    // move the tail back where it was, and restore `snake.axis` and
    // `snake.sign`, since we may have overwritten it during the turn
    sgns & 1 ? (snake->tail >>= axes & 1 ? board->width : 1)
             : (snake->tail <<= axes & 1 ? board->width : 1);
    axes & 1 ? (snake->axis |= snake->tail) : (snake->axis &= ~snake->tail);
    sgns & 1 ? (snake->sign |= snake->tail) : (snake->sign &= ~snake->tail);
    axes >>= 1, sgns >>= 1;
    board->bodies |= snake->tail;
  }

  board->heads = 0;

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
  unsigned int seed = 0; // for srand()
  char *j_snakes = jsonw_lookup("snakes", jsonw_beginobj(j_board));
  for (char *j_snake = jsonw_beginarr(j_snakes); j_snake;
       j_snake = jsonw_element(j_snake)) {

    char *j_id = jsonw_lookup("id", jsonw_beginobj(j_snake));
    char *j_id_end = jsonw_string(NULL, j_id);
    if (!j_id_end)
      fputs("bad snake id\n", stderr), exit(EXIT_FAILURE);
    ptrdiff_t j_id_sz = j_id_end - j_id;

    bool is_you = j_yid_sz == j_id_sz && memcmp(j_yid, j_id, j_yid_sz) == 0;
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

      seed <<= 1, seed ^= x ^ y;
      signed char dx = tx - x, dy = ty - y;

      if (hx == -1 && hy == -1)
        hx = x, hy = y;
      if (tx == x && ty == y)
        // several body parts stacked on top of eachother represents tail lag
        snake->taillag++;
      tx = x, ty = y;

      // store the path toward the head in `snake.axis` and `snake.sign`
      snake->axis |= (bb_t)(dy != 0) << x + y * board.width;
      snake->sign |= (bb_t)(dx > 0 || dy > 0) << x + y * board.width;
      board.bodies |= (bb_t)1 << x + y * board.width;
    }

    snake->head = (bb_t)1 << hx + hy * board.width;
    snake->tail = (bb_t)1 << tx + ty * board.width;
  }

  // fprintf(stderr, "eval %hd\n", evaluate(&board));

  // fprintf(stderr, "%s\n", req);

  short evals[MAX_DEPTH][4] = {0};

  // // XXX uncomment
  // srand(seed);
  // for (int d = 0; d < MAX_DEPTH; d++)
  //   for (int m = 0; m < 4; m++)
  //     evals[d][m] = rand() & USHRT_MAX & ~1;

  // int depth = 20;
  // minimax(&board, evals, EVAL_MIN, EVAL_MAX, depth);

  // iterative deepening: iteratively search deeper and deeper until we hit
  // `T_CUTOFF`, caching move evals as we go along so we can prune more branches
  // in subsequent iterations
  int best = 0;
  fprintf(stderr, "\nDEPTH\tMICROS\tTOTAL\n");
  clock_t start = clock(), now = start, prev = now;
  for (int depth = 1; depth <= MAX_DEPTH; depth++) {
    if ((now - start) > CLOCKS_PER_SEC * T_CUTOFF)
      break;
    minimax(&board, &best, evals, EVAL_MIN, EVAL_MAX, depth);
    prev = now, now = clock();
    fprintf(stderr, "%d\t%07lld\t%07lld\n", depth,
            (long long)(now - prev) * 1000000 / CLOCKS_PER_SEC,
            (long long)(now - start) * 1000000 / CLOCKS_PER_SEC);
  }

  char *moves[] = {"left", "right", "down", "up"}; // JSON escaped

  fprintf(stderr, "\nMOVE\tEVAL\tBEST\n");
  for (int m = 0; m < 4; m++)
    fprintf(stderr, "%s\t%+hd\t%d\n", moves[m], (*evals)[m], best == m);

  printf("Status: 200 OK\nContent-Type: application/json\n\n");
  printf("{\"move\":\"%s\"}\n", moves[best]);
}
