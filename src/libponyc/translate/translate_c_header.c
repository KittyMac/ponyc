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

// Note: useful commands
// clang -Xclang -ast-dump -fsyntax-only somefile.h

sds translate_c_header_abort(sds code, char * error) {
  return sdscatprintf(code, "C header parser failed: %s\n", error);
}

void addPonyTypeForCXType(CXType t, sds * code) {
  //fprintf(stderr, "CXType.kind is %d\n", t.kind);
  
  switch(t.kind) {
    case CXType_Void:                 ( *code = sdscatprintf(*code, "None") );    break;
    case CXType_Bool:                 ( *code = sdscatprintf(*code, "Bool") );    break;
    
    case CXType_Char_S:               
    case CXType_Char_U:               
    case CXType_UChar:                ( *code = sdscatprintf(*code, "U8") );    break;
                                    
    case CXType_Char16:               
    case CXType_UShort:               ( *code = sdscatprintf(*code, "U16") );    break;
                                    
    case CXType_Enum:               
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
  
    case CXType_Typedef:              {
                                        addPonyTypeForCXType(clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(t)), code);
                                      }
                                      break;
    case CXType_Elaborated:           {
                                        CXString name = clang_getTypeSpelling(clang_Type_getNamedType(t));
                                        const char * nameString = clang_getCString(name);
                                        if(!strncmp("struct ", nameString, 7)) {
                                          *code = sdscatprintf(*code, "%s", nameString + 7);
                                        }else{
                                          *code = sdscatprintf(*code, "%s", nameString);
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
                                        // If the pointee is elaborated or typedef, we don't use Pointer
                                        CXType pointeeType = clang_getPointeeType(t);
                                        if(pointeeType.kind == CXType_Elaborated) {
                                          addPonyTypeForCXType(pointeeType, code);
                                        }else if(pointeeType.kind == CXType_Typedef) {
                                          addPonyTypeForCXType(pointeeType, code);
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
  //fprintf(stderr, "CXType.kind is %d\n", t.kind);
  
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
                                          *code = sdscatprintf(*code, "%s", nameString + 7);
                                        }else{
                                          *code = sdscatprintf(*code, "%s", nameString);
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
                                        addPonyTypeForCXType(t, code);
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
      }
      if(compareTokenName(unit, allTokens[j], "#") && compareTokenName(unit, allTokens[j+1], "define") && j+3 < numTokens) {
        
        // does this define share a root name with the previous one? If it doesn't, it belongs in a new group
        if (sizeOfDefineGrouping == 0) {
          copyTokenName(unit, allTokens[j+2], primitiveName, sizeof(primitiveName));
        }else{
          unsigned long newRootIndex = identifyTokenRootName(unit, allTokens[j+2], primitiveName);
          if(primitiveRootIndex != 0 && newRootIndex == 0) {
            break;
          }
          primitiveRootIndex = newRootIndex;
          primitiveName[primitiveRootIndex] = 0;
        }
        sizeOfDefineGrouping++;
        j += 3;
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
      
            char * cleanedName = translate_function_name(defineNameNameString);
            
            if(defineValueNameString[0] == '\"') {
              *code = sdscatprintf(*code, "  fun %s():%s => %s\n", cleanedName + primitiveRootIndex, "String", defineValueNameString);
            }else{
              *code = sdscatprintf(*code, "  fun %s():%s => %s\n", cleanedName + primitiveRootIndex, "U32", defineValueNameString);
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
  
  sds * code = (sds *)client_data;
  
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  
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
      CXType paramType = clang_getCursorType(paramCursor);
      
      if (clang_getCursorKind(paramCursor) == CXCursor_ParmDecl) {
        *code = sdscatprintf(*code, "%s:", clang_getCString(paramName));
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
  
  //fprintf(stderr, ">> [%d] %s\n", kind, clang_getCString(name));
  
  clang_disposeString(name);
  
  return childVisit;
}



enum CXVisitorResult printStructFields(CXCursor cursor, CXClientData client_data) {
  sds * code = (sds *)client_data;
  
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

enum CXChildVisitResult printStructDeclarations(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void)parent);
  int childVisit = CXChildVisit_Recurse;
  
  sds * code = (sds *)client_data;
  
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  
  if (kind == CXCursor_TypedefDecl) {
    childVisit = CXChildVisit_Continue;
  }
  
  if (kind == CXCursor_StructDecl && type.kind == CXType_Record) {
    *code = sdscatprintf(*code, "struct %s\n", clang_getCString(name));
    
    clang_Type_visitFields(type, printStructFields, code);
    
    *code = sdscatprintf(*code, "\n");
    
    childVisit = CXChildVisit_Continue;
  }
  
  //fprintf(stderr, ">> [%d][%d}] %s\n", kind, type.kind, clang_getCString(name));
  
  clang_disposeString(name);
  
  return childVisit;
}




char* translate_c_header(bool print_generated_code, const char* file_name, const char* source_code)
{
	((void)file_name);
	
	// it is our responsibility to free the old "source code" which was provided
	unsigned long in_source_code_length = strlen(source_code)+1;
		
	// use the sds library to concat our pony code together, then copy it to a pony allocated buffer
	sds code = sdsnew("");
		
  // TODO: Use libclang to parser the C header and then generate comparable pony FFI code
  CXIndex index = clang_createIndex(0, 0);
  CXTranslationUnit unit = clang_parseTranslationUnit(
        index,
        file_name, NULL, 0,
        NULL, 0,
        CXTranslationUnit_None);
  
  
  if (unit != NULL) {
    CXCursor cursor = clang_getTranslationUnitCursor(unit);
    
    // 1. print all of the function declarations ( use @foo[None]() )
    clang_visitChildren(cursor, printFunctionDeclarations, &code);
    
    code = sdscatprintf(code, "\n");
    
    // 2. print all of the stright #defines (primitive SomeDefined\n  fun x():U32 => 2)
    printNumericDefinitions(unit, &code);
    
    code = sdscatprintf(code, "\n");
    
    // 3. transpile all structures
    clang_visitChildren(cursor, printStructDeclarations, &code);
    
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

