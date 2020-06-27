// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/code_info_get_info.h"

#include "cide/clang_utils.h"
#include "cide/main_window.h"
#include "cide/clang_parser.h"
#include "cide/qt_help.h"
#include "cide/qt_thread.h"

struct MemberAccessStruct {
  enum MemberType {
    Public = 0,
    Protected = 1,
    Private = 2,
    Unknown = 3,
    NumTypes = 4
  };
  
  inline bool HasAny() const {
    return !members[Public].isEmpty() ||
           !members[Protected].isEmpty() ||
           !members[Private].isEmpty() ||
           !members[Unknown].isEmpty();
  }
  
  QString members[4];
};

/// Stores a formatted description of members of a class/struct.
struct MemberList {
  MemberAccessStruct constructors;
  MemberAccessStruct destructors;
  MemberAccessStruct functions;
  // TODO: operators?
  MemberAccessStruct attributes;
  MemberAccessStruct records;
  
  QString nameFilter;
  
  // Helper
  std::shared_ptr<ClangTU> TU;
};

QString GetStorageClassString(CXCursor cursor) {
  switch (clang_Cursor_getStorageClass(cursor)) {
  case CX_SC_Invalid:               return "";  break;
  case CX_SC_None:                  return "";  break;  // QObject::tr("none")
  case CX_SC_Extern:                return QObject::tr("extern");  break;
  case CX_SC_Static:                return QObject::tr("static");  break;
  case CX_SC_PrivateExtern:         return QObject::tr("private extern");  break;
  case CX_SC_OpenCLWorkGroupLocal:  return QObject::tr("OpenCL work group local");  break;
  case CX_SC_Auto:                  return QObject::tr("auto");  break;
  case CX_SC_Register:              return QObject::tr("register");  break;
  }
  return "";
}

/// Line and column are 1-based here.
QString PrintLinkToLocation(const QString& path, int line, int column) {
  QString fileName = QFileInfo(path).fileName();
  QString lineString = QString::number(line);
  QString locationString = path + QStringLiteral(":") + lineString + QStringLiteral(":") + QString::number(column);
  return QStringLiteral("<a href=\"file://") + locationString + QStringLiteral("\">") +
         fileName + QStringLiteral(":") + lineString +
         QStringLiteral("</a>");
}

QString PrintLinkToDefinitionOrDeclarationLocation(CXCursor cursor) {
  CXSourceLocation location = clang_getCursorLocation(cursor);
  CXFile file;
  unsigned line, column;
  clang_getFileLocation(
      location,
      &file,
      &line,
      &column,
      nullptr);
  
  QString fullPath = GetClangFilePath(file);
  QString fileName = QFileInfo(fullPath).fileName();
  
  QString lineString = QString::number(line);
  QString locationString = fullPath + QStringLiteral(":") + lineString + QStringLiteral(":") + QString::number(column);
  bool isDefinition = clang_isCursorDefinition(cursor) || (clang_getCursorKind(cursor) == CXCursor_MacroDefinition);
  return (isDefinition ? QObject::tr("Definition: %1") : QObject::tr("Declaration: %1")).arg(
             QStringLiteral("<a href=\"file://") + locationString + QStringLiteral("\">") +
             fileName + QStringLiteral(":") + lineString +
             QStringLiteral("</a>"));
}

QString PrintLinkToCursorInfo(CXCursor cursor) {
  CXSourceLocation location = clang_getCursorLocation(cursor);
  CXFile file;
  unsigned line, column;
  clang_getFileLocation(
      location,
      &file,
      &line,
      &column,
      nullptr);
  
  QString fullPath = GetClangFilePath(file);
  
  QString locationString = fullPath + QStringLiteral(":") + QString::number(line) + QStringLiteral(":") + QString::number(column);
  return QStringLiteral("<a href=\"info://") + locationString + QStringLiteral("\">");
}

/// Called to print types in documentation displays.
QString PrintType(CXType type) {
  // qDebug() << "Printing cursor kind: " << ClangString(clang_getTypeKindSpelling(type.kind)).ToQString();
  // qDebug() << "DeclCursor for type" << ClangString(clang_getTypeSpelling(type)).ToQString() << "is of cursor type" << ClangString(clang_getCursorKindSpelling(clang_getCursorKind(declCursor))).ToQString();
  
  // If this is an array type, split off the array bracket [] and print the
  // underlying type.
  CXType arrayElemType = clang_getArrayElementType(type);
  if (arrayElemType.kind != CXType_Invalid) {
    QString typeSpelling = ClangString(clang_getTypeSpelling(type)).ToQString();
    int openArrayBracketPos = typeSpelling.lastIndexOf('[');
    if (openArrayBracketPos > 0) {
      return PrintType(arrayElemType) + typeSpelling.mid(openArrayBracketPos);
    }
  }
  
  // If we can get a cursor to the declaration of the type, print a link to it.
  CXCursor declCursor = clang_getTypeDeclaration(type);
  if (!clang_Cursor_isNull(declCursor) &&
      clang_getCursorKind(declCursor) != CXCursor_NoDeclFound) {
    QString typeSpelling = ClangString(clang_getTypeSpelling(type)).ToQString();
    
    // Separate the possible "const " prefix from the rest of the type spelling
    // such that "const " is not part of the inserted link.
    bool isConst = false;
    if (typeSpelling.startsWith(QStringLiteral("const "))) {
      isConst = true;
      typeSpelling = typeSpelling.mid(6);
    }
    
    // If there are template arguments, also exclude them from the inserted link
    // range.
    QString nonLinkTypeSpelling;
    int firstTemplateBracket = typeSpelling.indexOf('<');
    if (firstTemplateBracket >= 0) {
      nonLinkTypeSpelling = typeSpelling.mid(firstTemplateBracket);
      typeSpelling.chop(typeSpelling.size() - firstTemplateBracket);
    }
    
    // Try to link to any template specialization argument types separately.
    int numTemplateArgs = clang_Type_getNumTemplateArguments(type);
    int replacementPos = 0;  // character position in nonLinkTypeSpelling
    QString replacedNonLinkTypeSpelling;
    bool ok = true;
    for (int argIdx = 0; argIdx < numTemplateArgs; ++ argIdx) {
      CXType argType = clang_Type_getTemplateArgumentAsType(type, argIdx);
      
      // clang_Type_getTemplateArgumentAsType() does not handle template template
      // arguments or variadic packs. So we will not always get a valid type.
      if (argType.kind == CXType_Invalid) {
        continue;
      } else {
        // HACK: As there seems to be no libclang API for that, we need to find
        //       the range of this type within nonLinkTypeSpelling ourselves.
        // TODO: This is done here in a way that can break in unfortunate cases
        //       (since we do not parse the non-type template specialization arguments).
        //       A theoretical alternative would be to build the string
        //       containing the template arguments ourselves (instead of
        //       replacing within the given string), but that also does not seem
        //       possible, since we cannot necessarily retrieve types for all
        //       arguments (and thus we cannot print them separately).
        QString argSpelling = ClangString(clang_getTypeSpelling(argType)).ToQString();
        int argPos = nonLinkTypeSpelling.indexOf(argSpelling, replacementPos);
        if (argPos < 0) {
          // Something went wrong in the process. Give up.
          ok = false;
          break;
        }
        if (argPos > replacementPos) {
          replacedNonLinkTypeSpelling += nonLinkTypeSpelling.mid(replacementPos, argPos - replacementPos).toHtmlEscaped();
        }
        replacedNonLinkTypeSpelling += PrintType(argType);
        replacementPos = argPos + argSpelling.size();
      }
    }
    
    if (ok && !replacedNonLinkTypeSpelling.isEmpty()) {
      if (nonLinkTypeSpelling.size() > replacementPos) {
        replacedNonLinkTypeSpelling += nonLinkTypeSpelling.mid(replacementPos, nonLinkTypeSpelling.size() - replacementPos).toHtmlEscaped();
      }
      nonLinkTypeSpelling = replacedNonLinkTypeSpelling;
    } else {
      nonLinkTypeSpelling = nonLinkTypeSpelling.toHtmlEscaped();
    }
    
    return (isConst ? QStringLiteral("const ") : QStringLiteral("")) +
           PrintLinkToCursorInfo(declCursor) +
           typeSpelling.toHtmlEscaped() +
           QStringLiteral("</a>") +
           nonLinkTypeSpelling;
  }
  
  // Check whether we have a pointer or reference type.
  if (type.kind == CXType_Pointer) {
    CXType pointeeType = clang_getPointeeType(type);
    if (pointeeType.kind != CXType_Invalid) {
      return PrintType(pointeeType) + QStringLiteral("*");
    }
  } else if (type.kind == CXType_RValueReference ||
             type.kind == CXType_LValueReference) {
    CXType pointeeType = clang_getPointeeType(type);
    if (pointeeType.kind != CXType_Invalid) {
      return PrintType(pointeeType) + ((type.kind == CXType_LValueReference) ? QStringLiteral("&") : QStringLiteral("&&"));
    }
  }
  
  // We failed to decompose the type, thus print it as-is without a link.
  return ClangString(clang_getTypeSpelling(type)).ToQString().toHtmlEscaped();
}

