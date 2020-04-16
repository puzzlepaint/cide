// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/startup_dialog.h"

#include <QBoxLayout>
#include <QFileDialog>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSvgWidget>

#include "cide/about_dialog.h"
#include "cide/main_window.h"
#include "cide/new_project_dialog.h"
#include "cide/version.h"

StartupDialog::StartupDialog(MainWindow* mainWindow, QWidget* parent)
    : QDialog(parent),
      mainWindow(mainWindow) {
  setWindowTitle(tr("CIDE - Startup"));
  setWindowIcon(QIcon(":/cide/cide.png"));
  
  QVBoxLayout* iconAndVersionLayout = new QVBoxLayout();
  
  QSvgWidget* iconDisplay = new QSvgWidget(":/cide/cide.svg");
  QSize iconSize = QSize(150, 150);
  iconDisplay->setMinimumSize(iconSize);
  iconDisplay->setMaximumSize(iconSize);
  QLabel* versionLabel = new QLabel(tr("CIDE version %1").arg(GetCIDEVersion()));
  
  iconAndVersionLayout->addStretch(1);
  iconAndVersionLayout->addWidget(iconDisplay, 0, Qt::AlignHCenter);
  iconAndVersionLayout->addStretch(1);
  iconAndVersionLayout->addWidget(versionLabel);
  
  QVBoxLayout* recentProjectsLayout = new QVBoxLayout();
  
  QLabel* recentProjectsLabel = new QLabel(tr("Recent projects:"));
  recentProjectsLayout->addWidget(recentProjectsLabel);
  
  recentProjectList = new QListWidget();
  recentProjectsLayout->addWidget(recentProjectList);
  
  clearRecentProjectsButton = new QPushButton(tr("Clear list"));
  recentProjectsLayout->addWidget(clearRecentProjectsButton);
  
  QVBoxLayout* buttonsLayout = new QVBoxLayout();
  
  QPushButton* newProjectButton = new QPushButton(tr("New project"));
  buttonsLayout->addWidget(newProjectButton);
  
  QPushButton* openProjectButton = new QPushButton(tr("Open project / CMakeLists.txt"));
  buttonsLayout->addWidget(openProjectButton);
  
  QPushButton* proceedWithoutProjectButton = new QPushButton(tr("Proceed without project"));
  buttonsLayout->addWidget(proceedWithoutProjectButton);
  
  buttonsLayout->addStretch(1);
  
  QPushButton* aboutButton = new QPushButton(tr("About"));
  buttonsLayout->addWidget(aboutButton);
  
  QPushButton* exitButton = new QPushButton(tr("Exit"));
  buttonsLayout->addWidget(exitButton);
  
  QHBoxLayout* dialogLayout = new QHBoxLayout();
  dialogLayout->addLayout(iconAndVersionLayout);
  dialogLayout->addLayout(recentProjectsLayout);
  dialogLayout->addLayout(buttonsLayout);
  setLayout(dialogLayout);
  
  
  // Read the list of recent projects.
  // Filter out items for which the referenced files do not exist anymore.
  QSettings settings;
  std::vector<QString> recentProjects;
  int numRecentProjects = settings.beginReadArray("recentProjects");
  recentProjects.reserve(numRecentProjects);
  for (int i = 0; i < numRecentProjects; ++ i) {
    settings.setArrayIndex(i);
    QString recentProjectPath = settings.value("path").toString();
    if (!QFileInfo(recentProjectPath).exists()) {
      continue;
    }
    
    recentProjects.push_back(recentProjectPath);
    
    QListWidgetItem* newItem = new QListWidgetItem(recentProjectPath);
    recentProjectList->insertItem(0, newItem);
  }
  settings.endArray();
  settings.beginWriteArray("recentProjects");
  settings.remove("");  // remove previous entries in this group
  for (int i = 0; i < recentProjects.size(); ++ i) {
    settings.setArrayIndex(i);
    settings.setValue("path", recentProjects[i]);
  }
  settings.endArray();
  
  clearRecentProjectsButton->setEnabled(recentProjectList->count() > 0);
  
  newProjectButton->setFocus();
  
  // Ensure that the dialog has a reasonable starting size
  resize(std::max(640, width()), height());
  
  
  // --- Connections ---
  connect(newProjectButton, &QPushButton::clicked, this, &StartupDialog::NewProject);
  connect(openProjectButton, &QPushButton::clicked, this, &StartupDialog::OpenProject);
  connect(proceedWithoutProjectButton, &QPushButton::clicked, this, &QDialog::accept);
  connect(aboutButton, &QPushButton::clicked, this, &StartupDialog::About);
  connect(exitButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(recentProjectList, &QListWidget::itemClicked, [&](QListWidgetItem* item) {
    if (this->mainWindow->LoadProject(item->text(), this)) {
      accept();
    }
  });
  connect(clearRecentProjectsButton, &QPushButton::clicked, [&]() {
    recentProjectList->clear();
    clearRecentProjectsButton->setEnabled(false);
    
    QSettings settings;
    settings.beginWriteArray("recentProjects");
    settings.remove("");
    settings.endArray();
  });
}

void StartupDialog::NewProject() {
  if (mainWindow->NewProject(this)) {
    accept();
  }
}

void StartupDialog::OpenProject() {
  if (mainWindow->OpenProject(this)) {
    accept();
  }
}

void StartupDialog::About() {
  AboutDialog dialog(this);
  dialog.exec();
}
