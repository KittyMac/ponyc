#include "translate_json_schema.h"
#include "translate_text_resource.h"
#include "translate_c_header.h"
#include "../../libponyrt/gc/serialise.h"
#include "../../libponyrt/mem/pool.h"
#include "../pkg/program.h"
#include "../ast/ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define PONY_EXTENSION ".pony"
#define JSON_SCHEMA_EXTENSION ".schema.json"

// text files which get converted into code
#define MARKDOWN_EXTENSION ".md"
#define JSON_EXTENSION ".json"
#define TEXT_EXTENSION ".txt"
#define INFO_PLIST_EXTENSION "Info.plist"
#define C_HEADER_EXTENSION ".h"

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
  if( string_ends_with(file_name, PONY_EXTENSION) ||
    
      string_ends_with(file_name, MARKDOWN_EXTENSION) ||
      string_ends_with(file_name, JSON_EXTENSION) ||
      string_ends_with(file_name, TEXT_EXTENSION) ||
      
      string_ends_with(file_name, JSON_SCHEMA_EXTENSION) ||
      
      string_ends_with(file_name, INFO_PLIST_EXTENSION) ||
      
      string_ends_with(file_name, C_HEADER_EXTENSION)
    )
  {
    return true;
  }
  return false;
}

char* translate_source(program_t* program, const char* file_name, const char* source_code, bool print_generated_code)
{
  if(string_ends_with(file_name, INFO_PLIST_EXTENSION))
  {
    // For Info.plist, we should transpile the code to a primitive with values to access. For now we're just going
    // to provide nothing, because what we really want is to link in the plist file
		ponyint_pool_free_size(strlen(source_code)+1, (void *)source_code);
  	char * empty_code = (char*)ponyint_pool_alloc_size(1);
  	empty_code[0] = 0;
    
    // Flag the program to link to use the -sectcreate __TEXT __info_plist /path/to/Info.plist    
    program_assign_info_plist(program, strdup(file_name));
    
    return empty_code;
  }
  else if(string_ends_with(file_name, C_HEADER_EXTENSION))
  {
    return translate_c_header(print_generated_code, file_name, source_code);
  }
  else if(string_ends_with(file_name, JSON_SCHEMA_EXTENSION))
  {
    return translate_json_schema(print_generated_code, file_name, source_code);
  }
  else if(string_ends_with(file_name, MARKDOWN_EXTENSION))
  {
    return translate_text_resource(print_generated_code, file_name, "Markdown", source_code);
  }
  else if(string_ends_with(file_name, JSON_EXTENSION))
  {
    return translate_text_resource(print_generated_code, file_name, "Json", source_code);
  }
  else if(string_ends_with(file_name, TEXT_EXTENSION))
  {
    return translate_text_resource(print_generated_code, file_name, "Text", source_code);
  }
  return (char *)source_code;
}


void translate_source_package_begin(const char * qualified_name) {
  translate_text_resource_package_begin(qualified_name);
  translate_c_header_package_begin(qualified_name);
}

char* translate_source_package_end(bool print_generated_code) {
  // All transpilers who need to add whole package code now have the opportunity to do that. But it all needs to be
  // in a single source file, so we handle giving them the sdsstring from here..
  
  sds code = sdsnew("");
  
  // the resource transpilier uses the package end call to create a class with LUT to the resources compiled in.
  code = translate_text_resource_package_end(code);
  code = translate_c_header_package_end(code);
  
  
  // copy the code over to pony allocated memory
  size_t code_len = sdslen(code);
  char * pony_code = (char*)ponyint_pool_alloc_size(code_len + 1);
  strncpy(pony_code, code, code_len);
  pony_code[code_len] = 0;
  sdsfree(code);
  
  if (code_len > 0 && print_generated_code) {
    fprintf(stderr, "========================== autogenerated pony code ==========================\n");
    fprintf(stderr, "%s", pony_code);
    fprintf(stderr, "=============================================================================\n");
  }
  
  return pony_code;
}




// helper functions shared by future translation classes

