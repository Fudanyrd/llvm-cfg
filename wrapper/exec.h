#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "buf.h"

struct Exec {
  cbuf_t argv;
  cbuf_t envp;

  Exec() = default;

  Exec &input(FileDescriptor *sin) {
    this->sin = sin;
    return *this;
  }

  /** Capture data from output stream. */
  Exec &capture_stdout(FileDescriptor *sout) {
    this->sout = sout;
    return *this;
  }
  Exec &capture_stderr(FileDescriptor *serr) {
    this->serr = serr;
    return *this;
  }

  int run(void);

  /** Find the name of a executable from $PATH.
   * Eg, rm -> /usr/bin/rm in the stream.
   */
  int find_exe(const char *basename, CharStream &cs) const;

  /** run mktemp and store the result in cs. */
  static int mktemp(bool directory, CharStream &cs, 
                    const char *template_,
                    const char **envp);

  static int rm(bool recursive, bool force, const char *arg,
                const char **envp);

 private:
  FileDescriptor *sout{nullptr};
  FileDescriptor *serr{nullptr};
  FileDescriptor *sin{nullptr};
};
