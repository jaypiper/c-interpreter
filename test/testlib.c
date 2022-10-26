#include <stdio.h>
#include <stdlib.h>
int GET() {
   return 0;
}

void* MALLOC(int size) {
   return malloc(size);
}

void FREE(void* ptr) {
   free(ptr);
}

void PRINT(int x) {
   printf("%d\n",x);
}