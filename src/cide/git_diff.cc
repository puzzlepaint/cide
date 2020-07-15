// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/git_diff.h"

#include <git2.h>

#include "cide/document.h"
#include "cide/document_widget.h"
#include "cide/main_window.h"
#include "cide/qt_thread.h"
#include "cide/scroll_bar_minimap.h"

GitDiff& GitDiff::Instance() {
  static GitDiff instance;
  return instance;
}

void GitDiff::RequestDiff(const std::shared_ptr<Document>& document, DocumentWidget* widget, MainWindow* mainWindow) {
  diffMutex.lock();
  
  // Check for an existing diff request for this file
  for (int i = 0; i < newDiffRequests.size(); ++ i) {
    if (newDiffRequests[i].document.get() == document.get()) {
      diffMutex.unlock();
      return;
    }
  }
  
  newDiffRequests.emplace_back(document, widget, mainWindow);
  diffMutex.unlock();
  newDiffRequestCondition.notify_one();
}

void GitDiff::WidgetRemoved(DocumentWidget* widget) {
  std::unique_lock<std::mutex> lock(diffMutex);
  
  for (auto it = newDiffRequests.begin(); it != newDiffRequests.end(); ) {
    if (it->widget == widget) {
      it = newDiffRequests.erase(it);
    } else {
      ++ it;
    }
  }
  
  if (documentBeingDiffed.get() == widget->GetDocument().get()) {
    documentBeingDiffed = nullptr;
  }
}

void GitDiff::Exit() {
  mExit = true;
  newDiffRequestCondition.notify_all();
  if (mThread) {
    mThread->join();
    mThread.reset();
  }
}

GitDiff::GitDiff() {
  mExit = false;
  mThread.reset(new std::thread(&GitDiff::ThreadMain, this));
}

GitDiff::~GitDiff() {
  Exit();
}

void GitDiff::ThreadMain() {
  while (true) {
    std::unique_lock<std::mutex> lock(diffMutex);
    documentBeingDiffed = nullptr;
    if (mExit) {
      return;
    }
    while (newDiffRequests.empty()) {
      newDiffRequestCondition.wait(lock);
      if (mExit) {
        return;
      }
    }
    DiffRequest request = newDiffRequests.front();
    newDiffRequests.erase(newDiffRequests.begin());
    documentBeingDiffed = request.document;
    lock.unlock();
    
    CreateDiff(request);
  }
}

struct GitDiffLineCallbackStatus {
  int newToOldLineOffset = 0;
  int documentNumLines;
  
  std::vector<LineDiff> result;
};

int GitDiffLineCallback(const git_diff_delta* /*delta*/, const git_diff_hunk* /*hunk*/, const git_diff_line* line, void* payload) {
  GitDiffLineCallbackStatus* status = static_cast<GitDiffLineCallbackStatus*>(payload);
  
  if (line->origin == GIT_DIFF_LINE_ADDITION) {
    // qDebug() << "Addition at new line:" << line->new_lineno;
    
    // If the last recorded change was a deletion, merge them into a modification
    if (!status->result.empty() &&
        status->result.back().type == LineDiff::Type::Removed &&
        status->result.back().line == line->new_lineno - 1) {
      status->result.back().type = LineDiff::Type::Modified;
      status->result.back().numLines = line->num_lines;
    } else if (!status->result.empty() &&
               status->result.back().type == LineDiff::Type::Modified &&
               status->result.back().line <= line->new_lineno - 1 &&
               status->result.back().line + status->result.back().numRemovedLines > line->new_lineno - 1) {
      ++ status->result.back().numLines;
    } else {
      status->result.emplace_back(
          LineDiff::Type::Added,
          line->new_lineno - 1,
          line->num_lines,
          QStringLiteral(""));
    }
    
    status->newToOldLineOffset -= line->num_lines;
  } else if (line->origin == GIT_DIFF_LINE_DELETION) {
    // qDebug() << "Removal at old line:" << line->old_lineno;
    
    int showAtLine = line->old_lineno - 1 - status->newToOldLineOffset;
    
    if (!status->result.empty() &&
        status->result.back().type == LineDiff::Type::Removed &&
        status->result.back().line == showAtLine) {
      // Merge into the existing LineDiff.
      status->result.back().oldText += QString::fromUtf8(line->content, line->content_len);
      ++ status->result.back().numRemovedLines;
    } else {
      status->result.emplace_back(
          LineDiff::Type::Removed,
          showAtLine,
          1,
          QString::fromUtf8(line->content, line->content_len));  // TODO: Use the document's encoding setting (if we add such a setting)
      status->result.back().numRemovedLines = line->num_lines;
    }
    
    status->newToOldLineOffset += line->num_lines;
  } else if (line->origin == GIT_DIFF_LINE_ADD_EOFNL) {
    // qDebug() << "GIT_DIFF_LINE_ADD_EOFNL";
    
    if (!status->result.empty() &&
        status->result.back().line == status->documentNumLines - 1) {
      status->result.back().type = LineDiff::Type::Modified;
      if (!status->result.back().oldText.endsWith('\n')) {
        status->result.back().oldText += '\n';
      }
      status->result.back().oldText += QObject::tr("(newline added at end of line)\n");
    } else {
      status->result.emplace_back(
          LineDiff::Type::Modified,
          status->documentNumLines - 1,
          line->num_lines,
          QObject::tr("(newline added at end of line)"));
    }
    
    status->newToOldLineOffset -= line->num_lines;
  } else if (line->origin == GIT_DIFF_LINE_DEL_EOFNL) {
    // qDebug() << "GIT_DIFF_LINE_DEL_EOFNL";
    
    if (!status->result.empty() &&
        status->result.back().line == status->documentNumLines - 1) {
      status->result.back().type = LineDiff::Type::Modified;
      if (!status->result.back().oldText.endsWith('\n')) {
        status->result.back().oldText += '\n';
      }
      status->result.back().oldText += QObject::tr("(newline added at end of line)\n");
    } else {
      status->result.emplace_back(
          LineDiff::Type::Modified,
          status->documentNumLines - 1,
          1,
          QObject::tr("(newline removed at end of line)"));
    }
    
    status->newToOldLineOffset += line->num_lines;
  }
  
  return 0;
}

