#ifndef TRANSLATE_SOURCE_H
#define TRANSLATE_SOURCE_H

#include <stddef.h>
#include <platform.h>

typedef struct program_t program_t;

PONY_EXTERN_C_BEGIN
	
/** Given a file name, is this source code the compiler can handle?
 */
bool translate_valid_source_file(const char* file_name);

/** Given a file name and source code, generate pony code off of that representation (ie schema definition to pony classes)
 */
char* translate_source(program_t* program, const char* file_name, const char* source_code, bool print_generated_code);

/** Convert name into a pony compatibile class name
 */
const char* translate_class_name(const char* name, bool makePrivate);

/** Convert name into a pony compatibile function name
 */
const char* translate_function_name(const char* name);

/** Clean up after the two name translation functions above
 */
void translate_free_name(const char* name);

/** Know that a new source package is being compile
 */
void translate_source_package_begin(const char * qualified_name);

/** Know that a new source package is ending, add any other code you want to add
 */
char* translate_source_package_end(bool print_generated_code);

PONY_EXTERN_C_END

#endif