/// Called by PrintMember() for records (i.e., class/struct).
QString PrintRecordMember(CXCursor cursor, CXCursorKind kind, const QString& spellingString) {
  QString recordTypeString;
  CXCursorKind recordKind = kind;
  if (kind == CXCursor_ClassTemplate) {
    recordKind = clang_getTemplateCursorKind(cursor);
  }
  
  if (recordKind == CXCursor_StructDecl) {
    recordTypeString = QStringLiteral("struct");
  } else if (recordKind == CXCursor_UnionDecl) {
    recordTypeString = QStringLiteral("union");
  } else if (recordKind == CXCursor_ClassDecl) {
    recordTypeString = QStringLiteral("class");
  } else if (recordKind == CXCursor_EnumDecl) {
    recordTypeString = QStringLiteral("enum");
  } else {
    recordTypeString = QObject::tr("(unknown)");
  }
  
  QString result;
  if (clang_CXXRecord_isAbstract(cursor)) {
    result += "abstract ";
  }
  result += recordTypeString + " ";
  result += PrintLinkToCursorInfo(cursor) + "<b style=\"color:#880000;\">" + spellingString.toHtmlEscaped() + "</b></a>";
  return result;
}

/// Called by PrintMember for functions.
QString PrintFunctionMember(CXCursor cursor, CXCursorKind kind, const QString& spellingString) {
  // Return type
  QString returnType = PrintType(clang_getCursorResultType(cursor));
  
  // Arguments
  int numArgs = clang_Cursor_getNumArguments(cursor);
  QString argsString;
  if (numArgs == -1) {
    // TODO: Can we determine the arguments for function templates?
    if (kind != CXCursor_FunctionTemplate) {
      qDebug() << "PrintFunctionMember(): Failed to determine arguments";
    }
  } else {
    for (int argIdx = 0; argIdx < numArgs; ++ argIdx) {
      if (!argsString.isEmpty()) {
        argsString += ", ";
      }
      
      CXCursor argCursor = clang_Cursor_getArgument(cursor, argIdx);
      CXString argSpelling = clang_getCursorSpelling(argCursor);
      QString argName = QString::fromUtf8(clang_getCString(argSpelling));
      clang_disposeString(argSpelling);
      
      argsString += PrintType(clang_getCursorType(argCursor));
      argsString += " ";
      argsString += argName.toHtmlEscaped();
      
      // TODO: Get default value for the argument (if any)
      // argsString += GetClangText(clang_getCursorExtent(argCursor), TU->TU());
    }
  }
  
  if (clang_Cursor_isVariadic(cursor)) {
    if (!argsString.isEmpty()) {
      argsString += ", ";
    }
    argsString += "...";
  }
  
  bool isConst = clang_CXXMethod_isConst(cursor);
  bool isInlined = clang_Cursor_isFunctionInlined(cursor);
  bool isDefaulted = clang_CXXMethod_isDefaulted(cursor);
  bool isStatic = clang_CXXMethod_isStatic(cursor);
  bool isVirtual = clang_CXXMethod_isVirtual(cursor);
  bool isPureVirtual = clang_CXXMethod_isPureVirtual(cursor);
  
  QString color;
  if (kind == CXCursor_Constructor ||
      kind == CXCursor_Destructor) {
    color = QStringLiteral("af7e02");  // 175, 126, 2
  } else {
    color = QStringLiteral("000088");
  }
  
  QString result;
  
  if (kind == CXCursor_FunctionTemplate) {
    // TODO: For function templates, we do not seem to get arguments from libclang, so for now, print the cursor as-is
    CXPrintingPolicy printingPolicy = clang_getCursorPrintingPolicy(cursor);
    clang_PrintingPolicy_setProperty(printingPolicy, CXPrintingPolicy_TerseOutput, 1);  // print declaration only, skip body
    CXString cursorDisplayName = clang_getCursorPrettyPrinted(cursor, printingPolicy);
    clang_PrintingPolicy_dispose(printingPolicy);
    QString displayName = QString::fromUtf8(clang_getCString(cursorDisplayName));
    clang_disposeString(cursorDisplayName);
    if (displayName.endsWith(QStringLiteral(" {}"))) {
      displayName.chop(3);
    } else if (displayName.endsWith(QStringLiteral(" {\n}"))) {
      displayName.chop(4);
    }
    
    result = PrintLinkToCursorInfo(cursor) + "<b style=\"color:#" + color + ";\">";
    result += displayName.toHtmlEscaped();
    result += "</b></a>";
  } else {
    if (isStatic) {
      result += "static ";
    }
    if (isVirtual) {
      result += "virtual ";
    }
    if (isInlined) {
      result += "inline ";
    }
    result += returnType + " ";
    result += PrintLinkToCursorInfo(cursor) + "<b style=\"color:#" + color + ";\">";
    result += spellingString.toHtmlEscaped();
    result += "</b></a>";
    result += "(" + argsString + ")";
    if (isConst) {
      result += " const";
    }
    if (isDefaulted) {
      result += " = default";
    }
    if (isPureVirtual) {
      result += " = 0";
    }
  }
  
  return result;
}

/// Called by PrintMember for attributes.
QString PrintAttributeMember(CXCursor cursor, CXCursorKind /*kind*/, const QString& spellingString) {
  bool isMutable = clang_CXXField_isMutable(cursor);
  
  QString result;
  if (isMutable) {
    result += "mutable ";
  }
  if (clang_Cursor_getStorageClass(cursor) == CX_SC_Static) {
    result += "static ";
  }
  result += PrintType(clang_getCursorType(cursor)) + " ";
  result += PrintLinkToCursorInfo(cursor) + "<b style=\"color:#008800;\">" + spellingString.toHtmlEscaped() + "</b></a>";
  
  return result;
}

/// Prints the cursor for output within the member list of a class/struct.
QString PrintMember(CXCursor cursor) {
  CXCursorKind kind = clang_getCursorKind(cursor);
  QString cursorKindString = ClangString(clang_getCursorKindSpelling(kind)).ToQString();
  
  QString spellingString = ClangString(clang_getCursorSpelling(cursor)).ToQString();
  
  if (IsClassDeclLikeCursorKind(kind)) {
    return PrintRecordMember(cursor, kind, spellingString);
  } else if (kind == CXCursor_FunctionDecl ||
             kind == CXCursor_FunctionTemplate ||
             kind == CXCursor_CXXMethod ||
             kind == CXCursor_Constructor ||
             kind == CXCursor_Destructor ||
             kind == CXCursor_ConversionFunction) {
    return PrintFunctionMember(cursor, kind, spellingString);
  } else if (kind == CXCursor_FieldDecl ||
             kind == CXCursor_VarDecl ||
             kind == CXCursor_ParmDecl) {
    return PrintAttributeMember(cursor, kind, spellingString);
  }
  
  return spellingString;
}

// TODO: Sort members alphabetically?
CXChildVisitResult VisitClangAST_ListMembers(CXCursor cursor, CXCursor /*parent*/, CXClientData client_data) {
  MemberList* result = reinterpret_cast<MemberList*>(client_data);
  
  // Filter out cursors for AST elements that do not represent class/struct members.
  CXCursorKind kind = clang_getCursorKind(cursor);
  if (kind == CXCursor_CXXBaseSpecifier ||
      kind == CXCursor_CXXAccessSpecifier ||
      kind == CXCursor_FriendDecl ||
      kind == CXCursor_VisibilityAttr ||
      kind == CXCursor_UsingDeclaration ||
      kind == CXCursor_StaticAssert ||
      kind == CXCursor_TemplateTypeParameter ||
      kind == CXCursor_NonTypeTemplateParameter ||
      kind == CXCursor_TemplateTemplateParameter) {
    return CXChildVisit_Continue;
  }
  
  // Apply name filter, if any
  if (!result->nameFilter.isEmpty() &&
      ClangString(clang_getCursorSpelling(cursor)).ToQString() != result->nameFilter) {
    return CXChildVisit_Continue;
  }
  
  // Determine type of member
  MemberAccessStruct* memberStruct;
  if (kind == CXCursor_Constructor) {
    memberStruct = &result->constructors;
  } else if (kind == CXCursor_Destructor) {
    memberStruct = &result->destructors;
  } else if (kind == CXCursor_CXXMethod ||
             kind == CXCursor_FunctionTemplate) {
    memberStruct = &result->functions;
  } else if (kind == CXCursor_FieldDecl ||
             kind == CXCursor_VarDecl ||
             kind == CXCursor_EnumConstantDecl) {
    memberStruct = &result->attributes;
  } else if (kind == CXCursor_ClassDecl ||
             kind == CXCursor_StructDecl ||
             kind == CXCursor_UnionDecl ||
             kind == CXCursor_EnumDecl ||
             kind == CXCursor_ClassTemplate) {
    memberStruct = &result->records;
  } else {
    qDebug() << "Warning: Cursor kind not handled in VisitClangAST_ListMembers():"
             << ClangString(clang_getCursorKindSpelling(kind)).ToQString()
             << "with cursor spelling:"
             << ClangString(clang_getCursorSpelling(cursor)).ToQString();
    return CXChildVisit_Continue;
  }
  
  // Determine access specifier of member
  QString* desc;
  CX_CXXAccessSpecifier accessSpecifier = clang_getCXXAccessSpecifier(cursor);
  if (accessSpecifier == CX_CXXPublic) {
    desc = &memberStruct->members[MemberAccessStruct::Public];
  } else if (accessSpecifier == CX_CXXProtected) {
    desc = &memberStruct->members[MemberAccessStruct::Protected];
  } else if (accessSpecifier == CX_CXXPrivate) {
    desc = &memberStruct->members[MemberAccessStruct::Private];
  } else {
    desc = &memberStruct->members[MemberAccessStruct::Unknown];
  }
  
  // Print member
  if (!desc->isEmpty()) {
    *desc += "<br/>";
  }
  *desc += PrintMember(cursor);
  
  return CXChildVisit_Continue;
}


