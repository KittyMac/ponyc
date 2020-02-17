#include "../../libponyrt/gc/serialise.h"
#include "../../libponyrt/mem/pool.h"
#include "translate_source.h"
#include "sds/sds.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <clang-c/Index.h>

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })


typedef struct {
  sds * code;
  CXCursor previous;
}VisitorData;

static bool ponyTypeForStructIsPointer(CXCursor cursor);
static int unknownThingCounter = 0;

extern int string_ends_with(const char *str, const char *suffix);

// Note: useful commands
// clang -Xclang -ast-dump -fsyntax-only somefile.h

sds translate_c_header_abort(sds code, char * error) {
  return sdscatprintf(code, "C header parser failed: %s\n", error);
}

void addPonyTypeForCXType(CXType t, sds * code) {
  //CXString name = clang_getTypeKindSpelling(t.kind);
  //CXString name2 = clang_getCursorSpelling(clang_getTypeDeclaration(t));
  //fprintf(stderr, ">> CXType.kind is %d -- %s  -- %s\n", t.kind, clang_getCString(name), clang_getCString(name2));
  
  switch(t.kind) {
    case CXType_Void:                 ( *code = sdscatprintf(*code, "None") );    break;
    case CXType_Bool:                 ( *code = sdscatprintf(*code, "Bool") );    break;
    
    case CXType_Char_S:               
    case CXType_Char_U:               
    case CXType_UChar:                ( *code = sdscatprintf(*code, "U8") );    break;
                                    
    case CXType_Char16:               
    case CXType_UShort:               ( *code = sdscatprintf(*code, "U16") );    break;
                                    
    case CXType_Char32:               
    case CXType_UInt:                 ( *code = sdscatprintf(*code, "U32") );    break;
    case CXType_ULong:                ( *code = sdscatprintf(*code, "U64") );    break;
                                    
    case CXType_ULongLong:            
    case CXType_UInt128:              ( *code = sdscatprintf(*code, "U128") );    break;
                                    
                                    
    case CXType_SChar:                ( *code = sdscatprintf(*code, "I8") );    break;
    case CXType_WChar:                
    case CXType_Short:                ( *code = sdscatprintf(*code, "I16") );    break;
    case CXType_Int:                  ( *code = sdscatprintf(*code, "I32") );    break;
    case CXType_Long:                 ( *code = sdscatprintf(*code, "I64") );    break;
                                    
    case CXType_LongLong:             
    case CXType_Int128:               ( *code = sdscatprintf(*code, "I128") );    break;
                                    
    case CXType_Half:               
    case CXType_Float16:              
    case CXType_Float:                ( *code = sdscatprintf(*code, "F32") );    break;
    case CXType_Double:               ( *code = sdscatprintf(*code, "F64") );    break;
    case CXType_Float128:           
    case CXType_LongDouble:           ( *code = sdscatprintf(*code, "F128") );    break;
    
    case CXType_Enum:                 {
                                        addPonyTypeForCXType(clang_getEnumDeclIntegerType(clang_getTypeDeclaration(t)), code);
                                      }
                                      break;
    case CXType_Typedef:              {
                                        addPonyTypeForCXType(clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(t)), code);
                                      }
                                      break;
    case CXType_Elaborated:           {
                                        CXType elaboratedType = clang_Type_getNamedType(t);
                                        CXString elaboratedName = clang_getTypeSpelling(elaboratedType);
                                        const char * elaboratedNameString = clang_getCString(elaboratedName);
                                        if(!strncmp("struct ", elaboratedNameString, 7)) {
                                          *code = sdscatprintf(*code, "%s", translate_class_name(elaboratedNameString + 7));
                                        }else if(!strncmp("enum ", elaboratedNameString, 5) || elaboratedType.kind) {
                                          // We want the type of the enum, not the name...
                                          addPonyTypeForCXType(clang_Type_getNamedType(t), code);
                                        }else{
                                          *code = sdscatprintf(*code, "%s", translate_class_name(elaboratedNameString));
                                        }
                                        clang_disposeString(elaboratedName);
                                      }
                                      break;
    
    case CXType_ObjCId:
    case CXType_ObjCClass:
    case CXType_ObjCSel:
    case CXType_ObjCObjectPointer:
    case CXType_Pointer:
    case CXType_NullPtr:              {
                                        // If the pointee is elaborated or typedef, we don't use Pointer
                                        CXType pointeeType = clang_getPointeeType(t);
                                        if(pointeeType.kind == CXType_Elaborated || pointeeType.kind == CXType_Typedef) {
                                          bool shouldBePointer = ponyTypeForStructIsPointer(clang_getTypeDeclaration(pointeeType));
                                          if(shouldBePointer) { *code = sdscatprintf(*code, "Pointer["); }
                                          addPonyTypeForCXType(pointeeType, code);
                                          if(shouldBePointer) { *code = sdscatprintf(*code, "] tag"); }
                                        }else{
                                          *code = sdscatprintf(*code, "Pointer[");
                                          addPonyTypeForCXType(pointeeType, code);
                                          *code = sdscatprintf(*code, "] tag");
                                        }
                                      }
                                      break;
    
    case CXType_IncompleteArray:      ( *code = sdscatprintf(*code, "...") );    break;
    case CXType_DependentSizedArray:  ( *code = sdscatprintf(*code, "...") );    break;
    case CXType_VariableArray:        ( *code = sdscatprintf(*code, "...") );    break;
    
    default:                          ( *code = sdscatprintf(*code, "None") );    break;
  }
}

