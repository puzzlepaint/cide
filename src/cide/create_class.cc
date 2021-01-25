// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/create_class.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTextEdit>

#include "cide/cpp_utils.h"
#include "cide/main_window.h"
#include "cide/settings.h"

CreateClassDialog::CreateClassDialog(const QDir& parentFolder, const std::shared_ptr<Project>& project, MainWindow* mainWindow, QWidget* parent)
    : QDialog(parent),
      parentFolder(parentFolder),
      project(project),
      mainWindow(mainWindow) {
  setWindowTitle(tr("Create class"));
  setWindowIcon(QIcon(":/cide/cide.png"));
  
  QLabel* nameLabel = new QLabel(tr("Name: "));
  nameEdit = new QLineEdit();
  
  // TODO: Add options to create a default constructor / destructor
  // TODO: Add an option to create a singleton class
  headerOnlyCheck = new QCheckBox(tr("Create header-only class"));
  addHeaderToCMakeListsCheck = new QCheckBox(tr("Add header to CMakeLists.txt file (heuristically)"));
  addSourceToCMakeListsCheck = new QCheckBox(tr("Add source to CMakeLists.txt file (heuristically)"));
  addSourceToCMakeListsCheck->setChecked(true);
  
  QLabel* addToTargetLabel = new QLabel(tr("Add to target: "));
  addToTargetCombo = new QComboBox();
  // TODO: Select the current target by default instead of the target with the most source files?
  int largestTargetSize = 0;
  for (int i = 0; i < project->GetNumTargets(); ++ i) {
    const auto& target = project->GetTarget(i);
    if (!target.name.isEmpty()) {
      addToTargetCombo->addItem(target.name, i);
      if (target.sources.size() > largestTargetSize) {
        largestTargetSize = target.sources.size();
        addToTargetCombo->setCurrentIndex(addToTargetCombo->count() - 1);
      }
    }
  }
  
  QLabel* headerPathLabel = new QLabel(tr("Header path: "));
  headerPathEdit = new QLineEdit();
  
  QLabel* sourcePathLabel = new QLabel(tr("Source path: "));
  sourcePathEdit = new QLineEdit();
  
  QLabel* cmakePreviewLabel = new QLabel(tr("Preview of CMakeLists.txt change (edit to correct if necessary):"));
  cmakePreview = new QTextEdit();
  cmakePreview->setFont(Settings::Instance().GetDefaultFont());
  
  buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  
  // --- Layout ---
  QVBoxLayout* layout = new QVBoxLayout();
  
  QHBoxLayout* nameLayout = new QHBoxLayout();
  nameLayout->addWidget(nameLabel);
  nameLayout->addWidget(nameEdit);
  
  QHBoxLayout* addToTargetLayout = new QHBoxLayout();
  addToTargetLayout->addWidget(addToTargetLabel);
  addToTargetLayout->addWidget(addToTargetCombo);
  
  QGridLayout* pathsLayout = new QGridLayout();
  pathsLayout->addWidget(headerPathLabel, 0, 0);
  pathsLayout->addWidget(headerPathEdit, 0, 1);
  pathsLayout->addWidget(sourcePathLabel, 1, 0);
  pathsLayout->addWidget(sourcePathEdit, 1, 1);
  
  layout->addLayout(nameLayout);
  layout->addWidget(headerOnlyCheck);
  layout->addWidget(addHeaderToCMakeListsCheck);
  layout->addWidget(addSourceToCMakeListsCheck);
  layout->addLayout(addToTargetLayout);
  layout->addLayout(pathsLayout);
  layout->addWidget(cmakePreviewLabel);
  layout->addWidget(cmakePreview);
  layout->addWidget(buttonBox);
  setLayout(layout);
  
  nameEdit->setFocus();
  
  // --- Connections ---
  connect(nameEdit, &QLineEdit::textChanged, this, &CreateClassDialog::ClassNameChanged);
  connect(headerOnlyCheck, &QCheckBox::stateChanged, [&](int state) {
    sourcePathEdit->setEnabled(state == Qt::Unchecked);
    addSourceToCMakeListsCheck->setEnabled(state == Qt::Unchecked);
    UpdateCMakePreview();
  });
  connect(addHeaderToCMakeListsCheck, &QCheckBox::stateChanged, this, &CreateClassDialog::UpdateCMakePreview);
  connect(addSourceToCMakeListsCheck, &QCheckBox::stateChanged, this, &CreateClassDialog::UpdateCMakePreview);
  connect(headerPathEdit, &QLineEdit::textChanged, this, &CreateClassDialog::UpdateCMakePreview);
  connect(sourcePathEdit, &QLineEdit::textChanged, this, &CreateClassDialog::UpdateCMakePreview);
  connect(addToTargetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CreateClassDialog::UpdateCMakePreview);
}

