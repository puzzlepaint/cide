// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "new_project_dialog.h"

#include <QBoxLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

#include "cide/util.h"

NewProjectDialog::NewProjectDialog(const QString& existingCMakeFilePath, QWidget* parent)
    : QDialog(parent),
      existingCMakeFilePath(existingCMakeFilePath) {
  setWindowTitle(tr("New project"));
  setWindowIcon(QIcon(":/cide/cide.png"));
  
  QLabel* nameLabel = new QLabel(tr("Project name (must be a valid filename): "));
  nameEdit = new QLineEdit();
  QHBoxLayout* nameLayout = new QHBoxLayout();
  nameLayout->addWidget(nameLabel);
  nameLayout->addWidget(nameEdit);
  
  QLabel* folderLabel = new QLabel(existingCMakeFilePath.isEmpty() ? tr("Project folder: ") : tr("Build folder: "));
  folderEdit = new QLineEdit();
  if (!existingCMakeFilePath.isEmpty()) {
    QString buildDir;
    QDir cmakeFileDir = QFileInfo(existingCMakeFilePath).dir();
    for (const QString& existingDir : cmakeFileDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      if (existingDir.startsWith("build", Qt::CaseInsensitive)) {
        buildDir = cmakeFileDir.filePath(existingDir);
        break;
      }
    }
    if (buildDir.isEmpty()) {
      buildDir = cmakeFileDir.filePath("build");
    }
    folderEdit->setText(buildDir);
  }
  QPushButton* folderButton = new QPushButton(tr("..."));
  MinimizeButtonSize(folderButton, 1.5f);
  QHBoxLayout* folderLayout = new QHBoxLayout();
  folderLayout->addWidget(folderLabel);
  folderLayout->addWidget(folderEdit);
  folderLayout->addWidget(folderButton);
  
  QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &NewProjectDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &NewProjectDialog::reject);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addLayout(nameLayout);
  layout->addLayout(folderLayout);
  layout->addWidget(buttonBox);
  setLayout(layout);
  
  
  // --- Connections ---
  connect(folderButton, &QPushButton::clicked, [&]() {
    QString path = QFileDialog::getExistingDirectory(
        this,
        this->existingCMakeFilePath.isEmpty() ? tr("Choose project directory") : tr("Choose build directory"),
        QFileInfo(folderEdit->text()).dir().path(),
        QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty()) {
      folderEdit->setText(path);
    }
  });
}

bool NewProjectDialog::CreateProject() {
  if (existingCMakeFilePath.isEmpty()) {
    return CreateNewProject();
  } else {
    return CreateProjectForExistingCMakeListsTxtFile();
  }
}

QString NewProjectDialog::GetProjectFilePath() {
  QString projectName = nameEdit->text();
  
  if (existingCMakeFilePath.isEmpty()) {
    QDir dir(folderEdit->text());
    return dir.filePath(projectName + ".cide");
  } else {
    QDir cmakeFileDir = QFileInfo(existingCMakeFilePath).dir();
    return cmakeFileDir.filePath(projectName + ".cide");
  }
}

void NewProjectDialog::accept() {
  if (nameEdit->text().isEmpty()) {
    QMessageBox::warning(this, tr("New project"), tr("Please enter a name for the project."));
    return;
  }
  
  QDialog::accept();
}

bool NewProjectDialog::CreateNewProject() {
  // Create project directory
  QDir dir(folderEdit->text());
  if (!dir.mkpath(".")) {
    QMessageBox::warning(this, tr("New project"), tr("Failed to create project directory (%1).").arg(dir.path()));
    return false;
  }
  
  QString projectName = nameEdit->text();
  QString binaryName = projectName;
  QString srcSubfolderName = projectName;
  
  // Create project files
  // <project_name>.cide
  QFile projectFile(dir.filePath(projectName + ".cide"));
  if (!projectFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, tr("New project"), tr("Failed to create project file (%1).").arg(projectFile.fileName()));
    return false;
  }
  projectFile.write(QString(
      "name: %1\n"
      "projectCMakeDir: build\n"
      "buildDir: build\n"
      "buildTarget: %2\n"
      "runDir: build\n"
      "runCmd: ./%2\n").arg(projectName).arg(binaryName).toUtf8());
  projectFile.close();
  
  // CMakeLists.txt
  QString cmakeListsPath = dir.filePath("CMakeLists.txt");
  QFile cmakeListsFile(cmakeListsPath);
  if (!cmakeListsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, tr("New project"), tr("Failed to create CMakeLists.txt file (%1).").arg(cmakeListsFile.fileName()));
    return false;
  }
  cmakeListsFile.write(QString(
      "cmake_minimum_required(VERSION 3.0)\n"
      "\n"
      "project(%1)\n"
      "\n"
      "# To set a C++ standard:\n"
      "# set(CMAKE_CXX_STANDARD 11)\n"
      "\n"
      "add_executable(%2\n"
      "  src/%3/main.cc\n"
      ")\n"
      "target_compile_options(%2 PUBLIC\n"
      "  \"$<$<COMPILE_LANGUAGE:CXX>:-Wall>\"\n"
      "  \";$<$<COMPILE_LANGUAGE:CXX>:-Wextra>\"\n"
      "  \";$<$<COMPILE_LANGUAGE:CXX>:-O2>\"\n"
      "  \";$<$<COMPILE_LANGUAGE:CXX>:-msse2>\"\n"
      "  \";$<$<COMPILE_LANGUAGE:CXX>:-msse3>\"\n"
      ")\n").arg(projectName).arg(binaryName).arg(srcSubfolderName).toUtf8());
  cmakeListsFile.close();
  
  // src/<project_name> directory
  QDir srcDir = dir;
  srcDir.mkdir("src");
  srcDir.cd("src");
  srcDir.mkdir(srcSubfolderName);
  srcDir.cd(srcSubfolderName);
  
  // src/<project_name>/main.cc
  QString mainPath = srcDir.filePath("main.cc");
  QFile mainFile(mainPath);
  if (!mainFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, tr("New project"), tr("Failed to create main file (%1).").arg(mainFile.fileName()));
    return false;
  }
  mainFile.write(QString(
      "int main(int argc, char** argv) {\n"
      "  \n"
      "}\n").toUtf8());
  mainFile.close();
  
  // build directory
  dir.mkdir("build");
  
  return true;
}

bool NewProjectDialog::CreateProjectForExistingCMakeListsTxtFile() {
  QString projectName = nameEdit->text();
  QString binaryName = projectName;  // TODO: Try to guess from an add_executable() in the CMakeLists.txt file?
  QDir cmakeFileDir = QFileInfo(existingCMakeFilePath).dir();
  
  // Create build directory if it does not exist yet
  QString buildDirPath = folderEdit->text();
  QDir buildDir(buildDirPath);
  buildDir.mkpath(".");
  
  // Create <project_name>.cide
  QFile projectFile(cmakeFileDir.filePath(projectName + ".cide"));
  if (!projectFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, tr("New project"), tr("Failed to create project file (%1).").arg(projectFile.fileName()));
    return false;
  }
  projectFile.write(QString(
      "name: %1\n"
      "projectCMakeDir: %3\n"
      "buildDir: %3\n"
      "runDir: %3\n"
      "runCmd: ./%2\n").arg(projectName).arg(binaryName).arg(cmakeFileDir.relativeFilePath(buildDirPath)).toUtf8());
  projectFile.close();
  
  return true;
}
