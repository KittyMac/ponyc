#include "translate_json.h"
#include "../../libponyrt/gc/serialise.h"
#include "../../libponyrt/mem/pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define PONY_EXTENSION ".pony"
#define JSON_SCHEMA_EXTENSION ".schema.json"

// converts a JSON Schema file to Pony classes. Used in source.c.

int string_ends_with(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

bool translate_valid_source_file(const char* file_name)
{
	if(	string_ends_with(file_name, PONY_EXTENSION) ||
		string_ends_with(file_name, JSON_SCHEMA_EXTENSION) )
	{
		return true;
	}
	return false;
}

char* translate_source(const char* file_name, const char* source_code)
{
	if(string_ends_with(file_name, JSON_SCHEMA_EXTENSION))
	{
		return translate_json(file_name, source_code);
	}
	return (char *)source_code;
}

// helper functions shared by future translation classes

char* translate_class_name(char* file_name)
{
	// take a file name and turn it into a pony class name
	char * start = strrchr(file_name, '/');
	if (start == NULL) {
		start = file_name;
	} else {
		start += 1;
	}
	char * end = strchr(start, '.');
	if (end == NULL) {
		end = file_name + strlen(file_name);
	}
	
	char* class_name = (char*)ponyint_pool_alloc_size((end - start) + 1);
	
	int idx = 0;
	for (; start < end; start++) {
		if (idx == 0) {
			class_name[idx++] = (char)toupper(*start);
			continue;
		}
		if (isspace(*start) == true) {
			class_name[idx++] = '_';
			continue;
		}
		class_name[idx++] = *start;
	}
	
	return class_name;
}
