// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/find_and_replace_in_files.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressDialog>
#include <QPushButton>
#include <QTreeWidget>

#include "cide/main_window.h"
#include "cide/settings.h"


class LabelWithClickedSignal : public QLabel {
 Q_OBJECT
 public:
  LabelWithClickedSignal(const QString& text, QWidget* parent = nullptr)
      : QLabel(text, parent) {}
  
 signals:
  void clicked();
  
 protected:
  void mousePressEvent(QMouseEvent* event) override {
    QLabel::mousePressEvent(event);
    if (event->button() == Qt::LeftButton) {
      emit clicked();
    }
  }
};


QAction* FindAndReplaceInFiles::Initialize(MainWindow* mainWindow) {
  this->mainWindow = mainWindow;
  
  QAction* findAndReplaceInFilesAction = new ActionWithConfigurableShortcut(tr("Find and replace in files"), findAndReplaceInFilesShortcut, this);
  connect(findAndReplaceInFilesAction, &QAction::triggered, this, &FindAndReplaceInFiles::ShowDialogWithDefaultSettings);
  mainWindow->addAction(findAndReplaceInFilesAction);
  
  return findAndReplaceInFilesAction;
}

void FindAndReplaceInFiles::CreateDockWidget() {
  findAndReplaceInFilesDock = new QDockWidget(tr("Find/replace in files"));
  findAndReplaceInFilesDock->setFeatures(
      QDockWidget::DockWidgetClosable |
      QDockWidget::DockWidgetMovable |
      QDockWidget::DockWidgetFloatable |
      QDockWidget::DockWidgetVerticalTitleBar);
  
  findAndReplaceResultsLabel = new QLabel();
  QLabel* findAndReplaceReplacementLabel = new QLabel(tr("Replace with: "));
  findAndReplaceEdit = new QLineEdit();
  findAndReplaceEdit->setMinimumWidth(400);
  findAndReplaceReplaceButton = new QPushButton(tr("Replace"));
  connect(findAndReplaceReplaceButton, &QPushButton::clicked, this, &FindAndReplaceInFiles::ReplaceClicked);
  
  findAndReplaceResultsTree = new QTreeWidget();
  findAndReplaceResultsTree->setColumnCount(1);
  findAndReplaceResultsTree->setHeaderHidden(true);
  connect(findAndReplaceResultsTree, &QTreeWidget::itemActivated, [&](QTreeWidgetItem* item, int /*column*/) {
    QString locationString = item->data(0, Qt::UserRole).toString();
    if (!locationString.isEmpty()) {
      mainWindow->GotoDocumentLocation(locationString);
    }
  });
  
  QHBoxLayout* topLayout = new QHBoxLayout();
  topLayout->setContentsMargins(0, 0, 0, 0);
  topLayout->addWidget(findAndReplaceResultsLabel, 1);
  topLayout->addWidget(findAndReplaceReplacementLabel);
  topLayout->addWidget(findAndReplaceEdit);
  topLayout->addWidget(findAndReplaceReplaceButton);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addLayout(topLayout);
  layout->addWidget(findAndReplaceResultsTree, 1);
  
  QWidget* containerWidget = new QWidget();
  containerWidget->setLayout(layout);
  findAndReplaceInFilesDock->setWidget(containerWidget);
  
  mainWindow->addDockWidget(Qt::BottomDockWidgetArea, findAndReplaceInFilesDock);
}

