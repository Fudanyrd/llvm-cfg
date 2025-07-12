#include "argparse.h"
#include "exec.h"

extern "C" int main(int argc, char **argv, char **envp) {
  ArgParse parser(argv, envp);
  if (strcmp(CFG_SRC_DIR "/cxx.cpp", __FILE__) == 0)
    parser.link_lang = ArgParse::Lang::CXX;

  ArgGenerator exe(parser);
  exe.add_pass_plugin("-fpass-plugin=" CFG_EDGE_PASS)
     .add_pass_plugin("-fpass-plugin=" FUNC_CALL_PASS)
     .add_pass_plugin("-fpass-plugin=" FUNC_ENTRY_PASS)
     .add_compile_arg(SANCOV_DEFAULT_DEF);
    
  return exe.execute();
}
