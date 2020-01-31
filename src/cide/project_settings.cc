// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/project_settings.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedLayout>
#include <QPlainTextEdit>

#include "cide/main_window.h"

ProjectSettingsDialog::ProjectSettingsDialog(std::shared_ptr<Project> project, MainWindow* mainWindow, QWidget* parent)
    : QDialog(parent),
      project(project),
      mainWindow(mainWindow) {
  setWindowTitle(tr("Project settings for: %1").arg(project->GetName()));
  setWindowIcon(QIcon(":/cide/cide.png"));
  
  QHBoxLayout* layout = new QHBoxLayout();
  
  
  // Category list
  QListWidget* categoryList = new QListWidget();
  
  QListWidgetItem* newItem = new QListWidgetItem(tr("General"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::General));
  categoryList->addItem(newItem);
  
  newItem = new QListWidgetItem(tr("Editing"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::Editing));
  categoryList->addItem(newItem);
  
  newItem = new QListWidgetItem(tr("Code parsing"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::CodeParsing));
  categoryList->addItem(newItem);
  
  newItem = new QListWidgetItem(tr("File templates"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::FileTemplates));
  categoryList->addItem(newItem);
  
  layout->addWidget(categoryList);
  
  
  // Settings stack
  categoriesLayout = new QStackedLayout();
  categoriesLayout->addWidget(CreateGeneralCategory());
  categoriesLayout->addWidget(CreateEditingCategory());
  categoriesLayout->addWidget(CreateCodeParsingCategory());
  categoriesLayout->addWidget(CreateFileTemplatesCategory());
  
  layout->addLayout(categoriesLayout);
  
  
  QObject::connect(categoryList, &QListWidget::currentItemChanged, [&](QListWidgetItem* current, QListWidgetItem* /*previous*/) {
    if (!current) {
      return;
    }
    int categoryIndex = current->data(Qt::UserRole).toInt();
    categoriesLayout->setCurrentIndex(categoryIndex);
  });
  categoryList->setCurrentRow(0);
  
  QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
  QObject::connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  
  QVBoxLayout* dialogLayout = new QVBoxLayout();
  dialogLayout->addLayout(layout);
  dialogLayout->addWidget(buttonBox);
  setLayout(dialogLayout);
}

void ProjectSettingsDialog::closeEvent(QCloseEvent* /*event*/) {
  if (projectRequiresReconfiguration) {
    mainWindow->ReconfigureProject(project, this);
  }
}

QWidget* ProjectSettingsDialog::CreateGeneralCategory() {
  QLabel* projectNameLabel = new QLabel(tr("Project name: "));
  projectNameEdit = new QLineEdit(project->GetName());
  QHBoxLayout* projectNameLayout = new QHBoxLayout();
  projectNameLayout->addWidget(projectNameLabel);
  projectNameLayout->addWidget(projectNameEdit);
  
  QLabel* buildDirLabel = new QLabel(tr("Build directory: "));
  buildDirEdit = new QLineEdit(project->GetBuildDir().path());
  QPushButton* buildDirButton = new QPushButton(tr("..."));
  MinimizeButtonSize(buildDirButton, 1.5);
  QHBoxLayout* buildDirLayout = new QHBoxLayout();
  buildDirLayout->addWidget(buildDirLabel);
  buildDirLayout->addWidget(buildDirEdit);
  buildDirLayout->addWidget(buildDirButton);
  
  QLabel* buildThreadsLabel = new QLabel(tr("Number of build threads (0 meaning unspecified): "));
  QLineEdit* buildThreadsEdit = new QLineEdit(QString::number(project->GetBuildThreads()));
  buildThreadsEdit->setValidator(new QIntValidator(0, std::numeric_limits<int>::max(), buildThreadsEdit));
  QHBoxLayout* buildThreadsLayout = new QHBoxLayout();
  buildThreadsLayout->addWidget(buildThreadsLabel);
  buildThreadsLayout->addWidget(buildThreadsEdit);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addLayout(projectNameLayout);
  layout->addLayout(buildDirLayout);
  layout->addLayout(buildThreadsLayout);
  layout->addStretch(1);
  
  // --- Connections ---
  connect(projectNameEdit, &QLineEdit::textChanged, project.get(), &Project::SetName);
  connect(buildDirEdit, &QLineEdit::textChanged, [&](const QString& text) {
    project->SetBuildDir(QDir(text));
    projectRequiresReconfiguration = true;
  });
  connect(buildDirButton, &QPushButton::clicked, [&]() {
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Choose build directory"),
        buildDirEdit->text(),
        QFileDialog::DontUseNativeDialog);
    if (!dir.isEmpty()) {
      buildDirEdit->setText(dir);
    }
  });
  connect(buildThreadsEdit, &QLineEdit::textChanged, [&](const QString& text) {
    project->SetBuildThreads(text.toInt());
  });
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}

