#ifndef TRANSLATE_TEXT_RESOURCE_H
#define TRANSLATE_TEXT_RESOURCE_H

#include <stddef.h>
#include <platform.h>

PONY_EXTERN_C_BEGIN

/** Given a text file, return Pony code which contains the contents of it
 */
char* translate_text_resource(bool print_generated_code, const char* file_name, const char * file_type, const char* source_code);

PONY_EXTERN_C_END

#endif
