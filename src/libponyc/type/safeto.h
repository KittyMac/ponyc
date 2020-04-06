#ifndef TYPE_SAFETO_H
#define TYPE_SAFETO_H

#include <platform.h>
#include "../ast/ast.h"
#include "../pass/pass.h"

PONY_EXTERN_C_BEGIN

bool safe_to_write(pass_opt_t* opt, ast_t* ast, ast_t* type, ast_t* right);

bool safe_to_autorecover(ast_t* receiver, ast_t* type);

PONY_EXTERN_C_END

#endif
