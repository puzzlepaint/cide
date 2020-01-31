// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/code_info.h"

#include "cide/argument_hint_widget.h"
#include "cide/clang_utils.h"
#include "cide/code_info_code_completion.h"
#include "cide/code_info_get_info.h"
#include "cide/code_info_get_right_click_info.h"
#include "cide/code_info_goto_referenced_cursor.h"
#include "cide/main_window.h"
#include "cide/qt_thread.h"
#include "cide/text_utils.h"


CodeInfo::CodeInfo() {
  mExit = false;
  mThread.reset(new std::thread(&CodeInfo::ThreadMain, this));
}

CodeInfo::~CodeInfo() {
  Exit();
}

CodeInfo& CodeInfo::Instance() {
  static CodeInfo instance;
  return instance;
}

DocumentLocation CodeInfo::RequestCodeCompletion(DocumentWidget* widget) {
  // Find the location to invoke the completion at. It must point directly after
  // the relevant token. For example, for someObject->get^ with the cursor at
  // ^, the completion must be invoked after the "->", not after "get". Thus,
  // we start from the current cursor position and go back until hitting an
  // interesting position.
  DocumentLocation cursorLoc = widget->MapCursorToDocument();
  DocumentLocation codeCompletionInvocationLocation = 0;
  if (cursorLoc.offset > 0) {
    Document::CharacterIterator it(widget->GetDocument().get(), cursorLoc.offset - 1);
    while (it.IsValid()) {
      QChar character = it.GetChar();
      if (IsSymbol(character)) {
        break;
      } else if (character.isSpace()) {
        break;
      }
      -- it;
    }
    codeCompletionInvocationLocation = it.IsValid() ? (it.GetCharacterOffset() + 1) : 0;
  }
  
  std::unique_lock<std::mutex> lock(completeRequestMutex);
  if (!CheckDiscardPreviousRequest(widget, CodeInfoRequest::Type::CodeCompletion)) {
    return DocumentLocation::Invalid();
  }
  
  lastRequest.widget = widget;
  lastRequest.codeCompletionInvocationLocation = codeCompletionInvocationLocation;
  lastRequest.invocationCounter = widget->GetCodeCompletionInvocationCounter();
  lastRequest.type = CodeInfoRequest::Type::CodeCompletion;
  haveRequest = true;
  newCodeInfoRequestCondition.notify_one();
  
  return codeCompletionInvocationLocation;
}

bool CodeInfo::RequestRightClickInfo(DocumentWidget* widget, DocumentLocation invocationLocation) {
  std::unique_lock<std::mutex> lock(completeRequestMutex);
  if (!CheckDiscardPreviousRequest(widget, CodeInfoRequest::Type::RightClickInfo)) {
    return false;
  }
  
  lastRequest.widget = widget;
  lastRequest.codeCompletionInvocationLocation = invocationLocation;
  lastRequest.invocationCounter = widget->GetCodeCompletionInvocationCounter();  // TODO: This value is currently unused here
  lastRequest.type = CodeInfoRequest::Type::RightClickInfo;
  haveRequest = true;
  newCodeInfoRequestCondition.notify_one();
  return true;
}

bool CodeInfo::RequestCodeInfo(DocumentWidget* widget, DocumentLocation invocationLocation) {
  std::unique_lock<std::mutex> lock(completeRequestMutex);
  if (!CheckDiscardPreviousRequest(widget, CodeInfoRequest::Type::Info)) {
    return false;
  }
  
  lastRequest.widget = widget;
  lastRequest.codeCompletionInvocationLocation = invocationLocation;
  lastRequest.pathForReferences = widget->GetDocument()->path();
  lastRequest.dropUninterestingTokens = true;
  lastRequest.invocationCounter = widget->GetCodeCompletionInvocationCounter();  // TODO: This value is currently unused here
  lastRequest.type = CodeInfoRequest::Type::Info;
  haveRequest = true;
  newCodeInfoRequestCondition.notify_one();
  return true;
}

bool CodeInfo::RequestCodeInfo(DocumentWidget* widget, const QString& path, int line, int column, const QString& pathForReferences, bool dropUninterestingTokens) {
  std::unique_lock<std::mutex> lock(completeRequestMutex);
  if (!CheckDiscardPreviousRequest(widget, CodeInfoRequest::Type::Info)) {
    return false;
  }
  
  lastRequest.widget = widget;
  lastRequest.codeCompletionInvocationLocation = DocumentLocation::Invalid();
  lastRequest.invocationFile = path;
  lastRequest.invocationLine = line;
  lastRequest.invocationColumn = column;
  lastRequest.pathForReferences = pathForReferences;
  lastRequest.dropUninterestingTokens = dropUninterestingTokens;
  lastRequest.invocationCounter = widget->GetCodeCompletionInvocationCounter();  // TODO: This value is currently unused here
  lastRequest.type = CodeInfoRequest::Type::Info;
  haveRequest = true;
  newCodeInfoRequestCondition.notify_one();
  return true;
}

bool CodeInfo::GotoReferencedCursor(DocumentWidget* widget, DocumentLocation invocationLocation) {
  std::unique_lock<std::mutex> lock(completeRequestMutex);
  if (!CheckDiscardPreviousRequest(widget, CodeInfoRequest::Type::GotoReferencedCursor)) {
    return false;
  }
  
  lastRequest.widget = widget;
  lastRequest.codeCompletionInvocationLocation = invocationLocation;
  lastRequest.invocationCounter = -1;  // unused
  lastRequest.type = CodeInfoRequest::Type::GotoReferencedCursor;
  haveRequest = true;
  newCodeInfoRequestCondition.notify_one();
  return true;
}