struct GetChildCursorState {
  CXCursor result = clang_getNullCursor();
  bool foundResultWithDefinition = false;
};

CXChildVisitResult VisitClangAST_GetChildCursor(CXCursor cursor, CXCursor /*parent*/, CXClientData client_data) {
  GetChildCursorState* state = reinterpret_cast<GetChildCursorState*>(client_data);
  
  bool hasDefinition = !clang_Cursor_isNull(clang_getCursorDefinition(cursor));
  if (!state->foundResultWithDefinition || hasDefinition) {
    state->result = cursor;
  }
  state->foundResultWithDefinition = hasDefinition;
  
//   qDebug() << "Child cursor kind: " << ClangString(clang_getCursorKindSpelling(clang_getCursorKind(cursor))).ToQString()
//            << " spelling:" << ClangString(clang_getCursorSpelling(cursor)).ToQString()
//            << " has definition:" << hasDefinition
//            << " parent spelling:" << ClangString(clang_getCursorSpelling(parent)).ToQString();
  
  return CXChildVisit_Recurse;
}

/// Attempts to find a child cursor that might be more useful for querying
/// information than the original cursor.
CXCursor GetChildCursorWithDefinition(CXCursor cursor) {
  GetChildCursorState state;
  clang_visitChildren(cursor, &VisitClangAST_GetChildCursor, &state);
  return state.result;
}


CXChildVisitResult VisitClangAST_ListBaseClasses(CXCursor cursor, CXCursor /*parent*/, CXClientData client_data) {
  QString* result = reinterpret_cast<QString*>(client_data);
  
  CXCursorKind cursorKind = clang_getCursorKind(cursor);
  // qDebug() << "Base class visit: Got " << ClangString(clang_getCursorKindSpelling(cursorKind)).ToQString();
  if (cursorKind == CXCursor_CXXBaseSpecifier) {
    if (!result->isEmpty()) {
      *result += ", ";
    }
    CX_CXXAccessSpecifier accessSpecifier = clang_getCXXAccessSpecifier(cursor);
    QString accessString;
    if (accessSpecifier == CX_CXXPublic) {
      accessString = "public ";
    } else if (accessSpecifier == CX_CXXProtected) {
      accessString = "protected ";
    } else if (accessSpecifier == CX_CXXPrivate) {
      accessString = "private ";
    }
    *result += accessString + PrintLinkToCursorInfo(cursor) + ClangString(clang_getCursorSpelling(clang_getCursorReferenced(cursor))).ToQString().toHtmlEscaped() + "</a>";
  } else if (cursorKind == CXCursor_TemplateTypeParameter ||
             cursorKind == CXCursor_TemplateTemplateParameter) {
    // For some reason, we encounter these before base classes that use template
    // arguments (even though we never recurse in the visit), so we need to
    // continue here to find all base classes.
    // NOTE: I actually observed only CXCursor_TemplateTypeParameter and added CXCursor_TemplateTemplateParameter.
    return CXChildVisit_Continue;
  } else if (cursorKind >= CXCursor_FirstAttr && cursorKind <= CXCursor_LastAttr) {
    return CXChildVisit_Continue;
  } else {
    // qDebug() << "VisitClangAST_ListBaseClasses() breaking on " << ClangString(clang_getCursorSpelling(cursor)).ToQString()
    //          << " type" << ClangString(clang_getCursorKindSpelling(clang_getCursorKind(cursor))).ToQString();
    return CXChildVisit_Break;
  }
  
  return CXChildVisit_Continue;
}


QString PrintClassMembers(
    CXCursor definition,
    std::shared_ptr<ClangTU> TU,
    const QString& memberNameFilter = "") {
  MemberList memberList;
  memberList.TU = TU;
  memberList.nameFilter = memberNameFilter;
  clang_visitChildren(definition, &VisitClangAST_ListMembers, &memberList);
  
  const QString accessHeadingStart = QStringLiteral("<hr/><b style=\"color:#cccccc;\">");  // <h4 style=\"color:#555555;\">
  const QString accessHeadingEnd = QStringLiteral("</b>");  // </h4>
  const QString categoryHeadingStart = QStringLiteral("<br/>");  // <h4 style=\"color:#cccccc;\">
  const QString categoryHeadingEnd = QStringLiteral("<br/>");  // </h4>
  const QString typeNames[4] = {"public", "protected", "private", "unknown"};
  
  QString membersString;
  for (int type = 0; type < MemberAccessStruct::NumTypes; ++ type) {
    if (!memberList.constructors.members[type].isEmpty() ||
        !memberList.destructors.members[type].isEmpty() ||
        !memberList.functions.members[type].isEmpty() ||
        !memberList.attributes.members[type].isEmpty() ||
        !memberList.records.members[type].isEmpty()) {
      membersString += accessHeadingStart + typeNames[type] + ":" + accessHeadingEnd;
      
      if (!memberList.constructors.members[type].isEmpty() ||
          !memberList.destructors.members[type].isEmpty()) {
        membersString += categoryHeadingStart + /*typeNames[type] + " constructors / destructors:" +*/ categoryHeadingEnd;
        membersString += memberList.constructors.members[type];
        if (!memberList.constructors.members[type].isEmpty() &&
            !memberList.destructors.members[type].isEmpty()) {
          membersString += QStringLiteral("<br/>");
        }
        membersString += memberList.destructors.members[type];
      }
      if (!memberList.functions.members[type].isEmpty()) {
        membersString += categoryHeadingStart + /*typeNames[type] + " functions:" +*/ categoryHeadingEnd;
        membersString += memberList.functions.members[type];
      }
      if (!memberList.attributes.members[type].isEmpty()) {
        membersString += categoryHeadingStart + /*typeNames[type] + " attributes:" +*/ categoryHeadingEnd;
        membersString += memberList.attributes.members[type];
      }
      if (!memberList.records.members[type].isEmpty()) {
        membersString += categoryHeadingStart + /*typeNames[type] + " records:" +*/ categoryHeadingEnd;
        membersString += memberList.records.members[type];
      }
    }
  }
  
  return membersString;
}


QString GetClassLikeRecordType(CXCursor definition, CXCursorKind definitionKind) {
  CXCursorKind recordKind = definitionKind;
  if (definitionKind == CXCursor_ClassTemplate) {
    recordKind = clang_getTemplateCursorKind(definition);
  }
  
  QString recordTypeString;
  if (recordKind == CXCursor_StructDecl) {
    recordTypeString = QStringLiteral("struct");
  } else if (recordKind == CXCursor_UnionDecl) {
    recordTypeString = QStringLiteral("union");
  } else if (recordKind == CXCursor_ClassDecl) {
    recordTypeString = QStringLiteral("class");
  } else if (recordKind == CXCursor_EnumDecl) {
    recordTypeString = QStringLiteral("enum");
  } else {
    recordTypeString = QObject::tr("(unknown)");
  }
  return recordTypeString;
}


QString PrintClassForwardDeclaration(
    CXCursor definition,
    CXCursorKind definitionKind,
    const QString& /*tokenString*/,
    const QString& USRString,
    const QString& accessString,
    const QString& commentString) {
  // Get the record type (e.g., "class" or "struct" or "union" ...)
  QString recordTypeString = GetClassLikeRecordType(definition, definitionKind);
  
  // Test whether the class is declared within a class.
  QString parentString;
  if (definitionKind != CXCursor_ParmDecl) {
    CXCursor parent = clang_getCursorSemanticParent(definition);
    if (!clang_Cursor_isNull(parent)) {
      CXCursorKind parentKind = clang_getCursorKind(parent);
      if (IsClassDeclLikeCursorKind(parentKind)) {
        parentString = PrintLinkToCursorInfo(parent) + ClangString(clang_getCursorSpelling(parent)).ToQString().toHtmlEscaped() + QStringLiteral("</a>::");
      }
    }
  }
  
  // Build the HTML.
  return QObject::tr("forward declaration: ") +
         recordTypeString + " " +
         parentString + "<b>" + ClangString(clang_getCursorSpelling(definition)).ToQString().toHtmlEscaped() + "</b>" +
         (accessString.isEmpty() ? "" : ("<br/><br/>" + accessString)) +
         (USRString.isEmpty() ? "" : ("<br/><br/>" + USRString)) +
         (commentString.isEmpty() ? "" : ("<br/><br/><i>" + commentString + "</i>"));
}


