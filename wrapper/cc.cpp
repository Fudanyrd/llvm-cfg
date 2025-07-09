#include "wrapper.h"

extern "C" int main(int argc, char **argv, char **envp) {
  CompilerWrapper wrapper;
  if (strcmp(CFG_SRC_DIR "/cxx.cpp", __FILE__) == 0)
    wrapper.SetLang(CompilerWrapper::Lang::CXX);

  if (!wrapper.ParseArgs(argc, argv, envp)) {
    fprintf(stderr, "Error: %s\n", wrapper.GetErr());
    return 1;
  }

  int result = wrapper.Execute();
  if (result != 0) {
    fprintf(stderr, "Execution failed with error code: %d\n", result);
  }
  return result;
}
