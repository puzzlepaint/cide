// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/code_info_code_completion.h"

#include "cide/clang_parser.h"
#include "cide/clang_utils.h"
#include "cide/cpp_utils.h"
#include "cide/main_window.h"
#include "cide/qt_thread.h"

bool IsCursorOutsideOfAnyClassOrFunctionDefinition(CXCursor cursor) {
  CXCursor currentCursor = cursor;
  do {
    CXCursorKind kind = clang_getCursorKind(currentCursor);
    if (kind == CXCursor_TranslationUnit) {
      break;
    }
    if (IsFunctionDeclLikeCursorKind(kind) ||
        IsClassDeclLikeCursorKind(kind)) {
      return false;
    }
    
    currentCursor = clang_getCursorSemanticParent(currentCursor);
  } while (!clang_Cursor_isNull(currentCursor));
  return true;
}

CodeCompletionOperation::~CodeCompletionOperation() {
  if (results && !success) {
    clang_disposeCodeCompleteResults(results);
  }
}

inline void CodeCompletionOperation::InitializeInQtThread(
    const CodeInfoRequest& request,
    const std::shared_ptr<ClangTU>& /*TU*/,
    const QString& canonicalFilePath,
    int /*invocationLine*/,
    int /*invocationCol*/,
    std::vector<CXUnsavedFile>& /*unsavedFiles*/) {
  if (!GuessIsHeader(canonicalFilePath, nullptr)) {
    correspondingHeaderPath = FindCorrespondingHeaderOrSource(canonicalFilePath, request.widget->GetMainWindow()->GetProjects());
    // qDebug() << "Implementation completion debug: Guessed correspondingHeaderPath:" << correspondingHeaderPath;
  } else {
    // qDebug() << "Implementation completion debug: Guessed that we are in a header, not checking for corresponding file.";
  }
  
  cursorIsOutsideOfAnyClassOrFunctionDefinition = request.widget->GetDocument()->GetContextsAt(request.codeCompletionInvocationLocation).empty();
}

CodeCompletionOperation::Result CodeCompletionOperation::OperateOnTU(
    const CodeInfoRequest& request,
    const std::shared_ptr<ClangTU>& TU,
    const QString& canonicalFilePath,
    int invocationLine,
    int invocationCol,
    std::vector<CXUnsavedFile>& unsavedFiles) {
  TUOperationBase::Result result = Result::TUHasNotBeenReparsed;
  
  // If code completion is invoked in a place where we might want to show "Implement <...>" completion items,
  // do a full re-parse first. This is necessary to pick up added function declarations in header files
  // if code completion is invoked in the corresponding source files before those are re-parsed.
  // TODO: Should we only do this if we know that the corresponding header changed since the last parse?
  if (cursorIsOutsideOfAnyClassOrFunctionDefinition) {
    // qDebug() << "Implementation completion debug: Triggering reparse since code completion is invoked outside of a context";
    CXErrorCode parseResult = static_cast<CXErrorCode>(clang_reparseTranslationUnit(
        TU->TU(),
        unsavedFiles.size(),
        unsavedFiles.data(),
        clang_defaultReparseOptions(TU->TU())));
    if (parseResult == CXError_Success) {
      result = TUOperationBase::Result::TUHasBeenReparsed;
    }
  }
  
  // Perform code completion.
  unsigned options =
      CXCodeComplete_IncludeMacros |
      // CXCodeComplete_IncludeCodePatterns |
      CXCodeComplete_IncludeBriefComments |
      CXCodeComplete_SkipPreamble |
      CXCodeComplete_IncludeCompletionsWithFixIts;
  results = clang_codeCompleteAt(
      TU->TU(),
      canonicalFilePath.toUtf8().data(),
      invocationLine + 1,  // cursorLine + 1,
      invocationCol + 1,  // std::min(layoutLines[cursorLine].size() + 1, cursorCol + 1),
      unsavedFiles.data(),
      unsavedFiles.size(),
      options);
  // Obtain an up-to-date CXFile after the possible re-parse
  if (correspondingHeaderPath.isEmpty()) {
    correspondingHeader = nullptr;
  } else {
    correspondingHeader = clang_getFile(TU->TU(), correspondingHeaderPath.toLocal8Bit());
    // qDebug() << "Implementation completion debug: Got correspondingHeader:" << correspondingHeader;
  }
  if (results) {
    CreateCodeCompletionItems();
  }
  
  // Create custom "Implement <...>" completion items.
  if (cursorIsOutsideOfAnyClassOrFunctionDefinition) {
    CreateImplementationCompletionItems(request, TU, canonicalFilePath, invocationLine, invocationCol);
  }
  
  return result;
}

