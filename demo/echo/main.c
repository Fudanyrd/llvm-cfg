#include <stdio.h>
#include <stdlib.h>

extern void echo_puts(const char *);

int main(int argc, char **argv) {
    for (int i = 0; i <= argc; i++) {
        echo_puts(argv[i]);
    }

   exit(0);
}
