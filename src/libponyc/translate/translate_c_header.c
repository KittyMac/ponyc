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

#define kEnumContentRootMax 1024

typedef struct {
  sds * code;
  sds * dedup;
  sds * transparentStruct;
  
  CXCursor previous;
  CXTranslationUnit unit;
  
  CXCursor enum_last_root_cursor;
  unsigned long enum_content_root;
}VisitorData;

static int unknownThingCounter = 0;

extern int string_ends_with(const char *str, const char *suffix);

enum CXChildVisitResult cursorContainsStruct(CXCursor cursor, CXCursor parent, CXClientData client_data);






// Note: useful commands
// clang -Xclang -ast-dump -fsyntax-only somefile.h

sds translate_c_header_abort(sds code, char * error) {
  return sdscatprintf(code, "C header parser failed: %s\n", error);
}

bool checkForDuplicatedNames(const char * name, sds * dedup) {
  // dedup string stores \nname\n, so we do a simple string search for name conflicts
	sds key = sdsnew("");
  key = sdscatprintf(key, "\n%s\n", name);
  
  bool exists = strcasestr(*dedup, key) != 0;
  if(exists == false) {
    *dedup = sdscatprintf(*dedup, "%s", key);
  }
  sdsfree(key);
  return exists;
}

void setTransparentStruct(CXType type, CXClientData client_data) {
  VisitorData * data = (VisitorData *)client_data;
  sds * transparentStruct = data->transparentStruct;
  
  CXString name = clang_getCursorSpelling(clang_getTypeDeclaration(type));
  const char * nameString = clang_getCString(name);
  
  if (clang_visitChildren(clang_getTypeDeclaration(type), cursorContainsStruct, client_data) == 0) {
    // the underlying type must be a struct. We need to search inside for a Struct
    //fprintf(stderr, ">> failing transparent struct on typedef %s because it does not contain a struct\n", nameString);
    return;
  }
  
  //fprintf(stderr, ">> set transparent struct on %s [%d]\n", nameString, type.kind);
  
	sds key = sdsnew("");
  key = sdscatprintf(key, "\n%s\n", nameString);
  *transparentStruct = sdscatprintf(*transparentStruct, "%s", key);
  sdsfree(key);
  clang_disposeString(name);
}

bool checkTransparentStruct(CXType type, CXClientData client_data) {
  VisitorData * data = (VisitorData *)client_data;
  sds * transparentStruct = data->transparentStruct;
  
  CXString name = clang_getCursorSpelling(clang_getTypeDeclaration(type));
  const char * nameString = clang_getCString(name);
  
	sds key = sdsnew("");
  key = sdscatprintf(key, "\n%s\n", nameString);
  bool exists = strcasestr(*transparentStruct, key) != 0;
  sdsfree(key);
  clang_disposeString(name);
  
  return exists;
}