void CodeCompletionOperation::FinalizeInQtThread(const CodeInfoRequest& request) {
  if (!results) {
    qDebug() << "clang_codeCompleteAt() failed";
    request.widget->CloseCodeCompletion();
    return;
  }
  
  // If the request is not up-to-date anymore (the document's invocation
  // counter differs), discard the results.
  if (request.widget->GetCodeCompletionInvocationCounter() != request.invocationCounter) {
    // Here, there is no need to call request.widget->CloseCodeCompletion(), as
    // a new request should be on the way already.
    return;
  }
  
  if (results->NumResults == 0) {
    request.widget->CloseCodeCompletion();
    return;
  }
  
  // Show or close code completion
  if (items.empty()) {
    request.widget->CloseCodeCompletion();
  } else {
    request.widget->ShowCodeCompletion(request.codeCompletionInvocationLocation, std::move(items), results);
  }
  
  // Show or close argument hint
  if (hints.empty()) {
    request.widget->CloseArgumentHint();
  } else {
    request.widget->ShowArgumentHint(request.codeCompletionInvocationLocation, std::move(hints), currentParameter);
  }
  
  success = true;
}

void CodeCompletionOperation::CreateCodeCompletionItems() {
  // NOTE: Not sure whether the information below is of any use for us
  //   unsigned long long codeCompleteContexts = clang_codeCompleteGetContexts(results);
  //   qDebug() << "clang_codeCompleteGetContexts:" << codeCompleteContexts;
  //   unsigned isIncomplete;
  //   CXCursorKind containerKind = clang_codeCompleteGetContainerKind(results, &isIncomplete);
  //   qDebug() << "clang_codeCompleteGetContainerKind: containerKind:" << containerKind << "isIncomplete:" << isIncomplete;
  //   CXString containerUSR = clang_codeCompleteGetContainerUSR(results);
  //   qDebug() << "clang_codeCompleteGetContainerUSR:" << clang_getCString(containerUSR);
  //   clang_disposeString(containerUSR);
  
  // Build the code completion items
  items.reserve(results->NumResults);
  hints.reserve(16);
  
  for (int resultIndex = 0; resultIndex < results->NumResults; ++ resultIndex) {
    CXCompletionResult& result = results->Results[resultIndex];
    if (result.CursorKind == CXCursor_OverloadCandidate) {
      // This information is not useful as a code completion item itself.
      // However, it is useful for parameter hint display, and indirectly
      // for code completion since it says which return types are appropriate.
      // The latter is already used internally by libclang for its priority
      // scores I think, so we do not use it ourselves for this.
      int activeParameter;
      hints.emplace_back(results, resultIndex, &activeParameter);
      if (activeParameter != -1) {
        if (currentParameter >= 0 &&
            currentParameter != activeParameter) {
          qDebug() << "Error: Got different current-parameter indices from different function overloads:" << currentParameter << "vs" << activeParameter;
        }
        currentParameter = activeParameter;
      }
      continue;
    }
    
    if (clang_getCompletionAvailability(result.CompletionString) != CXAvailability_Available) {
      // NOTE: We currently fully discard un-available items (such as private
      //       class members for accesses outside of the class). We might display
      //       them in cases where the entered text is very similar to them.
      continue;
    }
    
    items.emplace_back(results, resultIndex);
    
    // NOTE: Debug code which outputs much of the information that libclang provides for completion items
    // qDebug() << "- result" << resultIndex;
    // 
    // qDebug() << "  CursorKind:" << result.CursorKind;
    // 
    // qDebug() << "  Priority (smaller is better):" << clang_getCompletionPriority(completion);
    // 
    // CXAvailabilityKind availability = clang_getCompletionAvailability(completion);
    // qDebug() << "  Availability:" << availability;
    // 
    // unsigned numFixits = clang_getCompletionNumFixIts(results, resultIndex);
    // qDebug() << "  numFixits" << numFixits;
    // for (int fixitIndex = 0; fixitIndex < numFixits; ++ fixitIndex) {
    //   CXSourceRange replacementRange;
    //   CXString replacement = clang_getCompletionFixIt(results, resultIndex, fixitIndex, &replacementRange);
    //   
    //   qDebug() << "  - Fixit with replacement" << clang_getCString(replacement);
    //   
    //   clang_disposeString(replacement);
    // }
    // 
    // unsigned numChunks = clang_getNumCompletionChunks(completion);
    // for (int chunkIndex = 0; chunkIndex < numChunks; ++ chunkIndex) {
    //   CXCompletionChunkKind kind = clang_getCompletionChunkKind(completion, chunkIndex);
    //   CXString text = clang_getCompletionChunkText(completion, chunkIndex);
    //   
    //   qDebug() << "  - Chunk kind" << kind << "text" << clang_getCString(text);
    //   
    //   clang_disposeString(text);
    //   
    //   // TODO: Also iterate over any children obtained with:
    //   //       clang_getCompletionChunkCompletionString()
    // }
    // 
    // unsigned numAnnotations = clang_getCompletionNumAnnotations(completion);
    // for (int annotationIndex = 0; annotationIndex < numAnnotations; ++ annotationIndex) {
    //   CXString annotation = clang_getCompletionAnnotation(completion, annotationIndex);
    //   qDebug() << "  - Annotation" << clang_getCString(annotation);
    //   clang_disposeString(annotation);
    // }
    // 
    // CXString parent = clang_getCompletionParent(completion, nullptr);
    // qDebug() << "  Parent:" << clang_getCString(parent);
    // clang_disposeString(parent);
    // 
    // CXString comment = clang_getCompletionBriefComment(completion);
    // qDebug() << "  Comment:" << clang_getCString(comment);
    // clang_disposeString(comment);
  }
}

