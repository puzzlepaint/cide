// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QDialog>
#include <QDir>

#include "cide/project.h"

class MainWindow;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QTextEdit;

class CreateClassDialog : public QDialog {
 public:
  CreateClassDialog(const QDir& parentFolder, const std::shared_ptr<Project>& project, MainWindow* mainWindow, QWidget* parent = nullptr);
  
 public slots:
  void accept() override;
  
 private slots:
  void ClassNameChanged();
  bool UpdateCMakePreview();
  
 private:
  void ApplyFileTemplateReplacements(QString* text, const QString& className, const QString& headerFilename);
  
  
  QLineEdit* nameEdit;
  QLineEdit* headerPathEdit;
  QLineEdit* sourcePathEdit;
  QCheckBox* headerOnlyCheck;
  QCheckBox* addHeaderToCMakeListsCheck;
  QCheckBox* addSourceToCMakeListsCheck;
  QComboBox* addToTargetCombo;
  QTextEdit* cmakePreview;
  QDialogButtonBox* buttonBox;
  
  QString cmakeListsPath;
  bool cmakePreviewSuccessful = false;
  
  QDir parentFolder;
  std::shared_ptr<Project> project;
  MainWindow* mainWindow;
};