void addPonyTypeForCXType(CXType t, bool isReturnType, CXClientData client_data) {
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  
  /*
  CXString name = clang_getTypeKindSpelling(t.kind);
  CXString name2 = clang_getCursorSpelling(clang_getTypeDeclaration(t));
  CXType eType = clang_Type_getNamedType(t);
  CXString eName = clang_getTypeSpelling(eType);
  fprintf(stderr, ">> CXType.kind is %d -- %s  -- %s -- [%d] %s\n", t.kind, clang_getCString(name), clang_getCString(name2), eType.kind, clang_getCString(eName));
  */
  
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
    case CXType_ULongLong:            
    case CXType_ULong:                ( *code = sdscatprintf(*code, "U64") );    break;
                                    
    case CXType_UInt128:              ( *code = sdscatprintf(*code, "U128") );    break;
                                    
                                    
    case CXType_SChar:                ( *code = sdscatprintf(*code, "I8") );    break;
    case CXType_WChar:                
    case CXType_Short:                ( *code = sdscatprintf(*code, "I16") );    break;
    case CXType_Int:                  ( *code = sdscatprintf(*code, "U32") );    break;
    case CXType_LongLong:             
    case CXType_Long:                 ( *code = sdscatprintf(*code, "I64") );    break;
                                    
    case CXType_Int128:               ( *code = sdscatprintf(*code, "I128") );    break;
                                    
    case CXType_Half:               
    case CXType_Float16:              
    case CXType_Float:                ( *code = sdscatprintf(*code, "F32") );    break;
    case CXType_Double:               ( *code = sdscatprintf(*code, "F64") );    break;
    case CXType_Float128:           
    case CXType_LongDouble:           ( *code = sdscatprintf(*code, "F128") );    break;
    
    case CXType_Record:               ( *code = sdscatprintf(*code, "U32") );    break;
    
    case CXType_Enum:                 {
                                        addPonyTypeForCXType(clang_getEnumDeclIntegerType(clang_getTypeDeclaration(t)), isReturnType, client_data);
                                      }
                                      break;
    case CXType_Typedef:              {
                                        addPonyTypeForCXType(clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(t)), isReturnType, client_data);
                                      }
                                      break;
    case CXType_Elaborated:           {
                                        CXType elaboratedType = clang_Type_getNamedType(t);
                                        CXString elaboratedName = clang_getTypeSpelling(elaboratedType);
                                        const char * elaboratedNameString = clang_getCString(elaboratedName);
                                        if(!strncmp("struct ", elaboratedNameString, 7)) {
                                          *code = sdscatprintf(*code, "%s", translate_class_name(elaboratedNameString + 7));
                                        }else if(!strncmp("enum ", elaboratedNameString, 5) || elaboratedType.kind == CXType_Enum) {
                                          // We want the type of the enum, not the name...
                                          addPonyTypeForCXType(elaboratedType, isReturnType, client_data);
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
                                          if(checkTransparentStruct(pointeeType, client_data)){
                                            *code = sdscatprintf(*code, "NullablePointer[");
                                          }else{
                                            *code = sdscatprintf(*code, "Pointer[");
                                          }
                                          addPonyTypeForCXType(pointeeType, isReturnType, client_data);
                                          *code = sdscatprintf(*code, "]");
                                          if(isReturnType == false){
                                            *code = sdscatprintf(*code, " tag");
                                          }
                                        }else{
                                          *code = sdscatprintf(*code, "Pointer[");
                                          addPonyTypeForCXType(pointeeType, isReturnType, client_data);
                                          *code = sdscatprintf(*code, "]");
                                          if(isReturnType == false){
                                            *code = sdscatprintf(*code, " tag");
                                          }
                                        }
                                      }
                                      break;
    
    case CXType_IncompleteArray:      ( *code = sdscatprintf(*code, "...") );    break;
    case CXType_DependentSizedArray:  ( *code = sdscatprintf(*code, "...") );    break;
    case CXType_VariableArray:        ( *code = sdscatprintf(*code, "...") );    break;
    
    
    case CXType_ConstantArray:        { 
                                          *code = sdscatprintf(*code, "Pointer[None]");
                                          if(isReturnType == false){
                                            *code = sdscatprintf(*code, " tag");
                                          }
                                      }
                                      break;
    case CXType_FunctionProto:        { 
                                          *code = sdscatprintf(*code, "None");
                                      }
                                      break;
    
    default:                          ( *code = sdscatprintf(*code, "*** UNKNOWN TYPE %d ***", t.kind) );    break;
  }
}

void addPonyDefaultValueForCXType(CXType t, CXClientData client_data) {
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  
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
                                        addPonyTypeForCXType(t, true, client_data);
                                        CXType pointeeType = clang_getPointeeType(t);
                                        if(pointeeType.kind == CXType_Elaborated || pointeeType.kind == CXType_Typedef) {
                                          if(checkTransparentStruct(pointeeType, client_data)){
                                            *code = sdscatprintf(*code, ".none()");
                                          }
                                        }
                                      }
                                      break;
        
    default:                          ( *code = sdscatprintf(*code, "0") );    break;
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

unsigned long identifyRootName(const char * nameString, const char * buffer) {  
  unsigned long minSize = min(strlen(nameString), strlen(buffer));
  for(unsigned long i = 0; i < minSize; i++) {
    if(tolower(nameString[i]) != tolower(buffer[i])) {
      return i;
    }
  }
  return minSize;
}