struct FindUnimplementedFunctionsVisitorData {
  const CodeInfoRequest* request;
  QString canonicalFilePath;
  CXFile invocationFile;
  CXFile headerFile;
  std::vector<CompletionItem>* items;
  QString invocationScopeQualifiers;
  bool inQtSignalsRegion;
};

QString GetCursorScopeQualifiers(CXCursor cursor) {
  QString result = QStringLiteral("");
  
  while (!clang_Cursor_isNull(cursor)) {
    CXCursorKind kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_TranslationUnit) {
      break;
    } else if (kind == CXCursor_Namespace || IsClassDeclLikeCursorKind(kind)) {
      result = ClangString(clang_getCursorSpelling(cursor)).ToQString() + QStringLiteral("::") + result;
    }
    
    cursor = clang_getCursorSemanticParent(cursor);
  }
  
  return result;
}

/// Determines the common prefix of scopeAtAccessLocation and accessedScope, ending at "::",
/// and removes it from accessedScope.
QString PrintRequiredScopeQualifiers(QString scopeAtAccessLocation, const QString& accessedScope) {
  int commonPrefixLen = 0;
  
  int minLen = std::min(scopeAtAccessLocation.size(), accessedScope.size());
  for (int i = 0; i < minLen; ++ i) {
    if (scopeAtAccessLocation[i] == accessedScope[i]) {
      if (i > 0 && accessedScope[i] == ':' && accessedScope[i - 1] == ':') {
        commonPrefixLen = i + 1;
      }
    } else {
      break;
    }
  }
  
  return accessedScope.mid(commonPrefixLen);
}