QString PrintClass(
    CXCursor definition,
    CXCursorKind definitionKind,
    bool isDefinition,
    const QString& /*tokenString*/,
    const QString& USRString,
    const QString& accessString,
    const QString& commentString,
    std::shared_ptr<ClangTU> TU) {
  // Retrieve the members.
  QString membersString = PrintClassMembers(definition, TU);
  
  // Get the record type (e.g., "class" or "struct" or "union" ...)
  QString recordTypeString = GetClassLikeRecordType(definition, definitionKind);
  
  // Get the base class(es).
  QString basesString;
  clang_visitChildren(definition, &VisitClangAST_ListBaseClasses, &basesString);
  if (!basesString.isEmpty()) {
    basesString = " : " + basesString;
  }
  
  // Test whether the class is declared within a class.
  QString parentString;
  if (definitionKind != CXCursor_ParmDecl) {
    CXCursor parent = clang_getCursorSemanticParent(definition);
    if (!clang_Cursor_isNull(parent)) {
      CXCursorKind parentKind = clang_getCursorKind(parent);
      if (IsClassDeclLikeCursorKind(parentKind)) {
        parentString = PrintLinkToCursorInfo(parent) + ClangString(clang_getCursorSpelling(parent)).ToQString().toHtmlEscaped() + QStringLiteral("</a>::");
      }
    }
  }
  
  // Build the HTML.
  return (clang_CXXRecord_isAbstract(definition) ? QStringLiteral("abstract ") : "") +
         recordTypeString + " " +
         parentString + "<b>" + ClangString(clang_getCursorSpelling(definition)).ToQString().toHtmlEscaped() + "</b>" + basesString +
         (accessString.isEmpty() ? "" : ("<br/><br/>" + accessString)) +
         (USRString.isEmpty() ? (isDefinition ? "" : ("<br/><br/>" + PrintLinkToDefinitionOrDeclarationLocation(definition))) : ("<br/><br/>" + USRString)) +
         (commentString.isEmpty() ? "" : ("<br/><br/><i>" + commentString + "</i>")) +
         membersString;
}


struct CheckIsFunctionSignalOrSlotVisitorData {
  QByteArray usr;
  bool isSignal;
  bool isSlot;
  bool found;
};

CXChildVisitResult VisitClangAST_CheckIsFunctionSignalOrSlot(CXCursor cursor, CXCursor /*parent*/, CXClientData client_data) {
  CheckIsFunctionSignalOrSlotVisitorData* data = reinterpret_cast<CheckIsFunctionSignalOrSlotVisitorData*>(client_data);
  CXCursorKind kind = clang_getCursorKind(cursor);
  
  // Remember the last encountered access specifier, watching out for annotations
  // that denote a Qt signals region.
  // (Note that these annotations are only present due to manually defining
  // the QT_ANNOTATE_ACCESS_SPECIFIER(x) accordingly, which is done by
  // CompileSettings::BuildCommandLineArgs().)
  if (kind == CXCursor_CXXAccessSpecifier) {
    data->isSignal = false;
    data->isSlot = false;
    return CXChildVisit_Recurse;  // Recurse to get possible CXCursor_AnnotateAttr cursors
  } else if (kind == CXCursor_AnnotateAttr) {
    QString annotation = ClangString(clang_getCursorSpelling(cursor)).ToQString();
    if (annotation == QStringLiteral("qt_signal")) {
      data->isSignal = true;
    } else if (annotation == QStringLiteral("qt_slot")) {
      data->isSlot = true;
    }
    return CXChildVisit_Recurse;
  }
  
  // If we found the declaration, stop.
  if (data->usr == ClangString(clang_getCursorUSR(cursor)).ToQByteArray()) {
    data->found = true;
    return CXChildVisit_Break;
  }
  
  return CXChildVisit_Continue;
}

void CheckIsFunctionSignalOrSlot(CXCursor declarationOrDefinition, CXCursor parent, bool* isSignal, bool* isSlot) {
  CheckIsFunctionSignalOrSlotVisitorData visitorData;
  visitorData.usr = ClangString(clang_getCursorUSR(declarationOrDefinition)).ToQByteArray();
  visitorData.isSignal = false;
  visitorData.isSlot = false;
  visitorData.found = false;
  
  clang_visitChildren(parent, &VisitClangAST_CheckIsFunctionSignalOrSlot, &visitorData);
  
  if (!visitorData.found) {
    qDebug() << "Warning: CheckIsFunctionSignalOrSlot() did not find the function declaration while visiting";
  }
  *isSignal = visitorData.isSignal;
  *isSlot = visitorData.isSlot;
}


QString PrintFunction(
    CXCursor definition,
    CXCursorKind definitionKind,
    bool isDefinition,
    const QString& tokenString,
    const QString& USRString,
    const QString& accessString,
    const QString& commentString) {
  // List the return type and (template) parameters of the function.
  int numArgs = clang_Cursor_getNumArguments(definition);
  QString argsString;
  if (numArgs == -1) {
    argsString = QObject::tr("failed to determine arguments");
  } else {
    for (int argIdx = 0; argIdx < numArgs; ++ argIdx) {
      if (!argsString.isEmpty()) {
        argsString += ",";
      }
      argsString += "<br/>&nbsp;&nbsp;&nbsp;&nbsp;";
      
      // TODO: Use clang_getNumArgTypes and clang_getArgType? Or is the cursor
      //       below sufficient?
      CXCursor argCursor = clang_Cursor_getArgument(definition, argIdx);
      argsString += PrintType(clang_getCursorType(argCursor)) + " " + ClangString(clang_getCursorSpelling(argCursor)).ToQString().toHtmlEscaped();
    }
  }
  
  if (clang_Cursor_isVariadic(definition)) {
    if (!argsString.isEmpty()) {
      argsString += ",";
    }
    argsString += "<br/>&nbsp;&nbsp;&nbsp;&nbsp;";
    
    argsString += "...";
  }
  
  // Get the return type.
  QString returnType = PrintType(clang_getCursorResultType(definition));
  
  bool isInlined = clang_Cursor_isFunctionInlined(definition);
  bool isDefaulted = clang_CXXMethod_isDefaulted(definition);
  bool isVirtual = clang_CXXMethod_isVirtual(definition);
  bool isPureVirtual = clang_CXXMethod_isPureVirtual(definition);
  
  // Show and link the function that the current function overrides (if any)
  QString overriddenString;
  std::vector<CXCursor> overrideWorkList = {definition};
  std::vector<CXCursor> overriddenCursors;
  while (!overrideWorkList.empty()) {
    CXCursor current = overrideWorkList.back();
    overrideWorkList.pop_back();
    
    CXCursor* overridden;
    unsigned numOverridden;
    clang_getOverriddenCursors(current, &overridden, &numOverridden);
    for (int i = 0; i < numOverridden; ++ i) {
      CXCursor candidate = overridden[i];
      
      // Check whether we have this override already
      bool isDuplicate = false;
      for (CXCursor other : overriddenCursors) {
        if (clang_equalCursors(candidate, other)) {
          isDuplicate = true;
          break;
        }
      }
      
      if (!isDuplicate) {
        overriddenCursors.push_back(candidate);
        overrideWorkList.push_back(candidate);
        
        if (overriddenString.isEmpty()) {
          overriddenString = QStringLiteral("<br/><br/>");
        } else {
          overriddenString += QStringLiteral("<br/>");
        }
        
        CXCursor overrideParent = clang_getCursorSemanticParent(candidate);
        overriddenString += QObject::tr("Overrides %1::%2")
            .arg(PrintLinkToCursorInfo(overrideParent) + ClangString(clang_getCursorSpelling(overrideParent)).ToQString() + QStringLiteral("</a>"))
            .arg(PrintLinkToCursorInfo(candidate) + ClangString(clang_getCursorSpelling(candidate)).ToQString() + QStringLiteral("</a>"));
      }
    }
    if (overridden) {
      clang_disposeOverriddenCursors(overridden);
    }
  }
  
  // TODO: List known functions overriding the current function (if any)
  
  QString extraText;
  if (clang_CXXConstructor_isConvertingConstructor(definition)) {
    extraText = "<br/>(converting constructor)";
  } else if (clang_CXXConstructor_isCopyConstructor(definition)) {
    extraText = "<br/>(copy constructor)";
  } else if (clang_CXXConstructor_isDefaultConstructor(definition)) {
    extraText = "<br/>(default constructor)";
  } else if (clang_CXXConstructor_isMoveConstructor(definition)) {
    extraText = "<br/>(move constructor)";
  }
  
  // Test whether the function is declared within a class (this applies
  // both to class members and to static functions within classes).
  QString parentString;
  QString accessWithSignalOrSlotString = accessString;
  if (definitionKind != CXCursor_ParmDecl) {
    CXCursor parent = clang_getCursorSemanticParent(definition);
    if (!clang_Cursor_isNull(parent)) {
      CXCursorKind parentKind = clang_getCursorKind(parent);
      if (IsClassDeclLikeCursorKind(parentKind)) {
        parentString = PrintLinkToCursorInfo(parent) + ClangString(clang_getCursorSpelling(parent)).ToQString().toHtmlEscaped() + QStringLiteral("</a>::");
        
        bool isSignal, isSlot;
        CheckIsFunctionSignalOrSlot(definition, parent, &isSignal, &isSlot);
        if (isSignal) {
          accessWithSignalOrSlotString += QStringLiteral(" <span style=\"background-color:#e8e8e8;\">signal</span>");
        } else if (isSlot) {
          accessWithSignalOrSlotString += QStringLiteral(" <span style=\"background-color:#e8e8e8;\">slot</span>");
        }
      }
    }
  }
  
  QString storageClassString = GetStorageClassString(definition);
  if (!storageClassString.isEmpty()) {
    storageClassString += QStringLiteral(" ");
  }
  
  // Build the HTML.
  return QObject::tr("%1%2%3%4 %5<b>%6</b>(%7)%8%9%10%11%12%15%13%14")
      .arg(storageClassString)
      .arg(isVirtual ? QStringLiteral("virtual ") : "")
      .arg(isInlined ? QStringLiteral("inline ") : "")
      .arg(returnType)
      .arg(parentString)
      .arg(tokenString.toHtmlEscaped())
      .arg(argsString)
      .arg(clang_CXXMethod_isConst(definition) ? QStringLiteral(" const") : (""))
      .arg(isDefaulted ? QStringLiteral(" = default") : "")
      .arg(isPureVirtual ? QStringLiteral(" = 0") : "")
      .arg(accessWithSignalOrSlotString.isEmpty() ? "" : ("<br/><br/>" + accessWithSignalOrSlotString))
      .arg(extraText)
      .arg(commentString.isEmpty() ? "" : QStringLiteral("<br/><br/><i>%1</i>").arg(commentString))
      .arg(overriddenString)
      .arg((USRString.isEmpty() ? (isDefinition ? "" : ("<br/><br/>" + PrintLinkToDefinitionOrDeclarationLocation(definition))) : ("<br/><br/>" + USRString)));
}


