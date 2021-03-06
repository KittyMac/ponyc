#include "../../libponyrt/gc/serialise.h"
#include "../../libponyrt/mem/pool.h"
#include "translate_source.h"
#include "sds/sds.h"
#include "jsmn/jsmn.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_TOKENS 8192
#define MAX_DELAYED_OBJECTS 8192

#define OBJREFLEN 8

// converts a JSON Schema file to Pony classes. Used in source.c.

static bool didExportInterface = false;

static int jsonprefix(const char *json, jsmntok_t *tok, const char *s) {
  int n = (int)strlen(s);
  if (tok->type == JSMN_STRING && n <= tok->end - tok->start &&
    strncmp(json + tok->start, s, n) == 0) {
    return 0;
  }
  return -1;
}

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
  if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
    strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}

char * translate_json_clean_pony_name(char * name) {
  // if our json keys match pony keywords we will get compile errors, so we find those
  // and deal with it somehow...
  if (!strcmp(name, "actor")) { return "actorJSON"; }
  if (!strcmp(name, "addressof")) { return "addressofJSON"; }
  if (!strcmp(name, "and")) { return "andJSON"; }
  if (!strcmp(name, "as")) { return "asJSON"; }
  if (!strcmp(name, "be")) { return "beJSON"; }
  if (!strcmp(name, "break")) { return "breakJSON"; }
  if (!strcmp(name, "class")) { return "classJSON"; }
  if (!strcmp(name, "compile_error")) { return "compile_errorJSON"; }
  if (!strcmp(name, "compile_intrinsic")) { return "compile_intrinsicJSON"; }
  
  if (!strcmp(name, "consume")) { return "consumeJSON"; }
  if (!strcmp(name, "continue")) { return "continueJSON"; }
  if (!strcmp(name, "delegate")) { return "delegateJSON"; }
  if (!strcmp(name, "digestof")) { return "digestofJSON"; }
  if (!strcmp(name, "do")) { return "doJSON"; }
  if (!strcmp(name, "else")) { return "elseJSON"; }
  if (!strcmp(name, "elseif")) { return "elseifJSON"; }
  if (!strcmp(name, "embed")) { return "embedJSON"; }
  if (!strcmp(name, "end")) { return "endJSON"; }
  if (!strcmp(name, "error")) { return "errorJSON"; }
  
  if (!strcmp(name, "for")) { return "forJSON"; }
  if (!strcmp(name, "fun")) { return "funJSON"; }
  if (!strcmp(name, "if")) { return "ifJSON"; }
  if (!strcmp(name, "ifdef")) { return "ifdefJSON"; }
  if (!strcmp(name, "in")) { return "inJSON"; }
  if (!strcmp(name, "interface")) { return "interfaceJSON"; }
  if (!strcmp(name, "is")) { return "isJSON"; }
  if (!strcmp(name, "isnt")) { return "isntJSON"; }
  if (!strcmp(name, "lambda")) { return "lambdaJSON"; }
  if (!strcmp(name, "let")) { return "letJSON"; }
  if (!strcmp(name, "match")) { return "matchJSON"; }
  if (!strcmp(name, "new")) { return "newJSON"; }
  if (!strcmp(name, "not")) { return "notJSON"; }
  if (!strcmp(name, "object")) { return "objectJSON"; }
  
  if (!strcmp(name, "primitive")) { return "primitiveJSON"; }
  if (!strcmp(name, "recover")) { return "recoverJSON"; }
  if (!strcmp(name, "repeat")) { return "repeatJSON"; }
  if (!strcmp(name, "return")) { return "returnJSON"; }
  if (!strcmp(name, "struct")) { return "structJSON"; }
  if (!strcmp(name, "then")) { return "thenJSON"; }
  if (!strcmp(name, "trait")) { return "traitJSON"; }
  if (!strcmp(name, "try")) { return "tryJSON"; }
  if (!strcmp(name, "type")) { return "typeJSON"; }
  if (!strcmp(name, "until")) { return "untilJSON"; }
  
  if (!strcmp(name, "use")) { return "useJSON"; }
  if (!strcmp(name, "var")) { return "varJSON"; }
  if (!strcmp(name, "where")) { return "whereJSON"; }
  if (!strcmp(name, "while")) { return "whileJSON"; }
  if (!strcmp(name, "with")) { return "withJSON"; }
  if (!strcmp(name, "xor")) { return "xorJSON"; }
  
  if (!strcmp(name, "iso")) { return "isoJSON"; }
  if (!strcmp(name, "val")) { return "valJSON"; }
  if (!strcmp(name, "tag")) { return "tagJSON"; }
  if (!strcmp(name, "trn")) { return "trnJSON"; }
  if (!strcmp(name, "box")) { return "boxJSON"; }
  if (!strcmp(name, "ref")) { return "refJSON"; }
  
  return name;
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

sds translate_json_add_global_interface(sds code)
{
  if (didExportInterface == false) {
    didExportInterface = true;
    
    code = sdscatprintf(code, "interface PonyJson\n");
    code = sdscatprintf(code, "  new empty()\n");
    code = sdscatprintf(code, "  new fromJson(obj:JsonObject val)?\n");
    code = sdscatprintf(code, "  new fromString(s:String val)?\n");
    code = sdscatprintf(code, "  new fromArray(s:Array[U8] val)?\n");
    code = sdscatprintf(code, "  fun appendJson(json':String iso):String iso^\n\n");
  }
  return code;
}


sds translate_json_add_property(sds code, const char *js, jsmntok_t *t, size_t idx, size_t count, size_t* delayedObjects)
{
  // property are like:
  // "firstName": {
  //   "type": "string",
  //   "description": "The person's first name."
  //   "default": "The person's first name."
  // },
  ((void)count);
    
  char * originalPropertyName = strndup(js + t[idx].start, t[idx].end - t[idx].start);
  char * propertyName = translate_json_clean_pony_name(originalPropertyName);
  
  idx += 1;
  
  
  if (t[idx].type == JSMN_OBJECT)
  {
    size_t typeIdx = translate_json_get_named_child_index(js, t, idx, count, "type");
    size_t descriptionIdx = translate_json_get_named_child_index(js, t, idx, count, "description");
    size_t defaultIdx = translate_json_get_named_child_index(js, t, idx, count, "default");
  
    if (descriptionIdx != 0) {
      char * propertyDescription = strndup(js + t[descriptionIdx+1].start, t[descriptionIdx+1].end - t[descriptionIdx+1].start);
      code = sdscatprintf(code, "  // %s\n", propertyDescription);
    }
    
    char * defaultValue = NULL;
    if (defaultIdx != 0) {
      defaultValue = strndup(js + t[defaultIdx+1].start, t[defaultIdx+1].end - t[defaultIdx+1].start);
    }

    code = sdscatprintf(code, "  var %s", propertyName);
    if (typeIdx == 0) {
      return translate_json_abort(code, "type for property not found");
    } else {
      if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
        if (defaultValue != NULL) {
          code = sdscatprintf(code, ":String = \"%s\"", defaultValue);
        } else {
          code = sdscatprintf(code, ":String = \"\"");
        }
      }
      if (jsoneq(js, &t[typeIdx + 1], "integer") == 0) {
        if (defaultValue != NULL) {
          code = sdscatprintf(code, ":I32 = %s", defaultValue);
        } else {
          code = sdscatprintf(code, ":I32 = 0");
        }
      }
      if (jsoneq(js, &t[typeIdx + 1], "number") == 0) {
        if (defaultValue != NULL) {
          code = sdscatprintf(code, ":F32 = %s", defaultValue);
        } else {
          code = sdscatprintf(code, ":F32 = 0.0");
        }
      }
      if (jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
        if (defaultValue != NULL) {
          code = sdscatprintf(code, ":Bool = %s", defaultValue);
        } else {
          code = sdscatprintf(code, ":Bool = false");
        }
      }
      if (jsonprefix(js, &t[typeIdx + 1], "#object") == 0) {
        char * objectTypeName = strndup(js + t[typeIdx + 1].start + OBJREFLEN, (t[typeIdx + 1].end - t[typeIdx + 1].start) - OBJREFLEN);
        code = sdscatprintf(code, ":%s = %s.empty()", objectTypeName, objectTypeName);
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
          code = sdscatprintf(code, ":Array[String] = Array[String]");
        }
        if (jsoneq(js, &t[childTypeIdx + 1], "integer") == 0) {
          code = sdscatprintf(code, ":Array[I32] = Array[I32]");
        }
        if (jsoneq(js, &t[childTypeIdx + 1], "number") == 0) {
          code = sdscatprintf(code, ":Array[F32] = Array[F32]");
        }
        if (jsoneq(js, &t[childTypeIdx + 1], "boolean") == 0) {
          code = sdscatprintf(code, ":Array[Bool] = Array[Bool]");
        }
        if (jsonprefix(js, &t[childTypeIdx + 1], "#object") == 0) {
          char * objectTypeName = strndup(js + t[childTypeIdx + 1].start + OBJREFLEN, (t[childTypeIdx + 1].end - t[childTypeIdx + 1].start) - OBJREFLEN);
          code = sdscatprintf(code, ":Array[%s] = Array[%s]", objectTypeName, objectTypeName);
        }
        if (jsoneq(js, &t[childTypeIdx + 1], "object") == 0) {
          
          size_t childTitleIdx = translate_json_get_named_child_index(js, t, childItemsIdx+1, count, "title");
          if (childTitleIdx == 0) {
            return translate_json_abort(code, "title for object in array not found");
          }
          
          char * originalChildTitleName = strndup(js + t[childTitleIdx+1].start, t[childTitleIdx+1].end - t[childTitleIdx+1].start);
          char * childTitleName = translate_json_clean_pony_name(originalChildTitleName);
          
          code = sdscatprintf(code, ":Array[%s] = Array[%s]", childTitleName, childTitleName);
          
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

sds translate_json_add_function_propery(sds code, char * className, const char *js, jsmntok_t *t, size_t idx, size_t count)
{
  
  code = sdscatprintf(code, "  fun ref add(that:%s):%s =>\n", className, className);
  code = sdscatprintf(code, "    let combined:%s ref = %s.empty()\n", className, className);
  
  while(idx < count) {
    
    char * originalPropertyName = strndup(js + t[idx].start, t[idx].end - t[idx].start);
    char * propertyName = translate_json_clean_pony_name(originalPropertyName);
    if (t[idx+1].type == JSMN_OBJECT)
    {     
      size_t typeIdx = translate_json_get_named_child_index(js, t, idx+1, count, "type");
      if (typeIdx == 0) {
        return translate_json_abort(code, "type for property not found");
      } else {
        if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
          code = sdscatprintf(code, "    combined.%s = %s + that.%s\n", propertyName, propertyName, propertyName);
        }
        if (jsoneq(js, &t[typeIdx + 1], "integer") == 0 || 
          jsoneq(js, &t[typeIdx + 1], "number") == 0) {
          code = sdscatprintf(code, "    combined.%s = %s + that.%s\n", propertyName, propertyName, propertyName);
        }
        if (jsonprefix(js, &t[typeIdx + 1], "#object") == 0) {
          // Should we require external objects to have an add() function?
        }
        if (jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
          code = sdscatprintf(code, "    combined.%s = %s or that.%s\n", propertyName, propertyName, propertyName);
        }
        if (jsoneq(js, &t[typeIdx + 1], "array") == 0) {
          code = sdscatprintf(code, "    for item in %s.values() do\n", propertyName);
          code = sdscatprintf(code, "      combined.%s.push(item)\n", propertyName);
          code = sdscatprintf(code, "    end\n");
          code = sdscatprintf(code, "    for item in that.%s.values() do\n", propertyName);
          code = sdscatprintf(code, "      combined.%s.push(item)\n", propertyName);
          code = sdscatprintf(code, "    end\n");
        }
      }
    }
    
    idx = translate_json_get_next_sibling(t, idx, count);
  }
  
  code = sdscatprintf(code, "    combined\n");
  
  return code;
}

sds translate_json_add_append_json(sds code, const char *js, jsmntok_t *t, size_t idx, size_t count)
{
  // property are like:
  // "firstName": {
  //   "type": "string",
  //   "description": "The person's first name."
  // },
  
  code = sdscatprintf(code, "  fun appendEscapedString(json':String iso, buf: String):String iso^ =>\n");
  code = sdscatprintf(code, "    var json = consume json'\n");
  code = sdscatprintf(code, "    for c in buf.values() do\n");
  code = sdscatprintf(code, "      match c\n");
  code = sdscatprintf(code, "      | 34 => json.push('\\\\'); json.push('\"')\n");
  code = sdscatprintf(code, "      | 92 => json.push('\\\\'); json.push('\\\\')\n");
  code = sdscatprintf(code, "      | '\\b' => json.push('\\\\'); json.push('b')\n");
  code = sdscatprintf(code, "      | '\\f' => json.push('\\\\'); json.push('f')\n");
  code = sdscatprintf(code, "      | '\\t' => json.push('\\\\'); json.push('t')\n");
  code = sdscatprintf(code, "      | '\\r' => json.push('\\\\'); json.push('r')\n");
  code = sdscatprintf(code, "      | '\\n' => json.push('\\\\'); json.push('n')\n");
  code = sdscatprintf(code, "      else json.push(c) end\n");
  code = sdscatprintf(code, "    end\n");
  code = sdscatprintf(code, "    consume json\n");

  code = sdscatprintf(code, "  fun string(): String iso^ =>\n");
  code = sdscatprintf(code, "    appendJson(recover String(1024) end)\n");
  code = sdscatprintf(code, "  fun appendJson(json':String iso):String iso^ =>\n");
  code = sdscatprintf(code, "    var json = consume json'\n");
  code = sdscatprintf(code, "    json.push('{')\n");
    
  while(idx < count) {
    
    char * originalPropertyName = strndup(js + t[idx].start, t[idx].end - t[idx].start);
    char * propertyName = translate_json_clean_pony_name(originalPropertyName);
    if (t[idx+1].type == JSMN_OBJECT)
    {
      size_t defaultIdx = translate_json_get_named_child_index(js, t, idx+1, count, "default");   
      char * defaultValue = NULL;
      if (defaultIdx != 0) {
        defaultValue = strndup(js + t[defaultIdx+1].start, t[defaultIdx+1].end - t[defaultIdx+1].start);
      }
      
      size_t typeIdx = translate_json_get_named_child_index(js, t, idx+1, count, "type");
      if (typeIdx == 0) {
        return translate_json_abort(code, "type for property not found");
      } else {
        if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
          if(defaultValue != NULL) { 
            code = sdscatprintf(code, "    if %s != \"%s\" then\n", propertyName, defaultValue);
          }
          code = sdscatprintf(code, "      json.append(\"\\\"%s\\\"\")\n", originalPropertyName);
          code = sdscatprintf(code, "      json.push(':')\n");
          code = sdscatprintf(code, "      json.push('\"')\n");
          code = sdscatprintf(code, "      json = appendEscapedString(consume json, %s.string())\n", propertyName);          
          code = sdscatprintf(code, "      json.push('\"')\n");
          code = sdscatprintf(code, "    json.push(',')\n");
          if(defaultValue != NULL) { 
            code = sdscatprintf(code, "    end\n");
          }
        }
        if (jsoneq(js, &t[typeIdx + 1], "integer") == 0 || 
          jsoneq(js, &t[typeIdx + 1], "number") == 0 || 
          jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
          if(defaultValue != NULL) { 
            code = sdscatprintf(code, "    if %s != %s then\n", propertyName, defaultValue);
          }
          code = sdscatprintf(code, "      json.append(\"\\\"%s\\\"\")\n", originalPropertyName);
          code = sdscatprintf(code, "      json.push(':')\n");
          code = sdscatprintf(code, "      json.append(%s.string())\n", propertyName);
          code = sdscatprintf(code, "    json.push(',')\n");
          if(defaultValue != NULL) { 
            code = sdscatprintf(code, "    end\n");
          }
        }
        if (jsonprefix(js, &t[typeIdx + 1], "#object") == 0) {
          code = sdscatprintf(code, "    json.append(\"\\\"%s\\\"\")\n", originalPropertyName);
          code = sdscatprintf(code, "    json.push(':')\n");
          code = sdscatprintf(code, "    json = %s.appendJson(consume json)\n", propertyName);
          code = sdscatprintf(code, "    json.push(',')\n");
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
          bool isObject = false;
          if (jsoneq(js, &t[childTypeIdx + 1], "string") == 0) {
            arrayType = "String";
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "integer") == 0) {
            arrayType = "I32";
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "number") == 0) {
            arrayType = "F32";
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "boolean") == 0) {
            arrayType = "Boolean";
          }
          if (jsonprefix(js, &t[childTypeIdx + 1], "#object") == 0) {
            arrayType = strndup(js + t[childTypeIdx + 1].start + OBJREFLEN, (t[childTypeIdx + 1].end - t[childTypeIdx + 1].start) - OBJREFLEN);
            isObject = true;
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "object") == 0) {
            size_t child2TitleIdx = translate_json_get_named_child_index(js, t, childItemsIdx+1, count, "title");
            if (child2TitleIdx == 0) {
              return translate_json_abort(code, "title for object in array not found");
            }
            arrayType = strndup(js + t[child2TitleIdx+1].start, t[child2TitleIdx+1].end - t[child2TitleIdx+1].start);
            isObject = true;
          }
          
          code = sdscatprintf(code, "    if %s.size() > 0 then\n", propertyName);
          code = sdscatprintf(code, "      json.append(\"\\\"%s\\\"\")\n", originalPropertyName);
          code = sdscatprintf(code, "      json.push(':')\n");
          code = sdscatprintf(code, "      json.push('[')\n");
          code = sdscatprintf(code, "      for item in %s.values() do\n", propertyName);
          if (!isObject) {
            code = sdscatprintf(code, "        json.push('\"')\n");
            code = sdscatprintf(code, "        json = appendEscapedString(consume json, item.string())\n");
            code = sdscatprintf(code, "        json.push('\"')\n");
          } else {                               
            code = sdscatprintf(code, "        json = item.appendJson(consume json)\n");
          }
          code = sdscatprintf(code, "        json.push(',')\n");
          code = sdscatprintf(code, "      end\n");
          code = sdscatprintf(code, "      if %s.size() > 0 then try json.pop()? end end\n", propertyName);
          code = sdscatprintf(code, "      json.push(']')\n");
          code = sdscatprintf(code, "      json.push(',')\n");
          code = sdscatprintf(code, "    end\n");
        }
      }
    }
    
    idx = translate_json_get_next_sibling(t, idx, count);
  }
  
  code = sdscatprintf(code, "    try if json(json.size()-1)? == ',' then json.pop()? end end\n");
  code = sdscatprintf(code, "    json.push('}')\n");
  code = sdscatprintf(code, "    consume json\n");
    
  return code;
}