void addPonyDefaultValueForCXType(CXType t, sds * code) {
  //fprintf(stderr, ">> CXType.kind is %d\n", t.kind);
  
  switch(t.kind) {
    case CXType_Void:                 ( *code = sdscatprintf(*code, "None") );    break;
    case CXType_Bool:                 ( *code = sdscatprintf(*code, "false") );    break;
    
    case CXType_Char_S:               
    case CXType_Char_U:               
    case CXType_UChar:              
    case CXType_Char16:               
    case CXType_UShort:             
    case CXType_Enum:               
    case CXType_Char32:               
    case CXType_UInt:
    case CXType_ULong:              
    case CXType_ULongLong:            
    case CXType_UInt128:            
    case CXType_SChar:
    case CXType_WChar:                
    case CXType_Short:
    case CXType_Int:
    case CXType_Long:               
    case CXType_LongLong:             
    case CXType_Int128:               ( *code = sdscatprintf(*code, "0") );    break;
                                    
    case CXType_Half:               
    case CXType_Float16:              
    case CXType_Float:
    case CXType_Double:
    case CXType_Float128:           
    case CXType_LongDouble:           ( *code = sdscatprintf(*code, "0.0") );    break;
  
    case CXType_Elaborated:           {
                                        CXString name = clang_getTypeSpelling(clang_Type_getNamedType(t));
                                        const char * nameString = clang_getCString(name);
                                        if(!strncmp("struct ", nameString, 7)) {
                                          *code = sdscatprintf(*code, "%s", translate_class_name(nameString + 7));
                                        }else{
                                          *code = sdscatprintf(*code, "%s", translate_class_name(nameString));
                                        }
                                        clang_disposeString(name);
                                      }
                                      break;
    
    case CXType_ObjCId:
    case CXType_ObjCClass:
    case CXType_ObjCSel:
    case CXType_ObjCObjectPointer:
    case CXType_Pointer:
    case CXType_NullPtr:              {
                                        // Note: we don't want "tag" at the end
                                        addPonyTypeForCXType(t, code);
                                        if(string_ends_with(*code, " tag")) {
                                          sdssetlen(*code, sdslen(*code)-4);
                                        }
                                        
                                      }
                                      break;
        
    default:                          ( *code = sdscatprintf(*code, "None") );    break;
  }
}








