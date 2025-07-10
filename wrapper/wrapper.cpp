
#include "wrapper.h"

#include <cstdio>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

static const char *pass_plugins[] = {
    "-fpass-plugin=" FUNC_ENTRY_PASS,
    "-fpass-plugin=" FUNC_CALL_PASS,
    "-fpass-plugin=" CFG_EDGE_PASS};

static char input_buf[2048];

bool CompilerWrapper::ParseArgs(int argc, char **argv, char **envp)
{
  // Implementation of argument parsing
  // This function will populate the member variables based on the command line arguments

  if (print_debug_output)
  {
    fprintf(stderr, "\033[01;36m[#]\033[0;m Initial args = [");
    for (int i = 1; argv[i]; i++)
    {
      fprintf(stderr, "%s ", argv[i]);
    }
    fprintf(stderr, "]\n");
  }

  assert(argv && "argv should not be null");
  assert(envp && "envp should not be null");
  for (int i = 0; i < argc; ++i)
  {
    assert(argv[i] && "argv[i] should not be null");
    if (strncmp("-fsanitize-coverage", argv[i], 19) == 0)
    {
      // ignore this.
      continue;
    }
    buf.append(argv[i], i == 0 ? reinterpret_cast<void **>(&arg_start) : nullptr);
  }
  for (int i = 0; envp[i] != nullptr; ++i)
  {
    assert(envp[i] && "envp[i] should not be null");
    buf.append(envp[i], i == 0 ? reinterpret_cast<void **>(&env_start) : nullptr);
  }

  bool set_lang = false;
  bool set_output = false;
  bool mf = false;
  const char *aend = arg_end();
  for (const char *arg = buf.next(arg_start); arg < aend; arg = buf.next(arg))
  {
    if (set_lang)
    {
      this->lang = arg;
      buf.record((void **)(&lang), arg);
      set_lang = false;
      continue;
    }
    else if (set_output)
    {
      this->output_file = arg;
      buf.record((void **)(&output_file), arg);
      set_output = false;
      continue;
    }
    else if (mf)
    {
      mf = false;
      margs.push_back((char *)arg);
      continue;
    }

    if (arg[0] != '-')
    {
      // is an input file.
      this->input_files.push_back((char *)arg);
      continue;
    }

    if (strncmp(arg, "-g", 2) == 0)
    {
      this->debug = arg;
      buf.record((void **)(&debug), arg);
    }
    else if (strncmp(arg, "-O", 2) == 0)
    {
      this->opt_level = arg;
      buf.record((void **)(&opt_level), arg);
    }
    else if (strcmp(arg, "-c") == 0)
    {
      stage = Stage::OBJECT;
    }
    else if (strcmp(arg, "-S") == 0)
    {
      stage = Stage::ASSEMBLY;
    }
    else if (strcmp(arg, "-E") == 0)
    {
      stage = Stage::PREPROCESS;
    }
    else if (strcmp(arg, "-o") == 0)
    {
      set_output = true;
    }
    else if (strcmp(arg, "-x") == 0)
    {
      set_lang = true;
    }
    else if (strcmp(arg, "-") == 0)
    {
      input_files.push_back((char *)"-");
    }
    else if (strncmp(arg, "-I", 2) == 0)
    {
      include_dirs.push_back((char *)arg);
    }
    else if (strncmp(arg, "-D", 2) == 0)
    {
      defines.push_back((char *)arg);
    }
    else if (strcmp(arg, "-MF") == 0 || strcmp(arg, "-MT") == 0)
    {
      margs.push_back((char *)arg);
      mf = true;
    }
    else if (strncmp(arg, "-M", 2) == 0)
    {
      margs.push_back((char *)arg);
    }
    else if (strcmp(arg, "-emit-llvm") == 0)
    {
      output_llvm = true;
    }
    else
    {
      flags.push_back((char *)arg);
    }
  }

  if (input_files.empty())
  {
    err_message = "No input file specified.";
    return false;
  }

  const char *eend = buf.buffer_end();
  aux.append(SANCOV_DEFAULT_DEF, (void **)&sancov_arg);
  for (const char *env = env_start; env < eend; env = buf.next(env))
  {
    if (strncmp(env, "CFG_CC=", 7) == 0)
    {
      aux.append(env + 7, reinterpret_cast<void **>(&cc_name));
    }
    else if (strncmp(env, "CFG_CXX=", 8) == 0)
    {
      aux.append(env + 8, reinterpret_cast<void **>(&cxx_name));
    }
  }

  if (!cc_name)
  {
    FILE *fp = popen("which clang", "r");
    if (!fp)
    {
      err_message = "Failed to execute 'which clang'";
      return false;
    }
    aux.append_line(fp, reinterpret_cast<void **>(&cc_name));
    pclose(fp);
  }

  if (!cxx_name)
  {
    FILE *fp = popen("which clang++", "r");
    if (!fp)
    {
      err_message = "Failed to execute 'which clang++'";
      return false;
    }
    aux.append_line(fp, reinterpret_cast<void **>(&cxx_name));
    pclose(fp);
  }

  aux.append("-emit-llvm", reinterpret_cast<void **>(&emit_llvm));
  aux.append("-S", reinterpret_cast<void **>(&gen_asm));

  return true;
}