sds translate_json_add_empty_constructor(sds code, const char *js, jsmntok_t *t, size_t idx, size_t count)
{
  bool didAddPonyDefault = false;
  
  code = sdscatprintf(code, "  new empty() =>\n");
  
  while(idx < count) {
    
    char * originalPropertyName = strndup(js + t[idx].start, t[idx].end - t[idx].start);
    char * propertyName = translate_json_clean_pony_name(originalPropertyName);
    if (t[idx+1].type == JSMN_OBJECT)
    {
      size_t typeIdx = translate_json_get_named_child_index(js, t, idx+1, count, "type");
      size_t ponyDefaultIdx = translate_json_get_named_child_index(js, t, idx+1, count, "pony-default");
      
      char * ponyDefaultValue = NULL;
      if (ponyDefaultIdx != 0) {
        ponyDefaultValue = strndup(js + t[ponyDefaultIdx+1].start, t[ponyDefaultIdx+1].end - t[ponyDefaultIdx+1].start);
      }
      
      if (ponyDefaultValue != NULL){
        didAddPonyDefault = true;
        
        if (typeIdx == 0) {
          return translate_json_abort(code, "type for property not found");
        } else {
          code = sdscatprintf(code, "    %s = %s\n", propertyName, ponyDefaultValue);
        }
      }
    }
    
    idx = translate_json_get_next_sibling(t, idx, count);
  }
  
  if (didAddPonyDefault == false) {
    code = sdscatprintf(code, "    None\n");
  }
  
  return code;
}

