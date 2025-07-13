#include "argparse.h"
#include "exec.h"

#include <queue>
#include <stack>
#include <utility>

const char *ArgParse::suffix_of(const char *path) {
  size_t i = strlen(path);
  if (i == 0) {
    return path;
  }

  i -= 1;
  while (i > 0 && path[i] != '.') {
    i--;
  }

  return path + i;
}

const char *ArgParse::output_suffix() const {
  const char *ret;
  switch (this->stage) {
    case (Stage::LINK): {
      ret = ".out";
      break;
    }
    case (Stage::ASSEMBLY): {
      ret = this->output_llvm ? ".ll" : ".s";
      break;
    }
    case (Stage::OBJECT): {
      ret = this->output_llvm ? ".bc" : ".o";
      break;
    }
    case (Stage::PREPROCESS): {
      ret = ".i";
    }
  }

  return ret;
}

ArgParse::ArgParse(char **argv, char **envp) {
  if (true) {
    Exec exe;
    exe.envp = (const char **)envp;
    if (exe.find_exe("clang", this->clang) == 0) {
      this->cc_name = this->clang.buffer();
    }
    if (exe.find_exe("clang++", this->clangpp) == 0) {
      this->cxx_name = this->clangpp.buffer();
    }
  }

  if (print_debug_output)
  {
    fprintf(stderr, DEBUG_PREFIX "Initial args = [");
    for (int i = 1; argv[i]; i++)
    {
      fprintf(stderr, "%s ", argv[i]);
    }
    fprintf(stderr, "]\n");
  }

  for (int i = 0; argv[i] != nullptr; i++) {
    arg_buf.append(argv[i], nullptr);
  }

  for (int i = 0; envp[i] != nullptr; i++) {
    env_buf.append(envp[i], nullptr);
  }

  if (print_debug_output) {
    fprintf(stderr, DEBUG_PREFIX "sizeof arg buffer %ld\n", arg_buf.getCapacity());
    fprintf(stderr, DEBUG_PREFIX "sizeof env buffer %ld\n", env_buf.getCapacity());
  }

  this->parse_arg();
  this->parse_env();

  if (!this->cc_name || !this->cxx_name) {
    this->err_message = (ERROR_PREFIX "cannot run which clang/clang++\n"
    "Hint: specify by CFG_CC and CFG_CXX environment variables.\n");
    return;
  }

#ifdef CFG_PRINT_DEBUG_OUTPUT
  fprintf(stderr, DEBUG_PREFIX "use CC=%s\n", this->cc_name);
  fprintf(stderr, DEBUG_PREFIX "use CXX=%s\n", this->cxx_name);
#endif 
}

void ArgParse::parse_env(void) {
  const char *eend = env_buf.buffer_end();
  const char *iter = env_buf.buffer();

  for (; iter < eend; iter = env_buf.next(iter)) {
    if (strncmp("CFG_CC=", iter, 7) == 0) {
      cc_name = iter + 7;
    } else if (strncmp("CFG_CXX=", iter, 8) == 0) {
      cxx_name = iter + 8;
    }
  }
}