int CompilerWrapper::compile(char *input_file)
{
  // std::string default_output = std::string(input_file) + ".o";

  struct ArgList envs, argl;
  for (const char *env = env_start; env < env_end(); env = buf.next(env))
  {
    if (strncmp(env, "CFG_CC=", 7) == 0 || strncmp(env, "CFG_CXX=", 8) == 0)
    {
      continue; // Skip CFG_CC and CFG_CXX
    }
    envs.push(const_cast<char *>(env)); // Store the environment variable
  }
  envs.push(nullptr); // Null-terminate the environment variables

  struct StringBuf temp_files;
  for (int i = 0; i < 4; i++)
  {
    FILE *fp = popen("mktemp --suffix=.ll", "r");
    if (!fp)
    {
      err_message = "Failed to execute 'mktemp --suffix=.ll'";
      normtemp(temp_files); // Clean up temporary files
      return 1;             // Return error code
    }
    temp_files.append_line(fp, nullptr);
  }

  char *temp_file = const_cast<char *>(temp_files.buffer());
  // clang [input.c] -S -emit-llvm -fsanitize-coverage=[..] [other args]
  argl.push(const_cast<char *>(cc_name));   // Add the compiler name
  argl.push((char *)sancov_arg);            // Add the sanitizer coverage argument
  argl.push(const_cast<char *>(emit_llvm)); // Add the emit-llvm flag
  argl.push(const_cast<char *>(gen_asm));   // Add the -S flag
  argl.push("-o");
  argl.push(temp_file);  // Add the temporary file name
  argl.push(input_file); // Add the input file
  for (char *dir : include_dirs)
  {
    argl.push(dir); // Add include directories
  }
  for (char *defs : defines)
  {
    argl.push(defs); // Add defines
  }
  for (char *flag : flags)
  {
    argl.push(flag); // Add additional flags
  }
  for (char *marg : margs)
  {
    argl.push(marg);
  }
  argl.push(nullptr);
  if (cmd(argl.buf, envs.buf, strcmp(input_file, "-") == 0) != 0)
  {
    err_message = "Failed to execute .c -> .ll";
    normtemp(temp_files); // Clean up temporary files
    return 1;             // Return error code
  }
  argl.clear();

  // clang -Xclang -fpass-plugin=/path/to/plugin[0] -S -emit-llvm [input] -o [temp-file]
  for (int i = 0; i < 3; i++)
  {
    input_file = temp_file;
    temp_file = (char *)temp_files.next(temp_file);
    argl.push(const_cast<char *>(cc_name)); // Add the C++ compiler name
    argl.push("-Xclang");
    argl.push((char *)(pass_plugins[i]));
    argl.push(const_cast<char *>(emit_llvm)); // Add the emit-llvm flag
    argl.push(const_cast<char *>(gen_asm));   // Add the -S flag
    argl.push("-o");
    argl.push(temp_file);
    argl.push(input_file); // Add the input file
    argl.push(nullptr);
    if (cmd(argl.buf, envs.buf) != 0)
    {
      err_message = "Failed to execute .ll -> .ll, pass plugin FIXME";
      normtemp(temp_files); // Clean up temporary files
      return 1;             // Return error code
    }
    argl.clear();
  }

  if (stage == Stage::ASSEMBLY || output_llvm)
  {
    argl.push((char *)"/usr/bin/cp");
    argl.push(temp_file);
    if (output_file)
    {
      argl.push((char *)output_file);
    }
    else
    {
      argl.push((char *)".");
    }
    argl.push(nullptr);

    if (cmd(argl.buf, envs.buf))
    {
      err_message = "cannot copy temp file.";
      normtemp(temp_files);
      return 1;
    }

    rmtemp(temp_files);
    return 0;
  }

  // .ll -> .o
  // clang -c .ll -o [output.o]
  argl.push(const_cast<char *>(cc_name)); // Add the C compiler name
  if (stage == Stage::OBJECT)
  {
    argl.push("-c");
  }
  else
  {
    // stage == Stage::ASSEMBLY
    argl.push("-S");
  }
  argl.push(temp_file); // Add the last temporary file
  if (output_file != nullptr)
  {
    argl.push("-o");
    argl.push((char *)output_file);
  }
  argl.push(nullptr);
  if (cmd(argl.buf, envs.buf) != 0)
  {
    err_message = "Failed to execute .ll -> .o or .s";
    normtemp(temp_files); // Clean up temporary files
    return 1;
  }
  argl.clear();

  rmtemp(temp_files); // Clean up temporary files
  return 0;
}

