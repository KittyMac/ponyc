#ifndef TRANSLATE_C_HEADER_H
#define TRANSLATE_C_HEADER_H

#include <stddef.h>
#include <platform.h>

PONY_EXTERN_C_BEGIN

void translate_c_header_package_begin(const char * qualified_name);
sds translate_c_header_package_end(sds code);
char* translate_c_header(bool print_generated_code, const char* file_name, const char* source_code);

PONY_EXTERN_C_END

#endif