unsigned long identifyTokenRootName(CXTranslationUnit unit, CXToken token, char * buffer) {
  CXString name = clang_getTokenSpelling(unit, token);
  const char * nameString = clang_getCString(name);
  unsigned long r = identifyRootName(nameString, buffer);
  clang_disposeString(name);  
  return r;
}

void printNumericDefinitions(CXTranslationUnit unit, CXClientData client_data) {
  
  CXToken * allTokens = NULL;
  unsigned numTokens = 0;
  
  CXCursor unitCursor = clang_getTranslationUnitCursor(unit);
  clang_tokenize(unit, clang_getCursorExtent(unitCursor), &allTokens, &numTokens);
  
  
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  sds * dedup = data->dedup;
  
  // Note: #defines in C don't need to be namespaced, they can just live anywhere. In Pony they need
  // to live inside of a primitive. We also want to separate them into meaningful groups (most c 
  // developers will group them visually).
  sds groups = sdsnew("");
  
  // 0. Identify all groups
  for(unsigned i = 0; i < numTokens-1; i++) {                
    // How many are in this group?  Do they have a common name root?
    int sizeOfDefineGrouping = 0;
    char primitiveName[1024] = {0};
    unsigned long primitiveRootIndex = 9999999;
    
    // Look for the next group...
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
            copyTokenName(unit, defineNameToken, primitiveName, sizeof(primitiveName));
          }else{
            unsigned long newRootIndex = identifyTokenRootName(unit, defineNameToken, primitiveName);
            if(newRootIndex == 0) {
              break;
            }
            
            if( newRootIndex < primitiveRootIndex ){
              primitiveRootIndex = newRootIndex;
              primitiveName[primitiveRootIndex] = 0;
              //fprintf(stderr, ">>>> new primitiveName: %s\n", primitiveName);
            }
          }
          sizeOfDefineGrouping++;
        }else{
          break;
        }
        
        j += 3;
        i = j;
      }else{
        break;
      }
    }
    
    if (sizeOfDefineGrouping >= 1) {
      const char * className = translate_class_name(primitiveName);
      if(checkForDuplicatedNames(className, dedup) == false) {
        // Save the name of this group
        groups = sdscatprintf(groups, "%s\n", primitiveName);
        //fprintf(stderr, ">> storing groupName: %s of %d items\n", primitiveName, sizeOfDefineGrouping);
      }
      translate_free_name(className);
    }
  }
  
  
  // 1. Go back through everything, one group at a time, and export all of the items
  // in those groups (even if they are not "physically" close to each other).
  int count;
  sds * tokens = sdssplitlen(groups, sdslen(groups), "\n" , 1, &count);
  for (int k = 0; k < count; k++) {
    sds groupName = tokens[k];
    
    if(sdslen(groupName) == 0) {
      continue;
    }
    
    //fprintf(stderr, ">> printing groupName: %s\n", groupName);
    const char * className = translate_class_name(groupName);
    
    *code = sdscatprintf(*code, "type %sRef is Pointer[%s]\n", className, className);
    *code = sdscatprintf(*code, "primitive %s\n", className);
    
    translate_free_name(className);
    
    for(unsigned i = 0; i < numTokens-1; i++) {                
      // How many are in this group?  Do they have a common name root?
      int sizeOfDefineGrouping = 0;
      char primitiveName[1024] = {0};
      unsigned long primitiveRootIndex = 0;
    
      // Look for the next group...
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
      
        if(!strcmp(groupName, primitiveName)){
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
            
                  // pony doesn't support qualifiers on its numerics
                  if(string_ends_with(defineValueNameString, "L")) {
                    sdssetlen(*code, sdslen(*code)-2);
                    *code = sdscatprintf(*code, "\n");
                  }
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
          
        }
      
      }
    }
    
    *code = sdscatprintf(*code, "\n");
  }
  sdsfreesplitres(tokens, count);
  
  
  
  
  
  
  sdsfree(groups);
}