sds translate_json_add_create_constructor(sds code, const char *js, jsmntok_t *t, size_t _idx, size_t count)
{
  bool didAddArgument = false;
  
  code = sdscatprintf(code, "  new create( ");
  
  size_t idx = _idx;
  while(idx < count) {
    
    char * originalPropertyName = strndup(js + t[idx].start, t[idx].end - t[idx].start);
    char * propertyName = translate_json_clean_pony_name(originalPropertyName);
    if (t[idx+1].type == JSMN_OBJECT)
    {
      size_t typeIdx = translate_json_get_named_child_index(js, t, idx+1, count, "type");

      didAddArgument = true;
      
      if (typeIdx == 0) {
        return translate_json_abort(code, "type for property not found");
      } else {
        
        const char * propertyType = NULL;
        if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
          propertyType = "String";
        }
        if (jsoneq(js, &t[typeIdx + 1], "integer") == 0) {
          propertyType = "I32";
        }
        if (jsoneq(js, &t[typeIdx + 1], "number") == 0) {
          propertyType = "F32";
        }
        if (jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
          propertyType = "Bool";
        }
        if (jsonprefix(js, &t[typeIdx + 1], "#object") == 0) {
          propertyType = strndup(js + t[typeIdx + 1].start + OBJREFLEN, (t[typeIdx + 1].end - t[typeIdx + 1].start) - OBJREFLEN);
        }
        
        if(propertyType != NULL) {
          code = sdscatprintf(code, "%s':%s,", propertyName, propertyType);
        }
      }
    }
    
    idx = translate_json_get_next_sibling(t, idx, count);
  }
  
  sdssetlen(code, sdslen(code)-1);
  
  code = sdscatprintf(code, " ) =>\n");
  
  if (didAddArgument == false) {
    code = sdscatprintf(code, "    None\n");
  } else {
    
    idx = _idx;
    while(idx < count) {
  
      char * originalPropertyName = strndup(js + t[idx].start, t[idx].end - t[idx].start);
      char * propertyName = translate_json_clean_pony_name(originalPropertyName);
      if (t[idx+1].type == JSMN_OBJECT)
      {
        size_t typeIdx = translate_json_get_named_child_index(js, t, idx+1, count, "type");

        didAddArgument = true;
    
        if (typeIdx == 0) {
          return translate_json_abort(code, "type for property not found");
        } else {
      
          const char * propertyType = NULL;
          if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
            propertyType = "String";
          }
          if (jsoneq(js, &t[typeIdx + 1], "integer") == 0) {
            propertyType = "I32";
          }
          if (jsoneq(js, &t[typeIdx + 1], "number") == 0) {
            propertyType = "F32";
          }
          if (jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
            propertyType = "Bool";
          }
          if (jsonprefix(js, &t[typeIdx + 1], "#object") == 0) {
            propertyType = strndup(js + t[typeIdx + 1].start + OBJREFLEN, (t[typeIdx + 1].end - t[typeIdx + 1].start) - OBJREFLEN);
          }
          
          if(propertyType != NULL) {
            code = sdscatprintf(code, "    %s = %s'\n", propertyName, propertyName);
          }
        }
      }
  
      idx = translate_json_get_next_sibling(t, idx, count);
    }
    
  }
  
  return code;
}

