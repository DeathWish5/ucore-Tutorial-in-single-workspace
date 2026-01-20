#include "../user.h"

#define LEN     100
#define P       5
#define MOD     998244353
#define ITER    140000

int main(void) {
    unsigned long s[LEN];
    int cur = 0;

    for (int i = 0; i < LEN; i++) s[i] = 0;
    s[0] = 1;

    for (int i = 1; i <= ITER; i++) {
        int next = (cur + 1 == LEN) ? 0 : cur + 1;
        s[next] = (s[cur] * P) % MOD;
        cur = next;
        if (i % 10000 == 0) {
            print_str("power_5 [");
            print_int(i);
            putchar('/');
            print_int(ITER);
            puts("]");
        }
    }

    print_int(P);
    putchar('^');
    print_int(ITER);
    print_str(" = ");
    print_long(s[cur]);
    print_str("(MOD ");
    print_long(MOD);
    puts(")");
    puts("Test power_5 OK!");
    return 0;
}