void CodeInfo::WidgetRemoved(DocumentWidget* widget) {
  std::unique_lock<std::mutex> lock(completeRequestMutex);
  
  if (haveRequest && lastRequest.widget == widget) {
    haveRequest = false;
  }
  
  if (haveRequestInProgress && requestInProgress.widget == widget) {
    haveRequestInProgress = false;
    requestInProgress.wasCanceled = true;
  }
}

void CodeInfo::Exit() {
  if (mThread) {
    mExit = true;
    newCodeInfoRequestCondition.notify_all();
    mThread->join();
    mThread = nullptr;
  }
}

bool CodeInfo::CheckDiscardPreviousRequest(DocumentWidget* widget, CodeInfoRequest::Type newRequestType) {
  if (!haveRequest) {
    return true;
  }
  
  if (static_cast<int>(lastRequest.type) < static_cast<int>(newRequestType)) {
    // There is a higher-priority request already, discard the new one.
    return false;
  }
  
  // Discard the old request in favor of the new one.
  if (lastRequest.type == CodeInfoRequest::Type::CodeCompletion) {
    widget->CodeCompletionRequestWasDiscarded();
  }
  return true;
}

void CodeInfo::ThreadMain() {
  while (true) {
    std::unique_lock<std::mutex> lock(completeRequestMutex);
    if (mExit) {
      return;
    }
    while (!haveRequest) {
      newCodeInfoRequestCondition.wait(lock);
      if (mExit) {
        return;
      }
    }
    requestInProgress = lastRequest;
    requestInProgress.wasCanceled = false;
    haveRequest = false;
    haveRequestInProgress = true;
    lock.unlock();
    
    // Perform the code completion
    if (requestInProgress.type == CodeInfoRequest::Type::CodeCompletion) {
      CodeCompletionOperation operation;
      LockTUForOperation(requestInProgress, true, &operation);
    } else if (requestInProgress.type == CodeInfoRequest::Type::Info) {
      GetInfoOperation operation;
      LockTUForOperation(requestInProgress, false, &operation);
    } else if (requestInProgress.type == CodeInfoRequest::Type::RightClickInfo) {
      GetRightClickInfoOperation operation;
      LockTUForOperation(requestInProgress, false, &operation);
    } else if (requestInProgress.type == CodeInfoRequest::Type::GotoReferencedCursor) {
      GotoReferencedCursorOperation operation;
      LockTUForOperation(requestInProgress, false, &operation);
    }
  }
}

void CodeInfo::LockTUForOperation(
    const CodeInfoRequest& request,
    bool getUnsavedFileContents,
    TUOperationBase* operation) {
  QString canonicalFilePath;
  int invocationLine;
  int invocationCol;
  std::vector<CXUnsavedFile> unsavedFiles;
  std::vector<std::string> unsavedFileContents;
  std::vector<std::string> unsavedFilePaths;
  std::shared_ptr<ClangTU> TU;
  bool exit = false;
  
  RunInQtThreadBlocking([&]() {
    // If the document has been closed in the meantime, we must not access its
    // widget anymore.
    if (!haveRequestInProgress) {
      exit = true;
      return;
    }
    
    const std::shared_ptr<Document>& document = request.widget->GetDocument();
    
    // Convert the invocation location to (file, line, column) format.
    if (request.codeCompletionInvocationLocation.IsValid()) {
      if (!request.widget->MapDocumentToLayout(request.codeCompletionInvocationLocation, &invocationLine, &invocationCol)) {
        qDebug() << "Error: Could not convert the code info invocation location to (line, col) format";
        exit = true;
        return;
      }
      
      canonicalFilePath = QFileInfo(document->path()).canonicalFilePath();
    } else {
      invocationLine = request.invocationLine - 1;
      invocationCol = request.invocationColumn - 1;
      
      canonicalFilePath = QFileInfo(request.invocationFile).canonicalFilePath();
    }
    
    // Get the most up-to-date libclang translation unit
    TU = document->GetTUPool()->TakeMostUpToDateTU();
    if (!TU || !TU->isInitialized()) {
      // NOTE: This is not logged as this may happen when the operation is
      //       invoked before the file was parsed.
      // qDebug() << "Could not get a libclang TU in LockTUForOperation()";
      exit = true;
      return;
    }
    
    if (getUnsavedFileContents) {
      // Get all unsaved files that are opened
      GetAllUnsavedFiles(request.widget->GetMainWindow(), &unsavedFiles, &unsavedFileContents, &unsavedFilePaths);
    }
    
    operation->InitializeInQtThread(request, TU, canonicalFilePath, invocationLine, invocationCol, unsavedFiles);
  });
  if (exit) {
    if (TU) {
      request.widget->GetDocument()->GetTUPool()->PutTU(TU, false);
    }
    return;
  }
  
  TUOperationBase::Result reparsed = operation->OperateOnTU(request, TU, canonicalFilePath, invocationLine, invocationCol, unsavedFiles);
  
  RunInQtThreadBlocking([&]() {
    // If the document has been closed in the meantime, we must not access its
    // widget anymore.
    if (!haveRequestInProgress) {
      exit = true;
      return;
    }
    
    const std::shared_ptr<Document>& document = request.widget->GetDocument();
    document->GetTUPool()->PutTU(TU, reparsed == TUOperationBase::Result::TUHasBeenReparsed);
    
    operation->FinalizeInQtThread(request);
  });
}
