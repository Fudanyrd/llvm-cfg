#include <unistd.h>

#include "buf.h"

struct Exec {
  cbuf_t argv;
  cbuf_t envp;

  Exec() = default;

  Exec &input(FileDescriptor *sin) {
    this->sin = sin;
  }

  /** Capture data from output stream. */
  Exec &capture_stdout(FileDescriptor *sout) {
    this->sout = sout;
  }
  Exec &capture_stderr(FileDescriptor *serr) {
    this->serr = serr;
  }

  int run(void);

 private:
  FileDescriptor *sout{nullptr};
  FileDescriptor *serr{nullptr};
  FileDescriptor *sin{nullptr};
};
