// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "rename_dialog.h"

#include <clang-c/Index.h>
#include <QBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QThread>
#include <QTreeWidget>

#include "cide/clang_index.h"
#include "cide/clang_parser.h"
#include "cide/clang_utils.h"
#include "cide/cpp_utils.h"
#include "cide/document_widget.h"
#include "cide/main_window.h"
#include "cide/parse_thread_pool.h"
#include "cide/qt_thread.h"

RenameDialog::RenameDialog(DocumentWidget* widget, const QString& itemUSR, const QString& itemSpelling, bool itemHasLocalDefinition, const DocumentRange& initialCursorOrSelectionRange, QWidget* parent)
    : QDialog(parent),
      itemUSR(itemUSR),
      itemSpelling(itemSpelling),
      initialCursorOrSelectionRange(initialCursorOrSelectionRange),
      widget(widget) {
  setWindowTitle(tr("Rename / find uses of \"%1\"").arg(itemSpelling));
  setWindowIcon(QIcon(":/cide/cide.png"));
  
  QLabel* searchingForLabel = new QLabel(tr("Searching for: <b>%1</b>").arg(itemSpelling.toHtmlEscaped()));
  
  localSearchCheck = new QRadioButton(tr("Local search"));
  QLabel* localSearchLabel = new QLabel(tr(
      "Searches in the current file, and all files included by it only. Very fast."));
  localSearchLabel->setWordWrap(true);
  
  semiGlobalSearchCheck = new QRadioButton(tr("Semi-global search"));
  QLabel* semiGlobalSearchLabel = new QLabel(tr(
      "Searches in all project files known to include a declaration of the search item, and in all files included by those files."
      " May miss occurrences if indexing has not finished yet, if libclang crashed for a file while indexing, or if source files are edited externally."));
  semiGlobalSearchLabel->setWordWrap(true);
  
  globalSearchCheck = new QRadioButton(tr("Global search"));
  QLabel* globalSearchLabel = new QLabel(tr(
      "Searches in all C/C++ files in the project directory and its subdirectories containing the search text,"
      " and in all files included by those files. Does not rely on indexing."));
  globalSearchLabel->setWordWrap(true);
  
  // TODO: Can we find uses within macros too?
  QLabel* warningLabel = new QLabel(tr("Warning: Uses added by macros will not be found."));
  warningLabel->setWordWrap(true);
  
  QLabel* occurrencesLabel = new QLabel(tr("Occurrences:"));
  
  occurrencesTree = new QTreeWidget();
  occurrencesTree->setHeaderHidden(true);
  
  QLabel* renameToLabel = new QLabel(tr("Rename to: "));
  renameToEdit = new QLineEdit(itemSpelling);
  
  searchInProgressLabel = new QLabel(tr("<b>Search in progress ...</b>"));
  renameButton = new QPushButton(tr("Rename"));
  QPushButton* closeButton = new QPushButton(tr("Close"));
  
  QGridLayout* searchModesLayout = new QGridLayout();
  searchModesLayout->addWidget(localSearchCheck, 0, 0);
  searchModesLayout->addWidget(localSearchLabel, 0, 1);
  searchModesLayout->addWidget(semiGlobalSearchCheck, 1, 0);
  searchModesLayout->addWidget(semiGlobalSearchLabel, 1, 1);
  searchModesLayout->addWidget(globalSearchCheck, 2, 0);
  searchModesLayout->addWidget(globalSearchLabel, 2, 1);
  searchModesLayout->setColumnStretch(1, 1);
  
  QHBoxLayout* renameLayout = new QHBoxLayout();
  renameLayout->addWidget(renameToLabel);
  renameLayout->addWidget(renameToEdit);
  
  QHBoxLayout* buttonsLayout = new QHBoxLayout();
  buttonsLayout->addWidget(searchInProgressLabel);
  buttonsLayout->addWidget(renameButton);
  buttonsLayout->addWidget(closeButton);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(searchingForLabel);
  layout->addLayout(searchModesLayout);
  layout->addWidget(warningLabel);
  layout->addWidget(occurrencesLabel);
  layout->addWidget(occurrencesTree);
  layout->addLayout(renameLayout);
  layout->addLayout(buttonsLayout);
  setLayout(layout);
  
  // Try to prevent the dialog from having a too small initial size
  resize(std::max(800, width()), std::max(600, height()));
  
  // --- Connections ---
  connect(localSearchCheck, &QRadioButton::clicked, this, &RenameDialog::SearchModeChanged);
  connect(semiGlobalSearchCheck, &QRadioButton::clicked, this, &RenameDialog::SearchModeChanged);
  connect(globalSearchCheck, &QRadioButton::clicked, this, &RenameDialog::SearchModeChanged);
  
  connect(renameButton, &QPushButton::clicked, this, &RenameDialog::Rename);
  connect(closeButton, &QPushButton::clicked, this, &RenameDialog::reject);
  
  // --- Set defaults ---
  if (itemHasLocalDefinition) {
    localSearchCheck->setChecked(true);
  } else {
    globalSearchCheck->setChecked(true);
  }
  
  renameToEdit->setFocus();
  // For some reason, setCursorPosition() did not work if called directly. Perhaps setFocus() overrides
  // the cursor setting in a delayed way? But setSelection() in the else case always worked.
  QTimer::singleShot(0, this, &RenameDialog::SetInitialCursorOrSelection);
  
  exitThread = false;
  SearchModeChanged();
  searchThread.reset(new std::thread(&RenameDialog::ThreadMain, this));
}

