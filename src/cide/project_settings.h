// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>

#include <QDialog>

#include "cide/project.h"

class MainWindow;
class QComboBox;
class QListWidget;
class QPlainTextEdit;
class QStackedLayout;

class ProjectSettingsDialog : public QDialog {
 Q_OBJECT
 public:
  ProjectSettingsDialog(std::shared_ptr<Project> project, MainWindow* mainWindow, QWidget* parent = nullptr);
  
 protected slots:
  void closeEvent(QCloseEvent* event) override;
  
 private:
  enum class Categories {
    General = 0,
    Editing,
    CodeParsing,
    FileTemplates
  };
  
  
  std::shared_ptr<Project> project;
  MainWindow* mainWindow;
  
  bool projectRequiresReconfiguration = false;
  
  
  QStackedLayout* categoriesLayout;
  
  // "General" category
  QWidget* CreateGeneralCategory();
  
  QLineEdit* projectNameEdit;
  QLineEdit* buildDirEdit;
  QLineEdit* spacesPerTabEdit;
  
  // "Editing" category
  QWidget* CreateEditingCategory();
  
  // "Code parsing" category
  QWidget* CreateCodeParsingCategory();
  
  // "File templates" category
  QWidget* CreateFileTemplatesCategory();
  
  QListWidget* templateList;
  QPlainTextEdit* templateEdit;
  bool saveTemplateChanges;
  QComboBox* filenameStyleCombo;
};
