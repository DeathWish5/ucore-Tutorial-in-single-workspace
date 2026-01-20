#include "../user.h"

int main(void) {
    puts("Try to access privileged CSR in U Mode");
    puts("Kernel should kill this application!");
    asm volatile("csrw stvec, zero");
    return 0;
}