QString PrintAttribute(
    CXCursor definition,
    CXCursorKind definitionKind,
    bool isDefinition,
    const QString& tokenString,
    const QString& USRString,
    const QString& accessString,
    const QString& commentString,
    const QString& valueString) {
  CXType definitionType = clang_getCursorType(definition);
  
  // Determine the size of the type
  long long size = clang_Type_getSizeOf(definitionType);
  QString sizeString;
  if (size == CXTypeLayoutError_Invalid) {
    sizeString = QObject::tr("invalid");
  } else if (size == CXTypeLayoutError_Incomplete) {
    sizeString = QObject::tr("incomplete");
  } else if (size == CXTypeLayoutError_Dependent) {
    sizeString = QObject::tr("dependent");
  } else {
    sizeString = QObject::tr("%1 bytes").arg(QString::number(size));
  }
  
  // Determine the alignment of the type
  long long align = clang_Type_getAlignOf(definitionType);
  QString alignString;
  if (align == CXTypeLayoutError_Invalid) {
    alignString = QObject::tr("invalid");
  } else if (align == CXTypeLayoutError_Incomplete) {
    alignString = QObject::tr("incomplete");
  } else if (align == CXTypeLayoutError_Dependent) {
    alignString = QObject::tr("dependent");
  } else if (align == CXTypeLayoutError_NotConstantSize) {
    alignString = QObject::tr("not constant size");
  } else {
    alignString = QObject::tr("%1 bytes").arg(QString::number(align));
  }
  
  // Get storage class
  QString storageClassString = GetStorageClassString(definition);
  
  // For class members
  bool isMutable = false;
  bool haveOffset = false;
  QString offsetString;
  if (definitionKind == CXCursor_FieldDecl) {
    // Determine whether the member is mutable
    isMutable = clang_CXXField_isMutable(definition);
    
    // Determine the offset of the member in the class layout
    haveOffset = true;
    long long int offset = clang_Cursor_getOffsetOfField(definition);
    if (offset == -1) {
      // The cursor is not a field declaration.
      haveOffset = false;
    } else if (offset == CXTypeLayoutError_Invalid) {
      // The cursor semantic parent is not a record field declaration.
      offsetString = QObject::tr("invalid");
    } else if (offset == CXTypeLayoutError_Incomplete) {
      // The field's type declaration is an incomplete type.
      offsetString = QObject::tr("incomplete");
    } else if (offset == CXTypeLayoutError_Dependent) {
      offsetString = QObject::tr("dependent");
    } else {
      offsetString = QObject::tr("%1 bytes").arg(QString::number(offset / 8));
    }
  }
  
  if (!storageClassString.isEmpty()) {
    storageClassString += QStringLiteral(" ");
  }
  
  // Test whether the attribute is declared within a class (this applies
  // both to class members and to static variables within classes).
  QString parentString;
  if (definitionKind != CXCursor_ParmDecl) {
    CXCursor parent = clang_getCursorSemanticParent(definition);
    if (!clang_Cursor_isNull(parent)) {
      CXCursorKind parentKind = clang_getCursorKind(parent);
      if (IsClassDeclLikeCursorKind(parentKind)) {
        parentString = PrintLinkToCursorInfo(parent) + ClangString(clang_getCursorSpelling(parent)).ToQString().toHtmlEscaped() + QStringLiteral("</a>::");
      }
    }
  }
  
  // Build HTML string
  return QObject::tr("%10%1%2 %9<b>%3</b><br/>%11%4<br/>%5%12%6Size: %7<br/>Aligned to: %8")
      .arg(isMutable ? "mutable " : "")
      .arg(PrintType(definitionType))
      .arg(tokenString.toHtmlEscaped())
      .arg(commentString.isEmpty() ? "" : QStringLiteral("<br/><i>%1</i><br/>").arg(commentString))
      .arg(valueString.isEmpty() ? "" : QObject::tr("<b>Value: %1</b><br/><br/>").arg(valueString.toHtmlEscaped()))
      .arg(haveOffset ? QObject::tr("Offset in parent: %1<br/>").arg(offsetString) : "")
      .arg(sizeString)
      .arg(alignString)
      .arg(parentString)
      .arg(storageClassString)
      .arg(accessString.isEmpty() ? "" : ("<br/>" + accessString + "<br/>"))
      .arg((USRString.isEmpty() ? (isDefinition ? "" : (PrintLinkToDefinitionOrDeclarationLocation(definition) + "<br/><br/>")) : (USRString + "<br/><br/>")));
}


QString PrintTypedef(
    CXCursor definition,
    bool isDefinition,
    const QString& tokenString,
    const QString& accessString,
    const QString& commentString) {
  return QStringLiteral("typedef <b>") + tokenString.toHtmlEscaped() + QStringLiteral("</b><br/>") +
         QObject::tr("Refers to: %1").arg(PrintType(clang_getTypedefDeclUnderlyingType(definition))) +
         (accessString.isEmpty() ? QStringLiteral("") : (QStringLiteral("<br/><br/>") + accessString)) +
         (isDefinition ? "" : (QStringLiteral("<br/><br/>") + PrintLinkToDefinitionOrDeclarationLocation(definition))) +
         (commentString.isEmpty() ? "" : QStringLiteral("<br/><br/><i>%1</i>").arg(commentString));
}


QString PrintEnumConstant(
    CXCursor definition,
    bool isDefinition,
    const QString& tokenString,
    const QString& commentString) {
  return QStringLiteral("enum constant <b>") + tokenString.toHtmlEscaped() + QStringLiteral("</b><br/>") +
         QObject::tr("<b>Value: %1</b>").arg(clang_getEnumConstantDeclValue(definition)) +
         (isDefinition ? "" : (QStringLiteral("<br/><br/>") + PrintLinkToDefinitionOrDeclarationLocation(definition))) +
         (commentString.isEmpty() ? "" : QStringLiteral("<br/><br/><i>%1</i>").arg(commentString));
}


QString PrintNamespace(
    CXCursor /*definition*/,
    const QString& tokenString,
    const QString& commentString) {
  return QStringLiteral("namespace <b>") + tokenString.toHtmlEscaped() + QStringLiteral("</b>") +
         (commentString.isEmpty() ? "" : QStringLiteral("<br/><br/><i>%1</i>").arg(commentString));
}


QString PrintTemplateTypeParameter(
    CXCursor definition,
    bool isDefinition,
    const QString& tokenString) {
  return QStringLiteral("template type parameter <b>") + tokenString.toHtmlEscaped() + QStringLiteral("</b>") +
         (isDefinition ? "" : (QStringLiteral("<br/><br/>") + PrintLinkToDefinitionOrDeclarationLocation(definition)));
}


QString PrintNonTypeTemplateParameter(
    CXCursor definition,
    bool isDefinition,
    const QString& /*tokenString*/,
    const std::shared_ptr<ClangTU>& TU) {
  return QStringLiteral("non-type template parameter <b>") + GetClangText(clang_getCursorExtent(definition), TU->TU()).toHtmlEscaped() + QStringLiteral("</b>") +
         (isDefinition ? "" : (QStringLiteral("<br/><br/>") + PrintLinkToDefinitionOrDeclarationLocation(definition)));
}


QString PrintOverloadedDeclRef(
    CXCursor definition,
    const QString& tokenString) {
  unsigned numDecls = clang_getNumOverloadedDecls(definition);
  if (numDecls == 0) {
    qDebug() << "Warning: Got 0 declarations for a CXCursor_OverloadedDeclRef cursor";
  }
  
  QString overloadsString;
  for (int i = 0; i < numDecls; ++ i) {
    if (!overloadsString.isEmpty()) {
      overloadsString += "<br/>";
    }
    
    CXCursor overload = clang_getOverloadedDecl(definition, i);
    if (clang_Cursor_isNull(overload)) {
      overloadsString += "(failed to determine overload)";
    } else {
      CXCursor overloadDefinition = clang_getCursorDefinition(overload);
      if (!clang_Cursor_isNull(overloadDefinition)) {
        overload = overloadDefinition;
      }
      
      // TODO: This is not actually necessarily a member of anything. Rename the print function to something like PrintFunctionShort() / PrintFunctionSignature?
      overloadsString += PrintFunctionMember(
          overload,
          clang_getCursorKind(overload),
          ClangString(clang_getCursorSpelling(overload)).ToQString());
    }
  }
  
  return QStringLiteral("<b>") + tokenString + QStringLiteral("</b><br/>") +
         QObject::tr("(reference to a set of overloaded functions or function templates,<br/>dependent on template parameters)") +
         (overloadsString.isEmpty() ? "" : (QStringLiteral("<br/><br/>Overload candidates:<br/>") + overloadsString));
}


