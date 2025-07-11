#include "exec.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

static char input_buf[INIT_CAPACITY];

static void xdup2(int oldfd, int newfd) {
  if (dup2(oldfd, newfd) == -1) {
    perror("dup2");
    _exit(1);
  }
}

int Exec::run() {
  int pin[2] = {-1, -1};
  int pout[2] = {-1, -1};
  int perr[2] = {-1, -1};

  if (sin != nullptr) {
    assert(sin->fd >= 0 && "invalid fd in stream input");    
    if (pipe(pin) != 0) {
      perror("pipe");
      return 1;
    }
  }

  if (sout != nullptr) {
    if (pipe(pout) != 0) {
      perror("pipe");
      return 1;
    }
  }
  if (serr != nullptr) {
    if (pipe(perr) != 0) {
      perror("pipe");
      return 1;
    }
    serr->setFd(perr[0]);
  }

  pid_t id = fork();
  if (id < 0) {
    perror("fork");
    return 1;
  }

  if (id == 0) {
    // child
    close(pin[1]);
    close(pout[0]);
    close(perr[0]);

    if (sin != nullptr) {
      xdup2(pin[0], STDIN_FILENO);
    }
    if (sout != nullptr) {
      xdup2(pout[1], STDOUT_FILENO);
    }
    if (serr != nullptr) {
      xdup2(perr[1], STDERR_FILENO);
    }

    if (execve(argv[0], (char * const *)argv, (char *const *)envp) < 0) {
      perror("execve");
      _exit(1);
    }

    /** should not reach here. */
    return -1;
  } else {
    // parent process
    close(pin[0]);
    close(pout[1]);
    close(perr[1]);

    if (sout) {
      sout->setFd(pout[0]);
    }
    if (serr) {
      serr->setFd(perr[0]);
    }

    /** read from sin. */
    if (sin) {
      auto nb = read(sin->fd, input_buf, sizeof(input_buf));
      while (nb > 0) {
        write(pin[1], input_buf, nb);
        nb = read(sin->fd, input_buf, sizeof(input_buf));
      }

      /** close input stream.  */
      sin->setFd(-1);
    }

    int status;
    waitpid(id, &status, 0);
    if (WIFEXITED(status)) {
      return WEXITSTATUS(status); // Return the exit status of the child process
    } else {
      return -1; // Indicate an error occurred
    }
  }

  /** It is your task to read from sout, serr and close them. */
}
