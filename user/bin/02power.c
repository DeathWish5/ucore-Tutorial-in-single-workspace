#include "../user.h"

#define P       3
#define MOD     10007
#define STEP    100000

int main(void) {
    unsigned int pow = 1;
    for (int i = 1; i <= STEP; i++) {
        pow = (pow * P) % MOD;
        if (i % 10000 == 0) {
            print_int(P);
            putchar('^');
            print_int(i);
            putchar('=');
            print_int(pow);
            print_str("(MOD ");
            print_int(MOD);
            puts(")");
        }
    }
    puts("Test power OK!");
    return 0;
}