QString PrintMacroDefinition(
    CXCursor definition,
    const QString& tokenString,
    const QString& commentString,
    const std::shared_ptr<ClangTU>& TU) {
  bool isFunctionLike = clang_Cursor_isMacroFunctionLike(definition);
  bool isMacroBuiltin = clang_Cursor_isMacroBuiltin(definition);
  QString qualifierString;
  if (isFunctionLike) {
    qualifierString += "function-like ";
  }
  if (isMacroBuiltin) {
    qualifierString += "builtin ";
  }
  
  // qDebug() << "macro definition cursor spelling: " << ClangString(clang_getCursorSpelling(definition)).ToQString();
  // qDebug() << "macro definition cursor extent text: " << GetClangText(clang_getCursorExtent(definition), TU->TU());
  
  return qualifierString + QStringLiteral("macro <b>") + tokenString.toHtmlEscaped() + QStringLiteral("</b>") +
         (commentString.isEmpty() ? "" : QStringLiteral("<br/><br/><i>%1</i>").arg(commentString)) +
         QObject::tr("<br/><br/><b>Defined to:</b><br/>%1").arg(
             GetClangText(clang_getCursorExtent(definition), TU->TU()).toHtmlEscaped().replace('\t', "  ").replace('\n', "<br/>")) +
         QStringLiteral("<br/><br/>") + PrintLinkToDefinitionOrDeclarationLocation(definition);  // TODO: Account for current tab width setting
}


static CXVisitorResult VisitReferences(void* context, CXCursor /*cursor*/, CXSourceRange range) {
  // If the reference is within a macro, the range is invalid. In this case,
  // we ignore it.
  if (clang_Range_isNull(range)) {
    return CXVisit_Continue;
  }
  
  std::vector<CXSourceRange>* referenceRanges = static_cast<std::vector<CXSourceRange>*>(context);
  referenceRanges->push_back(range);
  
  return CXVisit_Continue;
}


