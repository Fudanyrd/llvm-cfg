#include "argparse.h"
#include "exec.h"

#define DEBUG_PREFIX "\033[01;36m[#]\033[0;m "

ArgParse::ArgParse(char **argv, char **envp) {
  if (true) {
    FileDescriptor fobj;
    Exec exe;
    const char *which_clang[] = {"/usr/bin/which", "clang"};
    exe.argv = which_clang;
    exe.envp = (cbuf_t)envp;
    
    if (exe.run() == 0) {
      aux_buf.append_line(fobj.fd, &cc_name);
    }
  }
  if (true) {
    FileDescriptor fobj;
    Exec exe;
    const char *which_clang[] = {"/usr/bin/which", "clang++"};
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
  }

  this->parse_env();
  this->parse_arg();
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
