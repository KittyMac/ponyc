#include "../../libponyrt/gc/serialise.h"
#include "../../libponyrt/mem/pool.h"
#include "translate_source.h"
#include "sds/sds.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <clang-c/Index.h>

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
    
    case CXType_ObjCId:
    case CXType_ObjCClass:
    case CXType_ObjCSel:
    case CXType_ObjCObjectPointer:
    case CXType_Pointer:
    case CXType_NullPtr:
                                      *code = sdscatprintf(*code, "Pointer[");
                                      addPonyTypeForCXType(clang_getPointeeType(t), code);
                                      *code = sdscatprintf(*code, "] tag");
                                      break;
    
    case CXType_IncompleteArray:      ( *code = sdscatprintf(*code, "...") );    break;
    case CXType_DependentSizedArray:  ( *code = sdscatprintf(*code, "...") );    break;
    case CXType_VariableArray:        ( *code = sdscatprintf(*code, "...") );    break;
    
    default:                          ( *code = sdscatprintf(*code, "None") );    break;
  }
}

enum CXChildVisitResult printCursorType(CXCursor cursor, CXCursor parent, CXClientData client_data) {
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
  
  return CXChildVisit_Recurse;
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
    clang_visitChildren(cursor, printCursorType, &code);
    
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