void FindAndReplaceInFiles::ShowDialog(const QString& initialPath) {
  if (!ShowDialogInternal(initialPath)) {
    return;
  }
  
  if (!findAndReplaceInFilesDock) {
    CreateDockWidget();
  } else if (!findAndReplaceInFilesDock->isVisible()) {
    findAndReplaceInFilesDock->setVisible(true);
  }
  
  QDir startDir(searchFolderPath);
  if (!startDir.exists()) {
    QMessageBox::warning(mainWindow, tr("Find in files"), tr("Search directory does not exist."));
    return;
  }
  
  findAndReplaceResultsTree->clear();
  findAndReplaceEdit->setEnabled(false);
  findAndReplaceReplaceButton->setEnabled(false);
  
  QProgressDialog progress(tr("Searching in files..."), "Abort", 0, 0, mainWindow);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(200);
  
  // Count the number of files to search in to be able to show a progress estimate.
  // Also cache all the paths to the files to avoid iterating over the directories again.
  int numFilesToSearch = 0;
  filePaths.clear();
  filesWithOccurrencesPaths.clear();
  
  std::vector<QDir> doneList;
  std::vector<QDir> workList = {startDir};
  while (!workList.empty()) {
    qDebug() << "workList iteration (size: " << workList.size() << ")";
    progress.setValue(0);
    // According to the documentation of QProgressDialog, it should call processEvents()
    // itself during setValue() if set to modal, but that does not seem to be the case,
    // at least if the progress dialog has an unspecified maximum value (maximum set to 0).
    // Probably this is because the progress value always stays at 0 then.
    QCoreApplication::processEvents();
    if (progress.wasCanceled()) {
      findAndReplaceResultsLabel->setText(tr("Search canceled."));
      return;
    }
    
    QDir dir = workList.back();
    workList.pop_back();
    doneList.push_back(dir);
    
    for (const QFileInfo& fileInfo : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden)) {
      if (fileInfo.isDir()) {
        // Add this directory to the list of directories to search in if we did
        // not visit it yet (which might be the case if there was a symlink).
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
      } else {
        ++ numFilesToSearch;
        filePaths.push_back(fileInfo.filePath());
      }
    }
  }
  
  progress.setMaximum(numFilesToSearch);
  
  // Perform the actual search.
  QTreeWidgetItem* lastFileItem = nullptr;
  QString lastFilePath;
  int numFilesSearched = 0;
  int numOccurrences = 0;
  int occurrencesInFile = 0;
  
  for (const QString& filePath : filePaths) {
    progress.setValue(numFilesSearched);
    if (progress.wasCanceled()) {
      findAndReplaceResultsLabel->setText(tr("Search canceled."));
      return;
    }
    
    ++ numFilesSearched;
    
    Document* fileDocument;
    DocumentWidget* fileWidget;
    if (mainWindow->GetDocumentAndWidgetForPath(filePath, &fileDocument, &fileWidget)) {
      SearchInDocument(fileDocument, startDir, filePath, lastFileItem, lastFilePath, occurrencesInFile, numOccurrences);
    } else {
      SearchInFile(startDir, filePath, lastFileItem, lastFilePath, occurrencesInFile, numOccurrences);
    }
  }
  if (lastFileItem) {
    SetLastFileItem(startDir, lastFileItem, lastFilePath, occurrencesInFile);
  }
  
  findAndReplaceResultsLabel->setText(tr("Found %1 occurrences of %2 in %3.").arg(numOccurrences).arg(findText).arg(searchFolderPath));
  findAndReplaceEdit->setEnabled(true);
  findAndReplaceReplaceButton->setEnabled(true);
}

bool FindAndReplaceInFiles::ShowDialogInternal(const QString initialPath) {
  QSettings settings;
  
  QDialog dialog(mainWindow);
  dialog.setWindowTitle(tr("Find in files"));
  
  QGridLayout* layout = new QGridLayout();
  
  // Find: [   ] [ ] Match case
  QLabel* findLabel = new QLabel(tr("Find:"));
  layout->addWidget(findLabel, 0, 0);
  
  QLineEdit* findEdit = new QLineEdit(settings.value("findAndReplaceInFiles/find").toString());
  DocumentWidget* currentDocumentWidget = mainWindow->GetCurrentDocumentWidget();
  if (currentDocumentWidget) {
    QString selectedText = currentDocumentWidget->GetSelectedText();
    if (!selectedText.isEmpty() && selectedText.indexOf('\n') == -1) {
      findEdit->setText(selectedText);
    }
  }
  layout->addWidget(findEdit, 0, 1);
  
  QCheckBox* matchCaseCheck = new QCheckBox(tr("Match case"));
  matchCaseCheck->setChecked(true);
  layout->addWidget(matchCaseCheck, 0, 2);
  
  // In: [   ](...)(Set to current directory)
  QLabel* inLabel = new QLabel(tr("In:"));
  layout->addWidget(inLabel, 1, 0);
  
  QLineEdit* inEdit = new QLineEdit(initialPath.isEmpty() ? settings.value("findAndReplaceInFiles/in").toString() : initialPath);
  layout->addWidget(inEdit, 1, 1);
  
  QPushButton* inButton = new QPushButton("...");
  MinimizeButtonSize(inButton, 1.5);
  
  QPushButton* setToCurrentDirectoryButton = new QPushButton(tr("Set to current directory"));
  setToCurrentDirectoryButton->setEnabled(mainWindow->GetCurrentDocument() != nullptr);
  
  QHBoxLayout* buttonsLayout = new QHBoxLayout();
  buttonsLayout->setContentsMargins(0, 0, 0, 0);
  buttonsLayout->addWidget(inButton);
  buttonsLayout->addWidget(setToCurrentDirectoryButton);
  layout->addLayout(buttonsLayout, 1, 2);
  
  QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel);
  buttonBox->addButton(tr("Search"), QDialogButtonBox::AcceptRole);
  connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  
  QVBoxLayout* verticalLayout = new QVBoxLayout();
  verticalLayout->addLayout(layout);
  verticalLayout->addWidget(buttonBox);
  
  dialog.setLayout(verticalLayout);
  
  connect(inButton, &QPushButton::clicked, [&]() {
    QString dir = QFileDialog::getExistingDirectory(
        &dialog,
        tr("Choose search directory"),
        inEdit->text(),
        QFileDialog::DontUseNativeDialog);
    if (!dir.isEmpty()) {
      inEdit->setText(dir);
    }
  });
  
  connect(setToCurrentDirectoryButton, &QPushButton::clicked, [&]() {
    auto currentDocument = mainWindow->GetCurrentDocument();
    if (currentDocument) {
      inEdit->setText(QFileInfo(currentDocument->path()).dir().path());
    }
  });
  
  dialog.resize(800, dialog.height());
  
  findEdit->setFocus();
  
  if (dialog.exec() == QDialog::Rejected) {
    return false;
  }
  
  findText = findEdit->text();
  caseSensitivity = matchCaseCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
  searchFolderPath = inEdit->text();
  
  settings.setValue("findAndReplaceInFiles/find", findText);
  settings.setValue("findAndReplaceInFiles/in", searchFolderPath);
  
  return true;
}