void CreateClassDialog::accept() {
  QString className = nameEdit->text();
  QString headerPath = headerPathEdit->text();
  QString headerFilename = QFileInfo(headerPath).fileName();
  
  // Check for whether the files exist already and ask about overwriting in this case.
  if (QFileInfo(headerPath).exists()) {
    if (QMessageBox::question(this, tr("Overwrite existing file?"), tr("The header file %1 exists already. Overwrite it?").arg(headerPath)) == QMessageBox::No) {
      return;
    }
  }
  if (!headerOnlyCheck->isChecked()) {
    QString sourcePath = sourcePathEdit->text();
    if (QFileInfo(sourcePath).exists()) {
      if (QMessageBox::question(this, tr("Overwrite existing file?"), tr("The source file %1 exists already. Overwrite it?").arg(sourcePath)) == QMessageBox::No) {
        return;
      }
    }
  }
  
  // Create the source file
  if (!headerOnlyCheck->isChecked()) {
    QString sourcePath = sourcePathEdit->text();
    QString sourceFilename = QFileInfo(sourcePath).fileName();
    QFile source(sourcePath);
    if (!source.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      QMessageBox::warning(this, tr("Error"), tr("Could not create source file: %1.").arg(sourcePath));
      return;
    }
    QString sourceText = project->GetFileTemplate(static_cast<int>(Project::FileTemplate::SourceFile));
    ApplyFileTemplateReplacements(&sourceText, className, headerFilename);
    if (mainWindow->GetDefaultNewlineFormat() == NewlineFormat::CrLf) {
      sourceText.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    }
    source.write(sourceText.toUtf8());
    source.close();
    mainWindow->Open(sourcePath);
  }
  
  // Create the header file
  QFile header(headerPath);
  if (!header.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, tr("Error"), tr("Could not create header file: %1.").arg(headerPath));
    return;
  }
  QString headerText = project->GetFileTemplate(static_cast<int>(Project::FileTemplate::HeaderFile));
  ApplyFileTemplateReplacements(&headerText, className, headerFilename);
  if (mainWindow->GetDefaultNewlineFormat() == NewlineFormat::CrLf) {
    headerText.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
  }
  header.write(headerText.toUtf8());
  header.close();
  mainWindow->Open(headerPath);
  
  // Update the CMake file if enabled
  // NOTE: Do not call UpdateCMakePreview() here, as it would override changes made by the user
  if ((addHeaderToCMakeListsCheck->isChecked() ||
      (!headerOnlyCheck->isChecked() && addSourceToCMakeListsCheck->isChecked())) &&
      cmakePreviewSuccessful) {
    Document* document;
    DocumentWidget* widget;
    if (mainWindow->GetDocumentAndWidgetForPath(cmakeListsPath, &document, &widget)) {
      widget->SelectAll();
      widget->InsertText(cmakePreview->toPlainText());
      
      // Here, we do not reconfigure the project, since the user would need to save this file first.
    } else {
      QFile cmakeFile(cmakeListsPath);
      if (!cmakeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Error"), tr("Could not open CMakeLists.txt file: %1.").arg(cmakeListsPath));
        return;
      }
      cmakeFile.write(cmakePreview->toPlainText().toUtf8());
      cmakeFile.close();
      
      // Automatically reconfigure the project after changing CMakeLists.txt on disk
      mainWindow->ReconfigureProject(project, this);
    }
  }
  
  QDialog::accept();
}

