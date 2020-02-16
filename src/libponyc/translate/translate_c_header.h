#ifndef TRANSLATE_C_HEADER_H
#define TRANSLATE_C_HEADER_H

#include <stddef.h>
#include <platform.h>

PONY_EXTERN_C_BEGIN

char* translate_c_header(bool print_generated_code, const char* file_name, const char* source_code);

PONY_EXTERN_C_END

#endif
