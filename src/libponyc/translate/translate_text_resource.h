#ifndef TRANSLATE_TEXT_RESOURCE_H
#define TRANSLATE_TEXT_RESOURCE_H

#include <stddef.h>
#include <platform.h>
#include "sds/sds.h"

PONY_EXTERN_C_BEGIN

/** Given a text file, return Pony code which contains the contents of it
 */
char* translate_text_resource(bool print_generated_code, const char* file_name, const char * file_type, const char* source_code);


void translate_text_resource_package_begin(const char * qualified_name);
sds translate_text_resource_package_end(sds code);

PONY_EXTERN_C_END

#endif
