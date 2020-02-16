#include "genlib.h"
#include "genopt.h"
#include "genobj.h"
#include "genheader.h"
#include "genprim.h"
#include "../plugin/plugin.h"
#include "../reach/paint.h"
#include "../type/assemble.h"
#include "../../libponyrt/mem/pool.h"
#include "../type/lookup.h"
#include <string.h>

#ifdef PLATFORM_IS_POSIX_BASED
#  include <unistd.h>
#endif

static bool link_lib(compile_t* c, const char* file_o)
{
  errors_t* errors = c->opt->check.errors;

#if defined(PLATFORM_IS_POSIX_BASED)
  const char* file_lib = suffix_filename(c, c->opt->output, "lib", c->filename,
    ".a");
  if(c->opt->verbosity >= VERBOSITY_MINIMAL)
    fprintf(stderr, "Archiving %s\n", file_lib);

  size_t len = 32 + strlen(file_lib) + strlen(file_o);
  char* cmd = (char*)ponyint_pool_alloc_size(len);

#if defined(PLATFORM_IS_MACOSX)
  snprintf(cmd, len, "/usr/bin/ar -rcs %s %s", file_lib, file_o);
#else
  snprintf(cmd, len, "ar -rcs %s %s", file_lib, file_o);
#endif

  if(c->opt->verbosity >= VERBOSITY_TOOL_INFO)
    fprintf(stderr, "%s\n", cmd);
  if(system(cmd) != 0)
  {
    errorf(errors, NULL, "unable to link: %s", cmd);
    ponyint_pool_free_size(len, cmd);
    return false;
  }

  ponyint_pool_free_size(len, cmd);
#elif defined(PLATFORM_IS_WINDOWS)
  const char* file_lib = suffix_filename(c, c->opt->output, "", c->filename,
    ".lib");
  if(c->opt->verbosity >= VERBOSITY_MINIMAL)
    fprintf(stderr, "Archiving %s\n", file_lib);

  vcvars_t vcvars;

  if(!vcvars_get(c, &vcvars, errors))
  {
    errorf(errors, NULL, "unable to link: no vcvars");
    return false;
  }

  size_t len = strlen(vcvars.ar) + strlen(file_lib) + strlen(file_o) + 64;
  char* cmd = (char*)ponyint_pool_alloc_size(len);

  snprintf(cmd, len, "cmd /C \"\"%s\" /NOLOGO /OUT:%s %s\"", vcvars.ar,
    file_lib, file_o);

  if(c->opt->verbosity >= VERBOSITY_TOOL_INFO)
    fprintf(stderr, "%s\n", cmd);
  if(system(cmd) == -1)
  {
    errorf(errors, NULL, "unable to link: %s", cmd);
    ponyint_pool_free_size(len, cmd);
    return false;
  }

  ponyint_pool_free_size(len, cmd);
#endif

  return true;
}

static bool reachable_methods(compile_t* c, ast_t* ast)
{
  ast_t* id = ast_child(ast);
  ast_t* type = type_builtin(c->opt, ast, ast_name(id));

  ast_t* def = (ast_t*)ast_data(type);
  ast_t* members = ast_childidx(def, 4);
  ast_t* member = ast_child(members);

  while(member != NULL)
  {
    switch(ast_id(member))
    {
      case TK_NEW:
      case TK_BE:
      case TK_FUN:
      {
        AST_GET_CHILDREN(member, cap, m_id, typeparams);

        // Mark all non-polymorphic methods as reachable.
        if(ast_id(typeparams) == TK_NONE)
          reach(c->reach, type, ast_name(m_id), NULL, c->opt);
        break;
      }

      default: {}
    }

    member = ast_sibling(member);
  }

  ast_free_unattached(type);
  return true;
}