void GitDiff::CreateDiff(const DiffRequest& request) {
  // Get the current document content
  QByteArray documentTextUtf8;
  int documentNumLines;
  QString documentPath;
  int documentVersion;
  
  bool exit = false;
  RunInQtThreadBlocking([&]() {
    // If the document has been closed in the meantime, abort.
    if (documentBeingDiffed != request.document) {
      exit = true;
      return;
    }
    
    documentTextUtf8 = request.document->GetDocumentText().toUtf8();
    documentNumLines = request.document->LineCount();
    documentPath = request.document->path();
    documentVersion = request.document->version();
  });
  if (exit) {
    return;
  }
  
  // Open repository
  git_repository* repo = nullptr;
  int result = git_repository_open_ext(&repo, QFileInfo(documentPath).dir().path().toLocal8Bit(), 0, nullptr);
  std::shared_ptr<git_repository> repo_deleter(repo, [&](git_repository* repo){ git_repository_free(repo); });
  if (result == GIT_ENOTFOUND) {
    // There is no git repository at the project path.
    return;
  } else if (result != 0) {
    qDebug() << "Failed to open the git repository for file" << QFileInfo(documentPath).dir().path() << "(some possible reasons: repo corruption or system errors)";
    return;
  }
  
  if (git_repository_is_bare(repo)) {
    return;
  }
  
  QDir repoWorkdir(QString::fromLocal8Bit(git_repository_workdir(repo)));
  
  // Get the HEAD tree
  git_object* head = nullptr;
  result = git_revparse_single(&head, repo, "HEAD^{tree}");
  std::shared_ptr<git_object> head_deleter(head, [&](git_object* head){ git_object_free(head); });
  if (result != 0) {
    qDebug() << "GitDiff: failed to get \"HEAD^{tree}\" object from the git repository.";
    return;
  }
  
  git_tree* tree = nullptr;
  result = git_tree_lookup(&tree, repo, git_object_id(head));
  if (result != 0) {
    qDebug() << "GitDiff: failed to lookup the HEAD tree in the git repository.";
    return;
  }
  
  // Get the blob for the old file state
  git_blob* oldFileBlob = nullptr;
  git_tree_entry* oldFileEntry = nullptr;
  QByteArray fileRelativePath = repoWorkdir.relativeFilePath(documentPath).toLocal8Bit();
  result = git_tree_entry_bypath(&oldFileEntry, tree, fileRelativePath);
  std::shared_ptr<git_tree_entry> entry_deleter(oldFileEntry, [&](git_tree_entry* entry){ git_tree_entry_free(entry); });
  if (result == 0) {
    git_object_t oldFileEntryType = git_tree_entry_type(oldFileEntry);
    if (oldFileEntryType == GIT_OBJECT_BLOB) {
      result = git_blob_lookup(&oldFileBlob, repo, git_tree_entry_id(oldFileEntry));
      if (result != 0) {
        oldFileBlob = nullptr;
      }
    }
  }
  
  // Create the diff
  git_diff_options options;
  memset(&options, 0, sizeof(git_diff_options));
  options.version = GIT_DIFF_OPTIONS_VERSION;
  options.flags = GIT_DIFF_FORCE_TEXT;
  // TODO: We do not need the context lines for display. Can we thus set this to 0?
  //       Or does this change the behavior of the diff operation as well?
  // options.context_lines = 0;
  
  GitDiffLineCallbackStatus status;
  status.result.reserve(32);
  status.documentNumLines = documentNumLines;
  result = git_diff_blob_to_buffer(
      oldFileBlob,  // may be nullptr
      fileRelativePath,
      documentTextUtf8,
      documentTextUtf8.size(),
      fileRelativePath,
      &options,
      /*git_diff_file_cb file_cb*/ nullptr,
      /*git_diff_binary_cb binary_cb*/ nullptr,
      /*git_diff_hunk_cb hunk_cb*/ nullptr,
      &GitDiffLineCallback,
      &status);
  if (result != 0) {
    qDebug() << "GitDiff: git_diff_blob_to_buffer() failed.";
    return;
  }
  
  // TODO: Debug check for that we receive lines in successive order
  int lastLine = 0;
  for (const auto& line : status.result) {
    if (line.line < lastLine) {
      qDebug() << "DEBUG: Successive line order violated!";
    }
  }
  
  RunInQtThreadBlocking([&]() {
    // Abort if the document widget does not exist anymore
    if (documentBeingDiffed != request.document) {
      exit = true;
      return;
    }
    
    if (documentVersion != request.document->version()) {
      // The document version changed. Discard our results.
      // The next diff should already have been invoked.
      exit = true;
      return;
    }
    
    // Store the result and invoke redraw of the widget
    request.document->SwapDiffLines(&status.result);
    request.widget->update(request.widget->rect());
    request.widget->GetContainer()->GetMinimap()->SetDiffLines(request.document->diffLines());
  });
  if (exit) {
    return;
  }
}
