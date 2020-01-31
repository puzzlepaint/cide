// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/parse_thread_pool.h"

#include "cide/clang_parser.h"
#include "cide/document.h"
#include "cide/document_widget.h"
#include "cide/main_window.h"
#include "cide/qt_thread.h"

ParseThreadPool::ParseThreadPool() {
  mExit = false;
  numFinishedIndexingRequests = 0;
  
  constexpr int kThreadCount = 8;  // TODO: Make configurable
  mThreads.resize(kThreadCount);
  for (int i = 0; i < kThreadCount; ++ i) {
    mThreads[i].reset(new std::thread(&ParseThreadPool::ThreadMain, this));
  }
}

ParseThreadPool::~ParseThreadPool() {
  ExitAllThreads();
}

ParseThreadPool& ParseThreadPool::Instance() {
  static ParseThreadPool instance;
  return instance;
}

void ParseThreadPool::RequestParse(const std::shared_ptr<Document>& document, DocumentWidget* widget, MainWindow* mainWindow) {
  std::unique_lock<std::mutex> lock(parseRequestMutex);
  for (const ParseRequest& request : parseRequests) {
    if (request.document.get() == document.get()) {
      // There is already a queued parse request for this document.
      return;
    }
  }
  
  ParseRequest newRequest;
  newRequest.mode = ParseRequest::Mode::ParseIfOpen;
  newRequest.document = document;
  newRequest.canonicalPath = document->path();
  newRequest.mainWindow = mainWindow;
  newRequest.widget = widget;
  newRequest.isIndexingRequest = false;
  parseRequests.push_back(newRequest);
  lock.unlock();
  newParseRequestCondition.notify_one();
}

void ParseThreadPool::RequestParseIfOpenElseIndex(const QString& canonicalPath, MainWindow* mainWindow) {
  std::unique_lock<std::mutex> lock(parseRequestMutex);
  
  ParseRequest newRequest;
  newRequest.mode = ParseRequest::Mode::ParseIfOpenElseIndex;
  newRequest.canonicalPath = canonicalPath;
  newRequest.document = nullptr;
  newRequest.widget = nullptr;
  if (mainWindow) {
    for (int i = 0; i < mainWindow->GetNumDocuments(); ++ i) {
      if (mainWindow->GetDocument(i)->path() == canonicalPath) {
        newRequest.document = mainWindow->GetDocument(i);
        newRequest.widget = mainWindow->GetWidgetForDocument(newRequest.document.get());
      }
    }
  }
  newRequest.mainWindow = mainWindow;
  newRequest.isIndexingRequest = true;
  
  parseRequests.push_back(newRequest);
  lock.unlock();
  newParseRequestCondition.notify_one();
}

void ParseThreadPool::SetOpenAndCurrentDocuments(const QString& currentDocument, const QStringList& openDocuments) {
  std::unique_lock<std::mutex> lock(parseRequestMutex);
  currentDocumentPath = currentDocument;
  openDocumentPaths = openDocuments;
}

bool ParseThreadPool::DoesAParseRequestExistForDocument(const Document* document) {
  std::unique_lock<std::mutex> lock(parseRequestMutex);
  for (const ParseRequest& request : parseRequests) {
    if (request.document.get() == document) {
      return true;
    }
  }
  return false;
}

bool ParseThreadPool::IsDocumentBeingParsed(const Document* document) {
  std::unique_lock<std::mutex> lock(parseRequestMutex);
  for (const std::shared_ptr<Document>& otherDocument : documentsBeingParsed) {
    if (otherDocument.get() == document) {
      return true;
    }
  }
  return false;
}

void ParseThreadPool::WidgetRemoved(DocumentWidget* widget) {
  std::unique_lock<std::mutex> lock(parseRequestMutex);
  
  for (auto it = parseRequests.begin(); it != parseRequests.end(); ) {
    if (it->widget == widget) {
      it = parseRequests.erase(it);
    } else {
      ++ it;
    }
  }
  
  for (auto it = documentsBeingParsed.begin(); it != documentsBeingParsed.end(); ) {
    if ((*it).get() == widget->GetDocument().get()) {
      it = documentsBeingParsed.erase(it);
    } else {
      ++ it;
    }
  }
}