enum CXChildVisitResult printFunctionDeclarations(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void)parent);
  int childVisit = CXChildVisit_Recurse;
  
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  
  if(clang_getCursorAvailability(cursor) != CXAvailability_Available){
    return childVisit;
  }
  
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  const char * nameString = clang_getCString(name);
  
  //fprintf(stderr, ">> [%d] %s\n", kind, clang_getCString(name));
  
  if (kind == CXCursor_FunctionDecl) {
    if( !strcmp("__API_AVAILABLE", nameString) || 
        !strcmp("__API_DEPRECATED", nameString) || 
        !strcmp("__API_DEPRECATED_WITH_REPLACEMENT", nameString)
      ){
      clang_disposeString(name);
      return childVisit;
    }
    
    
    *code = sdscatprintf(*code, "use @%s", nameString);
    
    CXType resultType = clang_getCursorResultType(cursor);
    *code = sdscatprintf(*code, "[");
    addPonyTypeForCXType(resultType, true, client_data);
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
        //fprintf(stderr, ">> [%d] %s parameter\n", paramType.kind, clang_getCString(paramName));
        
        if(paramNameString[0] == 0) {
          *code = sdscatprintf(*code, "arg%d:", unknownThingCounter++);
        }else{
          *code = sdscatprintf(*code, "%s:", clang_getCString(paramName));
        }
        
        addPonyTypeForCXType(paramType, false, client_data);
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


enum CXChildVisitResult calculateFunctionTransparency(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void)parent);
  ((void)client_data);
  int childVisit = CXChildVisit_Recurse;
  
  if(clang_getCursorAvailability(cursor) != CXAvailability_Available){
    return childVisit;
  }
  
  // Search through all functions, all parameters. If we find a function which accepts a pointer to
  // a struct, then we flag that struct as needing to be transparent (ie it probably needs a 
  // full struct definition and not just a Pony primitive)
  
  int kind = clang_getCursorKind(cursor);
  if (kind == CXCursor_FunctionDecl) {
    
    int numParams = clang_Cursor_getNumArguments(cursor);
    for(int i = 0; i < numParams; i++){
      CXCursor paramCursor = clang_Cursor_getArgument(cursor, i);
      CXType paramType = clang_getCursorType(paramCursor);
      
      if (clang_getCursorKind(paramCursor) == CXCursor_ParmDecl) {        
        if(paramType.kind == CXType_Pointer){
          CXType pointeeType = clang_getPointeeType(paramType);
          if(pointeeType.kind == CXType_Elaborated || pointeeType.kind == CXType_Typedef) {
            setTransparentStruct(pointeeType, client_data);
          }
        }
      }
    }
    childVisit = CXChildVisit_Continue;
  }  
  return childVisit;
}


enum CXVisitorResult printStructFields(CXCursor cursor, CXClientData client_data) {
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  
  if (kind == CXCursor_FieldDecl) {
    
    if (type.kind == CXType_Elaborated) {
      *code = sdscatprintf(*code, "  embed %s: ", clang_getCString(name));
    }else{
      *code = sdscatprintf(*code, "  var %s: ", clang_getCString(name));
    }
    
    addPonyTypeForCXType(type, false, client_data);
    *code = sdscatprintf(*code, " = ");
    addPonyDefaultValueForCXType(type, client_data);
    
    
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

bool structShouldBeStruct(CXCursor cursor) {
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  
  int countOfFields = 0;
  if (kind == CXCursor_StructDecl && type.kind == CXType_Record) {
    clang_Type_visitFields(type, countStructFields, &countOfFields);
  }
  if(countOfFields == 0) {
    return false;
  }
  return true;
}

enum CXChildVisitResult cursorContainsStruct(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void)parent);
  ((void)client_data);
  
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  
  if (kind == CXCursor_StructDecl && type.kind == CXType_Record) {
    // but does it contain any children?
    if(structShouldBeStruct(cursor)) {
      return CXChildVisit_Break;
    }
  }
  return CXChildVisit_Recurse;
}


