// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class Document;
class DocumentWidget;
class MainWindow;

class GitDiff {
 public:
  static GitDiff& Instance();

  ~GitDiff();
  
  void RequestDiff(const std::shared_ptr<Document>& document, DocumentWidget* widget, MainWindow* mainWindow);
  
  /// Notifies GitDiff about the given @p widget being removed, such
  /// that parse threads will not try to access it anymore.
  void WidgetRemoved(DocumentWidget* widget);
  
  void Exit();
  
 private:
  struct DiffRequest {
    inline DiffRequest() = default;
    inline DiffRequest(const std::shared_ptr<Document>& document, DocumentWidget* widget, MainWindow* mainWindow)
        : document(document),
          widget(widget),
          mainWindow(mainWindow) {}
    
    std::shared_ptr<Document> document;
    DocumentWidget* widget;
    MainWindow* mainWindow;
  };
  
  GitDiff();
  
  void ThreadMain();
  
  void CreateDiff(const DiffRequest& request);
  
  // Thread input handling
  std::mutex diffMutex;
  std::condition_variable newDiffRequestCondition;
  std::vector<DiffRequest> newDiffRequests;
  std::shared_ptr<Document> documentBeingDiffed;
  
  // Threading
  std::atomic<bool> mExit;
  std::shared_ptr<std::thread> mThread;
};
