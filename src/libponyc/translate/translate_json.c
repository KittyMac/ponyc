#include "../../libponyrt/gc/serialise.h"
#include "../../libponyrt/mem/pool.h"
#include "translate_source.h"
#include "sds/sds.h"
#include "jsmn/jsmn.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// converts a JSON Schema file to Pony classes. Used in source.c.

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
	if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
		strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return 0;
	}
	return -1;
}

sds translate_json_abort(sds code, char * error) {
	return sdscatprintf(code, "JSON Schema parser failed: %s\n", error);
}

size_t translate_json_get_next_sibling(jsmntok_t *t, size_t idx, size_t count) {
	int parentIdx = t[idx].parent;
	idx += 1;
	while(idx < count) {
		if (t[idx].parent == parentIdx) {
			return idx;
		}
		idx += 1;
	}
	return idx + 1;
}

size_t translate_json_get_named_child_index(const char *js, jsmntok_t *t, size_t idx, size_t count, char * child_name) {
	if (t[idx].type == JSMN_OBJECT) {
		int parentIdx = (int)idx;
		
		idx += 1;
		while(idx < count) {
			if (t[idx].parent == parentIdx) {
				if (jsoneq(js, &t[idx], child_name) == 0) {
					return idx;
				}
				idx += 2;
			} else {
				idx += 1;
			}
		}
	}else{
		fprintf(stderr, "warning: translate_json_get_named_child_index() called on not an object\n");
	}
	return 0;
}

sds translate_json_add_use(sds code)
{
	code = sdscatprintf(code, "use \"json\"\n\n");
	
	return code;
}

sds translate_json_add_primitive(sds code, char * class_name)
{
	// we use a primitive to expose API to read and write the serialized objects
	code = sdscatprintf(code, "primitive %s\n", class_name);
	code = sdscatprintf(code, "  fun read() =>\n");
	code = sdscatprintf(code, "    None\n");
	code = sdscatprintf(code, "  fun write() =>\n");
	code = sdscatprintf(code, "    None\n");
	code = sdscatprintf(code, "\n");
	
	return code;
}


sds translate_json_add_property(sds code, const char *js, jsmntok_t *t, size_t idx, size_t count)
{
	// property are like:
	// "firstName": {
	//   "type": "string",
	//   "description": "The person's first name."
	// },
	((void)count);
		
	char * propertyName = strndup(js + t[idx].start, t[idx].end - t[idx].start);
	
	idx += 1;
	
	
	if (t[idx].type == JSMN_OBJECT)
	{
		size_t typeIdx = translate_json_get_named_child_index(js, t, idx, count, "type");
		size_t descriptionIdx = translate_json_get_named_child_index(js, t, idx, count, "description");
	
		if (descriptionIdx != 0) {
			char * propertyDescription = strndup(js + t[descriptionIdx+1].start, t[descriptionIdx+1].end - t[descriptionIdx+1].start);
			code = sdscatprintf(code, "  // %s\n", propertyDescription);
		}
	
		code = sdscatprintf(code, "  let %s", propertyName);
		if (typeIdx == 0) {
			return translate_json_abort(code, "type for property not found");
		} else {
			if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
				code = sdscatprintf(code, ":String val");
			}
			if (jsoneq(js, &t[typeIdx + 1], "integer") == 0) {
				code = sdscatprintf(code, ":I64 val");
			}
		}
	
		code = sdscatprintf(code, "\n\n");
	} else {
		return translate_json_abort(code, "property is not an object");
	}
		
	return code;
}