sds translate_json_add_getters_and_setters(sds code, const char *js, jsmntok_t *t, size_t idx, size_t count)
{
  while(idx < count) {
    
    char * originalPropertyName = strndup(js + t[idx].start, t[idx].end - t[idx].start);
    char * propertyName = translate_json_clean_pony_name(originalPropertyName);
    if (t[idx+1].type == JSMN_OBJECT)
    {
      size_t typeIdx = translate_json_get_named_child_index(js, t, idx+1, count, "type");
      if (typeIdx == 0) {
        return translate_json_abort(code, "type for property not found");
      } else {
        const char * propertyType = NULL;
        if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
          propertyType = "String";
        }
        if (jsoneq(js, &t[typeIdx + 1], "integer") == 0) {
          propertyType = "I32";
        }
        if (jsoneq(js, &t[typeIdx + 1], "number") == 0) {
          propertyType = "F32";
        }
        if (jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
          propertyType = "Bool";
        }
        if (jsonprefix(js, &t[typeIdx + 1], "#object") == 0) {
          propertyType = strndup(js + t[typeIdx + 1].start + OBJREFLEN, (t[typeIdx + 1].end - t[typeIdx + 1].start) - OBJREFLEN);
        }
        
        if(propertyType != NULL) {
          code = sdscatprintf(code, "  fun ref get_%s():%s => %s\n", originalPropertyName, propertyType, propertyName);
          code = sdscatprintf(code, "  fun ref set_%s(v:%s) => %s = v\n", originalPropertyName, propertyType, propertyName);
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
          bool isObject = false;
          if (jsoneq(js, &t[childTypeIdx + 1], "string") == 0) {
            arrayType = "String";
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "integer") == 0) {
            arrayType = "I32";
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "number") == 0) {
            arrayType = "F32";
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "boolean") == 0) {
            arrayType = "Boolean";
          }
          if (jsonprefix(js, &t[childTypeIdx + 1], "#object") == 0) {
            arrayType = strndup(js + t[childTypeIdx + 1].start + OBJREFLEN, (t[childTypeIdx + 1].end - t[childTypeIdx + 1].start) - OBJREFLEN);
            isObject = true;
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "object") == 0) {
            size_t child2TitleIdx = translate_json_get_named_child_index(js, t, childItemsIdx+1, count, "title");
            if (child2TitleIdx == 0) {
              return translate_json_abort(code, "title for object in array not found");
            }
            arrayType = strndup(js + t[child2TitleIdx+1].start, t[child2TitleIdx+1].end - t[child2TitleIdx+1].start);
            isObject = true;
          }
                    
          if(arrayType != NULL) {
            code = sdscatprintf(code, "  fun ref get_%s():Array[%s] => %s\n", originalPropertyName, arrayType, propertyName);
            code = sdscatprintf(code, "  fun ref set_%s(v:Array[%s]) => %s = v\n", originalPropertyName, arrayType, propertyName);
          }
        }
        
      }
    }
    
    idx = translate_json_get_next_sibling(t, idx, count);
  }
    
  return code;
}