void FindAndReplaceInFiles::ReplaceClicked() {
  QString errorMessages;
  QString replacementText = findAndReplaceEdit->text();
  
  for (const QString& filePath : filesWithOccurrencesPaths) {
    Document* fileDocument;
    DocumentWidget* fileWidget;
    if (mainWindow->GetDocumentAndWidgetForPath(filePath, &fileDocument, &fileWidget)) {
      ReplaceInDocument(fileWidget, replacementText);
    } else {
      ReplaceInFile(filePath, replacementText, &errorMessages);
    }
  }
  
  if (!errorMessages.isEmpty()) {
    QMessageBox::warning(mainWindow, tr("Error(s) while replacing"), errorMessages);
  }
  findAndReplaceReplaceButton->setEnabled(false);
}

void FindAndReplaceInFiles::ShowDialogWithDefaultSettings() {
  ShowDialog();
}

void FindAndReplaceInFiles::SetLastFileItem(const QDir& startDir, QTreeWidgetItem* lastFileItem, QString lastFilePath, int occurrencesInFile) {
  findAndReplaceResultsTree->setItemWidget(
      lastFileItem, 0,
      new QLabel(QStringLiteral("<b>%1</b>: %2 matches").arg(startDir.relativeFilePath(lastFilePath).toHtmlEscaped()).arg(occurrencesInFile)));
}

void FindAndReplaceInFiles::SearchInDocument(Document* document, const QDir& startDir, const QString& filePath, QTreeWidgetItem*& lastFileItem, QString& lastFilePath, int& occurrencesInFile, int& numOccurrences) {
  bool haveOccurrenceInFile = false;
  QTreeWidgetItem* lastLineItem = nullptr;
  std::vector<int> occurrenceColumns;  // zero-based
  int line = 0;  // one-based
  
  Document::LineIterator lineIt(document);
  while (lineIt.IsValid()) {
    ++ line;
    QString lineText = document->TextForRange(lineIt.GetLineRange());
    ++ lineIt;
    
    SearchInLine(
        line,
        lineText,
        haveOccurrenceInFile,
        lastLineItem,
        occurrenceColumns,
        startDir,
        filePath,
        lastFileItem,
        lastFilePath,
        occurrencesInFile,
        numOccurrences);
  }
}

void FindAndReplaceInFiles::SearchInFile(const QDir& startDir, const QString& filePath, QTreeWidgetItem*& lastFileItem, QString& lastFilePath, int& occurrencesInFile, int& numOccurrences) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  
  bool haveOccurrenceInFile = false;
  QTreeWidgetItem* lastLineItem = nullptr;
  std::vector<int> occurrenceColumns;  // zero-based
  int line = 0;  // one-based
  
  // TODO: Do not process the file line by line to allow for find texts that span multiple lines
  while (!file.atEnd()) {
    ++ line;
    QString lineText = QString::fromUtf8(file.readLine());  // TODO: Allow reading other formats than UTF-8 only
    
    SearchInLine(
        line,
        lineText,
        haveOccurrenceInFile,
        lastLineItem,
        occurrenceColumns,
        startDir,
        filePath,
        lastFileItem,
        lastFilePath,
        occurrencesInFile,
        numOccurrences);
  }
}