bool compareTokenName(CXTranslationUnit unit, CXToken token, const char * otherNameString) {
  CXString name = clang_getTokenSpelling(unit, token);
  const char * nameString = clang_getCString(name);
  bool didMatch = !strcmp(nameString, otherNameString);
  clang_disposeString(name);
  return didMatch;
}

void copyTokenName(CXTranslationUnit unit, CXToken token, char * buffer, int bufferSize) {
  CXString name = clang_getTokenSpelling(unit, token);
  const char * nameString = clang_getCString(name);
  strncpy(buffer, nameString, bufferSize);
  clang_disposeString(name);
}

unsigned long identifyTokenRootName(CXTranslationUnit unit, CXToken token, char * buffer) {
  CXString name = clang_getTokenSpelling(unit, token);
  const char * nameString = clang_getCString(name);
  
  unsigned long minSize = min(strlen(nameString), strlen(buffer));
  for(unsigned long i = 0; i < minSize; i++) {
    if(tolower(nameString[i]) != tolower(buffer[i])) {
      clang_disposeString(name);
      return i;
    }
  }
  
  clang_disposeString(name);
  return minSize;
}

void printNumericDefinitions(CXTranslationUnit unit, sds * code) {
  
  CXToken * allTokens = NULL;
  unsigned numTokens = 0;
  
  CXCursor unitCursor = clang_getTranslationUnitCursor(unit);
  clang_tokenize(unit, clang_getCursorExtent(unitCursor), &allTokens, &numTokens);
  
  /*
  for(unsigned i = 0; i < numTokens-1; i++) {
    CXToken token = allTokens[i];
    
    CXString name = clang_getTokenSpelling(unit, token);
    const char * nameString = clang_getCString(name);
    CXTokenKind kind = clang_getTokenKind(token);
    
    fprintf(stderr, ">> [%d] %s\n", kind, nameString);
    
    clang_disposeString(name);
  }*/
  
  for(unsigned i = 0; i < numTokens-1; i++) {                
    // How many are in this group?  Do they have a common name root?
    int sizeOfDefineGrouping = 0;
    char primitiveName[1024] = {0};
    unsigned long primitiveRootIndex = 0;
    
    for(unsigned j = i; j < numTokens; j++) {
      // skip any comments
      if (clang_getTokenKind(allTokens[j]) == CXToken_Comment){
        continue;
      }else if(compareTokenName(unit, allTokens[j], "#") && compareTokenName(unit, allTokens[j+1], "define") && j+3 < numTokens) {
        CXToken defineNameToken = allTokens[j + 2];
        CXTokenKind defineNameKind = clang_getTokenKind(defineNameToken);
        CXToken defineValueToken = allTokens[j + 3];
        CXTokenKind defineValueKind = clang_getTokenKind(defineValueToken);
        
        // Needs to be a define which equates to a literal
        if (defineNameKind == CXToken_Identifier && defineValueKind == CXToken_Literal) {
          // does this define share a root name with the previous one? If it doesn't, it belongs in a new group
          if (sizeOfDefineGrouping == 0) {
            copyTokenName(unit, allTokens[j+2], primitiveName, sizeof(primitiveName));
          }else{
            unsigned long newRootIndex = identifyTokenRootName(unit, allTokens[j+2], primitiveName);
            if(primitiveRootIndex != 0 && newRootIndex == 0) {
              break;
            }
            primitiveRootIndex = newRootIndex;
          
            if(primitiveRootIndex != 0) {
              primitiveName[primitiveRootIndex] = 0;
            }
          }
          sizeOfDefineGrouping++;
        }else{
          break;
        }
        
        j += 3;
      }else{
        break;
      }
    }
    
    if (sizeOfDefineGrouping >= 1) {
      
      *code = sdscatprintf(*code, "primitive %s\n", translate_class_name(primitiveName));
      
      for(unsigned j = i; j < numTokens; j++) {
        if (clang_getTokenKind(allTokens[j]) == CXToken_Comment){
          continue;
        }
        
        if(compareTokenName(unit, allTokens[j], "#") && compareTokenName(unit, allTokens[j+1], "define") && j+3 < numTokens) {
          CXToken defineNameToken = allTokens[j + 2];
          CXTokenKind defineNameKind = clang_getTokenKind(defineNameToken);
          CXToken defineValueToken = allTokens[j + 3];
          CXTokenKind defineValueKind = clang_getTokenKind(defineValueToken);
  
          if (defineNameKind == CXToken_Identifier && defineValueKind == CXToken_Literal) {
            CXString defineNameName = clang_getTokenSpelling(unit, defineNameToken);
            const char * defineNameNameString = clang_getCString(defineNameName);
            CXString defineValueName = clang_getTokenSpelling(unit, defineValueToken);
            const char * defineValueNameString = clang_getCString(defineValueName);
      
            const char * cleanedName = translate_function_name(defineNameNameString + primitiveRootIndex);
            
            if(defineValueNameString[0] == '\"') {
              *code = sdscatprintf(*code, "  fun %s():%s => %s\n", cleanedName, "String", defineValueNameString);
            }else{
              *code = sdscatprintf(*code, "  fun %s():%s => %s\n", cleanedName, "U32", defineValueNameString);
            }
      
          }
          
          j += 3;
          i = j;
          
          sizeOfDefineGrouping--;
          if (sizeOfDefineGrouping == 0){
            break;
          }
        }
      }
      
      *code = sdscatprintf(*code, "\n");
    }        
  }  
}


