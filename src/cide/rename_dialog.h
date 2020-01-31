// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <map>
#include <unordered_set>

#include <clang-c/Index.h>
#include <QDialog>

#include "cide/document_location.h"
#include "cide/document_range.h"
#include "cide/util.h"

class ClangTU;
class Document;
class DocumentWidget;
class QRadioButton;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

/// Dialog for renaming right-clicked items.
class RenameDialog : public QDialog {
 public:
  struct Occurrence {
    inline Occurrence(int line, int column, int length, const QString& lineText)
        : line(line),
          column(column),
          length(length),
          lineText(lineText) {}
    
    /// 0-based line of the occurrence
    int line;
    /// 0-based column of the first character of the occurrence
    int column;
    /// Length of the occurrence
    int length;
    /// Text of the line containing the occurrence.
    QString lineText;
  };
  
  
  RenameDialog(DocumentWidget* widget, const QString& itemUSR, const QString& itemSpelling, bool itemHasLocalDefinition, const DocumentRange& initialCursorOrSelectionRange, QWidget* parent = nullptr);
  ~RenameDialog();
  
  void ExitSearchThread();
  
 public slots:
  void SetInitialCursorOrSelection();
  void ClearSearchResults();
  void SearchModeChanged();
  void Rename();
  
 private:
  enum class SearchMode {
    LocalSearch = 0,
    SemiGlobalSearch,
    GlobalSearch
  };
  
  void ThreadMain();
  
  void PerformSearch(SearchMode mode);
  void PerformLocalSearch();
  void PerformSemiGlobalSearch();
  void PerformGlobalSearch();
  
  void SearchInFiles(const std::unordered_set<QString>& paths, bool searchInIncludedFiles);
  void SearchInFile(const QString& path, bool searchInIncludedFiles);
  
  void GetTUFromDocument(Document* document, std::shared_ptr<ClangTU>* TU);
  void ParseFileToGetTU(const QString& path, CXTranslationUnit* clangTU);
  
  void RenameInDocument(DocumentWidget* widget, const std::vector<Occurrence>& occurrences);
  void RenameInFileOnDisk(const QString& path, const std::vector<Occurrence>& occurrences);
  
  
  // Search thread
  std::shared_ptr<std::thread> searchThread;
  
  std::mutex searchMutex;
  SearchMode searchMode;
  bool haveNewSearchRequest;
  std::condition_variable newSearchRequestCondition;
  bool exitThread;
  std::map<QString, std::shared_ptr<std::vector<Occurrence>>> occurrenceMap;
  
  // Search results
  QString searchErrors;
  std::map<QString, std::shared_ptr<std::vector<Occurrence>>> resultMap;
  
  // The USR of the item to search for
  QString itemUSR;
  QString itemSpelling;
  
  // UI
  QRadioButton* localSearchCheck;
  QRadioButton* semiGlobalSearchCheck;
  QRadioButton* globalSearchCheck;
  
  QTreeWidget* occurrencesTree;
  
  QLineEdit* renameToEdit;
  QLabel* searchInProgressLabel;
  QPushButton* renameButton;
  
  DocumentRange initialCursorOrSelectionRange;
  
  // Widget this dialog was shown from.
  DocumentWidget* widget;
};