sds translate_json_add_object(sds code, const char *js, jsmntok_t *t, size_t count)
{
	if (count == 0)
	{
		return code;
	}
	
	if (t[0].type == JSMN_OBJECT)
	{
		size_t idx = 0;
		char * title = NULL;
		char * type = NULL;
		
		while(idx < count)
		{
			if (jsoneq(js, &t[idx], "title") == 0) {
				idx += 1;
				title = strndup(js + t[idx].start, t[idx].end - t[idx].start);
			}
			if (jsoneq(js, &t[idx], "type") == 0) {
				idx += 1;
				type = strndup(js + t[idx].start, t[idx].end - t[idx].start);
			}
			
			if (jsoneq(js, &t[idx], "properties") == 0) {
				if (title == NULL || type == NULL) {
					return translate_json_abort(code, "\"properties\" found but \"title\" or \"type\" are not defined");
				}
				else if (strcmp(type, "object") == 0)
				{
					code = sdscatprintf(code, "class %s\n", title);
					
					// add all of the properties for this type
					idx++;
					
					if (strcmp(type, "object") == 0) {
						idx++;
						while(idx < count) {
							code = translate_json_add_property(code, js, t, idx, count);
							idx = translate_json_get_next_sibling(t, idx, count);
						}
					} else {
						return translate_json_abort(code, "\"properties\" is not an \"object\"");
					}
					
					
				}
				else
				{
					return translate_json_abort(code, "\"properties\" found but \"type\" is not \"object\"");
				}
				
				
			}
			
			idx += 1;
		}
		
		return code;
	}

	// if we get here, we encountered an invalid key
	return translate_json_abort(code, "expected an object but didn't find one");

	/*
		// we're a normal key / value pair
		char * key = strndup(js + &t[0].start, &t[0].end - &t[0].start);
		char * value = strndup(js + &t[1].start, &t[1].end - &t[1].start);
		code = sdscatprintf(code, "class %s\n", class_name);
		code = sdscatprintf(code, "  // TODO: array of stuff named 'root'\n");
		return translate_json_add_objects(code, class_name, js, t + 1, count - 1);
	*/
	
	/*
	char * key = strndup(js + t->start, t->end - t->start);

	//char * class_name = translate_class_name(file_name);
    if (t->type == JSMN_PRIMITIVE) {
		fprintf(stderr, "*** primitive\n");
    }
    if (t->type == JSMN_STRING) {
		fprintf(stderr, "*** string\n");
    }
    if (t->type == JSMN_OBJECT) {
		fprintf(stderr, "*** object\n");
    }
    if (t->type == JSMN_ARRAY) {
		fprintf(stderr, "*** array\n");
    }
	
	fprintf(stderr, "*** %s\n", key);

	return translate_json_addroot(code, js, file_name, t + 1, count - 1);
*/
	/*
	if (t->type == JSMN_OBJECT) {
	return translate_json_dump(code, js, t + 1, count - 1);
	}
	if (t->type == JSMN_ARRAY) {
	fprintf(stderr, "*** array\n");
	}


	code = sdscatprintf(code, "class %s\n", class_name);*/


	//return code;
}

sds translate_json_dump(sds code, const char *js, jsmntok_t *t, size_t count)
{
	((void)js);
	
	//char * class_name = translate_class_name(file_name);
	//code = sdscatprintf(code, "class %s\n", class_name);
	fprintf(stderr, "count: %d\n", (int)count);
	
    if (t->type == JSMN_PRIMITIVE) {
		fprintf(stderr, "*** primitive\n");
    }
    if (t->type == JSMN_STRING) {
		fprintf(stderr, "*** string\n");
    }
    if (t->type == JSMN_OBJECT) {
		fprintf(stderr, "*** object\n");
    }
    if (t->type == JSMN_ARRAY) {
		fprintf(stderr, "*** array\n");
    }
	
	return code;
}

char* translate_json(const char* file_name, const char* source_code)
{
	// it is our responsibility to free the old "source code" which was provided
	unsigned long in_source_code_length = strlen(source_code)+1;
		
	// use the sds library to concat our pony code together, then copy it to a pony allocated buffer
	sds code = sdsnew("");
	
	char * class_name = translate_class_name(file_name);
	
	code = translate_json_add_use(code);
	
	code = translate_json_add_primitive(code, class_name);
		
	// each json schema file is a class with the name of the class matching the file name
    jsmn_parser parser;
    jsmntok_t tokens[128]; /* We expect no more than 128 tokens */

    jsmn_init(&parser);
    int num = jsmn_parse(&parser, source_code, strlen(source_code), tokens, sizeof(tokens) / sizeof(tokens[0]));	
	
    /* Assume the top-level element is an object */
    if (num >= 1) {
		code = translate_json_add_object(code, source_code, tokens, parser.toknext);
	}
	
	
	
	// free our incoming source code
	ponyint_pool_free_size(in_source_code_length, (void *)source_code);
	
	// copy our sds string to pony memory
	size_t code_len = sdslen(code);
	char * pony_code = (char*)ponyint_pool_alloc_size(code_len + 1);
	strncpy(pony_code, code, code_len);
	pony_code[code_len] = 0;
	sdsfree(code);
	
	fprintf(stderr, "========================== autogenerated pony code ==========================\n");
	fprintf(stderr, "%s", pony_code);
	fprintf(stderr, "=============================================================================\n");
	
	return pony_code;
}

