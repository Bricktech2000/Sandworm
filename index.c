#include "vendor/jsonw.h"
#include <stdio.h>
#include <stdlib.h>

#define AUTHOR "Bricktech2000"
#define COLOR "#bba98b"
#define HEAD "sand-worm"
#define TAIL "round-bum"

int main(void) {
  printf("Status: 200 OK\nContent-Type: application/json\n\n");

  char author[64], color[64], head[64], tail[64];
  if (*jsonw_escape(author, sizeof(author), AUTHOR) ||
      *jsonw_escape(color, sizeof(color), COLOR) ||
      *jsonw_escape(head, sizeof(head), HEAD) ||
      *jsonw_escape(tail, sizeof(tail), TAIL))
    abort();

  printf("{\"apiversion\":\"1\",\"author\":\"%s\","
         "\"color\":\"%s\",\"head\":\"%s\",\"tail\":\"%s\"}\n",
         author, color, head, tail);
}