void ArgParse::parse_arg(void) {
  const char *iter = arg_buf.buffer();
  // skip the first arg
  iter = arg_buf.next(iter);

  bool set_lang = false;
  bool set_output = false;
  bool mf = false;
  const char *aend = arg_buf.buffer_end();
  for ( ;iter < aend; iter = arg_buf.next(iter)) {
    if (set_output) {
      output_file = iter;
      set_output = false;
      continue;
    } else if (set_lang) {
      lang = iter;
      set_lang = false;
      continue;
    } else if (mf) {
      margs.push_back(iter);
      mf = false;
      continue;
    }

    if (iter[0] != '-') {
      input_files.push_back(iter);
      continue;
    }

    if (strncmp(iter, "-g", 2) == 0)
    {
      this->debug = iter;
    }
    else if (strncmp(iter, "-O", 2) == 0)
    {
      this->opt_level = iter;
    }
    else if (strcmp(iter, "-c") == 0)
    {
      stage = Stage::OBJECT;
    }
    else if (strcmp(iter, "-S") == 0)
    {
      stage = Stage::ASSEMBLY;
    }
    else if (strcmp(iter, "-E") == 0)
    {
      stage = Stage::PREPROCESS;
    }
    else if (strcmp(iter, "-o") == 0)
    {
      set_output = true;
    }
    else if (strcmp(iter, "-x") == 0)
    {
      set_lang = true;
    }
    else if (strcmp(iter, "-") == 0)
    {
      read_stdin = true;
      input_files.push_back((char *)"-");
    }
    else if (strncmp(iter, "-I", 2) == 0)
    {
      include_dirs.push_back((char *)iter);
    }
    else if (strncmp(iter, "-D", 2) == 0)
    {
      defines.push_back((char *)iter);
    }
    else if (strcmp(iter, "-MF") == 0 || strcmp(iter, "-MT") == 0)
    {
      margs.push_back((char *)iter);
      mf = true;
    }
    else if (strncmp(iter, "-M", 2) == 0)
    {
      margs.push_back((char *)iter);
    }
    else if (strcmp(iter, "-emit-llvm") == 0)
    {
      output_llvm = true;
    }
    else
    {
      flags.push_back((char *)iter);
    }
  }

  if (lang != nullptr) {
    if (strcmp(lang, "c") == 0) {
      link_lang = Lang::C;
    } else if (strcmp(lang, "c++") == 0) {
      link_lang = Lang::CXX;
    } else {
      err_message = "unknown language after -x flag.";
    }
  } else {
    if (read_stdin && stage != Stage::PREPROCESS) {
      err_message = "must specify -E or -x when reading stdin";
    }
  }

  if (input_files.empty()) {
    err_message = "No input files given";
  }
}

static void alst_xpush(struct ArgList &l, const char *arg) {
  if (arg != nullptr) {
    l.push(arg);
  }
}

int ArgGenerator::force_emit_ll(const char *input, const char *output) const {
  struct ArgList alst;

  alst.push(parser.link_lang == ArgParse::Lang::C ? parser.cc_name
                                                  : parser.cxx_name);
  const std::vector<const char *> &extras = extra_compile_args;
  for (const char *arg : extras) {
    alst.push(arg);
  }
  alst_xpush(alst, parser.debug);
  alst_xpush(alst, parser.opt_level);
  if (strcmp(input, "-") == 0) {
    alst.push("-x");
    alst.push(parser.lang);
  }

  for (const char *arg : parser.include_dirs) {
    alst.push(arg);
  }
  for (const char *arg : parser.defines) {
    alst.push(arg);
  }
  for (const char *arg : parser.flags) {
    alst.push(arg);
  }
  for (const char *arg : parser.margs) {
    alst.push(arg);
  }

  alst.push("-S");
  alst.push("-emit-llvm");
  alst.push(input);
  alst.push("-o");
  alst.push(output);
  alst.push(nullptr);

  Exec exe;
  exe.argv = alst.buf;
  exe.envp = elst.buf;
  return exe.run();
}

int ArgGenerator::compile_ll(const char *input, const char *output) const {
  struct ArgList alst;

  alst.push(parser.cc_name);
  for (const char *pass : extra_pass_names) {
    alst.push(pass);
  }

  alst.push(parser.stage == ArgParse::Stage::ASSEMBLY ? "-S" : "-c");
  alst.push(input);
  alst.push("-o");
  alst.push(output);
  alst.push(nullptr);

  Exec exe;
  exe.argv = alst.buf;
  exe.envp = elst.buf;
  return exe.run();
}

