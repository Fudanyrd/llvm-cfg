#include "wrapper.h"

extern "C" int main(int argc, char **argv, char **envp) {
  CompilerWrapper wrapper;
  #ifdef LINK_USING_CXX
    wrapper.SetLang(CompilerWrapper::Lang::CXX);
  #endif // LINK_USING_CXX

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