RenameDialog::~RenameDialog() {
  ExitSearchThread();
}

void RenameDialog::ExitSearchThread() {
  if (searchThread) {
    searchMutex.lock();
    exitThread = true;
    haveNewSearchRequest = true;
    searchMutex.unlock();
    newSearchRequestCondition.notify_all();
    
    // This basically just runs "searchThread->join();".
    // However, we have to run a Qt event loop while waiting for this,
    // since the searchThread may try to use RunInQtThreadBlocking() and
    // would deadlock otherwise.
    std::atomic<bool> exitFinished;
    exitFinished = false;
    std::thread exitThread([&]() {
      searchThread->join();
      exitFinished = true;
    });
    QEventLoop exitEventLoop;
    while (!exitFinished) {
      exitEventLoop.processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    exitThread.join();
    
    searchThread.reset();
  }
}

void RenameDialog::SetInitialCursorOrSelection() {
  if (initialCursorOrSelectionRange.IsEmpty()) {
    renameToEdit->deselect();
    renameToEdit->setCursorPosition(initialCursorOrSelectionRange.start.offset);
  } else {
    renameToEdit->setSelection(initialCursorOrSelectionRange.start.offset, initialCursorOrSelectionRange.end.offset - initialCursorOrSelectionRange.start.offset);
  }
}

void RenameDialog::ClearSearchResults() {
  occurrencesTree->clear();
  searchInProgressLabel->show();
  renameButton->setEnabled(false);
}

void RenameDialog::SearchModeChanged() {
  std::unique_lock<std::mutex> lock(searchMutex);
  
  ClearSearchResults();
  
  if (localSearchCheck->isChecked()) {
    searchMode = SearchMode::LocalSearch;
  } else if (semiGlobalSearchCheck->isChecked()) {
    searchMode = SearchMode::SemiGlobalSearch;
  } else {  // if (globalSearchCheck->isChecked()) {
    searchMode = SearchMode::GlobalSearch;
  }
  
  haveNewSearchRequest = true;
  lock.unlock();
  newSearchRequestCondition.notify_one();
}

void RenameDialog::Rename() {
  // If a search is currently running, abort it.
  ExitSearchThread();
  
  // Perform all replacements.
  MainWindow* mainWindow = widget->GetMainWindow();
  for (const auto& fileAndMap : resultMap) {
    QString path = fileAndMap.first;
    const std::vector<Occurrence>& occurrences = *fileAndMap.second;
    
    Document* document;
    DocumentWidget* documentWidget;
    if (mainWindow->GetDocumentAndWidgetForPath(path, &document, &documentWidget)) {
      RenameInDocument(documentWidget, occurrences);
    } else {
      RenameInFileOnDisk(path, occurrences);
    }
  }
  
  accept();
}

void RenameDialog::ThreadMain() {
  while (true) {
    SearchMode thisSearchMode;
    
    std::unique_lock<std::mutex> lock(searchMutex);
    if (exitThread) {
      return;
    }
    while (!haveNewSearchRequest) {
      newSearchRequestCondition.wait(lock);
      if (exitThread) {
        return;
      }
    }
    haveNewSearchRequest = false;
    thisSearchMode = searchMode;
    lock.unlock();
    
    PerformSearch(thisSearchMode);
    
    lock.lock();
    if (!haveNewSearchRequest) {
      resultMap.swap(occurrenceMap);
    }
    lock.unlock();
  }
}

void RenameDialog::PerformSearch(SearchMode mode) {
  searchErrors.clear();
  occurrenceMap.clear();
  
  RunInQtThreadBlocking([&]() {
    searchInProgressLabel->setText(tr("<b>Search in progress ...</b>"));
  });
  
  switch (mode) {
  case SearchMode::LocalSearch:
    PerformLocalSearch();
    break;
  case SearchMode::SemiGlobalSearch:
    PerformSemiGlobalSearch();
    break;
  case SearchMode::GlobalSearch:
    PerformGlobalSearch();
    break;
  }
  
  RunInQtThreadBlocking([&]() {
    // If there is already a new search request, do not add our results.
    if (haveNewSearchRequest) {
      return;
    }
    
    QDir projectDir = QDir::root();
    auto project = widget->GetMainWindow()->GetCurrentProject();
    if (project) {
      projectDir = QFileInfo(project->GetYAMLFilePath()).dir();
    }
    
    // Create tree widget items
    QTreeWidgetItem* lastFileItem = nullptr;
    for (const auto& fileAndMap : occurrenceMap) {
      QString path = fileAndMap.first;
      const std::vector<Occurrence>& occurrences = *fileAndMap.second;
      
      lastFileItem = new QTreeWidgetItem(occurrencesTree, lastFileItem);
      occurrencesTree->setItemWidget(
          lastFileItem, 0,
          new QLabel(QStringLiteral("<b>%1</b>: %2 matches").arg(projectDir.relativeFilePath(path).toHtmlEscaped()).arg(occurrences.size())));
      lastFileItem->setExpanded(true);
      
      // TODO: Merge multiple occurrences in the same line into a single QTreeWidgetItem
      QTreeWidgetItem* lastLineItem = nullptr;
      for (const Occurrence& occ : occurrences) {
        lastLineItem = new QTreeWidgetItem(lastFileItem, lastLineItem);
        lastLineItem->setFlags(lastLineItem->flags() | Qt::ItemNeverHasChildren);
        lastLineItem->setData(0, Qt::UserRole, QStringLiteral("file://%1:%2:%3").arg(path).arg(occ.line + 1).arg(occ.column + 1));
        QString labelText =
            tr("<span style=\"color:gray;\">Line %1:</span> %2").arg(occ.line + 1).arg(
                occ.lineText.left(occ.column).toHtmlEscaped() +
                QStringLiteral("<b style=\"background-color:#efedec;\">") + occ.lineText.mid(occ.column, occ.length).toHtmlEscaped() + QStringLiteral("</b>") +
                occ.lineText.right(occ.lineText.size() - (occ.column + occ.length)).toHtmlEscaped());
        QLabel* lineLabel = new QLabel(labelText);
        occurrencesTree->setItemWidget(lastLineItem, 0, lineLabel);
      }
    }
    
    // Update other widgets
    searchInProgressLabel->hide();
    renameButton->setEnabled(true);
    
    // Show error message if there were errors
    if (!searchErrors.isEmpty()) {
      QMessageBox::warning(this, tr("Occurrence search"), tr("The following error(s) occurred during occurrence search:\n\n%1").arg(searchErrors));
    }
  });
}

struct SearchForUSRVisitorData {
  CXTranslationUnit TU;
  bool searchInSearchFileOnly;
  CXFile searchFile;
  QString searchUSR;
  
  CXCursor result;
  
  bool debug;
};

CXChildVisitResult VisitClangAST_SearchForUSR(CXCursor cursor, CXCursor /*parent*/, CXClientData client_data) {
  SearchForUSRVisitorData* data = reinterpret_cast<SearchForUSRVisitorData*>(client_data);
  
  // Skip over cursors in other files
  if (data->searchInSearchFileOnly) {
    CXFile cursorFile;
    clang_getFileLocation(clang_getCursorLocation(cursor), &cursorFile, nullptr, nullptr, nullptr);
    if (!clang_File_isEqual(cursorFile, data->searchFile)) {
      return CXChildVisit_Continue;
    }
  }
  
  // Does the cursor refer to the searched-for USR?
  CXCursor referencedCursor = clang_getCursorReferenced(cursor);
  if (!clang_Cursor_isNull(referencedCursor)) {
    QString USR = ClangString(clang_getCursorUSR(referencedCursor)).ToQString();
    if (USR == data->searchUSR) {
      data->result = cursor;
      
      if (data->debug) {
        CXFile cursorFile;
        unsigned int cursorLine;
        clang_getFileLocation(clang_getCursorLocation(cursor), &cursorFile, &cursorLine, nullptr, nullptr);
        qDebug() << "Found a reference to the USR in file:" << GetClangFilePath(cursorFile) << "at line" << cursorLine;
      }
      
      bool isDeclaration = clang_isDeclaration(clang_getCursorKind(cursor));
      if (isDeclaration) {
        return CXChildVisit_Break;
      }
      
      // Found a cursor with the desired USR, but it is not a declaration.
      // Remember the cursor, but search on for a possible declaration cursor.
      
      // NOTE: Old code for finding the use ranges directly ourselves.
      //       The problem with this is that libclang does not expose all the AST nodes
      //       and makes it difficult to find the correct range in each case. It seems much
      //       safer to instead rely on clang_findReferencesInFile() being implemented properly.
      // CXCursorKind kind = clang_getCursorKind(cursor);
      // CXSourceRange range = clang_getNullRange();
      // if (kind == CXCursor_CallExpr) {
      //   // Use a child of this AST node instead, as this encompasses a too large range
      // } else if (kind == CXCursor_CXXMethod || kind == CXCursor_VarDecl) {
      //   range = clang_Cursor_getSpellingNameRange(cursor, 0, 0);
      // } else {
      //   range = clang_getCursorExtent(cursor);
      // }
      // 
      // if (!clang_Range_isNull(range)) {
      //   qDebug() << "Found instance with extent: " << GetClangText(range, data->TU)
      //            << " (kind:" << ClangString(clang_getCursorKindSpelling(kind)).ToQString() << ")";
      // }
    }
  }
  
  return CXChildVisit_Recurse;
}

struct VisitReferencesContext {
  CXTranslationUnit TU;
  int itemSpellingSize;
  QByteArray itemUSR;
  
  bool gotReferenceWithinMacro;
  std::vector<RenameDialog::Occurrence>* results;
  
  bool debug;
};

CXVisitorResult VisitReferences(void* context, CXCursor cursor, CXSourceRange range) {
  VisitReferencesContext* data = static_cast<VisitReferencesContext*>(context);
  
  // If the reference is within a macro, the range is invalid. In this case,
  // we ignore it.
  if (clang_Range_isNull(range)) {
    data->gotReferenceWithinMacro = true;
    return CXVisit_Continue;
  }
  
  // It seems that libclang sometimes returned wrong results. I tried to find
  // all uses of LicenseBrowser::loadResource() (defined in about_dialog.cc), which returned
  // some uses of other loadResource() functions from other classes. However,
  // in those cases, the file that was searched for did not include about_dialog.h,
  // so it did not see the file that contained the cursor. So, I am not sure whether
  // the wrong results were due to a wrong use of the function. In any case, we need
  // to filter the results for the correct USR. Let's hope that we do not miss any
  // uses as a result (in cases where the libclang cursor fails to expose the internal
  // AST properly).
  QByteArray USR = ClangString(clang_getCursorUSR(clang_getCursorReferenced(cursor))).ToQByteArray();
  if (USR != data->itemUSR) {
    return CXVisit_Continue;
  }
  
  CXFile file;
  
  unsigned int startLine;
  unsigned int startColumn;
  unsigned int startOffset;
  clang_getFileLocation(clang_getRangeStart(range), &file, &startLine, &startColumn, &startOffset);
  
  unsigned int endLine;
  unsigned int endColumn;
  unsigned int endOffset;
  clang_getFileLocation(clang_getRangeEnd(range), nullptr, &endLine, &endColumn, &endOffset);
  
  std::size_t textSize;
  const char* text = clang_getFileContents(data->TU, file, &textSize);
  QString lineText;
  if (text) {
    // Expand startOffset, endOffset to contain the line of the occurrence
    while (startOffset > 0 && text[startOffset] != '\n') {
      -- startOffset;
    }
    if (text[startOffset] == '\n') {
      ++ startOffset;
    }
    
    while (endOffset < static_cast<int>(textSize) - 1 && text[endOffset] != '\n') {
      ++ endOffset;
    }
    if (text[endOffset] == '\n') {
      -- endOffset;
    }
    
    lineText = QString::fromUtf8(text + startOffset, endOffset + 1 - startOffset);
  }
  
  int length;
  if (startLine == endLine) {
    length = endColumn - startColumn;
    if (length != data->itemSpellingSize) {
      qDebug() << "Warning: RenameDialog found an occurrence which is not of the expected length.";
    }
  } else {
    length = data->itemSpellingSize;
  }
  
  data->results->emplace_back(startLine - 1, startColumn - 1, length, lineText);
  
  if (data->debug) {
    qDebug() << "Found a reference. USR:" << ClangString(clang_getCursorUSR(clang_getCursorReferenced(cursor))).ToQString();
  }
  
  return CXVisit_Continue;
}

void RenameDialog::PerformLocalSearch() {
  QString path;
  RunInQtThreadBlocking([&]() {
    path = widget->GetDocument()->path();
  });
  SearchInFiles({path}, true);
}

void RenameDialog::PerformSemiGlobalSearch() {
  // First, find the definition and all declarations of the search item via its USR.
  std::unordered_set<QString> relevantFiles;
  RunInQtThreadBlocking([&]() {
    USRStorage::Instance().GetFilesForUSRLookup(widget->GetDocument()->path(), widget->GetMainWindow(), &relevantFiles);
  });
  std::vector<std::pair<QString, USRDecl>> foundDecls;  // pair of file path and USR
  USRStorage::Instance().LookupUSRs(
      itemUSR.toUtf8(),
      relevantFiles,
      &foundDecls);
  
  std::unordered_set<QString> filesWithDeclarations;
  for (const auto& item : foundDecls) {
    filesWithDeclarations.insert(item.first);
  }
  
  // Given all files containing declarations or the definition of the search item,
  // find all project files known to include at least one of these files.
  std::unordered_set<QString> filesToSearch;
  RunInQtThreadBlocking([&]() {
    if (haveNewSearchRequest) {
      return;
    }
    
    for (const auto& project : widget->GetMainWindow()->GetProjects()) {
      for (int targetIdx = 0; targetIdx < project->GetNumTargets(); ++ targetIdx) {
        const Target& target = project->GetTarget(targetIdx);
        for (const QString& fileWithDeclaration : filesWithDeclarations) {
          QStringList newFilesToSearch = target.FindAllFilesThatInclude(fileWithDeclaration);
          for (const QString& path : newFilesToSearch) {
            filesToSearch.insert(path);
          }
        }
      }
    }
  });
  
  // Search in the resulting files.
  SearchInFiles(filesToSearch, true);
}

void RenameDialog::PerformGlobalSearch() {
  constexpr bool kDebug = false;

  // Get the root path of the current file's project
  QString rootDir;
  RunInQtThreadBlocking([&]() {
    if (haveNewSearchRequest) {
      return;
    }
    // Default to starting the search from the current file's directory in case we do not find a project
    rootDir = QFileInfo(widget->GetDocument()->path()).dir().path();
    for (const auto& project : widget->GetMainWindow()->GetProjects()) {
      if (project->ContainsFile(widget->GetDocument()->path())) {
        rootDir = QFileInfo(project->GetYAMLFilePath()).dir().path();
        break;
      }
    }
  });

  if (kDebug) {
    qDebug() << "Global search root dir:" << rootDir;
  }
  
  // Find all C/C++ files within this directory and its subdirectories
  std::unordered_set<QString> paths;
  std::vector<QDir> doneList;
  std::vector<QDir> workList = {rootDir};
  while (!workList.empty()) {
    if (haveNewSearchRequest) {
      return;
    }
    QDir dir = workList.back();
    workList.pop_back();
    doneList.push_back(dir);
    
    for (const QFileInfo& fileInfo : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden)) {
      if (fileInfo.isDir()) {
        // Add this directory to the list of directories to search in if we did
        // not visit it yet (which might be the case if there was a symlink).
        if (kDebug) {
          qDebug() << "Considering child dir:" << fileInfo.filePath();
        }
        bool alreadyVisited = false;
        QDir childDir(fileInfo.filePath());
        for (const QDir& doneDir : doneList) {
          if (doneDir == childDir) {
            alreadyVisited = true;
            break;
          }
        }
        if (!alreadyVisited) {
          workList.push_back(childDir);
        }
      } else if (GuessIsCFile(fileInfo.filePath())) {
        // Read the file to see whether it contains the search term.
        if (kDebug) {
          qDebug() << "Considering file:" << fileInfo.filePath();
        }
        QFile file(fileInfo.filePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
          bool found = false;
          while (!file.atEnd()) {
            QString lineText = QString::fromUtf8(file.readLine());  // TODO: Allow reading other formats than UTF-8 only?
            if (lineText.contains(itemSpelling, Qt::CaseSensitive)) {
              found = true;
              break;
            }
          }
          if (found) {
            paths.insert(fileInfo.filePath());
            qDebug() << "--> Adding file to search set:" << fileInfo.filePath();
          }
        }
      }
    }
  }
  
  // Search in the resulting files.
  SearchInFiles(paths, true);
}