int ArgGenerator::preprocessor(const char *input, const char *output) const
{
  struct ArgList alst;

  /** prepare env vars */
  const char *suffix = ArgParse::suffix_of(input);
  alst.push(strcmp(suffix, ".c") == 0 ? parser.cc_name : parser.cxx_name);
  for (const char *arg : parser.include_dirs) {
    alst.push(arg);
  }
  for (const char *arg : parser.defines) {
    alst.push(arg);
  }
  for (const char *arg : parser.margs) {
    alst.push(arg);
  }
  for (const char *arg : parser.flags) {
    alst.push(arg);
  }
  if (parser.lang) {
    alst.push("-x");
    alst.push(parser.lang);
  }
  alst_xpush(alst, parser.debug);
  alst.push("-E");
  alst.push(input);

  if (output != nullptr) {
    alst.push("-o");
    alst.push(output);
  } /* else print to stdout. */
  alst.push(nullptr);

  Exec exe;
  exe.argv = alst.buf;
  exe.envp = elst.buf;
  return exe.run();
}

int ArgGenerator::compiler(const char *input, const char *output, bool llvm) const
{
  /** .i  .ipp => .s */
  struct ArgList alst;
  const char *suffix = ArgParse::suffix_of(input);
  bool append_flag = strcmp(suffix, ".ll") != 0;
  bool iscpp = strcmp(suffix, ".cpp") == 0;

  alst.push(iscpp ? parser.cxx_name : parser.cc_name);
  if (append_flag) {
    for (const char *arg : parser.flags) {
      alst.push(arg);
    }
    for (const char *arg : this->extra_compile_args) {
      alst.push(arg);
    }
  }
  alst_xpush(alst, parser.debug);
  alst_xpush(alst, parser.opt_level);
  alst.push("-S");
  alst.push(input);
  if (llvm) {
    alst.push("-emit-llvm");
  }
  alst.push("-o");
  alst.push(output);
  alst.push(nullptr);

  Exec exe;
  exe.argv = alst.buf;
  exe.envp = elst.buf;
  return exe.run();
}

int ArgGenerator::assembler(const char *input, const char *output, bool llvm) const
{
  struct ArgList alst;

  alst.push(parser.cc_name);
  alst_xpush(alst, parser.debug);
  alst_xpush(alst, parser.opt_level);
  alst.push("-c");
  alst.push(input);
  if (llvm) {
    alst.push("-emit-llvm");
  }
  alst.push("-o");
  alst.push(output);
  alst.push(nullptr);

  Exec exe;
  exe.argv = alst.buf;
  exe.envp = elst.buf;
  return exe.run();
}

int ArgGenerator::linker(const std::vector<const char *> &inputs, const char *output) const {
  struct ArgList alst;

  if (output == nullptr) {
    output = "a.out";
  }

  alst.push(parser.cxx_name);
  for (const char *arg : inputs) {
    alst.push(arg);
  }
  for (const char *arg : parser.flags) {
    alst.push(arg);
  }
  for (const char *arg : this->extra_link_args) {
    alst.push(arg);
  }
  alst_xpush(alst, parser.debug);
  alst.push("-o");
  alst.push(output);
  alst.push(nullptr);

  Exec exe;
  exe.argv = alst.buf;
  exe.envp = elst.buf;
  return exe.run();
}

int ArgGenerator::transform(const char *pass, const char *input, 
                            const char *output) const {
  assert(strncmp(pass, "-fpass-plugin=", 14) == 0 && "invalid pass name");
  struct ArgList alst;

  alst.push(parser.cc_name);
  alst_xpush(alst, parser.debug);
  alst.push(pass);
  alst.push("-S");
  alst.push(input);
  alst.push("-o");
  alst.push(output);
  alst.push("-emit-llvm");
  alst.push(nullptr);

  Exec exe;
  exe.argv = alst.buf;
  exe.envp = elst.buf;
  return exe.run();
}

