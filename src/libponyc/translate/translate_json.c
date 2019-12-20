#include "../../libponyrt/gc/serialise.h"
#include "../../libponyrt/mem/pool.h"
#include "translate_source.h"
#include "sds/sds.h"
#include "jsmn/jsmn.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_TOKENS 128
#define MAX_DELAYED_OBJECTS 128

// converts a JSON Schema file to Pony classes. Used in source.c.

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
	if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
		strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return 0;
	}
	return -1;
}

void translate_json_register_delayed_object(size_t idx, size_t* delayedObjects)
{
	// don't store duplicates
	for(int i = 0; i < MAX_DELAYED_OBJECTS; i++) {
		if (delayedObjects[i] == idx) {
			return;
		}
	}
	// add to the delayed object is there is room
	for(int i = 0; i < MAX_DELAYED_OBJECTS; i++) {
		if (delayedObjects[i] == 0) {
			delayedObjects[i] = idx;
			break;
		}
	}
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
	((void) class_name);
	// we use a primitive to expose API to read and write the serialized objects
	/*
	code = sdscatprintf(code, "primitive %s\n", class_name);
	code = sdscatprintf(code, "  fun read() =>\n");
	code = sdscatprintf(code, "    None\n");
	code = sdscatprintf(code, "  fun write() =>\n");
	code = sdscatprintf(code, "    None\n");
	code = sdscatprintf(code, "\n");
	*/
	return code;
}


sds translate_json_add_property(sds code, const char *js, jsmntok_t *t, size_t idx, size_t count, size_t* delayedObjects)
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
			if (jsoneq(js, &t[typeIdx + 1], "number") == 0) {
				code = sdscatprintf(code, ":F64 val");
			}
			if (jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
				code = sdscatprintf(code, ":Bool val");
			}
			if (jsoneq(js, &t[typeIdx + 1], "array") == 0) {
				size_t childItemsIdx = translate_json_get_named_child_index(js, t, t[typeIdx].parent, count, "items");
				if (childItemsIdx == 0) {
					return translate_json_abort(code, "items for array not found");
				}
				
				size_t childTypeIdx = translate_json_get_named_child_index(js, t, childItemsIdx+1, count, "type");
				if (childTypeIdx == 0) {
					return translate_json_abort(code, "type for items in array not found");
				}
				
				if (jsoneq(js, &t[childTypeIdx + 1], "string") == 0) {
					code = sdscatprintf(code, ":Array[String]");
				}
				if (jsoneq(js, &t[childTypeIdx + 1], "integer") == 0) {
					code = sdscatprintf(code, ":Array[I64]");
				}
				if (jsoneq(js, &t[childTypeIdx + 1], "number") == 0) {
					code = sdscatprintf(code, ":Array[F64]");
				}
				if (jsoneq(js, &t[childTypeIdx + 1], "boolean") == 0) {
					code = sdscatprintf(code, ":Array[Bool]");
				}
				if (jsoneq(js, &t[childTypeIdx + 1], "object") == 0) {
					
					size_t childTitleIdx = translate_json_get_named_child_index(js, t, childItemsIdx+1, count, "title");
					if (childTitleIdx == 0) {
						return translate_json_abort(code, "title for object in array not found");
					}
					
					char * childTitleName = strndup(js + t[childTitleIdx+1].start, t[childTitleIdx+1].end - t[childTitleIdx+1].start);
					
					code = sdscatprintf(code, ":Array[%s]", childTitleName);
					
					translate_json_register_delayed_object(childItemsIdx+1, delayedObjects);
				}
				
				
			}
			code = sdscatprintf(code, "\n\n");
		}		
	} else {
		return translate_json_abort(code, "property is not an object");
	}
		
	return code;
}