void RenameDialog::SearchInFiles(const std::unordered_set<QString>& paths, bool searchInIncludedFiles) {
  // TODO: Search in multiple files simultaneously with multithreading
  int numPaths = paths.size();
  int searchedPaths = 0;
  
  for (const QString& path : paths) {
    if (haveNewSearchRequest) {
      return;
    }
    RunInQtThreadBlocking([&]() {
      searchInProgressLabel->setText(tr("<b>Search in progress (%1%) ...</b>").arg((searchedPaths * 100) / numPaths));
    });
    
    SearchInFile(path, searchInIncludedFiles);
    ++ searchedPaths;
  }
}


static void VisitInclusions(
    CXFile included_file,
    CXSourceLocation* /*inclusion_stack*/,
    unsigned /*include_len*/,
    CXClientData client_data) {
  std::unordered_set<QString>* includedPaths = reinterpret_cast<std::unordered_set<QString>*>(client_data);
  includedPaths->insert(QFileInfo(GetClangFilePath(included_file)).canonicalFilePath());
}

void RenameDialog::SearchInFile(const QString& path, bool searchInIncludedFiles) {
  constexpr bool kDebug = false;
  if (kDebug) {
    qDebug() << "SearchInFile() called for file" << path << "and USR" << itemUSR;
  }
  
  // Obtain a TU for the file. If the file is open in the editor, we take an existing TU.
  // Otherwise, we have to parse the file.
  Document* pathDocument = nullptr;
  DocumentWidget* pathWidget = nullptr;
  RunInQtThreadBlocking([&]() {
    widget->GetMainWindow()->GetDocumentAndWidgetForPath(path, &pathDocument, &pathWidget);
  });
  
  std::shared_ptr<ClangTU> TU;
  CXTranslationUnit clangTU = nullptr;
  if (pathWidget) {
    GetTUFromDocument(pathDocument, &TU);
    if (kDebug) {
      qDebug() << "Search in" << path << ": Got TU from document:" << TU.get();
    }
    if (!TU) {
      return;
    }
    clangTU = TU->TU();
  } else {
    ParseFileToGetTU(path, &clangTU);
    if (kDebug) {
      qDebug() << "Search in" << path << ": Got TU by parsing:" << clangTU;
    }
    if (clangTU == nullptr) {
      return;
    }
  }
  
  // Search for a declaration cursor having the desired USR.
  SearchForUSRVisitorData visitorData;
  visitorData.TU = clangTU;
  visitorData.searchInSearchFileOnly = !searchInIncludedFiles;
  visitorData.searchFile = clang_getFile(clangTU, path.toLocal8Bit());
  visitorData.searchUSR = itemUSR;
  visitorData.result = clang_getNullCursor();
  visitorData.debug = kDebug;
  if (visitorData.searchFile) {
    clang_visitChildren(clang_getTranslationUnitCursor(clangTU), &VisitClangAST_SearchForUSR, &visitorData);
  } else {
    qDebug() << "ERROR: RenameDialog::SearchInFile(): searchFile not found in TU";
  }
  
  if (clang_Cursor_isNull(visitorData.result)) {
    qDebug() << "Warning: RenameDialog::SearchInFile(): No cursor with the searched-for USR found in file" << path;
  } else {
    if (kDebug) {
      qDebug() << "USR of the found declaration/definition:" << ClangString(clang_getCursorUSR(visitorData.result)).ToQString();
    }
    
    // Obtain all references to this cursor.
    std::vector<Occurrence> occurrences;
    
    VisitReferencesContext context;
    context.results = &occurrences;
    context.TU = clangTU;
    context.itemSpellingSize = itemSpelling.size();
    context.itemUSR = itemUSR.toUtf8();
    context.debug = kDebug;
    
    std::unordered_set<QString> filesToSearch;
    if (searchInIncludedFiles) {
      // TODO: Limit the files to search to files within the project tree?
      clang_getInclusions(clangTU, &VisitInclusions, &filesToSearch);
    } else {
      filesToSearch = {path};
    }
    
    CXCursorAndRangeVisitor referencesVisitor;
    referencesVisitor.context = &context;
    referencesVisitor.visit = &VisitReferences;
    for (const QString& fileToSearch : filesToSearch) {
      CXFile clangFile = clang_getFile(clangTU, fileToSearch.toLocal8Bit());
      if (clangFile) {
        context.gotReferenceWithinMacro = false;
        clang_findReferencesInFile(visitorData.result, clangFile, referencesVisitor);
        
        if (context.gotReferenceWithinMacro) {
          searchErrors += tr("Found a possible occurrence in a macro in file: %1\n").arg(fileToSearch);
        }
        
        // Store the occurrences in the occurrenceMap.
        // TODO: We could already have a result from another TU. Due to different
        //       preprocessor definitions, that result could be different. Merge the results.
        if (!occurrences.empty()) {
          std::shared_ptr<std::vector<Occurrence>> sharedVector(new std::vector<Occurrence>());
          sharedVector->swap(occurrences);
          occurrenceMap[fileToSearch] = sharedVector;
        }
      }
    }
  }
  
  // Return / dispose the file's TU.
  if (TU) {
    RunInQtThreadBlocking([&]() {
      widget->GetDocument()->GetTUPool()->PutTU(TU, false);
    });
  } else {
    clang_disposeTranslationUnit(clangTU);
  }
}