bool reachable_actors(compile_t* c, ast_t* program)
{
  if(c->opt->verbosity >= VERBOSITY_INFO)
    fprintf(stderr, " Library reachability\n");

  // Look for C-API actors in every package.
  bool found = false;
  ast_t* package = ast_child(program);

  while(package != NULL)
  {
    ast_t* module = ast_child(package);

    while(module != NULL)
    {
      ast_t* entity = ast_child(module);

      while(entity != NULL)
      {
        if(ast_id(entity) == TK_ACTOR)
        {
          ast_t* c_api = ast_childidx(entity, 5);

          if(ast_id(c_api) == TK_AT)
          {
            // We have an actor marked as C-API.
            if(!reachable_methods(c, entity))
              return false;

            found = true;
          }
        }

        entity = ast_sibling(entity);
      }

      module = ast_sibling(module);
    }

    package = ast_sibling(package);
  }

  if(!found)
  {
    // Note: this is no longer an error case.  we provide "pony_main()" to just
    // run pony with the main actor like normal exes
    return true;
  }

  if(c->opt->verbosity >= VERBOSITY_INFO)
    fprintf(stderr, " Selector painting\n");
  paint(&c->reach->types);

  plugin_visit_reach(c->reach, c->opt, true);

  return true;
}

bool genlib(compile_t* c, ast_t* program)
{
  if(	!reachable_actors(c, program) ||
     	!genheader(c))
    return false;


  errors_t* errors = c->opt->check.errors;

  // The first package is the main package. It has to have a Main actor.
  const char* main_actor = c->str_Main;
  const char* env_class = c->str_Env;
  const char* package_name = c->filename;

  if((c->opt->bin_name != NULL) && (strlen(c->opt->bin_name) > 0))
    c->filename = c->opt->bin_name;

  ast_t* package = ast_child(program);
  ast_t* main_def = ast_get(package, main_actor, NULL);

  if(main_def == NULL)
  {
    errorf(errors, NULL, "no Main actor found in package '%s'", package_name);
    return false;
  }

  // Generate the Main actor and the Env class.
  ast_t* main_ast = type_builtin(c->opt, main_def, main_actor);
  ast_t* env_ast = type_builtin(c->opt, main_def, env_class);

  deferred_reification_t* main_create = lookup(c->opt, main_ast, main_ast,
    c->str_create);

  if(main_create == NULL)
  {
    ast_free(main_ast);
    ast_free(env_ast);
    return false;
  }

  deferred_reify_free(main_create);

  if(c->opt->verbosity >= VERBOSITY_INFO)
    fprintf(stderr, " Reachability\n");
  reach(c->reach, main_ast, c->str_create, NULL, c->opt);
  reach(c->reach, main_ast, stringtab("runtime_override_defaults"), NULL, c->opt);
  reach(c->reach, env_ast, c->str__create, NULL, c->opt);

  if(c->opt->limit == PASS_REACH)
  {
    ast_free(main_ast);
    ast_free(env_ast);
    return true;
  }

  if(c->opt->verbosity >= VERBOSITY_INFO)
    fprintf(stderr, " Selector painting\n");
  paint(&c->reach->types);

  plugin_visit_reach(c->reach, c->opt, true);

  if(c->opt->limit == PASS_PAINT)
  {
    ast_free(main_ast);
    ast_free(env_ast);
    return true;
  }

  if(!gentypes(c))
  {
    ast_free(main_ast);
    ast_free(env_ast);
    return false;
  }

  if(c->opt->verbosity >= VERBOSITY_ALL)
    reach_dump(c->reach);

  reach_type_t* t_main = reach_type(c->reach, main_ast);
  reach_type_t* t_env = reach_type(c->reach, env_ast);

  ast_free(main_ast);
  ast_free(env_ast);

  if((t_main == NULL) || (t_env == NULL))
    return false;

  gen_main(c, t_main, t_env, true);

  plugin_visit_compile(c, c->opt);

  if(!genopt(c, true))
    return false;

  if(c->opt->runtimebc)
  {
    if(!codegen_merge_runtime_bitcode(c))
      return false;

    // Rerun the optimiser without the Pony-specific optimisation passes.
    // Inlining runtime functions can screw up these passes so we can't
    // run the optimiser only once after merging.
    if(!genopt(c, false))
      return false;
  }

  const char* file_o = genobj(c);

  if(file_o == NULL)
    return false;

  if(c->opt->limit < PASS_ALL)
    return true;

  if(!link_lib(c, file_o))
    return false;

#ifdef PLATFORM_IS_WINDOWS
  _unlink(file_o);
#else
  unlink(file_o);
#endif

  return true;
}
