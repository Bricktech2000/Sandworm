#include <stdio.h>

#define API_VERSION "1"
#define AUTHOR "Bricktech2000"

#define COLOR "#32a844"
#define HEAD "sand-worm"
#define TAIL "round-bum"

int main(void) {
  printf("Status: 200 OK\nContent-Type: application/json\n\n");

  printf("{\"apiversion\":\"%s\",\"author\":\"%s\",\"color\":\"%s\","
         "\"head\":\"%s\",\"tail\":\"%s\"}\n",
         API_VERSION, AUTHOR, COLOR, HEAD, TAIL);
}