void RenameDialog::GetTUFromDocument(Document* document, std::shared_ptr<ClangTU>* TU) {
  // Wait until no parse operation is active or scheduled for the file. Abort if we get a new search request.
  while (true) {
    if (haveNewSearchRequest) {
      return;
    }
    
    bool parsePending = false;
    RunInQtThreadBlocking([&]() {
      if (ParseThreadPool::Instance().DoesAParseRequestExistForDocument(document) ||
          ParseThreadPool::Instance().IsDocumentBeingParsed(document)) {
        parsePending = true;
      }
    });
    if (!parsePending) {
      break;
    }
    
    QThread::msleep(10);
  }
  
  // Wait until we get the current file's most up-to-date TU. Abort if we get a new search request.
  while (true) {
    if (haveNewSearchRequest) {
      return;
    }
    
    RunInQtThreadBlocking([&]() {
      *TU = document->GetTUPool()->TakeMostUpToDateTU();
    });
    if (TU) {
      break;
    }
    
    QThread::msleep(10);
  }
}

void RenameDialog::ParseFileToGetTU(const QString& path, CXTranslationUnit* clangTU) {
  std::vector<QByteArray> commandLineArgs;
  std::vector<const char*> commandLineArgPtrs;
  
  std::vector<CXUnsavedFile> unsavedFiles;
  std::vector<std::string> unsavedFileContents;
  std::vector<std::string> unsavedFilePaths;
  
  bool exit = false;
  RunInQtThreadBlocking([&]() {
    // Find compile settings for the file
    std::shared_ptr<Project> usedProject;
    CompileSettings* settings = FindParseSettingsForFile(path, widget->GetMainWindow()->GetProjects(), &usedProject);
    
    if (!settings) {
      qDebug() << "Warning: RenameDialog::ParseFileToGetTU(): Did not find compile settings for file:" << path;
      exit = true;
      return;
    }
    
    // Get command line arguments for parsing
    commandLineArgs = settings->BuildCommandLineArgs(true, path, usedProject.get());
    commandLineArgPtrs.resize(commandLineArgs.size());
    for (int i = 0; i < commandLineArgs.size(); ++ i) {
      commandLineArgPtrs[i] = commandLineArgs[i].data();
    }
    
    // Get the contents of all unsaved files from the main window
    GetAllUnsavedFiles(widget->GetMainWindow(), &unsavedFiles, &unsavedFileContents, &unsavedFilePaths);
  });
  if (exit) {
    return;
  }
  
  // Parse the file to obtain a translation unit.
  // Use settings for quick parsing, getting only the necessary information.
  ClangIndex index;
  
  unsigned parseOptions = CXTranslationUnit_KeepGoing;
  #if CINDEX_VERSION_MINOR >= 59
    parseOptions |= CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles;
  #endif
  
  constexpr int numAttempts = 3;
  for (int attempt = 0; attempt < numAttempts; ++ attempt) {
    CXErrorCode parseResult = CXError_Failure;
    parseResult = clang_parseTranslationUnit2(
        index.index(),
        path.toLocal8Bit().data(),
        commandLineArgPtrs.data(),
        commandLineArgPtrs.size(),
        unsavedFiles.data(),
        unsavedFiles.size(),
        parseOptions,
        clangTU);
    
    if (parseResult == CXError_Success) {
      return;
    }
    
    clang_disposeTranslationUnit(*clangTU);
    *clangTU = nullptr;
  }
  
  searchErrors += tr("Failed to parse file: %1\n").arg(path);
}

