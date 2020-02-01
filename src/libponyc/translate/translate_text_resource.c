#include "../../libponyrt/gc/serialise.h"
#include "../../libponyrt/mem/pool.h"
#include "translate_source.h"
#include "sds/sds.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>


static sds packageCode = NULL;
static int numberOfResourcesInPackage = 0;

void translate_text_resource_package_begin(const char * qualified_name)
{
	packageCode = sdsnew("");
	numberOfResourcesInPackage = 0;
	
	char * className = translate_class_name(qualified_name);
	
	packageCode = sdscatprintf(packageCode, "primitive %sTextResources\n", className);
	packageCode = sdscatprintf(packageCode, "  fun get(string:String):String? =>\n");
	packageCode = sdscatprintf(packageCode, "    match string\n");
}

sds translate_text_resource_package_end(sds code)
{
	if(numberOfResourcesInPackage > 0) {
		packageCode = sdscatprintf(packageCode, "    else error end\n");
		code = sdscatprintf(code, "%s", packageCode);
	}
	return code;
}

char* translate_text_resource(bool print_generated_code, const char* file_name, const char * file_type, const char* source_code)
{
	// it is our responsibility to free the old "source code" which was provided
	unsigned long in_source_code_length = strlen(source_code)+1;
		
	// use the sds library to concat our pony code together, then copy it to a pony allocated buffer
	sds code = sdsnew("");
	
	char * className = translate_class_name(file_name);
	
	// resources take the form of:
	// primitive ClassName
	// 		fun apply():String =>
	//"""
	//file contents goes here
	//"""
	//
	// And can then be accessed by ClassName() at any time
	
	packageCode = sdscatprintf(packageCode, "    | \"%s\" => %s%s()\n", className, className, file_type);
	numberOfResourcesInPackage++;
	
	code = sdscatprintf(code, "primitive %s%s\n", className, file_type);
	code = sdscatprintf(code, "  fun apply():String =>\n");
	code = sdscatprintf(code, "\"\"\"\n");
	code = sdscatprintf(code, "%s", source_code);
	code = sdscatprintf(code, "\"\"\"\n");
	
	// free our incoming source code
	ponyint_pool_free_size(in_source_code_length, (void *)source_code);
	
	// copy our sds string to pony memory
	size_t code_len = sdslen(code);
	char * pony_code = (char*)ponyint_pool_alloc_size(code_len + 1);
	strncpy(pony_code, code, code_len);
	pony_code[code_len] = 0;
	sdsfree(code);
	
	if (print_generated_code) {
		fprintf(stderr, "========================== autogenerated pony code ==========================\n");
		fprintf(stderr, "// Transpiled from %s\n\n", file_name);
		fprintf(stderr, "%s", pony_code);
		fprintf(stderr, "=============================================================================\n");
	}
	
	return pony_code;
}

