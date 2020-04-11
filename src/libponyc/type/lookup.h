#ifndef TYPE_LOOKUP_H
#define TYPE_LOOKUP_H

#include <platform.h>
#include "reify.h"
#include "../ast/ast.h"
#include "../pass/pass.h"

PONY_EXTERN_C_BEGIN

ast_t* get_uniontype_from_node(ast_t* type, ast_t* from);
int get_uniontypeidx_from_node(ast_t* type, ast_t* from);
ast_t* resolve_narrowed_union_type(ast_t* type, ast_t* from);

deferred_reification_t* lookup(pass_opt_t* opt, ast_t* from, ast_t* type,
  const char* name);

deferred_reification_t* lookup_try(pass_opt_t* opt, ast_t* from, ast_t* type,
  const char* name, bool allow_private);

PONY_EXTERN_C_END

#endif