const char * translate_clean_class_name_conflict(const char * name) {
  // if our json keys match pony keywords we will get compile errors, so we find those
  // and deal with it somehow...
  if (!strcmp(name, "Actor")) { return "ActorPony"; }
  if (!strcmp(name, "Addressof")) { return "AddressofPony"; }
  if (!strcmp(name, "And")) { return "AndPony"; }
  if (!strcmp(name, "As")) { return "AsPony"; }
  if (!strcmp(name, "Be")) { return "BePony"; }
  if (!strcmp(name, "Break")) { return "BreakPony"; }
  if (!strcmp(name, "Class")) { return "ClassPony"; }
  if (!strcmp(name, "Compile_error")) { return "Compile_errorPony"; }
  if (!strcmp(name, "Compile_intrinsic")) { return "Compile_intrinsicPony"; }
  
  if (!strcmp(name, "Consume")) { return "ConsumePony"; }
  if (!strcmp(name, "Continue")) { return "ContinuePony"; }
  if (!strcmp(name, "Delegate")) { return "DelegatePony"; }
  if (!strcmp(name, "Digestof")) { return "DigestofPony"; }
  if (!strcmp(name, "Do")) { return "DoPony"; }
  if (!strcmp(name, "Else")) { return "ElsePony"; }
  if (!strcmp(name, "Elseif")) { return "ElseifPony"; }
  if (!strcmp(name, "Embed")) { return "EmbedPony"; }
  if (!strcmp(name, "End")) { return "EndPony"; }
  if (!strcmp(name, "Error")) { return "ErrorPony"; }
  
  if (!strcmp(name, "Object")) { return "ObjectPony"; }
  
  if (!strcmp(name, "For")) { return "ForPony"; }
  if (!strcmp(name, "Fun")) { return "FunPony"; }
  if (!strcmp(name, "If")) { return "IfPony"; }
  if (!strcmp(name, "Ifdef")) { return "IfdefPony"; }
  if (!strcmp(name, "In")) { return "InPony"; }
  if (!strcmp(name, "Interface")) { return "InterfacePony"; }
  if (!strcmp(name, "Is")) { return "IsPony"; }
  if (!strcmp(name, "Isnt")) { return "IsntPony"; }
  if (!strcmp(name, "Lambda")) { return "LambdaPony"; }
  if (!strcmp(name, "Let")) { return "LetPony"; }
  if (!strcmp(name, "Match")) { return "MatchPony"; }
  if (!strcmp(name, "New")) { return "NewPony"; }
  if (!strcmp(name, "Not")) { return "NotPony"; }
  if (!strcmp(name, "Object")) { return "ObjectPony"; }
  
  if (!strcmp(name, "Primitive")) { return "PrimitivePony"; }
  if (!strcmp(name, "Recover")) { return "RecoverPony"; }
  if (!strcmp(name, "Repeat")) { return "RepeatPony"; }
  if (!strcmp(name, "Return")) { return "ReturnPony"; }
  if (!strcmp(name, "Struct")) { return "StructPony"; }
  if (!strcmp(name, "Then")) { return "ThenPony"; }
  if (!strcmp(name, "Trait")) { return "TraitPony"; }
  if (!strcmp(name, "Try")) { return "TryPony"; }
  if (!strcmp(name, "Type")) { return "TypePony"; }
  if (!strcmp(name, "Until")) { return "UntilPony"; }
  
  if (!strcmp(name, "Use")) { return "UsePony"; }
  if (!strcmp(name, "Var")) { return "VarPony"; }
  if (!strcmp(name, "Where")) { return "WherePony"; }
  if (!strcmp(name, "While")) { return "WhilePony"; }
  if (!strcmp(name, "With")) { return "WithPony"; }
  if (!strcmp(name, "Xor")) { return "XorPony"; }
  
  if (!strcmp(name, "Iso")) { return "IsoPony"; }
  if (!strcmp(name, "Val")) { return "ValPony"; }
  if (!strcmp(name, "Tag")) { return "TagPony"; }
  if (!strcmp(name, "Trn")) { return "TrnPony"; }
  if (!strcmp(name, "Box")) { return "BoxPony"; }
  if (!strcmp(name, "Ref")) { return "RefPony"; }
  
  return name;
}


const char* translate_class_name(const char* name, bool makePrivate)
{
  // take a file name and turn it into a pony class name
  const char * start = strrchr(name, '/');
  if (start == NULL) {
    start = name;
  } else {
    start += 1;
  }
  const char * end = strchr(start, '.');
  if (end == NULL) {
    end = name + strlen(name);
  }
  
  char* class_name = (char*)ponyint_pool_alloc_size((end - start) + 1);
  
  int idx = 0;
  bool uppercase_next = true;
  int uppercase_count = 0;
  
  // skip leading "_"
  for (; start < end; start++) {
    if(*start != '_') {
      break;
    }
  }
  
  if(makePrivate) {
    class_name[idx++] = '_';
  }
  
  for (; start < end; start++) {
    if (uppercase_next) {
      uppercase_count++;
      class_name[idx++] = (char)toupper(*start);
      if(isalpha(*start)){
        uppercase_next = false;
      }
      continue;
    }
    if (isspace(*start) == true || *start == '_') {
      uppercase_next = true;
      continue;
    }
    
    if(isupper(*start)) {
      uppercase_count++;
    }else{
      uppercase_count = -1;
    }
    
    if(uppercase_count == -1 && isupper(*start)) {
      class_name[idx++] = (char)tolower(*start);
    }else{
      uppercase_count = 0;
      class_name[idx++] = *start;
    }
    
  }
  class_name[idx] = 0;
  
  return translate_clean_class_name_conflict(class_name);
}




