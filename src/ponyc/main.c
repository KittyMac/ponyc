#include "../libponyc/ponyc.h"
#include "../libponyc/ast/parserapi.h"
#include "../libponyc/ast/bnfprint.h"
#include "../libponyc/pkg/package.h"
#include "../libponyc/pkg/buildflagset.h"
#include "../libponyc/pass/pass.h"
#include "../libponyc/options/options.h"
#include "../libponyc/ast/stringtab.h"
#include "../libponyc/ast/treecheck.h"
#include <platform.h>
#include "../libponyrt/mem/pool.h"
#include "../libponyc/codegen/genobj.h"
#include "../libponyc/codegen/genexe.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef PLATFORM_IS_POSIX_BASED
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

static size_t get_width()
{
  size_t width = 80;
#ifdef PLATFORM_IS_WINDOWS
  if(_isatty(_fileno(stdout)))
  {
    CONSOLE_SCREEN_BUFFER_INFO info;

    if(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    {
      int cols = info.srWindow.Right - info.srWindow.Left + 1;

      if(cols > width)
        width = cols;
    }
  }
#else
  if(isatty(STDOUT_FILENO))
  {
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
    {
      if(ws.ws_col > width)
        width = ws.ws_col;
    }
  }
#endif
  return width;
}

static bool should_abort_compile_due_to_modification_dates(const char * file_path, time_t most_recent_modified_date) {
  struct stat attr;
  fprintf(stderr, "Comparing %s\n", file_path);
  if(stat(file_path, &attr) == 0){
    if (attr.st_mtime > most_recent_modified_date) {
      return true;
    }
  }
  return false;
}

static bool compile_package(const char* path, pass_opt_t* opt,
  bool print_program_ast, bool print_package_ast)
{
  time_t most_recent_modified_date = 0;
    
  ast_t* program = program_load(path, opt, &most_recent_modified_date);

  if(program == NULL)
    return false;  
  
  if(print_program_ast)
    ast_fprint(stderr, program, opt->ast_print_width);

  if(print_package_ast)
    ast_fprint(stderr, ast_child(program), opt->ast_print_width);
  
  //////// Check if we even need to compile (compare target mod date to source mod date) //////////
  ast_t* package = ast_child(program);
  const char * filename = package_filename(package);
  if((opt->bin_name != NULL) && (strlen(opt->bin_name) > 0))
    filename = opt->bin_name;
  
  if(should_abort_compile_due_to_modification_dates(target_exe(filename, opt), most_recent_modified_date)) {
    fprintf(stderr, "No compilation required, all source files are older than existing target\n");
    return true;
  }
  if(should_abort_compile_due_to_modification_dates(target_obj(filename, opt), most_recent_modified_date)) {
    fprintf(stderr, "No compilation required, all source files are older than existing target\n");
    return true;
  }
  ////////////////////////////////////////////////////////////////////////////////////////////////

  bool ok = generate_passes(program, opt);
  ast_free(program);
  return ok;
}

int main(int argc, char* argv[])
{
  stringtab_init();

  pass_opt_t opt;
  pass_opt_init(&opt);

  opt.release = true;
  opt.output = ".";
  opt.ast_print_width = get_width();
  opt.argv0 = argv[0];

  ponyc_opt_process_t exit_code;
  bool print_program_ast;
  bool print_package_ast;

  opt_state_t s;
  ponyint_opt_init(ponyc_opt_std_args(), &s, &argc, argv);

  exit_code = ponyc_opt_process(&s, &opt, &print_program_ast,
                  &print_package_ast);

  if(exit_code == EXIT_255)
  {
    errors_print(opt.check.errors);
    pass_opt_done(&opt);
    return -1;
  } else if(exit_code == EXIT_0) {
    pass_opt_done(&opt);
    return 0;
  }

  bool ok = true;
  if(ponyc_init(&opt))
  {
    if(argc == 1)
    {
      ok &= compile_package(".", &opt, print_program_ast, print_package_ast);
    } else {
      for(int i = 1; i < argc; i++)
        ok &= compile_package(argv[i], &opt, print_program_ast,
          print_package_ast);
    }
  }

  if(!ok && errors_get_count(opt.check.errors) == 0)
    printf("Error: internal failure not reported\n");

  ponyc_shutdown(&opt);
  pass_opt_done(&opt);

  return ok ? 0 : -1;
}
