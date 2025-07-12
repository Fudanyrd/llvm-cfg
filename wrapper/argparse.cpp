#include "argparse.h"
#include "exec.h"

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
    FileDescriptor fobj;
    Exec exe;
    const char *which_clang[] = {"/usr/bin/which", "clang", nullptr};
    exe.argv = which_clang;
    exe.envp = (cbuf_t)envp;
    
    if (exe.run() == 0) {
      aux_buf.append_line(fobj.fd, &cc_name);
    }
  }
  if (true) {
    FileDescriptor fobj;
    Exec exe;
    const char *which_clang[] = {"/usr/bin/which", "clang++", nullptr};
    exe.argv = which_clang;
    exe.envp = (cbuf_t)envp;
    if (exe.run() == 0) {
      aux_buf.append_line(fobj.fd, &cxx_name);
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
    fprintf(stderr, DEBUG_PREFIX "sizeof aux buffer %ld\n", aux_buf.getCapacity());
  }

  this->parse_env();
  this->parse_arg();

  if (!this->cc_name || !this->cxx_name) {
    this->err_message = (ERROR_PREFIX "cannot run which clang/clang++\n"
    "Hint: specify by CFG_CC and CFG_CXX environment variables.\n");
  }
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

int ArgGenerator::execute() const {
  struct ArgList alst;
  struct ArgList elst;

  const StringBuf &envs = parser.Envp();
  const char *eiter = envs.buffer();
  const char *eend = envs.buffer_end();
  while (eiter < eend) {
    elst.push(eiter);
    eiter = envs.next(eiter);
  }

  if (!extra_pass_names.empty() && parser.runpass()) {
    int ret = 0;
    CharStream output(64);
    CharStream temp(64);
    Exec mktemp;
    if (mktemp.mktemp(false, temp, CFG_MKTEMP_TEMPLATE ".ll", elst.buf) != 0) {
      /** failure. */
      return 1;
    }
    for (const char *input : parser.input_files) {
      output.replace_suffix(input, parser.output_suffix());
      const char *opath = (parser.input_files.size() <= 1 && parser.output_file) 
        ? parser.output_file : output.buffer(); 
      if (force_emit_ll(input, temp.buffer()) != 0) {
        ret = 1;
        break;
      }
      if (compile_ll(temp.buffer(), opath)) {
        ret = 1;
        break;
      }
      output.clear();
    }

    Exec::rm(false, true, temp.buffer(), elst.buf);
    return ret;
  }

  alst.push(parser.link_lang == ArgParse::Lang::C ? parser.cc_name
                                                  : parser.cxx_name);
  const std::vector<const char *> &extras = 
    parser.stage == ArgParse::Stage::LINK ? extra_link_args
                                          : extra_compile_args;
  for (const char *arg : extras) {
    alst.push(arg);
  }
  alst_xpush(alst, parser.debug);
  alst_xpush(alst, parser.opt_level);
  if (parser.read_stdin && parser.stage != ArgParse::Stage::PREPROCESS) {
    alst.push("-x");
    alst.push(parser.lang);
  }

  for (const char *arg : parser.input_files) {
    alst.push(arg);
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

  switch (parser.stage) {
    case (ArgParse::Stage::PREPROCESS): {
      alst.push("-E");
      break;
    }
    case (ArgParse::Stage::ASSEMBLY): {
      alst.push("-S");
      break;
    }
    case (ArgParse::Stage::OBJECT): {
      alst.push("-c");
      break;
    }
    default: {
      break;
    }
  }
  if (parser.output_file) {
    alst.push("-o");
    alst.push(parser.output_file);
  }


  alst.push(nullptr);
  elst.push(nullptr);

  Exec exe;
  exe.argv = alst.buf;
  exe.envp = elst.buf;
  return exe.run();
}

int ArgGenerator::force_emit_ll(const char *input, const char *output) const {
  struct ArgList alst;
  struct ArgList elst;

  const StringBuf &envs = parser.Envp();
  const char *eiter = envs.buffer();
  const char *eend = envs.buffer_end();
  while (eiter < eend) {
    elst.push(eiter);
    eiter = envs.next(eiter);
  }

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
  elst.push(nullptr);

  Exec exe;
  exe.argv = alst.buf;
  exe.envp = elst.buf;
  return exe.run();
}

int ArgGenerator::compile_ll(const char *input, const char *output) const {
  struct ArgList alst;
  struct ArgList elst;

  const StringBuf &envs = parser.Envp();
  const char *eiter = envs.buffer();
  const char *eend = envs.buffer_end();
  while (eiter < eend) {
    elst.push(eiter);
    eiter = envs.next(eiter);
  }

  alst.push(parser.cc_name);
  for (const char *pass : extra_pass_names) {
    alst.push(pass);
  }

  alst.push(parser.stage == ArgParse::Stage::ASSEMBLY ? "-S" : "-c");
  alst.push(input);
  alst.push("-o");
  alst.push(output);
  alst.push(nullptr);
  elst.push(nullptr);

  Exec exe;
  exe.argv = alst.buf;
  exe.envp = elst.buf;
  return exe.run();
}
