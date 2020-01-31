// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/clang_utils.h"

#include "cide/main_window.h"

void GetAllUnsavedFiles(
    MainWindow* mainWindow,
    std::vector<CXUnsavedFile>* unsavedFiles,
    std::vector<std::string>* unsavedFileContents,
    std::vector<std::string>* unsavedFilePaths) {
  int numDocuments = mainWindow->GetNumDocuments();
  
  int numDocumentsWithUnsavedChanges = 0;
  for (int i = 0; i < numDocuments; ++ i) {
    Document* document = mainWindow->GetDocument(i).get();
    if (document->HasUnsavedChanges()) {
      ++ numDocumentsWithUnsavedChanges;
    }
  }
  
  unsavedFiles->resize(numDocumentsWithUnsavedChanges);
  unsavedFileContents->resize(numDocumentsWithUnsavedChanges);
  unsavedFilePaths->resize(numDocumentsWithUnsavedChanges);
  
  int outIndex = 0;
  for (int i = 0; i < numDocuments; ++ i) {
    Document* document = mainWindow->GetDocument(i).get();
    if (!document->HasUnsavedChanges()) {
      continue;
    }
    
    CXUnsavedFile& unsavedFile = (*unsavedFiles)[outIndex];
    std::string& unsavedFileContent = (*unsavedFileContents)[outIndex];
    std::string& unsavedFilePath = (*unsavedFilePaths)[outIndex];
    
    unsavedFileContent = document->GetDocumentText().toStdString();
    unsavedFilePath = QFileInfo(document->path()).canonicalFilePath().toStdString();
    unsavedFile.Filename = unsavedFilePath.c_str();
    unsavedFile.Contents = unsavedFileContent.c_str();
    unsavedFile.Length = unsavedFileContent.size();
    
    ++ outIndex;
  }
}


struct ContinueOrBreakParentSearchVisitorData {
  CXCursor lastForWhileDo;
  bool lookForSwitchStmt;
  unsigned int keywordLine;
  unsigned int keywordCol;
  bool foundKeywordCursor;
  std::vector<CXCursor> parentList;
};

CXChildVisitResult VisitClangAST_ContinueOrBreakParentSearch(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  ContinueOrBreakParentSearchVisitorData* data = static_cast<ContinueOrBreakParentSearchVisitorData*>(client_data);
  
  auto isPossibleContainer = [&](CXCursorKind kind) {
    return (kind == CXCursor_WhileStmt ||
            kind == CXCursor_ForStmt ||
            kind == CXCursor_CXXForRangeStmt ||
            kind == CXCursor_DoStmt ||
            (data->lookForSwitchStmt && kind == CXCursor_SwitchStmt));
  };
  
  bool found = false;
  for (int i = 0; i < data->parentList.size(); ++ i) {
    if (clang_equalCursors(parent, data->parentList[i])) {
      found = true;
      if (i < data->parentList.size() - 1) {
        data->parentList.erase(data->parentList.begin() + (i + 1), data->parentList.end());
      }
      break;
    }
  }
  if (!found) {
    data->parentList.push_back(parent);
  }
  
  // NOTE: Comparison via clang_equalCursors() did not seem to work.
  unsigned int line, col;
  clang_getFileLocation(clang_getCursorLocation(cursor), nullptr, &line, &col, nullptr);
  if (line == data->keywordLine &&
      col == data->keywordCol) {
    data->foundKeywordCursor = true;
    
    for (int i = static_cast<int>(data->parentList.size()) - 1; i >= 0; -- i) {
      CXCursorKind kind = clang_getCursorKind(data->parentList[i]);
      if (isPossibleContainer(kind)) {
        data->lastForWhileDo = data->parentList[i];
        break;
      }
    }
    
    return CXChildVisit_Break;
  }
  
  return CXChildVisit_Recurse;
}

bool FindContainerStatementForContinueOrBreak(CXCursor continueOrBreakCursor, CXCursor* containerCursor) {
  // clang_getCursorSemanticParent() returns the cursor to the containing function.
  CXCursor functionCursor = clang_getCursorSemanticParent(continueOrBreakCursor);
  
  ContinueOrBreakParentSearchVisitorData visitorData;
  visitorData.lastForWhileDo = clang_getNullCursor();
  visitorData.lookForSwitchStmt = (clang_getCursorKind(continueOrBreakCursor) == CXCursor_BreakStmt);
  clang_getFileLocation(clang_getCursorLocation(continueOrBreakCursor), nullptr, &visitorData.keywordLine, &visitorData.keywordCol, nullptr);
  visitorData.foundKeywordCursor = false;
  
  clang_visitChildren(functionCursor, &VisitClangAST_ContinueOrBreakParentSearch, &visitorData);
  
  if (visitorData.foundKeywordCursor && !clang_Cursor_isNull(visitorData.lastForWhileDo)) {
    *containerCursor = visitorData.lastForWhileDo;
    return true;
  } else {
    return false;
  }
}

QString GetLibclangVersion() {
  QString clangVersion = ClangString(clang_getClangVersion()).ToQString();
  if (clangVersion.startsWith("clang version ")) {
    clangVersion.remove(0, 14);
  }
  return clangVersion;
}