int CompilerWrapper::cmd(char **argv, char **envp, bool read_stdin)
{
  int pipfd[2];
  if (read_stdin) {
    if (!pipe(pipfd)) {
      perror("pipe");
      return 1;
    }
  }

  pid_t pid = fork();
  if (pid < 0)
  {
    perror("fork");
    exit(1);
  }

  if (print_debug_output && pid)
  {
    for (int i = 0; argv[i] != nullptr; ++i)
    {
      fprintf(stderr, "%s ", argv[i]);
    }
    fprintf(stderr, "\n");
  }

  if (pid == 0)
  {
    // Child process
    if (read_stdin) {
      close(pipfd[1]);
      dup2(pipfd[0], STDIN_FILENO);
    }

    int fd = open("/dev/null", O_WRONLY);
    if (fd < 0)
    {
      perror("open /dev/null");
      exit(1);
    }
    dup2(fd, STDOUT_FILENO); // Redirect stdout to the same as the parent
    if (execve(argv[0], argv, envp) < 0)
    {
      perror("execve");
      close(fd);
      exit(1);
    }
  }
  else
  {
    // Parent process
    if (read_stdin) {
      close(pipfd[0]);

      /** read from stdin and send the data to the pipe. */
      ssize_t nb;
      while ((nb = read(1, input_buf, sizeof(input_buf))) > 0) {
        write(pipfd[1], input_buf, nb);
      }
      close(pipfd[1]);
    }

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
    {
      return WEXITSTATUS(status); // Return the exit status of the child process
    }
    else
    {
      return -1; // Indicate an error occurred
    }
  }
}

int CompilerWrapper::Execute(void)
{
  if (this->stage == Stage::OBJECT || this->stage == Stage::ASSEMBLY)
  {
    for (char *input_file : input_files)
    {
      if (compile(input_file) != 0)
      {
        return 1; // Return error code if compilation fails
      }
    }

    return 0;
  }

  // preprocessing or linking does not require sancov flags.

  struct ArgList envs, argl;
  for (const char *env = env_start; env < env_end(); env = buf.next(env))
  {
    if (strncmp(env, "CFG_CC=", 7) == 0 || strncmp(env, "CFG_CXX=", 8) == 0)
    {
      continue; // Skip CFG_CC and CFG_CXX
    }
    envs.push(const_cast<char *>(env)); // Store the environment variable
  }
  envs.push(nullptr); // Null-terminate the environment variables

  if (link_lang == Lang::C)
  {
    argl.push(const_cast<char *>(cc_name)); // Add the C compiler name
  }
  else
  {
    argl.push(const_cast<char *>(cxx_name)); // Add the C++ compiler name
  }
  argl.push(SANCOV_DEFAULT_DEF);

  const char *arg = arg_start;
  arg = buf.next(arg);
  while (arg < arg_end())
  {
    argl.push(const_cast<char *>(arg)); // Add the argument to the list
    arg = buf.next(arg);
  }
  argl.push(nullptr);

  bool read_stdin = false;
  for (char *input_file : input_files) {
    if (strcmp(input_file, "-") == 0) {
      read_stdin = true;
      break;
    }
  }

  return cmd(argl.buf, envs.buf);
}
