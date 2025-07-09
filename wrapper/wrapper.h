#ifndef WRAPPER_H
#define WRAPPER_H

#include "buf.h"

#include <vector>

#ifndef INIT_ARG_LEN
#define INIT_ARG_LEN 64 // Default initial length for argument buffer
#endif                  // INIT_ARG_LEN

#ifndef SANCOV_DEFAULT_DEF
#define SANCOV_DEFAULT_DEF "-fsanitize-coverage=trace-pc-guard,pc-table,no-prune"
#endif // SANCOV_DEFAULT_DEF

#ifndef CFG_EDGE_PASS
#error "CFG_EDGE_PASS is not defined"
#endif
#ifndef FUNC_CALL_PASS
#error "FUNC_CALL_PASS is not defined"
#endif
#ifndef FUNC_ENTRY_PASS
#error "FUNC_ENTRY_PASS is not defined"
#endif

struct ArgList
{
  char **buf;
  size_t size, capacity;

  ArgList(size_t size = INIT_ARG_LEN)
  {
    assert(size > 0 && "Size must be greater than zero");
    buf = static_cast<char **>(malloc(size * sizeof(char *)));
    if (!buf)
    {
      throw std::bad_alloc(); // Handle memory allocation failure
    }
    this->size = 0;
    this->capacity = size; // Initialize capacity
  }
  ~ArgList()
  {
    free(buf); // Free the allocated memory
  }

  void push(char *arg)
  {
    if (size == capacity)
    {
      overflow(capacity * 2); // Double the capacity if needed
    }
    buf[size++] = arg; // Add the new argument to the buffer
  }

  void clear()
  {
    size = 0;
  }

private:
  void overflow(size_t new_capacity)
  {
    if (new_capacity > capacity)
    {
      capacity = new_capacity; // Double the capacity
      char **new_buf = static_cast<char **>(realloc(buf, capacity * sizeof(char *)));
      if (!new_buf)
      {
        throw std::bad_alloc(); // Handle memory allocation failure
      }
      buf = new_buf;
    }
  }
};

class CompilerWrapper
{

public:
  CompilerWrapper() = default;
  ~CompilerWrapper() = default;

  enum class Lang
  {
    C = 0,
    CXX
  };

  bool ParseArgs(int argc, char **argv, char **envp);
  int Execute(void);
  void SetLang(Lang lang)
  {
    this->link_lang = lang;
  }

  const char *GetErr(void) const
  {
    return err_message ? err_message : "No error";
  }

private:
  // const char *self{nullptr} = arg_start;
  char *sancov_arg{nullptr};
  char *cc_name{nullptr};
  char *cxx_name{nullptr};
  char *emit_llvm{nullptr};
  char *gen_asm{nullptr};

  const char *debug{nullptr};
  const char *opt_level{nullptr};
  std::vector<char *> input_files;
  const char *output_file{nullptr};
  const char *lang{nullptr}; /* c, c++ */
  enum Lang link_lang
  {
    Lang::C
  };
  bool print_debug_output{true};

  std::vector<char *> include_dirs;
  std::vector<char *> defines;
  std::vector<char *> flags;

  struct StringBuf buf, aux;
  char *arg_start{nullptr} /* = buf.buffer() */;
  char *env_start{nullptr} /* = arg_end */;
  // char *env_end{nullptr} = buf.buffer_end();
  char *err_message{nullptr};

  char *arg_end()
  {
    return env_start;
  }
  char *env_end()
  {
    return const_cast<char *>(buf.buffer_end());
  }

  int compile(char *input_file);
  int cmd(char **argv, char **envp);

  void rmtemp(StringBuf &buf)
  {
    struct ArgList argl;
    argl.push("/usr/bin/rm");
    argl.push("-f");
    char *temp_file = (char *)buf.buffer();
    for (int i = 0; i < 4; i++)
    {
      argl.push(temp_file);
      temp_file = (char *)buf.next(temp_file);
    }
    argl.push(nullptr);
    (void)cmd(argl.buf, NULL); // Ignore the return value of cleanup command
  }

  void normtemp(StringBuf &buf) {
    // print the temp files, but do not remove them.
    fprintf(stderr, "[TEMP] rm -f ");
    char *temp_file = (char *)buf.buffer();
    for (int i = 0; i < 4; i++)
    {
      fprintf(stderr, "%s ", temp_file);
      temp_file = (char *)buf.next(temp_file);
    }
    fprintf(stderr, "\n");
  }

  enum class Stage {
    LINK = 0, // ??
    PREPROCESS, // -E, execute preprocessor
    ASSEMBLY, // -S, generage .s file
    OBJECT, // -c, generate .i file
  } stage{Stage::LINK};
};

#endif // WRAPPER_H
