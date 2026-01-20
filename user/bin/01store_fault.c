#include "../user.h"

int main(void) {
    puts("Into Test store_fault, we will insert an invalid store operation...");
    puts("Kernel should kill this application!");
    *(volatile int *)0 = 0;
    return 0;
}
