// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/code_info_goto_referenced_cursor.h"

#include "cide/clang_utils.h"
#include "cide/main_window.h"
#include "cide/clang_parser.h"
#include "cide/qt_thread.h"

void GotoReferencedCursorOperation::SetJumpLocation(CXSourceLocation location) {
  CXFile cursorFile;
  unsigned cursorLine, cursorColumn;
  clang_getFileLocation(
      location,
      &cursorFile,
      &cursorLine,
      &cursorColumn,
      nullptr);
  jumpUrl = QStringLiteral("file://") + GetClangFilePath(cursorFile) + ":" + QString::number(cursorLine) + ":" + QString::number(cursorColumn);
}

GotoReferencedCursorOperation::Result GotoReferencedCursorOperation::OperateOnTU(
    const CodeInfoRequest& request,
    const std::shared_ptr<ClangTU>& TU,
    const QString& canonicalFilePath,
    int invocationLine,
    int invocationCol,
    std::vector<CXUnsavedFile>& /*unsavedFiles*/) {
  constexpr bool kDebug = false;
  
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
  
  if (kDebug) {
    qDebug() << "--- Ctrl-Click jump ---";
    qDebug() << "Cursor spelling:" << ClangString(clang_getCursorSpelling(cursor)).ToQString();
  }
  
  // Check whether we have an inclusion directive.
  // TODO: Unfortunately, it seems that for system includes, libclang yields a "NoDeclFound" cursor, making this fail.
  CXCursorKind kind = clang_getCursorKind(cursor);
  if (kind == CXCursor_InclusionDirective) {
    CXFile includedFile = clang_getIncludedFile(cursor);
    jumpUrl = QStringLiteral("file://") + GetClangFilePath(includedFile);
    if (kDebug) {
      qDebug() << "Got an inclusion directive. jumpUrl:" << jumpUrl;
    }
    return Result::TUHasNotBeenReparsed;
  }
  
  // For break and continue statements, jump to the containing statement that they
  // refer to.
  if (kind == CXCursor_ContinueStmt || kind == CXCursor_BreakStmt) {
    CXCursor containerCursor;
    if (FindContainerStatementForContinueOrBreak(cursor, &containerCursor)) {
      SetJumpLocation(clang_getCursorLocation(containerCursor));
      if (kDebug) {
        qDebug() << "Got a break or continue statement. jumpUrl:" << jumpUrl;
      }
      return Result::TUHasNotBeenReparsed;
    }
  }
  
  // Check whether we can find a matching definition/declaration via USRs.
  CXCursor referencedCursor = clang_getCursorReferenced(cursor);
  QByteArray USR = ClangString(clang_getCursorUSR(clang_Cursor_isNull(referencedCursor) ? cursor : referencedCursor)).ToQByteArray();
  if (!USR.isEmpty()) {
    if (kDebug) {
      qDebug() << "Checking USRs. The (referenced) cursor's USR is:" << USR;
    }
    
    std::unordered_set<QString> relevantFiles;
    
    RunInQtThreadBlocking([&]() {
      // If the document has been closed in the meantime, we must not access its
      // widget anymore.
      if (request.wasCanceled) {
        return;
      }
      
      USRStorage::Instance().GetFilesForUSRLookup(canonicalFilePath, request.widget->GetMainWindow(), &relevantFiles);
    });
    
    std::vector<std::pair<QString, USRDecl>> foundDecls;  // pair of file path and USR
    USRStorage::Instance().LookupUSRs(
        USR,
        relevantFiles,
        &foundDecls);
    
    // If the cursor points to one of the retrieved Decls, remove it from the list
    CXSourceLocation cursorLocation = clang_getCursorLocation(cursor);
    CXFile cursorFile;
    unsigned cursorLine, cursorColumn;
    clang_getFileLocation(
        cursorLocation,
        &cursorFile,
        &cursorLine,
        &cursorColumn,
        nullptr);
    
    for (int i = 0; i < foundDecls.size(); ++ i) {
      const auto& item = foundDecls[i];
      if (cursorLine == item.second.line &&
          cursorColumn == item.second.column &&
          canonicalFilePath == item.first) {
        foundDecls.erase(foundDecls.begin() + i);
        break;
      }
    }
    
    // Jump to the remaining Decl
    // TODO: If multiple declarations remain (without a definition among them), show a list to the user to pick from instead of simply jumping to a random one (the first in the list)
    if (foundDecls.size() > 0) {
      std::pair<QString, USRDecl>* jumpItem = &foundDecls.front();
      for (int i = 0; i < foundDecls.size(); ++ i) {
        if (foundDecls[i].second.isDefinition) {
          jumpItem = &foundDecls[i];
          break;
        }
      }
      
      jumpUrl = QStringLiteral("file://") + jumpItem->first + QStringLiteral(":") + QString::number(jumpItem->second.line) + QStringLiteral(":") + QString::number(jumpItem->second.column);
      if (kDebug) {
        qDebug() << "Jumping to a USR location. jumpUrl:" << jumpUrl;
        qDebug() << "List of all matched USRs (jumping to the first definition, or else to the first item if there is no definition):";
        for (int i = 0; i < foundDecls.size(); ++ i) {
          qDebug() << "File:" << foundDecls[i].first << ", spelling:" << foundDecls[i].second.spelling;
        }
      }
      return Result::TUHasNotBeenReparsed;
    }
  }
  
  // Try whether we get something from clang_getCursorReferenced()
  if (!clang_Cursor_isNull(referencedCursor)) {
    CXSourceLocation referencedLocation = clang_getCursorLocation(referencedCursor);
    SetJumpLocation(referencedLocation);
    if (kDebug) {
      qDebug() << "Jumping to clang_getCursorReferenced(). jumpUrl:" << jumpUrl;
    }
    return Result::TUHasNotBeenReparsed;
  }
  
  qDebug() << "No jump target found (cursor kind: " << ClangString(clang_getCursorKindSpelling(clang_getCursorKind(cursor))).ToQString()
           << " spelling:" << ClangString(clang_getCursorSpelling(cursor)).ToQString() << ").";
  return Result::TUHasNotBeenReparsed;
}

void GotoReferencedCursorOperation::FinalizeInQtThread(const CodeInfoRequest& request) {
  if (!jumpUrl.isEmpty()) {
    request.widget->GetMainWindow()->GotoDocumentLocation(jumpUrl);
  }
}