enum CXChildVisitResult printStructDeclarations(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void)parent);
  int childVisit = CXChildVisit_Recurse;
    
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  sds * dedup = data->dedup;
  
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
  
  
  if (kind == CXCursor_TypedefDecl) {
    // if we are a typedef and the previous entity was a struct without a name, then we need to make type declaration to it
    if(previousKind == CXCursor_StructDecl && previousType.kind == CXType_Record && previousNameString[0] == 0) {
      const char * className = translate_class_name(nameString);
      if(checkForDuplicatedNames(className, dedup) == false) {
        
        if(checkTransparentStruct(type, client_data)){
          *code = sdscatprintf(*code, "type %sRef is NullablePointer[%s]\n", className, className);
          *code = sdscatprintf(*code, "struct %s\n", className);
          clang_Type_visitFields(previousType, printStructFields, client_data);
          
        }else{
          *code = sdscatprintf(*code, "primitive %s\n", className);
          *code = sdscatprintf(*code, "type %sRef is Pointer[%s]\n", className, className);
        }
        
        *code = sdscatprintf(*code, "\n");
      }
      translate_free_name(className);
    }else{
      //fprintf(stderr, "++ [%d][%d}] %s\n", previousKind, previousType.kind, previousNameString);
    }
    childVisit = CXChildVisit_Continue;
  }
  if (kind == CXCursor_StructDecl && type.kind == CXType_Record) {
    if(nameString[0] != 0) {
      const char * className = translate_class_name(nameString);
      if(checkForDuplicatedNames(className, dedup) == false) {
        
        if(checkTransparentStruct(type, client_data)){
          *code = sdscatprintf(*code, "type %sRef is NullablePointer[%s]\n", className, className);
          *code = sdscatprintf(*code, "struct %s\n", className);
          clang_Type_visitFields(type, printStructFields, client_data);
          
        }else{
          *code = sdscatprintf(*code, "primitive %s\n", className);
          *code = sdscatprintf(*code, "type %sRef is Pointer[%s]\n", className, className);
        }
        
      }
      translate_free_name(className);
    }
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






enum CXChildVisitResult indentifyEnumContentRoot(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void) parent);
  
  VisitorData * data = (VisitorData *)client_data;
  
  int kind = clang_getCursorKind(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  const char * nameString = clang_getCString(name);
  
  if (kind == CXCursor_EnumConstantDecl) {
    if(data->enum_content_root == kEnumContentRootMax) {
      data->enum_content_root = strlen(nameString);
      data->enum_last_root_cursor = cursor;
    }else{
      CXString rootName = clang_getCursorSpelling(data->enum_last_root_cursor);
      const char * rootNameString = clang_getCString(rootName);
      
      unsigned long new_root = identifyRootName(rootNameString, nameString);
      if(new_root < data->enum_content_root){
        
        // safety net. If the new root is the same size as me, then set to 0 (avoid empty enum name)
        if(new_root == strlen(nameString) || new_root == strlen(rootNameString)) {
          new_root = 0;
        }
        
        data->enum_content_root = new_root;
        data->enum_last_root_cursor = cursor;
      }
      
      clang_disposeString(rootName);
    }
  }
  
  clang_disposeString(name);
  
  return CXChildVisit_Recurse;
}

enum CXChildVisitResult printEnumContents(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void) parent);
  
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  
  int kind = clang_getCursorKind(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  const char * nameString = clang_getCString(name);
  
  if (kind == CXCursor_EnumConstantDecl) {
    *code = sdscatprintf(*code, "  fun %s():U32", translate_function_name(nameString + data->enum_content_root));
    *code = sdscatprintf(*code, " => 0x%llX\n", clang_getEnumConstantDeclUnsignedValue(cursor));
  }
  
  clang_disposeString(name);
  
  return CXChildVisit_Recurse;
}