QWidget* ProjectSettingsDialog::CreateEditingCategory() {
  QCheckBox* insertSpacesOnTabCheck = new QCheckBox(tr("Insert spaces when pressing tab"));
  insertSpacesOnTabCheck->setChecked(project->GetInsertSpacesOnTab());
  
  QLabel* spacesPerTabLabel = new QLabel(tr("Spaces per tab: "));
  spacesPerTabEdit = new QLineEdit(QString::number(project->GetSpacesPerTab()));
  if (project->GetSpacesPerTab() == -1) {
    spacesPerTabEdit->setText(tr("(not configured yet)"));
  }
  // TODO: Not using the following at the moment, since we may insert "(not configured yet)" above:
  //       spacesPerTabEdit->setValidator(new QIntValidator(0, std::numeric_limits<int>::max(), spacesPerTabEdit));
  QHBoxLayout* spacesPerTabLayout = new QHBoxLayout();
  spacesPerTabLayout->addWidget(spacesPerTabLabel);
  spacesPerTabLayout->addWidget(spacesPerTabEdit);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(insertSpacesOnTabCheck);
  layout->addLayout(spacesPerTabLayout);
  layout->addStretch(1);
  
  // --- Connections ---
  connect(insertSpacesOnTabCheck, &QCheckBox::stateChanged, project.get(), &Project::SetInsertSpacesOnTab);
  connect(spacesPerTabEdit, &QLineEdit::textChanged, [&](const QString& text) {
    bool ok;
    int value = text.toInt(&ok);
    if (ok) {
      project->SetSpacesPerTab(value);
    } else {
      project->SetSpacesPerTab(-1);
    }
  });
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}

QWidget* ProjectSettingsDialog::CreateCodeParsingCategory() {
  QCheckBox* defaultCompilerCheck = new QCheckBox(tr("Use the default instead of the CMake-configured compiler for directory queries"));
  defaultCompilerCheck->setChecked(project->GetUseDefaultCompiler());
  
  QLabel* defaultCompilerExplanationLabel = new QLabel(tr(
      "It is usually best to use a version of clang to query the internal directories, such that CIDE can parse the project files properly with libclang."
      " Thus this option should usually be left enabled, given that clang is configured as the default compiler in the program settings."));
  defaultCompilerExplanationLabel->setWordWrap(true);
  
  QCheckBox* indexAllProjectFilesCheck = new QCheckBox(tr("Automatically index all project files (may take very long for large projects)"));
  indexAllProjectFilesCheck->setChecked(project->GetIndexAllProjectFiles());
  
  QLabel* indexExplanationLabel = new QLabel(tr(
      "Indexing determines the list of included files for each source file and remembers locations of declarations and definitions."
      " It thus makes the IDE know the proper parse settings for header files if those are not entered in the CMakeLists.txt."
      " It also enables the global symbol search, and associating definitions with declarations if those"
      " are not in the same translation unit. If automatic indexing is disabled, these functions will only work for files that"
      " have been opened since CIDE was started."));
  indexExplanationLabel->setWordWrap(true);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(defaultCompilerCheck);
  layout->addWidget(defaultCompilerExplanationLabel);
  layout->addWidget(indexAllProjectFilesCheck);
  layout->addWidget(indexExplanationLabel);
  layout->addStretch(1);
  
  // --- Connections ---
  connect(defaultCompilerCheck, &QCheckBox::stateChanged, [&](int state) {
    project->SetUseDefaultCompiler(state == Qt::Checked);
  });
  connect(indexAllProjectFilesCheck, &QCheckBox::stateChanged, [&](int state) {
    project->SetIndexAllProjectFiles(state == Qt::Checked);
  });
  
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}