enum CXChildVisitResult printFunctionDeclarations(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void)parent);
  int childVisit = CXChildVisit_Recurse;
  
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  
  //fprintf(stderr, ">> [%d] %s\n", kind, clang_getCString(name));
  
  if (kind == CXCursor_FunctionDecl) {
    *code = sdscatprintf(*code, "use @%s", clang_getCString(name));
    
    CXType resultType = clang_getCursorResultType(cursor);
    *code = sdscatprintf(*code, "[");
    addPonyTypeForCXType(resultType, code);
    *code = sdscatprintf(*code, "]");
    
    // run through all of the arguments
    *code = sdscatprintf(*code, "(");
    int numParams = clang_Cursor_getNumArguments(cursor);
    for(int i = 0; i < numParams; i++){
      CXCursor paramCursor = clang_Cursor_getArgument(cursor, i);
      CXString paramName = clang_getCursorSpelling(paramCursor);
      const char * paramNameString = clang_getCString(paramName);
      CXType paramType = clang_getCursorType(paramCursor);
      
      if (clang_getCursorKind(paramCursor) == CXCursor_ParmDecl) {
        //fprintf(stderr, ">> %s parameter\n", clang_getCString(paramName));
        if(paramNameString[0] == 0) {
          *code = sdscatprintf(*code, "arg%d:", unknownThingCounter++);
        }else{
          *code = sdscatprintf(*code, "%s:", clang_getCString(paramName));
        }
        
        addPonyTypeForCXType(paramType, code);
        if (i < (numParams-1)) {
          *code = sdscatprintf(*code, ", ");
        }
        clang_disposeString(paramName);
      } else {
        fprintf(stderr, "<unhandled function parameter> [%d] %s\n", clang_getCursorKind(paramCursor), clang_getCString(paramName));
      }
    }
    
    // is this a variadic function?
    if(clang_isFunctionTypeVariadic (type)) {
      *code = sdscatprintf(*code, ", ...");
    }
    
    *code = sdscatprintf(*code, ")\n");
    
    childVisit = CXChildVisit_Continue;
  }
  
  clang_disposeString(name);
  
  return childVisit;
}