sds translate_json_add_read_constructor(sds code, const char *js, jsmntok_t *t, size_t idx, size_t count)
{
  // property are like:
  // "firstName": {
  //   "type": "string",
  //   "description": "The person's first name."
  // },
  
  
  
  code = sdscatprintf(code, "  new fromString(jsonString:String val)? =>\n");
  code = sdscatprintf(code, "    let doc: JsonDoc val = recover val JsonDoc.>parse(jsonString)? end\n");
  code = sdscatprintf(code, "    let obj = doc.data as JsonObject val\n");
  code = sdscatprintf(code, "    _read(obj)?\n");
  
  code = sdscatprintf(code, "  new fromArray(jsonArray:Array[U8] val)? =>\n");
  code = sdscatprintf(code, "    let doc: JsonDoc val = recover val JsonDoc.>parse(String.from_array(jsonArray))? end\n");
  code = sdscatprintf(code, "    let obj = doc.data as JsonObject val\n");
  code = sdscatprintf(code, "    _read(obj)?\n");
  
  code = sdscatprintf(code, "  new fromJson(obj:JsonObject val)? =>\n");
  code = sdscatprintf(code, "    _read(obj)?\n");
  
  code = translate_json_add_getters_and_setters(code, js, t, idx, count);
  
  code = sdscatprintf(code, "  fun ref _read(obj:JsonObject val)? =>\n");
  
  // this looks odd, but i want future versions of this to throw an error if a required field
  // is not there. But the compiler won't let us make it partial if there is no error clause 
  // in the method (even if it is never called)
  code = sdscatprintf(code, "    if false then error end\n");
  
  while(idx < count) {
    
    char * originalPropertyName = strndup(js + t[idx].start, t[idx].end - t[idx].start);
    char * propertyName = translate_json_clean_pony_name(originalPropertyName);
    if (t[idx+1].type == JSMN_OBJECT)
    {
      size_t typeIdx = translate_json_get_named_child_index(js, t, idx+1, count, "type");
      size_t ponyDefaultIdx = translate_json_get_named_child_index(js, t, idx+1, count, "pony-default");
      size_t defaultIdx = translate_json_get_named_child_index(js, t, idx+1, count, "default");
      
      char * ponyDefaultValue = NULL;
      if (ponyDefaultIdx != 0) {
        ponyDefaultValue = strndup(js + t[ponyDefaultIdx+1].start, t[ponyDefaultIdx+1].end - t[ponyDefaultIdx+1].start);
      }
      
      char * defaultValue = NULL;
      if (defaultIdx != 0) {
        defaultValue = strndup(js + t[defaultIdx+1].start, t[defaultIdx+1].end - t[defaultIdx+1].start);
      }
      
      if (typeIdx == 0) {
        return translate_json_abort(code, "type for property not found");
      } else {
                
        if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
          if (ponyDefaultValue != NULL) {
            code = sdscatprintf(code, "    %s = try obj.data(\"%s\")? as String else %s end\n", propertyName, originalPropertyName, ponyDefaultValue);
          } else if (defaultValue != NULL) {
            code = sdscatprintf(code, "    %s = try obj.data(\"%s\")? as String else \"%s\" end\n", propertyName, originalPropertyName, defaultValue);
          } else {
            code = sdscatprintf(code, "    %s = try obj.data(\"%s\")? as String else \"\" end\n", propertyName, originalPropertyName);
          }
        }
        if (jsoneq(js, &t[typeIdx + 1], "integer") == 0) {
          code = sdscatprintf(code, "    %s = try obj.data(\"%s\")? as I32 else %s end\n", propertyName, originalPropertyName, (ponyDefaultValue != NULL ? ponyDefaultValue : (defaultValue != NULL ? defaultValue : "0") ));
        }
        if (jsoneq(js, &t[typeIdx + 1], "number") == 0) {
          // Note: we want to accept both I32 and F32 (and convert to F32)
          code = sdscatprintf(code, "    %s = try (obj.data(\"%s\")? as F32) else try (obj.data(\"%s\")? as I32).f32() else %s end end\n", propertyName, originalPropertyName, originalPropertyName, (ponyDefaultValue != NULL ? ponyDefaultValue : (defaultValue != NULL ? defaultValue : "0.0") ));
        }
        if (jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
          code = sdscatprintf(code, "    %s = try obj.data(\"%s\")? as Bool else %s end\n", propertyName, originalPropertyName, (ponyDefaultValue != NULL ? ponyDefaultValue : (defaultValue != NULL ? defaultValue : "false") ));
        }
        if (jsonprefix(js, &t[typeIdx + 1], "#object") == 0) {
          char * objectType = strndup(js + t[typeIdx + 1].start + OBJREFLEN, (t[typeIdx + 1].end - t[typeIdx + 1].start) - OBJREFLEN);
          code = sdscatprintf(code, "    try\n");
          code = sdscatprintf(code, "      %s = %s.fromJson(obj.data(\"%s\")? as JsonObject val)?\n", propertyName, objectType, originalPropertyName);
          code = sdscatprintf(code, "    else\n");
          code = sdscatprintf(code, "      %s = %s.fromString(obj.data(\"%s\")? as String val)?\n", propertyName, objectType, originalPropertyName);
          code = sdscatprintf(code, "    end\n");
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
          bool isObject = false;
          if (jsoneq(js, &t[childTypeIdx + 1], "string") == 0) {
            arrayType = "String";
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "integer") == 0) {
            arrayType = "I32";
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "number") == 0) {
            arrayType = "F32";
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "boolean") == 0) {
            arrayType = "Boolean";
          }
          if (jsonprefix(js, &t[childTypeIdx + 1], "#object") == 0) {
            arrayType = strndup(js + t[childTypeIdx + 1].start + OBJREFLEN, (t[childTypeIdx + 1].end - t[childTypeIdx + 1].start) - OBJREFLEN);
            isObject = true;
          }
          if (jsoneq(js, &t[childTypeIdx + 1], "object") == 0) {
            size_t child2TitleIdx = translate_json_get_named_child_index(js, t, childItemsIdx+1, count, "title");
            if (child2TitleIdx == 0) {
              return translate_json_abort(code, "title for object in array not found");
            }
            arrayType = strndup(js + t[child2TitleIdx+1].start, t[child2TitleIdx+1].end - t[child2TitleIdx+1].start);
            isObject = true;
          }
          
          code = sdscatprintf(code, "    try\n");
          code = sdscatprintf(code, "      let %sArr = obj.data(\"%s\")? as JsonArray val\n", propertyName, originalPropertyName);
          code = sdscatprintf(code, "      for item in %sArr.data.values() do\n", propertyName);
          if (isObject) {
            code = sdscatprintf(code, "        try %s.push(%s.fromJson(item as JsonObject val)?) end\n", propertyName, arrayType);
          } else {
            code = sdscatprintf(code, "        try %s.push(item as %s) end\n", propertyName, arrayType);
          }
          code = sdscatprintf(code, "      end\n");
          code = sdscatprintf(code, "    end\n");
        }
      }
    }
    
    idx = translate_json_get_next_sibling(t, idx, count);
  }
    
  return code;
}

