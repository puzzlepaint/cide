// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QDialog>

class MainWindow;
class QLineEdit;

class NewProjectDialog : public QDialog {
 public:
  NewProjectDialog(MainWindow* mainWindow, const QString& existingCMakeFilePath, QWidget* parent = nullptr);
  
  bool CreateProject();
  
  QString GetProjectFilePath();
  
 public slots:
  void accept() override;
  
 private:
  bool CreateNewProject();
  bool CreateProjectForExistingCMakeListsTxtFile();
  QString TryGuessProjectName();
  
  QLineEdit* nameEdit = nullptr;
  QLineEdit* folderEdit = nullptr;
  
  QString existingCMakeFilePath;
  
  MainWindow* mainWindow;
};
