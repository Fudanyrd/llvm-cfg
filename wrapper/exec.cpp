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

int Exec::find_exe(const char *basename, CharStream &cs) const
{
  if (basename == nullptr) {
    return 1;
  }
  const size_t baselen = strlen(basename);
  cs.clear();
  if (basename[0] == '/') {
    // absolute path.
    int fd = (open(basename, O_RDONLY));
    (void)close(fd);
    cs.append(basename);
    return fd >= 0 ? 0 : 1;
  }

  const char *pathbuf = nullptr;
  for (int i = 0; envp[i] != nullptr; envp[i]++) {
    if (strncmp("PATH=", envp[i], 5) == 0) {
      pathbuf = envp[i] + 5;
      break;
    }
  }

  if (pathbuf == nullptr) {
    #ifdef CFG_PRINT_DEBUG_OUTPUT
      fprintf(stderr, ERROR_PREFIX "PATH is not set!\n");
    #endif
    return 1;
  }

  cs.clear();

  /** split by : */
  size_t i = 0;
  FileDescriptor fobj;
  while (pathbuf[i] != 0) {
    size_t j = i + 1;
    while (pathbuf[j] != ':' && pathbuf[j] != 0) {
      j++;
    }

    if (j > i) {
      cs.extend(j - i + 1 + baselen + 1); 
      cs.append(pathbuf + i, j - i);
      cs.append('/');
      cs.append(basename);
      #ifdef CFG_PRINT_DEBUG_OUTPUT
        fprintf(stderr, DEBUG_PREFIX "trying %s\n", cs.buffer());
      #endif 

      fobj.setFd(open(cs.buffer(), O_RDONLY));
      if (fobj.valid()) {
        return 0;
      }
    }

    /** advance. */
    cs.clear();
    i = pathbuf[j] == ':' ? j + 1 : j;
  }

  /** not found. */
  return 1;
}
