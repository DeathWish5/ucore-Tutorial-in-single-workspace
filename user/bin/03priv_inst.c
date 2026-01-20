#include "../user.h"

int main(void) {
    puts("Try to execute privileged instruction in U Mode");
    puts("Kernel should kill this application!");
    asm volatile("sret");
    return 0;
}