QWidget* ProjectSettingsDialog::CreateFileTemplatesCategory() {
  templateList = new QListWidget();
  
  QLabel* descriptionLabel = new QLabel(tr(
      "File templates used in creating new files can be configured here. Available variables which will be replaced:\n"
      "${LicenseHeader} : Inserts the license header template\n"
      "${ClassName} : The class name entered in the \"New class\" dialog\n"
      "${HeaderFilename} : The filename of the header generated by the \"New class\" dialog"));
  descriptionLabel->setWordWrap(true);
  
  QListWidgetItem* item = new QListWidgetItem(tr("License header"));
  item->setData(Qt::UserRole, static_cast<int>(Project::FileTemplate::LicenseHeader));
  templateList->addItem(item);
  
  item = new QListWidgetItem(tr("Header file"));
  item->setData(Qt::UserRole, static_cast<int>(Project::FileTemplate::HeaderFile));
  templateList->addItem(item);
  
  item = new QListWidgetItem(tr("Source file"));
  item->setData(Qt::UserRole, static_cast<int>(Project::FileTemplate::SourceFile));
  templateList->addItem(item);
  
  templateEdit = new QPlainTextEdit();
  
  QLabel* filenameStyleLabel = new QLabel(tr("Filename style: "));
  filenameStyleCombo = new QComboBox();
  filenameStyleCombo->addItem(tr("CamelCase"), static_cast<int>(Project::FilenameStyle::CamelCase));
  filenameStyleCombo->addItem(tr("lowercase_with_underscores"), static_cast<int>(Project::FilenameStyle::LowercaseWithUnderscores));
  filenameStyleCombo->addItem(tr("(not configured)"), static_cast<int>(Project::FilenameStyle::NotConfigured));
  filenameStyleCombo->setCurrentIndex(static_cast<int>(project->GetFilenameStyle()));
  
  QHBoxLayout* filenameStyleLayout = new QHBoxLayout();
  filenameStyleLayout->setContentsMargins(0, 0, 0, 0);
  filenameStyleLayout->setMargin(0);
  filenameStyleLayout->addWidget(filenameStyleLabel);
  filenameStyleLayout->addWidget(filenameStyleCombo);
  
  QLabel* sourceFileExtensionLabel = new QLabel(tr("Source file extension: "));
  QLineEdit* sourceFileExtensionEdit = new QLineEdit(project->GetSourceFileExtension());
  
  QHBoxLayout* sourceFileExtensionLayout = new QHBoxLayout();
  sourceFileExtensionLayout->setContentsMargins(0, 0, 0, 0);
  sourceFileExtensionLayout->setMargin(0);
  sourceFileExtensionLayout->addWidget(sourceFileExtensionLabel);
  sourceFileExtensionLayout->addWidget(sourceFileExtensionEdit);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(templateList, 1);
  layout->addWidget(templateEdit, 2);
  layout->addSpacing(20);
  layout->addLayout(filenameStyleLayout);
  layout->addLayout(sourceFileExtensionLayout);
  
  // --- Connections ---
  saveTemplateChanges = true;
  
  auto updateEditFunc = [&]() {
    int index = templateList->currentRow();
    if (index < 0) {
      return;
    }
    int templateIndex = templateList->item(templateList->currentRow())->data(Qt::UserRole).toInt();
    
    saveTemplateChanges = false;
    templateEdit->setPlainText(project->GetFileTemplate(templateIndex));
    saveTemplateChanges = true;
  };
  connect(templateList, &QListWidget::currentRowChanged, updateEditFunc);
  updateEditFunc();
  
  connect(templateEdit, &QPlainTextEdit::textChanged, [&]() {
    if (!saveTemplateChanges) {
      return;
    }
    int index = templateList->currentRow();
    if (index < 0) {
      return;
    }
    int templateIndex = templateList->item(templateList->currentRow())->data(Qt::UserRole).toInt();
    project->SetFileTemplate(templateIndex, templateEdit->toPlainText());
  });
  
  connect(filenameStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [&](int currentIndex) {
    Project::FilenameStyle style = static_cast<Project::FilenameStyle>(filenameStyleCombo->itemData(currentIndex, Qt::UserRole).toInt());
    project->SetFilenameStyle(style);
  });
  
  connect(sourceFileExtensionEdit, &QLineEdit::textChanged, [&](const QString& text) {
    project->SetSourceFileExtension(text);
  });
  
  templateList->setCurrentRow(0);
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}