QString PrintTypeForImplementationCompletion(CXType type, const QString& currentScope) {
  // If this is an array type, split off the array bracket [] and print the
  // underlying type.
  CXType arrayElemType = clang_getArrayElementType(type);
  if (arrayElemType.kind != CXType_Invalid) {
    QString typeSpelling = ClangString(clang_getTypeSpelling(type)).ToQString();
    int openArrayBracketPos = typeSpelling.lastIndexOf('[');
    if (openArrayBracketPos > 0) {
      return PrintTypeForImplementationCompletion(arrayElemType, currentScope) + typeSpelling.mid(openArrayBracketPos);
    }
  }
  
  CXCursor declCursor = clang_getTypeDeclaration(type);
  if (!clang_Cursor_isNull(declCursor) &&
      clang_getCursorKind(declCursor) != CXCursor_NoDeclFound) {
    QString typeSpelling = ClangString(clang_getTypeSpelling(type)).ToQString();
    
    // Separate the possible "const " prefix from the rest of the type spelling
    bool isConst = false;
    if (typeSpelling.startsWith(QStringLiteral("const "))) {
      isConst = true;
      typeSpelling = typeSpelling.mid(6);
    }
    
    // If there are template arguments, also exclude them
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
          replacedNonLinkTypeSpelling += nonLinkTypeSpelling.mid(replacementPos, argPos - replacementPos);
        }
        replacedNonLinkTypeSpelling += PrintTypeForImplementationCompletion(argType, currentScope);
        replacementPos = argPos + argSpelling.size();
      }
    }
    
    if (ok && !replacedNonLinkTypeSpelling.isEmpty()) {
      if (nonLinkTypeSpelling.size() > replacementPos) {
        replacedNonLinkTypeSpelling += nonLinkTypeSpelling.mid(replacementPos, nonLinkTypeSpelling.size() - replacementPos);
      }
      nonLinkTypeSpelling = replacedNonLinkTypeSpelling;
    }
    
    return (isConst ? QStringLiteral("const ") : QStringLiteral("")) +
           PrintRequiredScopeQualifiers(currentScope, typeSpelling) +
           nonLinkTypeSpelling;
  }
  
  // Check whether we have a pointer or reference type.
  if (type.kind == CXType_Pointer) {
    CXType pointeeType = clang_getPointeeType(type);
    if (pointeeType.kind != CXType_Invalid) {
      return PrintTypeForImplementationCompletion(pointeeType, currentScope) + QStringLiteral("*");
    }
  } else if (type.kind == CXType_RValueReference ||
             type.kind == CXType_LValueReference) {
    CXType pointeeType = clang_getPointeeType(type);
    if (pointeeType.kind != CXType_Invalid) {
      return PrintTypeForImplementationCompletion(pointeeType, currentScope) + QStringLiteral("&");
    }
  }
  
  // We failed to decompose the type, thus print it as-is.
  return ClangString(clang_getTypeSpelling(type)).ToQString();
}