GetInfoOperation::Result GetInfoOperation::OperateOnTU(
    const CodeInfoRequest& request,
    const std::shared_ptr<ClangTU>& TU,
    const QString& canonicalFilePath,
    int invocationLine,
    int invocationCol,
    std::vector<CXUnsavedFile>& /*unsavedFiles*/) {
  constexpr bool kDebug = false;
  
  if (kDebug) {
    qDebug() << "--- DEBUG GetInfo() results ---";
  }
  
  // Set the infoTokenRange output variable to null
  infoTokenRange = clang_getNullRange();
  
  // Try to get a cursor for the given source location
  CXFile clangFile = clang_getFile(TU->TU(), canonicalFilePath.toUtf8().data());
  if (clangFile == nullptr) {
    qDebug() << "Warning: GetInfo(): Cannot get the CXFile for" << canonicalFilePath << "in the TU.";
    return Result::TUHasNotBeenReparsed;
  }
  
  CXSourceLocation requestLocation = clang_getLocation(TU->TU(), clangFile, invocationLine + 1, invocationCol + 1);
  CXCursor cursor = clang_getCursor(TU->TU(), requestLocation);
  if (clang_Cursor_isNull(cursor)) {
    return Result::TUHasNotBeenReparsed;
  }
  
  // --- Get information about the cursor ---
  
  if (kDebug) {
    qDebug() << "Cursor extent:" << GetClangText(clang_getCursorExtent(cursor), TU->TU());
    qDebug() << "Cursor spelling:" << ClangString(clang_getCursorSpelling(cursor)).ToQString();
  }
  
  // Get cursor kind.
  CXCursorKind kind = clang_getCursorKind(cursor);
  if (kDebug) {
    CXString kindString = clang_getCursorKindSpelling(kind);
    qDebug() << "Cursor kind:" << QString::fromUtf8(clang_getCString(kindString));
    clang_disposeString(kindString);
  }
  
  // Get the token under the cursor. Note that simply using clang_getToken()
  // does not work, since its use would require to know the location at which
  // the token starts.
  bool tokenFound = false;
  QString tokenString;
  
  unsigned invocationOffset;
  clang_getSpellingLocation(requestLocation, /*file*/ nullptr, /*line*/ nullptr, /*column*/ nullptr, &invocationOffset);
  
  CXToken* tokens;
  unsigned numTokens;
  clang_tokenize(TU->TU(), clang_getCursorExtent(cursor), &tokens, &numTokens);
  for (int tokenIndex = 0; tokenIndex < numTokens; ++ tokenIndex) {
    CXSourceRange tokenRange = clang_getTokenExtent(TU->TU(), tokens[tokenIndex]);
    
    CXSourceLocation start = clang_getRangeStart(tokenRange);
    unsigned startOffset;
    clang_getSpellingLocation(start, /*file*/ nullptr, /*line*/ nullptr, /*column*/ nullptr, &startOffset);
    if (startOffset > invocationOffset) {
      continue;
    }
    
    CXSourceLocation end = clang_getRangeEnd(tokenRange);
    unsigned endOffset;
    clang_getSpellingLocation(end, /*file*/ nullptr, /*line*/ nullptr, /*column*/ nullptr, &endOffset);
    if (endOffset <= invocationOffset) {
      continue;
    }
    
    // Found the token under the cursor.
    tokenString = ClangString(clang_getTokenSpelling(TU->TU(), tokens[tokenIndex])).ToQString();
    
    if (request.dropUninterestingTokens) {
      CXTokenKind tokenKind = clang_getTokenKind(tokens[tokenIndex]);
      if (((tokenKind == CXToken_Keyword) &&
          (tokenString != QStringLiteral("break") && tokenString != QStringLiteral("continue"))) ||
          tokenKind == CXToken_Literal ||
          tokenKind == CXToken_Comment ||
          (tokenKind == CXToken_Punctuation &&
          (tokenString == QStringLiteral("{") ||
            tokenString == QStringLiteral("}") ||
            tokenString == QStringLiteral(";")))) {
        // The token under the cursor is of an uninteresting kind. Stop.
        clang_disposeTokens(TU->TU(), tokens, numTokens);
        return Result::TUHasNotBeenReparsed;
      }
    }
    
    infoTokenRange = tokenRange;
    
    tokenFound = true;
    break;
  }
  clang_disposeTokens(TU->TU(), tokens, numTokens);
  
  if (kDebug) {
    if (tokenFound) {
      qDebug() << "Token:" << tokenString;
    } else {
      qDebug() << "Token not found!";
    }
  }
  
  // Prefer the cursor spelling over the raw token, if available. Otherwise,
  // when hovering the '(' for a constructor call, we would show 'ClassName::('
  // instead of 'ClassName::ClassName'.
  QString spellingString = ClangString(clang_getCursorSpelling(cursor)).ToQString();
  if (!spellingString.isEmpty()) {
    tokenString = spellingString;
  }
  
  // Get the cursor's type.
  CXType type = clang_getCursorType(cursor);
  QString typeString = PrintType(type);
  
  CXType canonicalType = clang_getCanonicalType(type);
  if (!clang_equalTypes(type, canonicalType)) {
    typeString += QObject::tr(" (which is: %1)").arg(PrintType(canonicalType));
  }
  
  if (kDebug) {
    qDebug() << "Type of cursor:" << typeString;
  }
  
  // TODO: For calls, query whether they are virtual with clang_Cursor_isDynamicCall()
  
  // Query the definition and collect information about it:
  // - If it's a class/struct, try to iterate over the resulting cursor's
  //   children to assemble a list of members
  // - If it's a function, get the parameter list
  // - If it's a variable/member, get the type
  // TODO: Would it make sense to perform this in another part of this
  //       background task, ensuring that the translation unit is fully
  //       re-parsed first?
  // TODO: Could it be that "definition" is now misnamed and could point to a
  //       declaration as well?
  CXCursor definition = clang_getCursorDefinition(cursor);
  if (clang_Cursor_isNull(definition) &&
      (kind == CXCursor_Constructor ||
       kind == CXCursor_Destructor ||
       kind == CXCursor_CXXMethod)) {
    definition = cursor;
  } else if (clang_Cursor_isNull(definition) /*&&
             (kind == CXCursor_MemberRefExpr ||
              kind == CXCursor_DeclRefExpr)*/) {
    definition = clang_getCursorReferenced(cursor);
  }
  if (kind == CXCursor_CallExpr && clang_Cursor_isNull(definition)) {
    // Attempt to descend into the CallExpr to get a cursor that yields a usable definition.
    // TODO: This might yield a definition that is irrelevant to the original invocation location, however.
    //       This issue here should probably be fixed in libclang instead.
    CXCursor childCursor = GetChildCursorWithDefinition(cursor);
    definition = clang_getCursorDefinition(childCursor);
  }
  if (!clang_Cursor_isNull(definition)) {
    // Get file/line/column of the definition
    CXSourceLocation definitionLocation = clang_getCursorLocation(definition);
    CXFile definitionFile;
    unsigned definitionLine, definitionColumn;
    clang_getFileLocation(
        definitionLocation,
        &definitionFile,
        &definitionLine,
        &definitionColumn,
        nullptr);
    QString definitionFilePath = GetClangFilePath(definitionFile);
    if (kDebug) {
      qDebug() << "Found definition at:" << definitionFilePath + QStringLiteral(":") + QString::number(definitionLine) + QStringLiteral(":") + QString::number(definitionColumn);
    }
    
    // Search for additional declarations or a definition via USRs.
    std::unordered_set<QString> relevantFiles;
    
    RunInQtThreadBlocking([&]() {
      // If the document has been closed in the meantime, we must not access its
      // widget anymore.
      if (request.wasCanceled) {
        return;
      }
      
      USRStorage::Instance().GetFilesForUSRLookup(canonicalFilePath, request.widget->GetMainWindow(), &relevantFiles);
    });
    
    // In the USRStorage, search over all these files for the USR.
    std::vector<std::pair<QString, USRDecl>> foundDecls;  // pair of file path and USR
    USRStorage::Instance().LookupUSRs(
        ClangString(clang_getCursorUSR(definition)).ToQByteArray(),
        relevantFiles,
        &foundDecls);
    
    // If we did not find the definition that we have via a libclang cursor via
    // an USR, add it.
    bool definitionCursorFoundViaUSR = false;
    for (const auto& item : foundDecls) {
      if (definitionLine == item.second.line &&
          definitionColumn == item.second.column &&
          definitionFilePath == item.first) {
        definitionCursorFoundViaUSR = true;
        break;
      }
    }
    
    // qDebug() << "definitionCursorFoundViaUSR:" << definitionCursorFoundViaUSR << " (cursor:" << definitionFilePath << ":" << definitionLine << ":" << definitionColumn;
    if (!definitionCursorFoundViaUSR) {
      foundDecls.push_back(std::make_pair(definitionFilePath, USRDecl("", definitionLine, definitionColumn, clang_isCursorDefinition(definition), clang_getCursorKind(definition))));
    }
    // Build definition and declarations string.
    QString USRDefinition;
    QString USRDeclarations;
    int numDeclarations = 0;
    
    for (const std::pair<QString, USRDecl>& pathAndDecl : foundDecls) {
      if (pathAndDecl.second.isDefinition) {
        // Definition.
        USRDefinition = QObject::tr("Definition: %1").arg(
            PrintLinkToLocation(pathAndDecl.first, pathAndDecl.second.line, pathAndDecl.second.column));
      } else {
        // Declaration.
        ++ numDeclarations;
        
        if (!USRDeclarations.isEmpty()) {
          USRDeclarations += QStringLiteral("<br/>");
        }
        USRDeclarations += QObject::tr("Declaration: %1").arg(
            PrintLinkToLocation(pathAndDecl.first, pathAndDecl.second.line, pathAndDecl.second.column));
      }
    }
    if (IsClassDeclLikeCursorKind(clang_getCursorKind(definition))) {
      // Hide the declarations for classes, since there may be very many of them,
      // and they are not too useful
      // TODO: Make clicking this line show the list of all declarations?
      USRDeclarations = QObject::tr("(%1 declarations)").arg(numDeclarations);
    }
    QString USRString =
        USRDefinition +
        ((USRDefinition.isEmpty() || USRDeclarations.isEmpty()) ? QStringLiteral("") : QStringLiteral("<br/>")) +
        USRDeclarations;
    
    // Get the access specifier
    QString accessString;
    CX_CXXAccessSpecifier accessSpecifier = clang_getCXXAccessSpecifier(definition);
    if (accessSpecifier == CX_CXXPublic) {
      accessString = QStringLiteral("<span style=\"background-color:#e8ffe8;\">public</span>");
    } else if (accessSpecifier == CX_CXXProtected) {
      accessString = QStringLiteral("<span style=\"background-color:#ffffdd;\">protected</span>");
    } else if (accessSpecifier == CX_CXXPrivate) {
      accessString = QStringLiteral("<span style=\"background-color:#ffe8e8;\">private</span>");
    }
    
    // Get the comment.
    QString commentString = ClangString(clang_Cursor_getRawCommentText(definition)).ToQString();
    if (commentString.startsWith("/*") && commentString.endsWith("*/")) {
      commentString = commentString.mid(2, commentString.size() - 4).toHtmlEscaped().replace('\n', "<br/>");
    } else {
      int startToRemoveAt = 0;
      while (true) {
        int pos = commentString.indexOf("//", startToRemoveAt);
        if (pos == -1) {
          break;
        }
        pos += 2;
        while (pos < commentString.size() &&
              commentString[pos] == '/') {
          ++ pos;
        }
        
        commentString.remove(startToRemoveAt, pos - startToRemoveAt);
        
        startToRemoveAt = commentString.indexOf('\n', startToRemoveAt);
        ++ startToRemoveAt;
        if (startToRemoveAt >= commentString.size()) {
          break;
        }
      }
      
      commentString = commentString.toHtmlEscaped();
      commentString.replace('\n', "<br/>");
    }
    
    // If there is no comment, try getting documentation via QtHelp.
    if (commentString.isEmpty()) {
      // Get the fully-qualified name of the definition
      // (e.g., "std::unordered_map" instead of only "unordered_map").
      QString lookupName;
      CXCursor currentCursor = definition;
      while (!clang_Cursor_isNull(currentCursor)) {
        lookupName = ClangString(clang_getCursorSpelling(currentCursor)).ToQString() + (lookupName.isEmpty() ? QStringLiteral("") : (QStringLiteral("::") + lookupName));
        currentCursor = clang_getCursorSemanticParent(currentCursor);
        
        if (clang_getCursorKind(currentCursor) == CXCursor_TranslationUnit) {
          break;
        }
      }
      
      // CXPrintingPolicy printingPolicy = clang_getCursorPrintingPolicy(definition);
      // clang_PrintingPolicy_setProperty(printingPolicy, CXPrintingPolicy_TerseOutput, 1);  // print declaration only, skip body
      // clang_PrintingPolicy_setProperty(printingPolicy, CXPrintingPolicy_FullyQualifiedName, 1);
      // clang_PrintingPolicy_setProperty(printingPolicy, CXPrintingPolicy_SuppressTemplateArgsInCXXConstructors, 1);
      // QString lookupName = ClangString(clang_getCursorPrettyPrinted(definition, printingPolicy)).ToQString();
      // clang_PrintingPolicy_dispose(printingPolicy);
      
      while (true) {
        helpUrl = QtHelp::Instance().QueryIdentifier(lookupName);
        if (helpUrl.isValid()) {
          break;
        } else {
          // Try removing the part after the last :: and search again.
          int doubleColonPos = lookupName.lastIndexOf("..");
          if (doubleColonPos >= 0) {
            lookupName.chop(lookupName.size() - doubleColonPos);
          } else {
            break;
          }
        }
      }
    }
    
    // Try to evaluate the cursor if it has a constant value.
    QString valueString;
    for (int attempt = 0; attempt < 2; ++ attempt) {
      CXCursor cursorToEvaluate;
      if (attempt == 0) {
        cursorToEvaluate = cursor;
      } else {
        cursorToEvaluate = definition;
      }
      
      // We need to exclude non-const types here, otherwise we will get their
      // initial value, which may be modified later and is thus not what we
      // want here.
      if (!clang_isConstQualifiedType(clang_getCursorType(cursorToEvaluate))) {
        continue;
      }
      
      CXEvalResult evalResult = clang_Cursor_Evaluate(cursorToEvaluate);
      switch (clang_EvalResult_getKind(evalResult)) {
      case CXEval_Int:
        if (clang_EvalResult_isUnsignedInt(evalResult)) {
          valueString = QObject::tr("%1 (unsigned integer)").arg(QString::number(clang_EvalResult_getAsUnsigned(evalResult)));
        } else {
          valueString = QObject::tr("%1 (signed integer)").arg(QString::number(clang_EvalResult_getAsLongLong(evalResult)));
        }
        break;
      case CXEval_Float:
        valueString = QObject::tr("%1 (floating-point number)").arg(QString::number(clang_EvalResult_getAsDouble(evalResult)));
        break;
      case CXEval_ObjCStrLiteral:
      case CXEval_StrLiteral:
      case CXEval_CFStr:
      case CXEval_UnExposed:
      case CXEval_Other:
        valueString = QString::fromUtf8(clang_EvalResult_getAsStr(evalResult));
        break;
      }
      clang_EvalResult_dispose(evalResult);
      
      if (!valueString.isEmpty()) {
        break;
      }
    }
    
    // Collect more information about the definition based on its type
    CXCursorKind definitionKind = clang_getCursorKind(definition);
    if (definitionKind == CXCursor_OverloadedDeclRef &&
        clang_getNumOverloadedDecls(definition) == 1) {
      CXCursor onlyOverloadDefinition = clang_getCursorDefinition(clang_getOverloadedDecl(definition, 0));
      if (!clang_Cursor_isNull(onlyOverloadDefinition)) {
        if (kDebug) {
          qDebug() << "Debug: Replacing a CXCursor_OverloadedDeclRef definition with its only overload";
        }
        definition = onlyOverloadDefinition;
      }
    }
    
    if (IsClassDeclLikeCursorKind(definitionKind)) {
      if (!clang_isCursorDefinition(definition)) {
        htmlString = PrintClassForwardDeclaration(
            definition,
            definitionKind,
            tokenString,
            USRString,
            accessString,
            commentString);
      } else {
        htmlString = PrintClass(
            definition,
            definitionKind,
            clang_equalCursors(cursor, definition),
            tokenString,
            USRString,
            accessString,
            commentString,
            TU);
      }
    } else if (definitionKind == CXCursor_FunctionDecl ||
               definitionKind == CXCursor_FunctionTemplate ||
               definitionKind == CXCursor_CXXMethod ||
               definitionKind == CXCursor_Constructor ||
               definitionKind == CXCursor_Destructor ||
               definitionKind == CXCursor_ConversionFunction) {
      htmlString = PrintFunction(
          definition,
          definitionKind,
          clang_equalCursors(cursor, definition),
          tokenString,
          USRString,
          accessString,
          commentString);
    } else if (definitionKind == CXCursor_FieldDecl ||
               definitionKind == CXCursor_VarDecl ||
               definitionKind == CXCursor_ParmDecl) {
      htmlString = PrintAttribute(
          definition,
          definitionKind,
          clang_equalCursors(cursor, definition),
          tokenString,
          USRString,
          accessString,
          commentString,
          valueString);
    } else if (definitionKind == CXCursor_TypedefDecl) {
      htmlString = PrintTypedef(
          definition,
          clang_equalCursors(cursor, definition),
          tokenString,
          accessString,
          commentString);
    } else if (definitionKind == CXCursor_EnumConstantDecl) {
      htmlString = PrintEnumConstant(
          definition,
          clang_equalCursors(cursor, definition),
          tokenString,
          commentString);
    } else if (definitionKind == CXCursor_Namespace) {
      htmlString = PrintNamespace(
          definition,
          tokenString,
          commentString);
    } else if (definitionKind == CXCursor_TemplateTypeParameter) {
      htmlString = PrintTemplateTypeParameter(
          definition,
          clang_equalCursors(cursor, definition),
          tokenString);
    } else if (definitionKind == CXCursor_NonTypeTemplateParameter) {
      htmlString = PrintNonTypeTemplateParameter(
          definition,
          clang_equalCursors(cursor, definition),
          tokenString,
          TU);
    } else if (definitionKind == CXCursor_OverloadedDeclRef) {
      htmlString = PrintOverloadedDeclRef(
          definition,
          tokenString);
    } else if (definitionKind == CXCursor_MacroDefinition) {
      htmlString = PrintMacroDefinition(
          definition,
          tokenString,
          commentString,
          TU);
    } else {
      // Unsupported definition kind - print the info that we have
      qDebug() << "Code info: Unsupported definition kind:" << ClangString(clang_getCursorKindSpelling(definitionKind)).ToQString();
      
      if (!typeString.isEmpty()) {
        typeString = QObject::tr("<br/>Type: %1").arg(typeString);
      }
      
      QString referenceString = QObject::tr("<br/><br/>References %1").arg(
          PrintLinkToCursorInfo(definition) +
          ClangString(clang_getCursorSpelling(definition)).ToQString().toHtmlEscaped() +
          "</a>");
      
      htmlString = QObject::tr("<b>%1</b>%2%3%4")
          .arg(tokenString)
          .arg(typeString)
          .arg(valueString.isEmpty() ? "" : QObject::tr("<br/><br/><b>Value: %1</b>").arg(valueString.toHtmlEscaped()))
          .arg(referenceString);
    }
  }
  
  // Check for CXCursor_MemberRefExpr without a definition. At least in one case
  // that I debugged, this happened due to a CXXDependentScopeMemberExpr, meaning
  // that the member was actually not resolved in clang's AST. However, what we
  // can try to do is to compile a set of possible candidates.
  if (clang_Cursor_isNull(definition) &&
      kind == CXCursor_MemberRefExpr) {
    htmlString = QObject::tr("Member reference which could not be resolved,<br/>possibly because it depends on a template parameter.");
    
    // Try to get the class that the member is from
    CXCursor containingObjectCursor = GetChildCursorWithDefinition(cursor);
    CXCursor containingObjectDefinition = clang_getCursorDefinition(containingObjectCursor);
    if (kDebug) {
      qDebug() << "containingObjectDefinition spelling:" << ClangString(clang_getCursorSpelling(containingObjectDefinition)).ToQString();
    }
    CXType containingObjectType = clang_getCursorType(containingObjectDefinition);
    if (kDebug) {
      qDebug() << "Type: " << ClangString(clang_getTypeSpelling(containingObjectType)).ToQString();
    }
    while (containingObjectType.kind == CXType_Pointer ||
           containingObjectType.kind == CXType_RValueReference ||
           containingObjectType.kind == CXType_LValueReference) {
      containingObjectType = clang_getPointeeType(containingObjectType);
      if (kDebug) {
        qDebug() << "Pointed-to type: " << ClangString(clang_getTypeSpelling(containingObjectType)).ToQString();
      }
    }
    CXCursor objectDeclaration = clang_getTypeDeclaration(containingObjectType);
    if (kDebug) {
      qDebug() << "objectDeclaration spelling:" << ClangString(clang_getCursorSpelling(objectDeclaration)).ToQString();
    }
    if (!clang_Cursor_isNull(objectDeclaration) &&
        clang_getCursorKind(objectDeclaration) != CXCursor_NoDeclFound) {
      // Search the class definition for members whose spelling matches
      // tokenString and list those as possible candidates.
      QString membersString = PrintClassMembers(objectDeclaration, TU, tokenString);
      if (!membersString.isEmpty()) {
        htmlString += QObject::tr("<br/>Possible referenced members include:%1").arg(membersString);
      }
    }
  }
  
  // Find references to this cursor's definition in the given file (request.pathForReferences).
  referenceRanges.clear();
  
  CXFile referencesFile = clang_getFile(TU->TU(), request.pathForReferences.toUtf8().data());
  if (kind == CXCursor_ContinueStmt || kind == CXCursor_BreakStmt) {
    // Special case for "continue" and "break":
    // Highlight the statement itself and the for/while/do which it refers to.
    // Unfortunately, clang_getCursorSemanticParent() first returns the function in which
    // the continue/break statement occurs, skipping the for/while/do/etc. statements.
    // clang_getCursorLexicalParent() seemed to return a null cursor.
    // So, we have to get the semantic parent, iterate over all its children, and remember
    // the last for/while/do encountered before the continue/break cursor.
    if (clang_File_isEqual(referencesFile, clangFile)) {
      CXCursor containerCursor;
      if (FindContainerStatementForContinueOrBreak(cursor, &containerCursor)) {
        CXToken* highlightToken = clang_getToken(TU->TU(), clang_getCursorLocation(containerCursor));
        referenceRanges.push_back(clang_getTokenExtent(TU->TU(), *highlightToken));
        clang_disposeTokens(TU->TU(), highlightToken, 1);
        // TODO: The commented-out line would add the whole range of the for/while/... to the highlight.
        //       With the above, we only highlight the start. Should we also highlight the end of the range somehow?
        // referenceRanges.push_back(clang_getCursorExtent(containerCursor));
        referenceRanges.push_back(clang_getCursorExtent(cursor));
        if (kind == CXCursor_BreakStmt) {
          htmlString = QObject::tr("<b>break</b><br/>(highlighting the statement that this breaks out of)");
        } else {
          htmlString = QObject::tr("<b>continue</b><br/>(highlighting the statement that this continues)");
        }
      }
    }
  } else {
    if (referencesFile == nullptr) {
      qDebug() << "Warning: GetInfo(): Cannot get the CXFile for" << request.pathForReferences << "in the TU for finding references.";
    } else {
      CXCursorAndRangeVisitor referencesVisitor;
      referencesVisitor.context = &referenceRanges;
      referencesVisitor.visit = &VisitReferences;
      clang_findReferencesInFile(cursor, referencesFile, referencesVisitor);
    }
  }
  
  if (kDebug) {
    qDebug() << "Final HTML sent to DocumentWidget:\n" << htmlString;
  }
  
  return Result::TUHasNotBeenReparsed;
}