int ArgGenerator::execute() const {
  struct TempPool {
    TempPool() = default;
    ~TempPool() {
      for (CharStream &tempfile : pool) {
        (void)Exec::rm(false, true, tempfile.buffer(), envp);
      }
    }

    const char *next(const char *tmpl) {
      pool.push_back(CharStream(64));
      CharStream &tmp = pool.back();
      (void)Exec::mktemp(false, tmp, tmpl, this->envp);
      return tmp.buffer();
    }

    const char *operator[](size_t i) const {
      return pool[i].buffer();
    }

    std::vector<CharStream> pool;
    const char **envp{nullptr};
  } tempfiles;
  tempfiles.envp = this->elst.buf;

  std::stack<std::pair<const char *, const char *>> sources;
  std::stack<std::pair<const char *, const char *>> preprocessed;
  std::stack<std::pair<const char *, const char *>> assembly;
  std::stack<std::pair<const char *, const char *>> ll_assembly;
  std::stack<std::pair<const char *, const char *>> object;
  std::stack<std::pair<const char *, const char *>> ll_object;
  /* maybe linker-script, static/shared libraries. */
  std::vector<const char *> ld_script;

  for (const char *input : parser.input_files) {
    const char *suffix = ArgParse::suffix_of(input);
    const std::pair<const char *,const char *> obj = {input, input};
    if (strcmp(suffix, ".c") == 0 || strcmp(suffix, ".cpp") == 0
    || strcmp(suffix, ".cc") == 0 || strcmp(suffix, ".cxx") == 0) {
      sources.push(obj);
    } else if (strcmp(suffix, ".i") == 0 || strcmp(suffix, ".ipp") == 0) {
      preprocessed.push(obj);
    } else if (strcmp(suffix, ".s") == 0 || strcmp(suffix, ".S") == 0
    || strcmp(suffix, ".asm") == 0) {
      assembly.push(obj);
    } else if (strcmp(suffix, ".ll") == 0) {
      ll_assembly.push(obj);
    } else if (strcmp(suffix, ".bc") == 0) {
      ll_object.push(obj);
    } else if (strcmp(suffix, ".o") == 0) {
      object.push(obj);
    } else {
      // assume to be a linker script.
    #ifdef CFG_PRINT_DEBUG_OUTPUT
      fprintf(stderr, "%s is a linker script\n", input);
    #endif
      ld_script.push_back(input);
    }
  }

  CharStream tbuf(64);

  /** source -> preprocessed */
  bool ctmp = parser.stage != ArgParse::Stage::PREPROCESS;
  while (!sources.empty()) {
    const auto obj = sources.top();
    const char *input = obj.first, *output;
    bool iscpp = strcmp(ArgParse::suffix_of(input), ".c") == 0;
    sources.pop();

    if (parser.input_files.size() == 1 && parser.stage == ArgParse::Stage::PREPROCESS
     && parser.output_file != nullptr) {
      output = parser.output_file;
    } else if (ctmp) {
      output = tempfiles.next(iscpp ? CFG_MKTEMP_TEMPLATE ".i"
                                    : CFG_MKTEMP_TEMPLATE ".cpp");
    } else {
      tbuf.clear();
      tbuf.replace_suffix(obj.second, iscpp ? ".i" : ".cpp");
      output = tbuf.buffer();
    }

    if (this->preprocessor(input, output) != 0) {
      fprintf(stderr, ERROR_PREFIX " failed to preprocess %s\n", input);
      fflush(stderr);
      return 1;
    }
    preprocessed.push({output, obj.second});
  }
  if (parser.stage == ArgParse::Stage::PREPROCESS) {
    // can exit.
    return 0;
  }

  /** preprocessed -> ll_assembly, use clang compiler */
  ctmp = !(parser.stage == ArgParse::Stage::ASSEMBLY && parser.output_llvm);
  while (!preprocessed.empty()) {
    const auto obj = preprocessed.top();
    const char *input = obj.first;
    preprocessed.pop();

    const char *output;
    const char *ofile;
    if (parser.input_files.size() == 1 
     && parser.stage == ArgParse::Stage::ASSEMBLY
     && parser.output_file != nullptr 
     && parser.output_llvm) {
      /** use specified output. */
      output = parser.output_file;
    } else if (ctmp) {
      /** use temp file */
      output = tempfiles.next(CFG_MKTEMP_TEMPLATE ".ll");
    } else {
      /** use default output */
      tbuf.clear();
      tbuf.replace_suffix(obj.second, ".ll");
      output = tbuf.buffer();
    }


    /** run pass plugin on the ll assembly. */
    const auto &passes = this->extra_pass_names;
    const size_t npass = passes.size();
    TempPool tpool;
    tpool.envp = this->elst.buf;
    ofile = (npass == 0) ? output : tpool.next(
      CFG_MKTEMP_TEMPLATE ".ll");
    if (this->compiler(input, ofile, true) != 0) {
      fprintf(stderr, ERROR_PREFIX "failed to run compiler\n");
      return 1;
    }
    if (npass != 0) {
      for (size_t i = 0; i + 1 < npass; i++) {
        input = ofile;
        ofile = tpool.next(CFG_MKTEMP_TEMPLATE ".ll");
  
        if (this->transform(passes[i], input, ofile) != 0) {
          fprintf(stderr, ERROR_PREFIX "failed to run pass %s\n", passes[i]);
          return 1;
        }
      }

      input = ofile;
      ofile = output;
      if (this->transform(passes.back(), input, ofile) != 0) {
        fprintf(stderr, ERROR_PREFIX "failed to run pass %s\n", passes.back());
        return 1;
      }
    }
    
    /** tpool now deletes all temp file. */
    ll_assembly.push({output, obj.second});
  }
  if (parser.stage == ArgParse::Stage::ASSEMBLY && parser.output_llvm) {
    /** stop here. */
    return 0;
  }

  /** ll_assembly -> assembly */
  ctmp = !(parser.stage == ArgParse::Stage::ASSEMBLY);
  while (!ll_assembly.empty()) {
    const auto obj = ll_assembly.top();
    const char *input = obj.first;
    ll_assembly.pop();

    const char *output;
    if (parser.input_files.size() == 1 
     && parser.stage == ArgParse::Stage::ASSEMBLY
     && parser.output_file != nullptr) {
      /** use specified output. */
      output = parser.output_file;
    } else if (ctmp) {
      /** use temp file */
      output = tempfiles.next(CFG_MKTEMP_TEMPLATE ".s");
    } else {
      /** use default output */
      tbuf.clear();
      tbuf.replace_suffix(obj.second, ".s");
      output = tbuf.buffer();
    }

    if (compiler(input, output, false) != 0) {
      fprintf(stderr, ERROR_PREFIX "llvm assembler failed.\n");
      return 1;
    }

    assembly.push({output, obj.second});
  }
  if (parser.stage == ArgParse::Stage::ASSEMBLY) {
    return 0;
  }

  /** assembly -> object */
  ctmp = !(parser.stage == ArgParse::Stage::OBJECT);
  while (!assembly.empty()) {
    const auto obj = assembly.top();
    const char *input = obj.first;
    assembly.pop();

    const char *output;
    if (parser.input_files.size() == 1 
     && parser.stage == ArgParse::Stage::OBJECT
     && parser.output_file != nullptr) {
      /** use specified output. */
      output = parser.output_file;
    } else if (ctmp) {
      /** use temp file */
      output = tempfiles.next(CFG_MKTEMP_TEMPLATE ".o");
    } else {
      /** use default output */
      tbuf.clear();
      tbuf.replace_suffix(obj.second, ".o");
      output = tbuf.buffer();
    }

    if (this->assembler(input, output, false) != 0) {
      fprintf(stderr, ERROR_PREFIX "failed to run assembler\n");
      return 1;
    } 

    object.push({output, obj.first});
  }
  if (parser.stage == ArgParse::Stage::OBJECT) {
    return 0;
  }

  /** linking */
  ctmp = false;
  if (true) {
    const char *output;
    if (parser.output_file != nullptr) {
      output = parser.output_file;
    } else {
      output = "a.out";
    }

    while (!object.empty()) {
      const char *input = object.top().first;
      object.pop();
      ld_script.push_back(input);
    }

    if (this->linker(ld_script, output) != 0) {
      fprintf(stderr, ERROR_PREFIX "linker command failed\n");
      return 1;
    }
  }

  return 0;
}