sds translate_json_add_constructor(sds code, const char *js, jsmntok_t *t, size_t idx, size_t count)
{
	// property are like:
	// "firstName": {
	//   "type": "string",
	//   "description": "The person's first name."
	// },
	
	code = sdscatprintf(code, "  new create(obj:JsonObject) =>\n");
	
	while(idx < count) {
		
		char * propertyName = strndup(js + t[idx].start, t[idx].end - t[idx].start);
		if (t[idx+1].type == JSMN_OBJECT)
		{
			size_t typeIdx = translate_json_get_named_child_index(js, t, idx+1, count, "type");
			if (typeIdx == 0) {
				return translate_json_abort(code, "type for property not found");
			} else {
				if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
					code = sdscatprintf(code, "    %s = try obj.data(\"%s\")? as String else \"\" end\n", propertyName, propertyName);
				}
				if (jsoneq(js, &t[typeIdx + 1], "integer") == 0) {
					code = sdscatprintf(code, "    %s = try obj.data(\"%s\")? as I64 else 0 end\n", propertyName, propertyName);
				}
				if (jsoneq(js, &t[typeIdx + 1], "number") == 0) {
					code = sdscatprintf(code, "    %s = try obj.data(\"%s\")? as F64 else 0.0 end\n", propertyName, propertyName);
				}
				if (jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
					code = sdscatprintf(code, "    %s = try obj.data(\"%s\")? as Bool else false end\n", propertyName, propertyName);
				}
				if (jsoneq(js, &t[typeIdx + 1], "array") == 0) {
					
					size_t childItemsIdx = translate_json_get_named_child_index(js, t, t[typeIdx].parent, count, "items");
					if (childItemsIdx == 0) {
						return translate_json_abort(code, "items for array not found");
					}
					
					size_t childTypeIdx = translate_json_get_named_child_index(js, t, childItemsIdx+1, count, "type");
					if (childTypeIdx == 0) {
						return translate_json_abort(code, "type for items in array not found");
					}
					
					char * arrayType = NULL;
					if (jsoneq(js, &t[childTypeIdx + 1], "string") == 0) {
						arrayType = "String";
					}
					if (jsoneq(js, &t[childTypeIdx + 1], "integer") == 0) {
						arrayType = "Integer";
					}
					if (jsoneq(js, &t[childTypeIdx + 1], "number") == 0) {
						arrayType = "Number";
					}
					if (jsoneq(js, &t[childTypeIdx + 1], "boolean") == 0) {
						arrayType = "Boolean";
					}
					if (jsoneq(js, &t[childTypeIdx + 1], "object") == 0) {
						size_t child2TitleIdx = translate_json_get_named_child_index(js, t, childItemsIdx+1, count, "title");
						if (child2TitleIdx == 0) {
							return translate_json_abort(code, "title for object in array not found");
						}
						arrayType = strndup(js + t[child2TitleIdx+1].start, t[child2TitleIdx+1].end - t[child2TitleIdx+1].start);
					}
					
					code = sdscatprintf(code, "    let %sArr = try obj.data(\"%s\")? as JsonArray else JsonArray end\n", propertyName, propertyName);
					code = sdscatprintf(code, "    %s = Array[%s](%sArr.data.size())\n", propertyName, arrayType, propertyName);
					code = sdscatprintf(code, "    for item in %sArr.data.values() do\n", propertyName);
					if (jsoneq(js, &t[typeIdx + 1], "object") == 0) {
						code = sdscatprintf(code, "      try %s.push(%s(item as JsonObject)) end\n", propertyName, arrayType);
					} else {
						code = sdscatprintf(code, "      try %s.push(item as %s) end\n", propertyName, arrayType);
					}
					code = sdscatprintf(code, "    end\n");
					
					//code = sdscatprintf(code, "    for item in arr.data.values() do\n");
					//code = sdscatprintf(code, "      try array.push(item as %s) end\n", type);						
					//code = sdscatprintf(code, "    end\n");
					
					
					//code = sdscatprintf(code, "    %s = try obj.data(\"%s\")? as Bool else false end\n", propertyName, propertyName);
				}
			}
		}
		
		idx = translate_json_get_next_sibling(t, idx, count);
	}
		
	return code;
}