void ParseThreadPool::ExitAllThreads() {
  mExit = true;
  newParseRequestCondition.notify_all();
  for (int i = 0; i < mThreads.size(); ++ i) {
    mThreads[i]->join();
  }
  mThreads.clear();
}

int ParseThreadPool::FindRequestToParse() {
  // Priorities:
  // 0 - no special prioritization
  // 1 - document is open
  // 2 - document is current
  int bestPriority = -1;
  constexpr int bestPossiblePriority = 2;
  int bestIndex = -1;
  
  // Iterate over all pending parse requests
  for (std::size_t i = 0; i < parseRequests.size(); ++ i) {
    const ParseRequest& candidateRequest = parseRequests[i];
    
    // If the document is being parsed, do not start parsing it again before the
    // first parse exits.
    bool isBeingParsed = false;
    for (const std::shared_ptr<Document>& document : documentsBeingParsed) {
      if (document.get() == candidateRequest.document.get()) {
        isBeingParsed = true;
        break;
      }
    }
    if (isBeingParsed) {
      continue;
    }
    
    // Determine the priority
    int priority = 0;
    if (candidateRequest.canonicalPath == currentDocumentPath) {
      priority = 2;
    } else if (openDocumentPaths.contains(candidateRequest.canonicalPath)) {
      priority = 1;
    }
    
    // New best request?
    if (priority > bestPriority) {
      bestPriority = priority;
      bestIndex = i;
      if (bestPriority == bestPossiblePriority) {
        // No further search necessary
        break;
      }
    }
  }
  
  return bestIndex;
}

void ParseThreadPool::ThreadMain() {
  while (true) {
    std::unique_lock<std::mutex> lock(parseRequestMutex);
    if (mExit) {
      return;
    }
    int parseRequestIndex;
    while ((parseRequestIndex = FindRequestToParse()) == -1) {
      newParseRequestCondition.wait(lock);
      if (mExit) {
        return;
      }
    }
    ParseRequest request = parseRequests[parseRequestIndex];
    parseRequests.erase(parseRequests.begin() + parseRequestIndex);
    if (request.document) {
      documentsBeingParsed.push_back(request.document);
    }
    lock.unlock();
    
    // Perform the parsing.
    if (request.mode == ParseRequest::Mode::ParseIfOpen || /* TODO ) {
      ParseFile(request.document ? request.document.get() : nullptr, request.mainWindow);
    } else if (*/ request.mode == ParseRequest::Mode::ParseIfOpenElseIndex) {
      ParseFileIfOpenElseIndex(request.canonicalPath, request.document ? request.document.get() : nullptr, request.mainWindow);
    } else {
      qDebug() << "Error: Parse request mode not handled:" << static_cast<int>(request.mode);
    }
    
    if (request.document && request.widget) {
      RunInQtThreadBlocking([&]() {
        // If the document has been closed in the meantime, we must not access its
        // widget anymore.
        if (!ParseThreadPool::Instance().IsDocumentBeingParsed(request.document.get())) {
          return;
        }
        
        request.widget->update(request.widget->rect());
      });
    }
    
    if (request.isIndexingRequest) {
      ++ numFinishedIndexingRequests;
      emit IndexingRequestFinished();
    }
    
    if (request.document) {
      lock.lock();
      for (std::size_t i = 0; i < documentsBeingParsed.size(); ++ i) {
        if (documentsBeingParsed[i].get() == request.document.get()) {
          documentsBeingParsed.erase(documentsBeingParsed.begin() + i);
          break;
        }
      }
      lock.unlock();
      
      // Removing the current document from documentsBeingParsed may cause another
      // parse request to become un-blocked, so wake up another thread.
      newParseRequestCondition.notify_one();
    }
  }
}
