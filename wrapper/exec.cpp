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

int Exec::mktemp(bool directory, CharStream &cs, 
                 const char *template_,
                 const char **envp) {
  Exec which_mktemp;
  which_mktemp.envp = envp;
  CharStream mktemp_path(64);
  int ret = which_mktemp.find_exe("mktemp", mktemp_path);
  if (ret != 0) {
    #ifdef CFG_PRINT_DEBUG_OUTPUT
      fprintf(stderr, ERROR_PREFIX "cannot locate mktemp\n");
    #endif
    /** failure! */
    return ret;
  }

  Exec exe_mktemp;
  const char *args[4];
  int next = 0;
  args[next++] = mktemp_path.buffer();
  if (directory) {
    args[next++] = "--directory";
  }
  args[next++] = template_;
  args[next++] = nullptr;
  exe_mktemp.argv = (const char **)args;
  exe_mktemp.envp = envp;

  FileDescriptor mktemp_output;
  ret = exe_mktemp.capture_stdout(&mktemp_output)
                  .run();
  if (ret != 0) {
    #ifdef CFG_PRINT_DEBUG_OUTPUT
      fprintf(stderr, ERROR_PREFIX "cannot execute mktemp\n");
    #endif
    /** failure! */
    return ret;
  }
  
  /** parse output to get temp file name. */
  cs.clear();
  char ch;
  auto nb = read(mktemp_output.fd, &ch, 1);
  if (nb < 0) {
    /** must exit when this happens. */
    #ifdef CFG_PRINT_DEBUG_OUTPUT
      perror("Exec::mktemp: read");
    #endif 
    return 1;
  }

  while (nb > 0 && ch != '\n') {
    cs.append(ch);
    nb = read(mktemp_output.fd, &ch, 1);
    if (nb < 0) {
      /** must exit when this happens. */
      #ifdef CFG_PRINT_DEBUG_OUTPUT
        perror("Exec::mktemp: read");
      #endif 
      return 1;
    }
  }

  cs.append((char)0);
  #ifdef CFG_PRINT_DEBUG_OUTPUT
    fprintf(stderr, DEBUG_PREFIX "create tempfile %s\n", cs.buffer());
  #endif
  return 0;
}

int Exec::rm(bool recurse, bool force,
             const char *arg,
             const char **envp) {
  Exec which_rm;
  which_rm.envp = envp;
  CharStream rm_path(64);
  int ret = which_rm.find_exe("rm", rm_path);
  if (ret != 0) {
    #ifdef CFG_PRINT_DEBUG_OUTPUT
      fprintf(stderr, ERROR_PREFIX "cannot locate rm\n");
    #endif
    /** failure! */
    return ret;
  }

  Exec exe_rm;
  const char *args[6];
  int next = 0;
  args[next++] = rm_path.buffer();
  if (recurse) {
    args[next++] = "-r";
  }
  if (force) {
    args[next++] = "-f";
  }
  args[next++] = arg;
  args[next++] = nullptr;
  exe_rm.argv = (const char **)args;
  exe_rm.envp = envp;
  return exe_rm.run();
}

int Exec::cp(const char *src, const char *dst, const char **envp) {
  Exec which_cp;
  which_cp.envp = envp;
  CharStream cp_path(64);
  int ret = which_cp.find_exe("cp", cp_path);
  if (ret != 0) {
    #ifdef CFG_PRINT_DEBUG_OUTPUT
      fprintf(stderr, ERROR_PREFIX "cannot locate cp\n");
    #endif
    /** failure! */
    return ret;
  }

  Exec exe_cp;
  const char *args[6];
  int next = 0;
  args[next++] = cp_path.buffer();
  args[next++] = src;
  args[next++] = dst;
  args[next++] = nullptr;
  exe_cp.argv = (const char **)args;
  exe_cp.envp = envp;
  return exe_cp.run();
}