void RenameDialog::RenameInDocument(DocumentWidget* widget, const std::vector<Occurrence>& occurrences) {
  widget->GetDocument()->StartUndoStep();
  for (int i = static_cast<int>(occurrences.size()) - 1; i >= 0; -- i) {
    const Occurrence& occ = occurrences[i];
    
    DocumentLocation startLoc = widget->MapLineColToDocumentLocation(occ.line, occ.column);
    DocumentRange occRange(startLoc, startLoc + occ.length);
    widget->Replace(occRange, renameToEdit->text());
  }
  widget->GetDocument()->EndUndoStep();
}

void RenameDialog::RenameInFileOnDisk(const QString& path, const std::vector<Occurrence>& occurrences) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::warning(this, tr("Rename"), tr("Failed to open file: %1").arg(path));
    return;
  }
  
  QString modifiedFileText;
  
  int currentOccurrence = 0;
  int line = -1;  // zero-based
  while (!file.atEnd()) {
    ++ line;
    
    QString lineText = QString::fromUtf8(file.readLine());  // TODO: Allow reading other formats than UTF-8 only?
    
    if (currentOccurrence < occurrences.size() &&
        occurrences[currentOccurrence].line == line) {
      const Occurrence& occ = occurrences[currentOccurrence];
      lineText = occ.lineText;
      lineText.replace(occ.column, occ.length, renameToEdit->text());
      
      ++ currentOccurrence;
    }
    
    modifiedFileText += lineText;
  }
  
  file.close();
  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    file.write(modifiedFileText.toUtf8());
  } else {
    QMessageBox::warning(this, tr("Rename"), tr("File not writable: %1").arg(path));
  }
}
