#ifndef TRANSLATE_SOURCE_H
#define TRANSLATE_SOURCE_H

#include <stddef.h>
#include <platform.h>

PONY_EXTERN_C_BEGIN
	
/** Given a file name, is this source code the compiler can handle?
 */
bool translate_valid_source_file(const char* file_name);

/** Given a file name and source code, generate pony code off of that representation (ie schema definition to pony classes)
 */
char* translate_source(const char* file_name, const char* source_code, bool print_generated_code);

/** Convert file name into a pony compatibile class name
 */
char* translate_class_name(const char* file_name);

PONY_EXTERN_C_END

#endif
