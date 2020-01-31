// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "cide/document_widget.h"


// Groups the information that is needed for a code info request.
struct CodeInfoRequest {
  /// The types are ordered by priority. There can only be one pending request at each
  /// point in time, and lower-priority requests are discarded if a higher-priority
  /// request is made while the lower-priority one has not been started processing yet.
  /// The type with the lowest number has the highest priority.
  enum class Type {
    GotoReferencedCursor = 0,
    RightClickInfo,
    CodeCompletion,
    Info
  };
  
  DocumentWidget* widget;
  DocumentLocation codeCompletionInvocationLocation;
  int invocationCounter;
  Type type;
  bool wasCanceled;
  
  /// The attributes below are used if codeCompletionInvocationLocation is invalid.
  QString invocationFile;
  int invocationLine;  // 1-based
  int invocationColumn;  // 1-based
  
  /// For RequestCodeInfo(): Path to the file in which references to the obtained
  /// cursor will be searched for.
  QString pathForReferences;
  
  /// For RequestCodeInfo(): Whether to abort if the token at the invocation
  /// position is uninteresting.
  bool dropUninterestingTokens;
};


/// Operations performed by the CodeInfo thread are implemented by subclassing
/// this class.
struct TUOperationBase {
  enum class Result {
    TUHasBeenReparsed = 0,
    TUHasNotBeenReparsed
  };
  
  /// This is called from the main (Qt) thread after taking the TU out of the
  /// TU pool. It may do initializations that need to be done in the Qt thread.
  /// It is however advisable to do as much work as possible in OperateOnTU()
  /// instead, since that is called from a background thread and will thus not
  /// block the UI.
  virtual inline void InitializeInQtThread(
      const CodeInfoRequest& /*request*/,
      const std::shared_ptr<ClangTU>& /*TU*/,
      const QString& /*canonicalFilePath*/,
      int /*invocationLine*/,
      int /*invocationCol*/,
      std::vector<CXUnsavedFile>& /*unsavedFiles*/) {}
  
  /// This is called with the given TU taken out of the TU pool (so it is safe
  /// to use). This is called in a background thread however, so it must not
  /// interact with the UI. Must return whether the TU has been re-parsed
  /// within the function. Note that operations handled by CodeInfo are supposed
  /// to be relatively quick and are thus not expected to reparse the (whole) TU,
  /// so it is not expected that an operation actually returns TUHasBeenReparsed.
  /// 
  /// The vector @p unsavedFiles is only valid if the corresponding parameter
  /// is set when calling OperateOnTU().
  /// 
  /// invocationLine and invocationCol are 0-based here.
  virtual Result OperateOnTU(
      const CodeInfoRequest& request,
      const std::shared_ptr<ClangTU>& TU,
      const QString& canonicalFilePath,
      int invocationLine,
      int invocationCol,
      std::vector<CXUnsavedFile>& unsavedFiles) = 0;
  
  /// This is called after the TU has been returned, and is executed within the
  /// main (Qt) thread, so this function may interact with the UI.
  virtual void FinalizeInQtThread(const CodeInfoRequest& request) = 0;
};


/// Singleton class maintaining a thread that performs operations on the
/// most up-to-date libclang translation unit, such as code completion or
/// querying the AST (abstract syntax tree) for information about the code.
class CodeInfo {
 public:
  ~CodeInfo();
  
  /// Returns the singleton instance.
  static CodeInfo& Instance();
  
  /// Requests code completion for the given widget (at the current cursor
  /// position) in the background thread.
  /// If the request was rejected because there is a higher-priority request already, returns an invalid location.
  DocumentLocation RequestCodeCompletion(DocumentWidget* widget);
  
  /// Requests info for showing the right-click menu.
  /// Returns true if the request was accepted, false otherwise (if there was a higher-priority request already).
  bool RequestRightClickInfo(DocumentWidget* widget, DocumentLocation invocationLocation);
  
  /// Requests code info for the given widget in the background thread.
  /// Returns true if the request was accepted, false otherwise (if there was a higher-priority request already).
  bool RequestCodeInfo(DocumentWidget* widget, DocumentLocation invocationLocation);
  /// In this variant of RequestInfo(), line and column are 1-based.
  /// @p pathForReferences specifies the file in which references to the
  /// obtained cursor will be searched for.
  /// Returns true if the request was accepted, false otherwise (if there was a higher-priority request already).
  bool RequestCodeInfo(DocumentWidget* widget, const QString& path, int line, int column, const QString& pathForReferences, bool dropUninterestingTokens);
  
  /// Requests to jump to the libclang cursor referenced at the given document
  /// location, performed in the background thread.
  /// Returns true if the request was accepted, false otherwise (if there was a higher-priority request already).
  bool GotoReferencedCursor(DocumentWidget* widget, DocumentLocation invocationLocation);
  
  /// Notifies the background thread about the given @p widget being
  /// removed, such that it will not try to access it anymore.
  void WidgetRemoved(DocumentWidget* widget);
  
  void Exit();
  
 private:
  CodeInfo();
  
  /// Checks whether the new request type will discard the potentially already queued old request.
  /// Returns true if the new request can be made, false if the old request has higher priority.
  bool CheckDiscardPreviousRequest(DocumentWidget* widget, CodeInfoRequest::Type newRequestType);
  
  void ThreadMain();
  
  void LockTUForOperation(
      const CodeInfoRequest& request,
      bool getUnsavedFileContents,
      TUOperationBase* operation);
  
  
  /// Request which was made, but is not being worked on yet
  bool haveRequest = false;
  CodeInfoRequest lastRequest;
  
  /// Request which is being worked on
  bool haveRequestInProgress = false;
  CodeInfoRequest requestInProgress;
  
  std::atomic<bool> mExit;
  
  std::mutex completeRequestMutex;
  std::condition_variable newCodeInfoRequestCondition;
  
  std::shared_ptr<std::thread> mThread;
};
