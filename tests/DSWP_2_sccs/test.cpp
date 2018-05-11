#include <stdio.h>
#include <stdint.h>

int main (int argc, char *argv[]){
  int v1, v2, v1_temp;
  v1 = argc;
  v2 = ((argc * 5) / 2) + 1;

  for (uint32_t i = 0; i < (UINT32_MAX / 2); ++i) {

    // SCC 1
    v1_temp = v1;
    v1 = v1 + 1;
    v1 = v1 - 3;

    // SCC 2
    v2 = v2 - v1_temp;
    v2 = v2 * 3;
    v2 = v2 / 2;
  }

  printf("%d, %d\n", v1, v2);
  return 0;
}
