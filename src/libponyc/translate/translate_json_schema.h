#ifndef TRANSLATE_JSON_H
#define TRANSLATE_JSON_H

#include <stddef.h>
#include <platform.h>

PONY_EXTERN_C_BEGIN

/** Given a JSON schema string, return Pony code which represents it
 */
char* translate_json(const char* file_name, const char* source_code);

PONY_EXTERN_C_END

#endif