sds translate_json_add_use(sds code, const char *js, jsmntok_t *t, size_t idx, size_t count)
{
  code = sdscatprintf(code, "use \"json\"\n");
  
  if (count == 0)
  {
    return code;
  }
  
  if (t[idx].type == JSMN_OBJECT)
  {   
    while(idx < count)
    {
      if (jsoneq(js, &t[idx], "pony-use") == 0) {
        idx += 1;
        char * pony_use = strndup(js + t[idx].start, t[idx].end - t[idx].start);
        
        // each pony-use decelaration is its own
        // "pony-use": "math"
        code = sdscatprintf(code, "use \"%s\"\n", pony_use);
      }     
      idx += 1;
    }
  }
  
  code = sdscatprintf(code, "\n");
  
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
    char * pony_traits = NULL;
    
    while(idx < count)
    {
      if (jsoneq(js, &t[idx], "title") == 0) {
        idx += 1;
        title = strndup(js + t[idx].start, t[idx].end - t[idx].start);
      }
      if (jsoneq(js, &t[idx], "pony-traits") == 0) {
        idx += 1;
        pony_traits = strndup(js + t[idx].start, t[idx].end - t[idx].start);
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
            bool isObject = false;
            if (typeIdx == 0) {
              return translate_json_abort(code, "type for items not found");
            } else {
              if (jsoneq(js, &t[typeIdx + 1], "string") == 0) {
                type = "String";
              }
              if (jsoneq(js, &t[typeIdx + 1], "integer") == 0) {
                type = "I32";
              }
              if (jsoneq(js, &t[typeIdx + 1], "number") == 0) {
                type = "F32";
              }
              if (jsoneq(js, &t[typeIdx + 1], "boolean") == 0) {
                type = "Bool";
              }
              if (jsonprefix(js, &t[typeIdx + 1], "#object") == 0) {
                type = strndup(js + t[typeIdx + 1].start + OBJREFLEN, (t[typeIdx + 1].end - t[typeIdx + 1].start) - OBJREFLEN);
                isObject = true;
              }
              if (jsoneq(js, &t[typeIdx + 1], "object") == 0) {
                // embedded object, we need to make a class for this (later)
                if (titleIdx == 0) {
                  return translate_json_abort(code, "object in an array does not have a title");
                }
                
                char * title2 = strndup(js + t[titleIdx+1].start, t[titleIdx+1].end - t[titleIdx+1].start);
                
                translate_json_register_delayed_object(itemsIdx+1, delayedObjects);
                
                type = title2;
                isObject = true;
              }
            }
            
            idx = translate_json_get_next_sibling(t, idx, count);
            
            
            code = sdscatprintf(code, "  let array:Array[%s] = Array[%s]\n\n", type, type);
            
            
            code = sdscatprintf(code, "  new empty() =>\n");
            code = sdscatprintf(code, "    None\n");
            
            code = sdscatprintf(code, "  new fromString(jsonString:String val)? =>\n");
            code = sdscatprintf(code, "    let doc: JsonDoc val = recover val JsonDoc.>parse(jsonString)? end\n");
            code = sdscatprintf(code, "    let obj = doc.data as JsonArray val\n");
            code = sdscatprintf(code, "    _read(obj)?\n");
            
            code = sdscatprintf(code, "  new fromArray(jsonArray:Array[U8] val)? =>\n");
            code = sdscatprintf(code, "    let doc: JsonDoc val = recover val JsonDoc.>parse(String.from_array(jsonArray))? end\n");
            code = sdscatprintf(code, "    let obj = doc.data as JsonArray val\n");
            code = sdscatprintf(code, "    _read(obj)?\n");
            
            code = sdscatprintf(code, "  new fromJson(arr:JsonArray val)? =>\n");
            code = sdscatprintf(code, "    _read(arr)?\n");
            
            code = sdscatprintf(code, "  fun ref _read(arr:JsonArray val)? =>\n");
            code = sdscatprintf(code, "    for item in arr.data.values() do\n");
            if (isObject) {
              code = sdscatprintf(code, "      array.push(%s.fromJson(item as JsonObject val)?)\n", type);
            } else {
              code = sdscatprintf(code, "      array.push(item as %s)\n", type);
            }
            code = sdscatprintf(code, "    end\n");
            
            code = sdscatprintf(code, "  fun appendEscapedString(json':String iso, buf: String):String iso^ =>\n");
            code = sdscatprintf(code, "    var json = consume json'\n");
            code = sdscatprintf(code, "    for c in buf.values() do\n");
            code = sdscatprintf(code, "      match c\n");
            code = sdscatprintf(code, "      | 34 => json.push('\\\\'); json.push('\"')\n");
            code = sdscatprintf(code, "      | 92 => json.push('\\\\'); json.push('\\\\')\n");
            code = sdscatprintf(code, "      | '\\b' => json.push('\\\\'); json.push('b')\n");
            code = sdscatprintf(code, "      | '\\f' => json.push('\\\\'); json.push('f')\n");
            code = sdscatprintf(code, "      | '\\t' => json.push('\\\\'); json.push('t')\n");
            code = sdscatprintf(code, "      | '\\r' => json.push('\\\\'); json.push('r')\n");
            code = sdscatprintf(code, "      | '\\n' => json.push('\\\\'); json.push('n')\n");
            code = sdscatprintf(code, "      else json.push(c) end\n");
            code = sdscatprintf(code, "    end\n");
            code = sdscatprintf(code, "    consume json\n");
            
            
            
            code = sdscatprintf(code, "  fun ref apply(i: USize):%s ? =>\n", type);
            code = sdscatprintf(code, "    array(i)?\n\n");
            code = sdscatprintf(code, "  fun values():ArrayValues[%s, this->Array[%s]]^ =>\n", type, type);
            code = sdscatprintf(code, "    array.values()\n\n");
            code = sdscatprintf(code, "  fun size():USize =>\n");
            code = sdscatprintf(code, "    array.size()\n\n");
            code = sdscatprintf(code, "  fun ref push(value:%s) =>\n", type);
            code = sdscatprintf(code, "    array.push(value)\n\n");
            code = sdscatprintf(code, "  fun ref pop():%s^ ? =>\n", type);
            code = sdscatprintf(code, "    array.pop()?\n\n");
            code = sdscatprintf(code, "  fun string(): String iso^ =>\n");
            code = sdscatprintf(code, "    appendJson(recover String(1024) end)\n");
            code = sdscatprintf(code, "  fun appendJson(json':String iso):String iso^ =>\n");
            code = sdscatprintf(code, "    var json = consume json'\n");
            code = sdscatprintf(code, "    json.push('[')\n");
            code = sdscatprintf(code, "    for item in array.values() do\n");
            if (!isObject) {
              code = sdscatprintf(code, "      json.push('\"')\n");
              code = sdscatprintf(code, "      json = appendEscapedString(consume json, item.string())\n");
              code = sdscatprintf(code, "      json.push('\"')\n");
            } else {
              code = sdscatprintf(code, "      json = item.appendJson(consume json)\n");
            }
            code = sdscatprintf(code, "      json.push(',')\n");
            code = sdscatprintf(code, "    end\n");
            code = sdscatprintf(code, "    if array.size() > 0 then try json.pop()? end end\n");
            code = sdscatprintf(code, "    json.push(']')\n");
            code = sdscatprintf(code, "    consume json\n");
            
            code = sdscatprintf(code, "\n");
            
            code = sdscatprintf(code, "  fun ref self():%s => this\n", title);
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
          
          code = sdscatprintf(code, "primitive %sParse\n", title);
          code = sdscatprintf(code, "  fun apply(from:(String val|Array[U8] val)):(%s|None) =>\n", title);
          code = sdscatprintf(code, "    match from\n");
          code = sdscatprintf(code, "    | let string:String val => try %s.fromString(string)? else None end\n", title);
          code = sdscatprintf(code, "    | let array:Array[U8] val => try %s.fromArray(array)? else None end\n", title);
          code = sdscatprintf(code, "    end\n");
          
          code = sdscatprintf(code, "class %s", title);
          
          if(pony_traits != NULL) {
            code = sdscatprintf(code, " is %s", pony_traits);
          }
          
          code = sdscatprintf(code, "\n");
          
          // add all of the properties for this type
          idx++;
          
          if (strcmp(type, "object") == 0) {
            idx++;
            
            size_t objectIdx = idx;
            while(idx < count) {
              code = translate_json_add_property(code, js, t, idx, count, delayedObjects);
              idx = translate_json_get_next_sibling(t, idx, count);
            }
            
            code = translate_json_add_create_constructor(code, js, t, objectIdx, count);
            code = translate_json_add_empty_constructor(code, js, t, objectIdx, count);
            code = translate_json_add_read_constructor(code, js, t, objectIdx, count);
            code = translate_json_add_append_json(code, js, t, objectIdx, count);
            code = translate_json_add_function_propery(code, title, js, t, objectIdx, count);
            
            code = sdscatprintf(code, "  fun ref self():%s => this\n", title);
            code = sdscatprintf(code, "  fun clone():%s iso^ ? => recover iso %s.fromString(string())? end\n", title, title);
            code = sdscatprintf(code, "\n");
            
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


char* translate_json_schema(bool print_generated_code, const char* file_name, const char* source_code)
{
  ((void)file_name);
  
  // it is our responsibility to free the old "source code" which was provided
  unsigned long in_source_code_length = strlen(source_code)+1;
    
  // use the sds library to concat our pony code together, then copy it to a pony allocated buffer
  sds code = sdsnew("");
    
  // each json schema file is a class with the name of the class matching the file name
  jsmn_parser parser;
  jsmntok_t tokens[MAX_TOKENS];
  size_t delayedObjects[MAX_DELAYED_OBJECTS] = {0}; 

  jsmn_init(&parser);
  int num = jsmn_parse(&parser, source_code, strlen(source_code), tokens, sizeof(tokens) / sizeof(tokens[0]));

  code = translate_json_add_use(code, source_code, tokens, 0, parser.toknext);
  code = translate_json_add_global_interface(code);
  
  /* Assume the top-level element is an object */
  if (num >= 1) {
    code = translate_json_add_object(code, source_code, tokens, 0, parser.toknext, delayedObjects);
    
    for(int i = 0; i < MAX_DELAYED_OBJECTS; i++) {
      if (delayedObjects[i] != 0) {
        code = translate_json_add_object(code, source_code, tokens, delayedObjects[i], parser.toknext, delayedObjects);
      }
    }
  } else {
    code = translate_json_abort(code, "unable to parse json schema (it may be too large, see MAX_TOKENS)");
  }
  
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

