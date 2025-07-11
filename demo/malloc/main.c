#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

void *cfg_malloc(size_t size);

static void seg_fault_handler(int arg __attribute__((unused))) {
  write(2, "\033[01;31m[!]\033[0;m Oops, fail\n", 29);
  _exit(0);
}

int main(int argc, char **argv, char **envp) {
  if (signal(SIGSEGV, seg_fault_handler) == SIG_ERR) {
    perror("signal");
    _exit(1);
  }

  for (int i = 0; i < 65536; i++) {
    int *buf = cfg_malloc(sizeof(int) * 8);
    /** if buf is null, write to buf will crash. */
    *buf = 0xC0FFEE;
    free(buf);
  }

  fprintf(stderr, "\033[01;92m[+]\033[0;m Completed.\n");
  return 0;
}