sds translate_json_add_object(sds code, const char *js, jsmntok_t *t, size_t idx, size_t count, size_t* delayedObjects)
{
	if (count == 0)
	{
		return code;
	}
	
	if (t[idx].type == JSMN_OBJECT)
	{
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
				

				if (strcmp(type, "array") == 0)
				{
					code = sdscatprintf(code, "class %s\n", title);
					
					idx++;
					
					size_t itemsIdx = translate_json_get_named_child_index(js, t, t[idx].parent, count, "items");
					
					if (itemsIdx > 0) {
						
						size_t typeIdx = translate_json_get_named_child_index(js, t, itemsIdx+1, count, "type");
						size_t titleIdx = translate_json_get_named_child_index(js, t, itemsIdx+1, count, "title");
						char * type = NULL;
						if (typeIdx == 0) {
							return translate_json_abort(code, "type for items not found");
						} else {
							if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
								type = "String";
							}
							if (jsoneq(js, &t[typeIdx + 1], "integer") == 0) {
								type = "I64";
							}
							if (jsoneq(js, &t[typeIdx + 1], "number") == 0) {
								type = "F64";
							}
							if (jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
								type = "Bool";
							}
							if (jsoneq(js, &t[typeIdx + 1], "object") == 0) {
								// embedded object, we need to make a class for this (later)
								if (titleIdx == 0) {
									return translate_json_abort(code, "object in an array does not have a title");
								}
								
								char * title2 = strndup(js + t[titleIdx+1].start, t[titleIdx+1].end - t[titleIdx+1].start);
								
								translate_json_register_delayed_object(itemsIdx+1, delayedObjects);
								
								type = title2;
							}
						}
						
						idx = translate_json_get_next_sibling(t, idx, count);
						
						
						code = sdscatprintf(code, "  let array:Array[%s]\n\n", type);
						
						code = sdscatprintf(code, "  new create(arr:JsonArray) =>\n");
						code = sdscatprintf(code, "    array = Array[%s](arr.data.size())\n", type);
						code = sdscatprintf(code, "    for item in arr.data.values() do\n");
						if (jsoneq(js, &t[typeIdx + 1], "object") == 0) {
							code = sdscatprintf(code, "      try array.push(%s(item as JsonObject)) end\n", type);
						} else {
							code = sdscatprintf(code, "      try array.push(item as %s) end\n", type);
						}
						code = sdscatprintf(code, "    end\n");
						
						code = sdscatprintf(code, "  fun ref apply(i: USize):%s ? =>\n", type);
						code = sdscatprintf(code, "    array(i)?\n\n");
						
						
						code = sdscatprintf(code, "\n");
					} else {
						return translate_json_abort(code, "\"array\" is not followed by \"items\"");
					}
				}
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
						
						size_t objectIdx = idx;
						while(idx < count) {
							code = translate_json_add_property(code, js, t, idx, count, delayedObjects);
							idx = translate_json_get_next_sibling(t, idx, count);
						}
						
						code = translate_json_add_constructor(code, js, t, objectIdx, count);
						
						
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
    jsmntok_t tokens[MAX_TOKENS];
	size_t delayedObjects[MAX_DELAYED_OBJECTS] = {0}; 

    jsmn_init(&parser);
    int num = jsmn_parse(&parser, source_code, strlen(source_code), tokens, sizeof(tokens) / sizeof(tokens[0]));	
	
    /* Assume the top-level element is an object */
    if (num >= 1) {
		code = translate_json_add_object(code, source_code, tokens, 0, parser.toknext, delayedObjects);
		
		for(int i = 0; i < MAX_DELAYED_OBJECTS; i++) {
			if (delayedObjects[i] != 0) {
				code = translate_json_add_object(code, source_code, tokens, delayedObjects[i], parser.toknext, delayedObjects);
			}
		}
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