void GetInfoOperation::FinalizeInQtThread(const CodeInfoRequest& request) {
  // // If the request is not up-to-date anymore (the document's invocation
  // // counter differs), discard the results.
  // // TODO: This has been commented out, as the previous codeInfoCounter was changed to a
  // //       codeCompletionCounter. Can we remove this, or was this helpful?
  // if (request.widget->GetCodeInfoInvocationCounter() != request.invocationCounter) {
  //   return;
  // }
  
  // Convert CXSourceRanges to DocumentRanges
  Document* document = request.widget->GetDocument().get();
  std::vector<unsigned> lineOffsets;
  lineOffsets.resize(document->LineCount());
  Document::LineIterator lineIt(document);
  int lineIndex = 0;
  while (lineIt.IsValid()) {
    lineOffsets[lineIndex] = lineIt.GetLineStart().offset;
    
    ++ lineIndex;
    ++ lineIt;
  }
  if (lineIndex != lineOffsets.size()) {
    qDebug() << "Error: Line iterator returned a different line count than Document::LineCount().";
  }
  
  DocumentRange tokenDocumentRange;
  if (clang_Range_isNull(infoTokenRange)) {
    tokenDocumentRange = DocumentRange::Invalid();
  } else {
    tokenDocumentRange = CXSourceRangeToDocumentRange(infoTokenRange, lineOffsets);
  }
  
  std::vector<DocumentRange> referenceDocumentRanges;
  referenceDocumentRanges.reserve(referenceRanges.size());
  for (CXSourceRange range : referenceRanges) {
    if (!clang_Range_isNull(range)) {
      referenceDocumentRanges.push_back(CXSourceRangeToDocumentRange(range, lineOffsets));
    }
  }
  
  // Make the gathered information available to the DocumentWidget
  request.widget->SetCodeTooltip(tokenDocumentRange, htmlString, helpUrl, referenceDocumentRanges);
}