const char * translate_clean_function_name_conflict(const char * name) {
  // if our json keys match pony keywords we will get compile errors, so we find those
  // and deal with it somehow...
  if (!strcmp(name, "actor")) { return "actor_pony"; }
  if (!strcmp(name, "addressof")) { return "addressof_pony"; }
  if (!strcmp(name, "and")) { return "and_pony"; }
  if (!strcmp(name, "as")) { return "as_pony"; }
  if (!strcmp(name, "be")) { return "be_pony"; }
  if (!strcmp(name, "break")) { return "break_pony"; }
  if (!strcmp(name, "class")) { return "class_pony"; }
  if (!strcmp(name, "compile_error")) { return "compile_error_pony"; }
  if (!strcmp(name, "compile_intrinsic")) { return "compile_intrinsic_pony"; }
  
  if (!strcmp(name, "consume")) { return "consume_pony"; }
  if (!strcmp(name, "continue")) { return "continue_pony"; }
  if (!strcmp(name, "delegate")) { return "delegate_pony"; }
  if (!strcmp(name, "digestof")) { return "digestof_pony"; }
  if (!strcmp(name, "do")) { return "do_pony"; }
  if (!strcmp(name, "else")) { return "else_pony"; }
  if (!strcmp(name, "elseif")) { return "elseif_pony"; }
  if (!strcmp(name, "embed")) { return "embed_pony"; }
  if (!strcmp(name, "end")) { return "end_pony"; }
  if (!strcmp(name, "error")) { return "error_pony"; }
  
  if (!strcmp(name, "object")) { return "object_pony"; }
  
  if (!strcmp(name, "for")) { return "for_pony"; }
  if (!strcmp(name, "fun")) { return "fun_pony"; }
  if (!strcmp(name, "if")) { return "if_pony"; }
  if (!strcmp(name, "ifdef")) { return "ifdef_pony"; }
  if (!strcmp(name, "in")) { return "in_pony"; }
  if (!strcmp(name, "interface")) { return "interface_pony"; }
  if (!strcmp(name, "is")) { return "is_pony"; }
  if (!strcmp(name, "isnt")) { return "isnt_pony"; }
  if (!strcmp(name, "lambda")) { return "lambda_pony"; }
  if (!strcmp(name, "let")) { return "let_pony"; }
  if (!strcmp(name, "match")) { return "match_pony"; }
  if (!strcmp(name, "new")) { return "new_pony"; }
  if (!strcmp(name, "not")) { return "not_pony"; }
  if (!strcmp(name, "object")) { return "object_pony"; }
  
  if (!strcmp(name, "primitive")) { return "primitive_pony"; }
  if (!strcmp(name, "recover")) { return "recover_pony"; }
  if (!strcmp(name, "repeat")) { return "repeat_pony"; }
  if (!strcmp(name, "return")) { return "return_pony"; }
  if (!strcmp(name, "struct")) { return "struct_pony"; }
  if (!strcmp(name, "then")) { return "then_pony"; }
  if (!strcmp(name, "trait")) { return "trait_pony"; }
  if (!strcmp(name, "try")) { return "try_pony"; }
  if (!strcmp(name, "type")) { return "type_pony"; }
  if (!strcmp(name, "until")) { return "until_pony"; }
  
  if (!strcmp(name, "use")) { return "use_pony"; }
  if (!strcmp(name, "var")) { return "var_pony"; }
  if (!strcmp(name, "where")) { return "where_pony"; }
  if (!strcmp(name, "while")) { return "while_pony"; }
  if (!strcmp(name, "with")) { return "with_pony"; }
  if (!strcmp(name, "xor")) { return "xor_pony"; }
  
  if (!strcmp(name, "iso")) { return "iso_pony"; }
  if (!strcmp(name, "val")) { return "val_pony"; }
  if (!strcmp(name, "tag")) { return "tag_pony"; }
  if (!strcmp(name, "trn")) { return "trn_pony"; }
  if (!strcmp(name, "box")) { return "box_pony"; }
  if (!strcmp(name, "ref")) { return "ref_pony"; }
  
  return name;
}

const char* translate_function_name(const char* name)
{
  // take a file name and turn it into a pony class name
  const char * start = strrchr(name, '/');
  if (start == NULL) {
    start = name;
  } else {
    start += 1;
  }
  const char * end = strchr(start, '.');
  if (end == NULL) {
    end = name + strlen(name);
  }
  
  char* class_name = (char*)ponyint_pool_alloc_size((end - start) + 1);
  
  int idx = 0;
  for (; start < end; start++) {
    if (isspace(*start) == true) {
      continue;
    }
    class_name[idx++] = (char)tolower(*start);
  }
  class_name[idx] = 0;
  
  // function names may not have a trailing _
  if(class_name[idx-1] == '_') {
    class_name[idx-1] = 0;
  }
  
  return translate_clean_function_name_conflict(class_name);
}

void translate_free_name(const char* name)
{
  ponyint_pool_free_size(strlen(name)+1, (void *)name);
}