void FindAndReplaceInFiles::SearchInLine(
    int line,
    QString lineText,
    bool& haveOccurrenceInFile,
    QTreeWidgetItem*& lastLineItem,
    std::vector<int>& occurrenceColumns,
    const QDir& startDir,
    const QString& filePath,
    QTreeWidgetItem*& lastFileItem,
    QString& lastFilePath,
    int& occurrencesInFile,
    int& numOccurrences) {
  occurrenceColumns.clear();
  int column = 0;
  while ((column = lineText.indexOf(findText, column, caseSensitivity)) != -1) {
    // Found an occurrence at (line, column)
    occurrenceColumns.push_back(column);
    
    column += findText.size();
  }
  
  if (!occurrenceColumns.empty()) {
    if (!haveOccurrenceInFile) {
      // Insert the top-level tree widget item for the file.
      if (lastFileItem) {
        SetLastFileItem(startDir, lastFileItem, lastFilePath, occurrencesInFile);
      }
      occurrencesInFile = 0;
      lastFileItem = new QTreeWidgetItem(findAndReplaceResultsTree, lastFileItem);
      lastFilePath = filePath;
      lastFileItem->setExpanded(true);
      
      filesWithOccurrencesPaths.push_back(filePath);
      
      haveOccurrenceInFile = true;
    }
    
    numOccurrences += occurrenceColumns.size();
    occurrencesInFile += occurrenceColumns.size();
    
    lastLineItem = new QTreeWidgetItem(lastFileItem, lastLineItem);
    lastLineItem->setFlags(lastLineItem->flags() | Qt::ItemNeverHasChildren);
    lastLineItem->setData(0, Qt::UserRole, QStringLiteral("file://%1:%2:%3").arg(filePath).arg(line).arg(occurrenceColumns[0] + 1));
    
    // Highlight the occurrences in the text
    if (lineText.endsWith('\n')) {
      lineText.chop(1);
    }
    
    QString markupText;
    int cursor = 0;
    for (int column : occurrenceColumns) {
      markupText += lineText.mid(cursor, column - cursor).toHtmlEscaped();
      markupText += QStringLiteral("<b style=\"background-color:#efedec;\">");
      markupText += lineText.mid(column, findText.size()).toHtmlEscaped();
      markupText += QStringLiteral("</b>");
      cursor = column + findText.size();
    }
    markupText += lineText.mid(cursor).toHtmlEscaped();
    
    LabelWithClickedSignal* lineLabel = new LabelWithClickedSignal(tr("<span style=\"color:gray;\">Line %1:</span> %2").arg(line).arg(markupText));
    connect(lineLabel, &LabelWithClickedSignal::clicked, [=]() {
      // Invoke the itemActivated() signal of the QTreeWidget for this item.
      QMetaObject::invokeMethod(
          findAndReplaceResultsTree,
          "itemActivated",
          Qt::DirectConnection,
          Q_ARG(QTreeWidgetItem*, lastLineItem),
          Q_ARG(int, 0));
    });
    findAndReplaceResultsTree->setItemWidget(lastLineItem, 0, lineLabel);
  }
}

void FindAndReplaceInFiles::ReplaceInDocument(DocumentWidget* widget, const QString& replacementText) {
  widget->ReplaceAll(findText, replacementText, caseSensitivity == Qt::CaseSensitive, false);
}

void FindAndReplaceInFiles::ReplaceInFile(const QString& filePath, const QString& replacementText, QString* errorMessages) {
  // TODO: Properly support finding strings that go beyond a single line.
  //       To do so, we must account for the possibility that the newline encoding differs between the file and the find / replacement text.
  
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return;
  }
  QString fileText = QString::fromUtf8(file.readAll());  // TODO: Allow reading other formats than UTF-8 only
  file.close();
  
  int c = fileText.size() - 1;
  while ((c = fileText.lastIndexOf(findText, c, caseSensitivity)) != -1) {
    // Replace this occurrence
    fileText.replace(c, findText.size(), replacementText);
    
    c -= 1;
    if (c < 0) {
      break;
    }
  }
  
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(fileText.toUtf8());  // TODO: Allow saving other formats than UTF-8 only
  } else {
    *errorMessages += tr("File not writable: %1\n").arg(filePath);
  }
}

#include "find_and_replace_in_files.moc"