CXChildVisitResult VisitClangAST_FindUnimplementedFunctions(CXCursor cursor, CXCursor /*parent*/, CXClientData client_data) {
  FindUnimplementedFunctionsVisitorData* data = reinterpret_cast<FindUnimplementedFunctionsVisitorData*>(client_data);
  
  // Skip over cursors in irrelevant files
  CXFile cursorFile;
  clang_getFileLocation(
      clang_getCursorLocation(cursor),
      &cursorFile,
      nullptr,
      nullptr,
      nullptr);
  if (!clang_File_isEqual(cursorFile, data->invocationFile) &&
      (!data->headerFile || !clang_File_isEqual(cursorFile, data->headerFile))) {
    return CXChildVisit_Continue;
  }
  
  // Descend into namespaces and classes and extern "C" { ... }
  CXCursorKind kind = clang_getCursorKind(cursor);
  if (kind == CXCursor_Namespace ||
      kind == CXCursor_UnexposedDecl ||  // this is required to recurse into: extern "C" { ... }
      IsClassDeclLikeCursorKind(kind)) {
    return CXChildVisit_Recurse;
  }
  
  // Remember the last encountered access specifier, watching out for annotations
  // that denote a Qt signals region. We should not suggest to implement those
  // functions. (Note that these annotations are only present due to manually defining
  // the QT_ANNOTATE_ACCESS_SPECIFIER(x) accordingly, which is done by
  // CompileSettings::BuildCommandLineArgs().)
  if (kind == CXCursor_CXXAccessSpecifier) {
    data->inQtSignalsRegion = false;
    return CXChildVisit_Recurse;  // Recurse to get possible CXCursor_AnnotateAttr cursors
  } else if (kind == CXCursor_AnnotateAttr) {
    QString annotation = ClangString(clang_getCursorSpelling(cursor)).ToQString();
    if (annotation == QStringLiteral("qt_signal")) {
      data->inQtSignalsRegion = true;
    }
    return CXChildVisit_Continue;
  } else if (data->inQtSignalsRegion) {
    return CXChildVisit_Continue;
  }
  
  // Skip over all cursors but function-like cursors.
  if (!IsFunctionDeclLikeCursorKind(kind)) {
    return CXChildVisit_Continue;
  }
  
  // We got a function-like cursor. If it is a definition, skip.
  if (clang_isCursorDefinition(cursor) || clang_Cursor_isFunctionInlined(cursor)) {
    return CXChildVisit_Continue;
  }
  
  // Pure virtual functions must not have an implementation.
  if (clang_CXXMethod_isPureVirtual(cursor)) {
    return CXChildVisit_Continue;
  }
  
  // Filter out a few Qt-specific functions that are added by the moc mechanism in Qt
  // and should thus not be implemented manually.
  QString functionName = ClangString(clang_getCursorSpelling(cursor)).ToQString();
  if (functionName == QStringLiteral("metaObject") &&
      clang_CXXMethod_isConst(cursor) &&
      ClangString(clang_getTypeSpelling(clang_getCursorResultType(cursor))).ToQString().startsWith(QStringLiteral("const QMetaObject")) &&
      clang_Cursor_getNumArguments(cursor) == 0) {
    return CXChildVisit_Continue;
  } else if (functionName == QStringLiteral("qt_metacast") &&
      ClangString(clang_getTypeSpelling(clang_getCursorResultType(cursor))).ToQString().startsWith(QStringLiteral("void")) &&
      clang_Cursor_getNumArguments(cursor) == 1) {
    return CXChildVisit_Continue;
  } else if (functionName == QStringLiteral("qt_metacall") &&
      ClangString(clang_getTypeSpelling(clang_getCursorResultType(cursor))).ToQString().startsWith(QStringLiteral("int")) &&
      clang_Cursor_getNumArguments(cursor) == 3) {
    return CXChildVisit_Continue;
  } else if (functionName == QStringLiteral("qt_static_metacall") &&
      ClangString(clang_getTypeSpelling(clang_getCursorResultType(cursor))).ToQString().startsWith(QStringLiteral("void")) &&
      clang_Cursor_getNumArguments(cursor) == 4) {
    return CXChildVisit_Continue;
  }
  
  // We have a declaration without definition. Try to find the definition via USRs.
  std::unordered_set<QString> relevantFiles;
  
  bool exit = false;
  RunInQtThreadBlocking([&]() {
    // If the document has been closed in the meantime, we must not access its
    // widget anymore.
    if (data->request->wasCanceled) {
      exit = true;
      return;
    }
    USRStorage::Instance().GetFilesForUSRLookup(data->canonicalFilePath, data->request->widget->GetMainWindow(), &relevantFiles);
  });
  if (exit) {
    return CXChildVisit_Break;
  }
  std::vector<std::pair<QString, USRDecl>> foundDecls;  // pair of file path and USR
  USRStorage::Instance().LookupUSRs(
      ClangString(clang_getCursorUSR(cursor)).ToQByteArray(),
      relevantFiles,
      &foundDecls);
  
  for (const auto& item : foundDecls) {
    if (item.second.isDefinition) {
      return CXChildVisit_Continue;
    }
  }
  
  // We did not find a definition. Create an implementation completion item.
  // - Remove "static", "override", "virtual" if present.
  // - Remove default values for arguments.
  // - Add qualifiers if required, e.g., turn SomeNestedStruct into
  //   Container::SomeNestedStruct within the return type of a function in Container.
  //   Or add a namespace if the class is defined within one, but we are currently
  //   not in it.
  
  QString declarationQualifiers = GetCursorScopeQualifiers(clang_getCursorSemanticParent(cursor));
  QString definitionQualifiers = PrintRequiredScopeQualifiers(data->invocationScopeQualifiers, declarationQualifiers);
  
  QString completionString;
  QString qualifiedFunctionName;
  
  if (kind != CXCursor_Constructor &&
      kind != CXCursor_Destructor) {
    completionString += PrintTypeForImplementationCompletion(clang_getCursorResultType(cursor), data->invocationScopeQualifiers) + QStringLiteral(" ");
  }
  qualifiedFunctionName = definitionQualifiers + functionName;
  completionString += qualifiedFunctionName + QStringLiteral("(");
  int numArgs = clang_Cursor_getNumArguments(cursor);
  QString argsString;
  if (numArgs == -1) {
    argsString = QObject::tr("failed to determine arguments");
  } else {
    for (int argIdx = 0; argIdx < numArgs; ++ argIdx) {
      if (!argsString.isEmpty()) {
        argsString += ", ";
      }
      
      CXCursor argCursor = clang_Cursor_getArgument(cursor, argIdx);
      CXString argSpelling = clang_getCursorSpelling(argCursor);
      QString argName = QString::fromUtf8(clang_getCString(argSpelling));
      clang_disposeString(argSpelling);
      
      argsString += PrintTypeForImplementationCompletion(clang_getCursorType(argCursor), declarationQualifiers);
      argsString += " ";
      argsString += argName;
    }
  }
  if (clang_Cursor_isVariadic(cursor)) {
    if (!argsString.isEmpty()) {
      argsString += ", ";
    }
    argsString += "...";
  }
  completionString += argsString + QStringLiteral(")");
  if (clang_CXXMethod_isConst(cursor)) {
    completionString += QStringLiteral(" const");
  }
  completionString += QStringLiteral(" {\n  \n}");
  
  data->items->emplace_back();
  CompletionItem& newItem = data->items->back();
  newItem.displayText = QStringLiteral("-> Implement: %1").arg(qualifiedFunctionName);
  newItem.returnTypeText = QStringLiteral("");
  newItem.displayStyles.emplace_back(std::make_pair(0, CompletionItem::DisplayStyle::FilterText));
  newItem.displayStyles.emplace_back(std::make_pair(14, CompletionItem::DisplayStyle::Placeholder));
  newItem.filterText = completionString;
  newItem.clangCompletionIndex = -1;
  newItem.numFixits = 0;
  newItem.isAvailable = true;
  newItem.priority = 0;  // always show the implementation completions at the beginning
  
  return CXChildVisit_Continue;
}

