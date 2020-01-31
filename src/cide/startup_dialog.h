// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QDialog>

class MainWindow;
class QListWidget;

class StartupDialog : public QDialog {
 public:
  StartupDialog(MainWindow* mainWindow, QWidget* parent = nullptr);
  
 public slots:
  void NewProject();
  void OpenProject();
  void About();
  
 private:
  MainWindow* mainWindow;
  QListWidget* recentProjectList;
  QPushButton* clearRecentProjectsButton;
};
