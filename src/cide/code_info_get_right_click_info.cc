// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/code_info_get_right_click_info.h"

#include "cide/clang_utils.h"
#include "cide/main_window.h"
#include "cide/clang_parser.h"
#include "cide/qt_help.h"
#include "cide/qt_thread.h"


GetRightClickInfoOperation::Result GetRightClickInfoOperation::OperateOnTU(
    const CodeInfoRequest& /*request*/,
    const std::shared_ptr<ClangTU>& TU,
    const QString& canonicalFilePath,
    int invocationLine,
    int invocationCol,
    std::vector<CXUnsavedFile>& /*unsavedFiles*/) {
  clickedCursorUSR = QStringLiteral("");
  clickedCursorSpelling = QStringLiteral("");
  clickedTokenSpelling = QStringLiteral("");
  clickedTokenSpellingRange = clang_getNullRange();
  cursorHasLocalDefinition = false;
  
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
  
  // Find the token at the given source location
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
    clickedTokenSpelling = ClangString(clang_getTokenSpelling(TU->TU(), tokens[tokenIndex])).ToQString();
    clickedTokenSpellingRange = tokenRange;
    
    break;
  }
  clang_disposeTokens(TU->TU(), tokens, numTokens);
  
  
  // --- Get information about the cursor ---
  clickedCursorSpelling = ClangString(clang_getCursorSpelling(cursor)).ToQString();
  
  CXCursor referencedCursor = clang_getCursorReferenced(cursor);
  if (!clang_Cursor_isNull(referencedCursor)) {
    clickedCursorUSR = ClangString(clang_getCursorUSR(referencedCursor)).ToQString();
  }
  
  // Check whether the cursor's definition is within a function body.
  CXCursor definition = clang_getCursorDefinition(cursor);
  if (!clang_Cursor_isNull(definition)) {
    CXCursor definitionParent = clang_getCursorSemanticParent(definition);
    while (!clang_Cursor_isNull(definitionParent)) {
      if (IsFunctionDeclLikeCursorKind(clang_getCursorKind(definitionParent))) {
        cursorHasLocalDefinition = true;
        break;
      }
      definitionParent = clang_getCursorSemanticParent(definitionParent);
    }
  }
  
  // qDebug() << "-- right click: --";
  // qDebug() << "clickedCursorUSR: " << clickedCursorUSR;
  // qDebug() << "clickedCursorSpelling: " << clickedCursorSpelling;
  
  return Result::TUHasNotBeenReparsed;
}

void GetRightClickInfoOperation::FinalizeInQtThread(const CodeInfoRequest& request) {
  // // If the request is not up-to-date anymore (the document's invocation
  // // counter differs), discard the results.
  // // TODO: This has been commented out, as the previous codeInfoCounter was changed to a
  // //       codeCompletionCounter. Can we remove this, or was this helpful?
  // if (request.widget->GetCodeInfoInvocationCounter() != request.invocationCounter) {
  //   return;
  // }
  
  // Convert CXSourceRange to DocumentRange
  // TODO: There is a copy of this code in code_info_get_info.cc
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
  
  DocumentRange clickedTokenRange;
  if (clang_Range_isNull(clickedTokenSpellingRange)) {
    clickedTokenRange = DocumentRange::Invalid();
  } else {
    clickedTokenRange = CXSourceRangeToDocumentRange(clickedTokenSpellingRange, lineOffsets);
  }
  
  // Show the right-click menu
  request.widget->ShowRightClickMenu(clickedCursorUSR, clickedCursorSpelling, cursorHasLocalDefinition, clickedTokenSpelling, clickedTokenRange);
}