enum CXChildVisitResult printEnumDeclarations(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ((void)parent);
  int childVisit = CXChildVisit_Recurse;
  
  VisitorData * data = (VisitorData *)client_data;
  sds * code = data->code;
  sds * dedup = data->dedup;
  
  int kind = clang_getCursorKind(cursor);
  CXType type = clang_getCursorType(cursor);
  CXString name = clang_getCursorSpelling(cursor);
  const char * nameString = clang_getCString(name);
  
  CXString parentName = clang_getCursorSpelling(parent);
  const char * parentNameString = clang_getCString(parentName);
  
  if (kind == CXCursor_EnumDecl && type.kind == CXType_Enum) {
    if(clang_getCursorKind(parent) == CXCursor_TypedefDecl) {
      const char * className = translate_class_name(parentNameString);
      if(checkForDuplicatedNames(className, dedup) == false) {
        *code = sdscatprintf(*code, "primitive %sEnum\n", className);
        
        data->enum_content_root = kEnumContentRootMax;
        clang_visitChildren(cursor, indentifyEnumContentRoot, client_data);
        clang_visitChildren(cursor, printEnumContents, client_data);
        
      }else{
        childVisit = CXChildVisit_Continue;
      }
      translate_free_name(className);
    }else{
      if(nameString[0] == 0) {
        clang_disposeString(parentName);
        clang_disposeString(name);
        return CXChildVisit_Continue;
      }
      
      const char * className = translate_class_name(nameString);
      if(checkForDuplicatedNames(className, dedup) == false) {
        *code = sdscatprintf(*code, "primitive %sEnum\n", className);
        
        data->enum_content_root = kEnumContentRootMax;
        clang_visitChildren(cursor, indentifyEnumContentRoot, client_data);
        clang_visitChildren(cursor, printEnumContents, client_data);
        
        
      }else{
        childVisit = CXChildVisit_Continue;
      }
      translate_free_name(className);
      
    }
  }
    
  //fprintf(stderr, ">> [%d][%d}] %s\n", kind, type.kind, clang_getCString(name));
  
  clang_disposeString(parentName);
  clang_disposeString(name);
  
  return childVisit;
}





static sds deduplicateDefinitionsInPackage = NULL;
static sds transparentStruct = NULL;

void translate_c_header_package_begin(const char * qualified_name)
{
  ((void)qualified_name);
	deduplicateDefinitionsInPackage = sdsnew("");
  transparentStruct = sdsnew("");
}

sds translate_c_header_package_end(sds code)
{
	sdsfree(deduplicateDefinitionsInPackage);
  sdsfree(transparentStruct);
	return code;
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
  
  fprintf(file, "#define int8_t signed char\n");
  fprintf(file, "#define uint8_t unsigned char\n");
  fprintf(file, "#define int16_t signed int\n");
  fprintf(file, "#define uint16_t unsigned int\n");
  fprintf(file, "#define int32_t signed long int\n");
  fprintf(file, "#define uint32_t unsigned long int\n");
  fprintf(file, "#define int64_t signed long long int\n");
  fprintf(file, "#define uint64_t unsigned long long int\n");
  
  fprintf(file, "%s\n", source_code);
  fclose(file);
  
  // Use libclang to parser the C header and then generate comparable pony FFI code
  CXIndex index = clang_createIndex(0, 0);
  
  char *args[] = {"-c", "-nostdinc", "-nostdlibinc", "-nobuiltininc", NULL};

  CXTranslationUnit unit = clang_parseTranslationUnit(
        index,
        tmpFileName, 
        (const char *const *)args, 4,
        NULL, 0,
        CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_Incomplete);
  
  
  if (unit != NULL) {
    CXCursor cursor = clang_getTranslationUnitCursor(unit);
    
    VisitorData data = {0};
    data.code = &code;
    data.dedup = &deduplicateDefinitionsInPackage;
    data.transparentStruct = &transparentStruct;
    data.unit = unit;
    
    // 1. print all of the function declarations ( use @foo[None]() )
    clang_visitChildren(cursor, calculateFunctionTransparency, &data);
    clang_visitChildren(cursor, printFunctionDeclarations, &data);
    
    code = sdscatprintf(code, "\n");
    
    // 2. print all of the stright #defines (primitive SomeDefined\n  fun x():U32 => 2)
    printNumericDefinitions(unit, &data);
    
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