void CreateClassDialog::ClassNameChanged() {
  QString className = nameEdit->text();
  buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!className.isEmpty());
  if (className.isEmpty()) {
    return;
  }
  
  // Split the class name into "words" heuristically
  QStringList words;
  QString currentWord = className.at(0);
  for (int i = 1; i < className.size(); ++ i) {
    // Start a new word?
    if (className[i].isLetter() && className[i].isUpper() &&
        className[i - 1].isLetter() && className[i - 1].isLower()) {
      words.push_back(currentWord);
      currentWord = "";
    }
    
    currentWord += className[i];
  }
  if (!currentWord.isEmpty()) {
    words.push_back(currentWord);
  }
  
  // Build the filename with the words using the selected filename style
  QString filename;
  if (project->GetFilenameStyle() == Project::FilenameStyle::CamelCase) {
    for (const QString& word : words) {
      filename += word[0].toUpper() + word.mid(1).toLower();
    }
  } else if (project->GetFilenameStyle() == Project::FilenameStyle::LowercaseWithUnderscores) {
    for (const QString& word : words) {
      if (!filename.isEmpty()) {
        filename += QStringLiteral("_");
      }
      filename += word.toLower();
    }
  } else if (project->GetFilenameStyle() == Project::FilenameStyle::NotConfigured) {
    filename = className;
  } else {
    qDebug() << "Error: Unhandled filename style: " << static_cast<int>(project->GetFilenameStyle());
  }
  
  // Update the filename in the UI
  QString headerFileExtension;
  if (!project->GetHeaderFileExtension().isEmpty()) {
    headerFileExtension = project->GetHeaderFileExtension();
  } else {
    headerFileExtension = QStringLiteral("h");  // set default value
  }
  
  QString sourceFileExtension;
  if (!project->GetSourceFileExtension().isEmpty()) {
    sourceFileExtension = project->GetSourceFileExtension();
  } else {
    sourceFileExtension = QStringLiteral("cpp");  // set default value
  }
  
  headerPathEdit->setText(parentFolder.filePath(filename + "." + headerFileExtension));
  sourcePathEdit->setText(parentFolder.filePath(filename + "." + sourceFileExtension));
  
  // Update the CMake preview
  UpdateCMakePreview();
}

void GetPathPrefixAndIndentation(int existingFilePos, const QString& fileText, QString* pathPrefix, QString* newlineAndIndentation) {
  for (int i = existingFilePos - 1; i >= 0; -- i) {
    if (IsWhitespace(fileText[i])) {
      *pathPrefix = fileText.mid(i + 1, existingFilePos - i - 1);
      
      // We need to cut off everything after the last slash. This is because
      // we might have been looking for "settings.cc", but actually found
      // "project_settings.cc". So to get the path prefix, we have to cut
      // off the "project_".
      // TODO: This will not work in case the files are listed without a path prefix.
      //       We could thus improve the heuristics even more by forcing a suitable
      //       character before the found filename, e.g., whitespace or a slash,
      //       to handle this case as well.
      int lastSlashPos = pathPrefix->lastIndexOf('/');
      if (lastSlashPos >= 0) {
        pathPrefix->chop(pathPrefix->size() - (lastSlashPos + 1));
      }
      break;
    }
  }
  
  int newlinePos = fileText.lastIndexOf('\n', existingFilePos);
  if (newlinePos >= 0) {
    while (IsWhitespace(fileText[newlinePos])) {
      *newlineAndIndentation += fileText[newlinePos];
      ++ newlinePos;
      if (newlinePos >= existingFilePos) {
        break;
      }
    }
  }
}