void CodeCompletionOperation::CreateImplementationCompletionItems(
    const CodeInfoRequest& request,
    const std::shared_ptr<ClangTU>& TU,
    const QString& canonicalFilePath,
    int invocationLine,
    int invocationCol) {
  // Try to get a cursor for the given source location
  CXFile clangFile = clang_getFile(TU->TU(), canonicalFilePath.toUtf8().data());
  if (clangFile == nullptr) {
    return;
  }
  
  CXSourceLocation requestLocation = clang_getLocation(TU->TU(), clangFile, invocationLine + 1, invocationCol + 1);
  CXCursor cursor = clang_getCursor(TU->TU(), requestLocation);
  if (clang_Cursor_isNull(cursor)) {
    return;
  }
  
  if (!IsCursorOutsideOfAnyClassOrFunctionDefinition(cursor)) {
    return;
  }
  
  // Iterate over all function declarations before the cursor in this file.
  // If we think that the current file is a source file, also include the header that we believe corresponds to this source file.
  // We cannot simply iterate over all headers, since implementations of functions in libraries might be linked
  // in without being visible to the compiler. So we might get lots of invalid items from those.
  // For each visited declaration, if no definition is found for it via USRs, an implementation completion item is added for it.
  FindUnimplementedFunctionsVisitorData visitorData;
  visitorData.request = &request;
  visitorData.canonicalFilePath = canonicalFilePath;
  visitorData.invocationFile = clangFile;
  visitorData.headerFile = correspondingHeader;
  visitorData.items = &items;
  visitorData.invocationScopeQualifiers = GetCursorScopeQualifiers(cursor);
  visitorData.inQtSignalsRegion = false;
  
  clang_visitChildren(clang_getTranslationUnitCursor(TU->TU()), &VisitClangAST_FindUnimplementedFunctions, &visitorData);
}
