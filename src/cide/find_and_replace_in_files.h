// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QObject>

class Document;
class DocumentWidget;
class MainWindow;
class QAction;
class QDir;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class FindAndReplaceInFiles : public QObject {
 Q_OBJECT
 public:
  /// Returns the QAction which shows the find-and-replace window.
  QAction* Initialize(MainWindow* mainWindow);
  
 public slots:
  void CreateDockWidget();
  
  void ShowDialog(const QString& initialPath = "");
  
  void ReplaceClicked();
  
 private slots:
  void ShowDialogWithDefaultSettings();
  
 private:
  /// Shows the search dialog that allows entering the text to serach for, set
  /// the search directory, etc. Returns true if the dialog was accepted. The
  /// entered information is put into the corresponding attributes of this
  /// class (findText, searchFolderPath).
  bool ShowDialogInternal(const QString initialPath);
  
  void SetLastFileItem(const QDir& startDir, QTreeWidgetItem* lastFileItem, QString lastFilePath, int occurrencesInFile);
  
  void SearchInDocument(Document* document, const QDir& startDir, const QString& filePath, QTreeWidgetItem*& lastFileItem, QString& lastFilePath, int& occurrencesInFile, int& numOccurrences);
  void SearchInFile(const QDir& startDir, const QString& filePath, QTreeWidgetItem*& lastFileItem, QString& lastFilePath, int& occurrencesInFile, int& numOccurrences);
  
  // TODO: Support finding strings that go beyond a single line. So, make a SearchInDocumentText() instead.
  void SearchInLine(
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
      int& numOccurrences);
  
  void ReplaceInDocument(DocumentWidget* widget, const QString& replacementText);
  void ReplaceInFile(const QString& filePath, const QString& replacementText, QString* errorMessages);
  
  
  QDockWidget* findAndReplaceInFilesDock = nullptr;
  QLabel* findAndReplaceResultsLabel;
  QLineEdit* findAndReplaceEdit;
  QPushButton* findAndReplaceReplaceButton;
  QTreeWidget* findAndReplaceResultsTree;
  
  /// Text that should be searched for.
  QString findText;
  
  /// Case sensitivity mode for the search.
  Qt::CaseSensitivity caseSensitivity;
  
  /// Path of the root folder for the search.
  QString searchFolderPath;
  
  /// Paths of all files in which the search / replace should be done.
  std::vector<QString> filePaths;
  
  /// Paths of all files in which occurrences were found.
  std::vector<QString> filesWithOccurrencesPaths;
  
  MainWindow* mainWindow;
};