enum CXVisitorResult printStructFields(CXCursor cursor, CXClientData client_data) {
  sds * code = (sds *) client_data;
  
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  
  if (kind == CXCursor_FieldDecl) {
    
    if (type.kind == CXType_Elaborated) {
      *code = sdscatprintf(*code, "  embed %s: ", clang_getCString(name));
    }else{
      *code = sdscatprintf(*code, "  var %s: ", clang_getCString(name));
    }
    
    addPonyTypeForCXType(type, code);
    *code = sdscatprintf(*code, " = ");
    addPonyDefaultValueForCXType(type, code);
    
    
    *code = sdscatprintf(*code, "\n");
  }
  
  return CXVisit_Continue;
}

enum CXVisitorResult countStructFields(CXCursor cursor, CXClientData client_data) {
  int * count = (int *)client_data;
  if (clang_getCursorKind(cursor) == CXCursor_FieldDecl) {
    *count += 1;
  }
  return CXVisit_Continue;
}

char * ponyTypeForStruct(CXCursor cursor) {
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  
  int countOfFields = 0;
  if (kind == CXCursor_StructDecl && type.kind == CXType_Record) {
    clang_Type_visitFields(type, countStructFields, &countOfFields);
  }
  if(countOfFields == 0) {
    return "primitive";
  }
  return "struct";
}

bool ponyTypeForStructIsPointer(CXCursor cursor) {
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  
  int countOfFields = 0;
  if (kind == CXCursor_StructDecl && type.kind == CXType_Record) {
    clang_Type_visitFields(type, countStructFields, &countOfFields);
  }
  return (countOfFields == 0);
}

enum CXChildVisitResult printStructDeclarations(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void)parent);
  int childVisit = CXChildVisit_Recurse;
    
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  const char * nameString = clang_getCString(name);
  
  //int parentKind = clang_getCursorKind(parent);
  //CXType parentType = clang_getCursorType(parent);
  CXString parentName = clang_getCursorSpelling(parent);
  //const char * parentNameString = clang_getCString(parentName);
  
  int previousKind = clang_getCursorKind(data->previous);
  CXType previousType = clang_getCursorType(data->previous);
  CXString previousName = clang_getCursorSpelling(data->previous);
  const char * previousNameString = clang_getCString(previousName);
  
  
  // Ok, here's the deal:
  // typedef struct YYY { ... } XXX;
  // In the above, struct with name YYY is encountered first, then typedef with XXX encountered second.  YYY can be missing.
  // Structs don't require typedef, so struct YYY { ... }; is valid.
  if (kind == CXCursor_TypedefDecl) {
    // if we are a typedef and the previous entity was a struct without a name, then we need to make type declaration to it
    if(previousKind == CXCursor_StructDecl && previousType.kind == CXType_Record && previousNameString[0] == 0) {
      *code = sdscatprintf(*code, "type %s is Struct%d\n\n", translate_class_name(nameString), unknownThingCounter-1);
    }
    childVisit = CXChildVisit_Continue;
  }
  
  if (kind == CXCursor_StructDecl && type.kind == CXType_Record) {
    if(nameString[0] == 0) {
       *code = sdscatprintf(*code, "%s Struct%d\n", ponyTypeForStruct(cursor), unknownThingCounter++);
    }else{
      *code = sdscatprintf(*code, "%s %s\n", ponyTypeForStruct(cursor), translate_class_name(nameString));
    }
     
    clang_Type_visitFields(type, printStructFields, code);
    *code = sdscatprintf(*code, "\n");
    childVisit = CXChildVisit_Continue;
  }
  
  //fprintf(stderr, ">> [%d][%d}] %s\n", kind, type.kind, clang_getCString(name));
  
  clang_disposeString(name);
  clang_disposeString(parentName);
  clang_disposeString(previousName);
  
  data->previous = cursor;
  
  return childVisit;
}



