#include "argparse.h"
#include "exec.h"

extern "C" int main(int argc, char **argv, char **envp) {
  ArgParse parser(argv, envp);
  if (strcmp(CFG_SRC_DIR "/cxx.cpp", __FILE__) == 0)
    parser.link_lang = ArgParse::Lang::CXX;

  if (parser.err_message != nullptr) {
    fprintf(stderr, ERROR_PREFIX "%s\n", parser.err_message);
    return 1;
  }

  ArgGenerator exe(parser);
  exe.add_pass_plugin("-fpass-plugin=" CFG_EDGE_PASS)
     .add_pass_plugin("-fpass-plugin=" FUNC_CALL_PASS)
     .add_pass_plugin("-fpass-plugin=" FUNC_ENTRY_PASS)
     .add_compile_arg(SANCOV_DEFAULT_DEF)
     .add_link_arg(SANCOV_DEFAULT_DEF);
    
  return exe.execute();
}
