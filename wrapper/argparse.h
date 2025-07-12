#include "buf.h"

class ArgParse {
 public:
  enum class Lang
  {
    C = 0,
    CXX
  };

  enum class Stage {
    LINK = 0, // ??
    PREPROCESS, // -E, execute preprocessor
    ASSEMBLY, // -S, generage .s file
    OBJECT, // -c, generate .i file
  };

  ArgParse() = delete;
  ArgParse(char **argv, char **envp); 

  ~ArgParse() = default;

  const StringBuf &Envp(void) const {
    return env_buf;
  }

  const char *output_suffix() const;
  static const char *suffix_of(const char *path);
  bool runpass() const {
    return this->stage == Stage::ASSEMBLY || this->stage == Stage::OBJECT;
  }

 // private:
  /** NOTE: these fields are immutable. Modify them at your own risk. */
  const char *cc_name{nullptr}; // [env] CFG_CC=
  const char *cxx_name{nullptr}; // [env] CFG_CXX=

  const char *debug{nullptr}; // -g, -gdwarf-4, etc.
  const char *opt_level{nullptr}; // -O2, -O3, ..
  const char *output_file{nullptr}; // -o a.out
  const char *lang{nullptr}; /* c, c++ */
  enum Lang link_lang{Lang::C};

  std::vector<const char *> input_files;
  std::vector<const char *> include_dirs;
  std::vector<const char *> defines;
  std::vector<const char *> flags;
  std::vector<const char *> margs; /* -MD, -MT, -MF, etc */

  Stage stage{Stage::LINK};

  bool output_llvm{false}; // -emit-llvm
  bool read_stdin{false};
  const char *err_message{nullptr};
  /** END NOTE */

#ifdef CFG_PRINT_DEBUG_OUTPUT
  bool print_debug_output{true};
#else
  bool print_debug_output{false};
#endif // CFG_PRINT_DEBUG_OUTPUT

 private:
  StringBuf arg_buf;
  StringBuf env_buf;
  /** this buffer is small. */
  StringBuf aux_buf{StringBuf(256)};

  void parse_arg(void);
  void parse_env(void);
};

class ArgGenerator {
 public:
  ArgGenerator(ArgParse &argparser): parser(argparser) {}
  virtual ~ArgGenerator() = default;

  virtual int execute() const;

  ArgGenerator &add_compile_arg(const char *arg) {
    extra_compile_args.push_back(arg);
    return *this;
  }

  ArgGenerator &add_link_arg(const char *arg) {
    extra_link_args.push_back(arg);
    return *this;
  }

  ArgGenerator &add_pass_plugin(const char *pass) {
    extra_pass_names.push_back(pass);
    return *this;
  }

 protected:
  ArgParse &parser;
  std::vector<const char *> extra_compile_args;
  std::vector<const char *> extra_link_args;
  std::vector<const char *> extra_pass_names;
  StringBuf aux_buf{StringBuf(1024)};
  CharStream stream;

  /** output must be .ll */
  int force_emit_ll(const char *input, const char *output) const;
  int compile_ll(const char *ll_input, const char *output) const;
};
