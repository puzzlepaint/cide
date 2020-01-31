// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>
#include <vector>

#include <QObject>
#include <QString>

class Document;
class DocumentWidget;
class MainWindow;

struct ParseRequest {
  enum class Mode {
    /// If the document is open and has not been parsed yet, parse it normally.
    /// Otherwise, index it only.
    ParseIfOpenElseIndex = 0,
    
    /// If the document is open, parse it. Otherwise, do nothing.
    /// TODO: This is deprecated since, if we got a parse request (originating
    ///       from a change to a document), it always seems desirable to update
    ///       the document's indexing information, even if it has been closed.
    ParseIfOpen
  };
  
  Mode mode;
  std::shared_ptr<Document> document;
  QString canonicalPath;
  DocumentWidget* widget;
  MainWindow* mainWindow;
  bool isIndexingRequest;
};

class ParseThreadPool : public QObject {
 Q_OBJECT
 public:
  static ParseThreadPool& Instance();
  
  void RequestParse(const std::shared_ptr<Document>& document, DocumentWidget* widget, MainWindow* mainWindow);
  
  void RequestParseIfOpenElseIndex(const QString& canonicalPath, MainWindow* mainWindow);
  
  /// Notifies the ParseThreadPool about the current and open documents, which
  /// it uses for prioritizing parse requests.
  void SetOpenAndCurrentDocuments(const QString& currentDocument, const QStringList& openDocuments);
  
  bool DoesAParseRequestExistForDocument(const Document* document);
  
  bool IsDocumentBeingParsed(const Document* document);
  
  /// Notifies the ParseThreadPool about the given @p widget being removed, such
  /// that parse threads will not try to access it anymore.
  void WidgetRemoved(DocumentWidget* widget);
  
  void ExitAllThreads();
  
  inline int GetNumFinishedIndexingRequests() const { return numFinishedIndexingRequests; }
  
 signals:
  void IndexingRequestFinished();
  
 private:
  ParseThreadPool();
  ~ParseThreadPool();
  
  int FindRequestToParse();
  
  void ThreadMain();
  
  
  std::atomic<int> numFinishedIndexingRequests;
  
  // For request prioritization
  QString currentDocumentPath;
  QStringList openDocumentPaths;
  
  std::atomic<bool> mExit;
  
  std::mutex parseRequestMutex;
  std::condition_variable newParseRequestCondition;
  std::vector<ParseRequest> parseRequests;
  std::vector<std::shared_ptr<Document>> documentsBeingParsed;
  
  std::vector<std::shared_ptr<std::thread>> mThreads;
};
