#include <stdio.h>
static unsigned int counter = 0;

void echo_puts(const char *ch) {
    counter ++;
    if (ch == NULL && counter < 4) {
        printf("null\n");
        return;
    }

    puts(ch);
}