bool CreateClassDialog::UpdateCMakePreview() {
  addToTargetCombo->setEnabled(
      addHeaderToCMakeListsCheck->isChecked() ||
      (!headerOnlyCheck->isChecked() && addSourceToCMakeListsCheck->isChecked()));
  
  auto disablePreview = [&](const QString& errorReason) {
    cmakePreviewSuccessful = false;
    cmakePreview->setEnabled(false);
    cmakePreview->setPlainText(errorReason);
  };
  
  if (!addHeaderToCMakeListsCheck->isChecked() &&
      (headerOnlyCheck->isChecked() || !addSourceToCMakeListsCheck->isChecked())) {
    disablePreview("");
    return false;
  }
  
  // Starting from the source path, search for the first directory
  // that contains a CMakeLists.txt file.
  QDir cmakelistsDir = QFileInfo(headerOnlyCheck->isChecked() ? headerPathEdit->text() : sourcePathEdit->text()).dir();
  while (true) {
    QString cmakePath = cmakelistsDir.filePath("CMakeLists.txt");
    if (QFile::exists(cmakePath)) {
      break;
    }
    
    // If we are at the project root or cannot go further up, abort.
    if (QFileInfo(cmakelistsDir.path()).canonicalFilePath() == project->GetDir() ||
        !cmakelistsDir.cdUp()) {
      disablePreview(tr("(could not find CMakeLists.txt)"));
      return false;
    }
  }
  
  // // Search for other source files in the source file directory, get the one with the longest filename
  // QDir fileDir = QFileInfo(headerOnlyCheck->isChecked() ? headerPathEdit->text() : sourcePathEdit->text()).dir();
  // QString longestCFilename = "";
  // for (const QString& filename : fileDir.entryList(QDir::NoDotAndDotDot | QDir::Files)) {
  //   if (GuessIsCFile(fileDir.filePath(filename))) {
  //     if (filename.size() > longestCFilename.size()) {
  //       longestCFilename = filename;
  //     }
  //   }
  // }
  
  // Read the CMakeLists.txt file.
  cmakeListsPath = QFileInfo(cmakelistsDir.filePath("CMakeLists.txt")).canonicalFilePath();
  QFile cmakeListsFile(cmakeListsPath);
  if (!cmakeListsFile.open(QIODevice::ReadOnly)) {
    disablePreview(tr("(could not open %1)").arg(cmakeListsPath));
    return false;
  }
  QString cmakeText = QString::fromUtf8(cmakeListsFile.readAll());
  cmakeListsFile.close();
  
  // Heuristically search for the correct insertion point in the CMakeLists.txt file:
  // First, check where to insert the files, assuming alphabetical ordering.
  int targetIndex = addToTargetCombo->currentData().toInt();
  const Target& target = project->GetTarget(targetIndex);
  
  std::vector<std::pair<std::string, bool>> orderedSources;  // <filename, isNew>
  for (const SourceFile& existingSource : target.sources) {
    orderedSources.emplace_back(QFileInfo(existingSource.path).fileName().toStdString(), false);
  }
  if (addHeaderToCMakeListsCheck->isChecked()) {
    orderedSources.emplace_back(QFileInfo(headerPathEdit->text()).fileName().toStdString(), true);
  }
  if (!headerOnlyCheck->isChecked() && addSourceToCMakeListsCheck->isChecked()) {
    orderedSources.emplace_back(QFileInfo(sourcePathEdit->text()).fileName().toStdString(), true);
  }
  std::sort(orderedSources.begin(), orderedSources.end(), [&](const std::pair<std::string, bool>& a, const std::pair<std::string, bool>& b) {
    return a.first < b.first;
  });
  
  // Find the "surrounding" existing filenames in the CMakeLists.txt file to insert the new files next to them.
  // Use the same path prefix (parent directories) as for the existing files.
  std::vector<DocumentRange> insertedRanges;
  for (int i = 0; i < orderedSources.size(); ++ i) {
    const std::pair<std::string, bool>& item = orderedSources[i];
    if (!item.second) {
      // For existing files, do nothing.
      continue;
    }
    
    // Try to find the previous existing source to insert the new one afterwards.
    if (i > 0) {
      const std::string& existingFilename = orderedSources[i - 1].first;
      int existingFilePos = cmakeText.indexOf(QString::fromStdString(existingFilename));
      if (existingFilePos >= 0) {
        QString pathPrefix;
        QString newlineAndIndentation;
        GetPathPrefixAndIndentation(existingFilePos, cmakeText, &pathPrefix, &newlineAndIndentation);
        
        QString insertion = newlineAndIndentation + pathPrefix + QString::fromStdString(item.first);
        
        int insertionPos = existingFilePos + existingFilename.size();
        cmakeText.insert(insertionPos, insertion);
        for (DocumentRange& range : insertedRanges) {
          if (range.start >= insertionPos) {
            range.start += insertion.size();
            range.end += insertion.size();
          }
        }
        insertedRanges.push_back(DocumentRange(
            insertionPos,
            insertionPos + insertion.size()));
        
        continue;
      }
    }
    
    // If that did not work, try to find the next existing source to insert the new one before,
    // and if that fails as well, try all others.
    bool inserted = false;
    for (int attempt = 0; attempt < orderedSources.size() + 1; ++ attempt) {
      int existingIndex;
      if (attempt == 0) {
        existingIndex = i + 1;
      } else {
        existingIndex = attempt - 1;
        if (orderedSources[existingIndex].second) {
          continue;
        }
      }
      
      if (existingIndex >= 0 && existingIndex < orderedSources.size()) {
        const std::string& existingFilename = orderedSources[existingIndex].first;
        int existingFilePos = cmakeText.indexOf(QString::fromStdString(existingFilename));
        if (existingFilePos >= 0) {
          QString pathPrefix;
          QString newlineAndIndentation;
          GetPathPrefixAndIndentation(existingFilePos, cmakeText, &pathPrefix, &newlineAndIndentation);
          
          QString insertion = newlineAndIndentation + pathPrefix + QString::fromStdString(item.first);
          
          int newlinePos = cmakeText.lastIndexOf('\n', existingFilePos);
          if (newlinePos < 0) {
            newlinePos = 0;
          }
          if (cmakeText[newlinePos] == '\r') {
            newlinePos = std::max(0, newlinePos - 1);
          }
          cmakeText.insert(newlinePos, insertion);
          for (DocumentRange& range : insertedRanges) {
            if (range.start >= newlinePos) {
              range.start += insertion.size();
              range.end += insertion.size();
            }
          }
          insertedRanges.push_back(DocumentRange(
              newlinePos,
              newlinePos + insertion.size()));
          
          inserted = true;
          break;
        }
      }
    }
    if (inserted) {
      continue;
    }
    
    // If that also failed, give up.
    disablePreview(tr("(could not find the correct place to insert new files for the selected target in %1)").arg(cmakeListsPath));
    return false;
  }
  
  cmakePreview->setPlainText(cmakeText);
  QTextCursor cursor = cmakePreview->textCursor();
  int minOffset = std::numeric_limits<int>::max();
  int maxOffset = 0;
  for (const DocumentRange& range : insertedRanges) {
    minOffset = std::min(minOffset, range.start.offset);
    maxOffset = std::max(maxOffset, range.end.offset);
    
    // NOTE: Attempt to color the inserted ranges; this colored all ranges instead.
    // cursor.setPosition(range.start.offset, QTextCursor::MoveAnchor);
    // cursor.setPosition(range.end.offset, QTextCursor::KeepAnchor);
    // QTextCharFormat format = cursor.charFormat();
    // format.setForeground(QBrush(qRgb(255, 80, 80)));
    // cursor.setCharFormat(format);
  }
  
  cursor.setPosition(minOffset, QTextCursor::MoveAnchor);
  cursor.setPosition(maxOffset, QTextCursor::KeepAnchor);
  cmakePreview->setTextCursor(cursor);
  cmakePreview->ensureCursorVisible();
  
  cmakePreview->setEnabled(true);
  cmakePreviewSuccessful = true;
  return true;
}

void CreateClassDialog::ApplyFileTemplateReplacements(QString* text, const QString& className, const QString& headerFilename) {
  text->replace(
      "${LicenseHeader}",
      project->GetFileTemplate(static_cast<int>(Project::FileTemplate::LicenseHeader)));
  
  text->replace(
      "${ClassName}",
      className);
  
  text->replace(
      "${HeaderFilename}",
      headerFilename);
}