enum CXChildVisitResult printEnumDeclarations(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void)parent);
  int childVisit = CXChildVisit_Recurse;
  
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  const char * nameString = clang_getCString(name);
  
  CXString parentName = clang_getCursorSpelling(parent);
  const char * parentNameString = clang_getCString(parentName);
  
  // TODO: strip the root name from enums (ie in this case, strip curlinfo_ )
  // fun curlinfo_none():U32 => 0x0
  // fun curlinfo_effective_url():U32 => 0x100001
  // fun curlinfo_response_code():U32 => 0x200002
  // fun curlinfo_total_time():U32 => 0x300003

  
  if (kind == CXCursor_EnumDecl && type.kind == CXType_Enum) {
    if(clang_getCursorKind(parent) == CXCursor_TypedefDecl) {
      *code = sdscatprintf(*code, "primitive %sEnum\n", translate_class_name(parentNameString));
    }else{
      if(nameString[0] == 0) {
        clang_disposeString(parentName);
        clang_disposeString(name);
        return CXChildVisit_Continue;
      }
      *code = sdscatprintf(*code, "primitive %sEnum\n", translate_class_name(nameString));
    }
  }
  if (kind == CXCursor_EnumConstantDecl) {
    *code = sdscatprintf(*code, "  fun %s():U32", translate_function_name(nameString));
    *code = sdscatprintf(*code, " => 0x%llX\n", clang_getEnumConstantDeclUnsignedValue(cursor));
  }
    
  //fprintf(stderr, ">> [%d][%d}] %s\n", kind, type.kind, clang_getCString(name));
  
  clang_disposeString(parentName);
  clang_disposeString(name);
  
  return childVisit;
}


char* translate_c_header(bool print_generated_code, const char* file_name, const char* source_code)
{
	((void)file_name);
	
	// it is our responsibility to free the old "source code" which was provided
	unsigned long in_source_code_length = strlen(source_code)+1;
  
  unknownThingCounter = 0;
		
	// use the sds library to concat our pony code together, then copy it to a pony allocated buffer
	sds code = sdsnew("");
	
  // Ensure we have common types in our header. do this by writing code to a temp file
  // then appending the source code content (does libclang have in memory reader? not that i found)
  const char * tmpFileName = "/tmp/pony_c_header_transpiler.h";
  FILE * file = fopen(tmpFileName, "w");
  
  fprintf(file, "typedef signed char  int8_t\n");
  fprintf(file, "typedef unsigned char  uint8_t\n");
  fprintf(file, "typedef signed int  int16_t\n");
  fprintf(file, "typedef unsigned int 	uint16_t\n");
  fprintf(file, "typedef signed long int 	int32_t\n");
  fprintf(file, "typedef unsigned long int 	uint32_t\n");
  fprintf(file, "typedef signed long long int 	int64_t\n");
  fprintf(file, "typedef unsigned long long int 	uint64_t\n");
  
  fprintf(file, "%s\n", source_code);
  fclose(file);
  
  // Use libclang to parser the C header and then generate comparable pony FFI code
  CXIndex index = clang_createIndex(0, 0);
  CXTranslationUnit unit = clang_parseTranslationUnit(
        index,
        tmpFileName, NULL, 0,
        NULL, 0,
        CXTranslationUnit_SkipFunctionBodies);
  
  
  if (unit != NULL) {
    CXCursor cursor = clang_getTranslationUnitCursor(unit);
    
    VisitorData data = {0};
    data.code = &code;
    
    // 1. print all of the function declarations ( use @foo[None]() )
    clang_visitChildren(cursor, printFunctionDeclarations, &data);
    
    code = sdscatprintf(code, "\n");
    
    // 2. print all of the stright #defines (primitive SomeDefined\n  fun x():U32 => 2)
    printNumericDefinitions(unit, &code);
    
    code = sdscatprintf(code, "\n");
    
    // 3. transpile all structures
    clang_visitChildren(cursor, printStructDeclarations, &data);
    
    // 3. transpile enumerations
    clang_visitChildren(cursor, printEnumDeclarations, &data);
    
    clang_disposeTranslationUnit(unit);
    
  } else {
    code = translate_c_header_abort(code, "unable to parse C header file");
  }
  
  clang_disposeIndex(index);
  
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



