// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "main_window.h"

#include <QBoxLayout>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

#include "cide/about_dialog.h"
#include "cide/cpp_utils.h"
#include "cide/clang_parser.h"
#include "cide/crash_backup.h"
#include "cide/new_project_dialog.h"
#include "cide/parse_thread_pool.h"
#include "cide/project_settings.h"
#include "cide/search_bar.h"
#include "cide/settings.h"
#include "cide/util.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
  nextTabDataIndex = 0;
  
  currentFrameCanonicalPath = "";
  currentFrameLine = -1;
  currentFrameAddr = "";
  currentFrameLevel = -1;
  
  setWindowTitle("CIDE");
  setWindowIcon(QIcon(":/cide/cide.png"));
  
  setStyleSheet("QMainWindow::separator {"
      "width: 2px;"
      "height: 0px;"
      "margin: 0px;"
      "padding: 0px;"
  "}");
  
  // Toolbar
  QToolBar* toolbar = new QToolBar();
  
  QLabel* buildTargetLabel = new QLabel(tr("Build target: "));
  toolbar->addWidget(buildTargetLabel);
  buildTargetCombo = new QComboBox();
  buildTargetCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  connect(buildTargetCombo, &QComboBox::currentTextChanged, this, &MainWindow::BuildTargetChanged);
  toolbar->addWidget(buildTargetCombo);
  
  toolbar->addSeparator();
  
  QToolButton* searchBarModeButton = new QToolButton();
  searchBarModeButton->setIcon(QIcon(":/cide/magnifying-glass.png"));
  searchBarModeButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  searchBarModeButton->setPopupMode(QToolButton::InstantPopup);
  QMenu* searchBarModeMenu = new QMenu();
  searchFilesClickAction = searchBarModeMenu->addAction("", this, &MainWindow::SearchFiles);
  searchLocalContextsClickAction = searchBarModeMenu->addAction("", this, &MainWindow::SearchLocalContexts);
  globalSymbolSearchClickAction = searchBarModeMenu->addAction("", this, &MainWindow::GlobalSymbolSearch);
  UpdateSearchClickActionLabels();
  searchBarModeButton->setMenu(searchBarModeMenu);
  toolbar->addWidget(searchBarModeButton);
  
  searchBar = new SearchBar(this);
  toolbar->addWidget(searchBar);
  
  addToolBar(toolbar);
  
  // Tab bar
  // NOTE: Do not connect anything to QTabBar::currentChanged() here. This is
  //       done after adding a tab (and removed before adding a tab) such that
  //       the connection is not called while a half-constructed tab exists.
  tabBar = new TabBar(this);
  tabBar->setElideMode(Qt::ElideNone);
  tabBar->setExpanding(false);
  tabBar->setMovable(true);
  tabBar->setDocumentMode(true);
  
  documentLayout = new QStackedLayout();
  
  connect(tabBar, &TabBar::tabCloseRequested, this, QOverload<int>::of(&MainWindow::CloseDocument));
  connect(tabBar, &TabBar::CloseAllOtherTabs, this, &MainWindow::CloseAllOtherDocuments);
  connect(tabBar, &TabBar::CloseAllTabs, this, &MainWindow::CloseAllDocuments);
  
  QAction* reloadFileAction = new ActionWithConfigurableShortcut(tr("Reload file"), reloadFileShortcut, this);
  connect(reloadFileAction, &QAction::triggered, this, &MainWindow::ReloadFile);
  addAction(reloadFileAction);
  
  QAction* goToLeftTabAction = new ActionWithConfigurableShortcut(tr("Go to left tab"), goToLeftTabShortcut, this);
  connect(goToLeftTabAction, &QAction::triggered, this, &MainWindow::GoToLeftTab);
  addAction(goToLeftTabAction);
  
  QAction* goToRightTabAction = new ActionWithConfigurableShortcut(tr("Go to right tab"), goToRightTabShortcut, this);
  connect(goToRightTabAction, &QAction::triggered, this, &MainWindow::GoToRightTab);
  addAction(goToRightTabAction);
  
  QAction* switchHeaderSourceAction = new ActionWithConfigurableShortcut(tr("Switch header/source"), switchHeaderSourceShortcut, this);
  connect(switchHeaderSourceAction, &QAction::triggered, this, &MainWindow::SwitchHeaderSource);
  addAction(switchHeaderSourceAction);
  
  QAction* globalSymbolSearchAction = new ActionWithConfigurableShortcut(tr("Global symbol search"), searchGlobalSymbolsShortcut, this);
  connect(globalSymbolSearchAction, &QAction::triggered, this, &MainWindow::GlobalSymbolSearch);
  addAction(globalSymbolSearchAction);
  
  QAction* searchLocalContextsAction = new ActionWithConfigurableShortcut(tr("Search local contexts"), searchLocalContextsShortcut, this);
  connect(searchLocalContextsAction, &QAction::triggered, this, &MainWindow::SearchLocalContexts);
  addAction(searchLocalContextsAction);
  
  QAction* searchFilesAction = new ActionWithConfigurableShortcut(tr("Search files"), searchInFilesShortcut, this);
  connect(searchFilesAction, &QAction::triggered, this, &MainWindow::SearchFiles);
  addAction(searchFilesAction);
  
  QAction* buildAction = new ActionWithConfigurableShortcut(tr("Build current target"), buildCurrentTargetShortcut, this);
  connect(buildAction, &QAction::triggered, this, &MainWindow::BuildCurrentTarget);
  addAction(buildAction);
  
#ifndef WIN32
  QAction* debugAction = new ActionWithConfigurableShortcut(tr("Debug"), startDebuggingShortcut, this);
  connect(debugAction, &QAction::triggered, this, &MainWindow::DebugCurrentProject);
  addAction(debugAction);
#endif
  
  QVBoxLayout* vLayout = new QVBoxLayout();
  vLayout->setContentsMargins(0, 0, 0, 0);
  vLayout->setSpacing(0);
  vLayout->addWidget(tabBar);
  vLayout->addLayout(documentLayout);
  
  QWidget* contentWidget = new QWidget(this);
  contentWidget->setLayout(vLayout);
  setCentralWidget(contentWidget);
  
  // Find-and-replace in files
  QAction* findAndReplaceInFilesAction = findAndReplaceInFiles.Initialize(this);
  
  // Menu bar
  QMenuBar* menuBar = new QMenuBar();
  
  QMenu* fileMenu = new QMenu(tr("File"));
  
  QAction* newFileAction = new ActionWithConfigurableShortcut(tr("New"), newFileShortcut, this);
  connect(newFileAction, &QAction::triggered, this, &MainWindow::New);
  fileMenu->addAction(newFileAction);
  
  QAction* openFileAction = new ActionWithConfigurableShortcut(tr("Open"), openFileShortcut, this);
  connect(openFileAction, &QAction::triggered, this, QOverload<>::of(&MainWindow::Open));
  fileMenu->addAction(openFileAction);
  
  saveAction = new ActionWithConfigurableShortcut(tr("Save"), saveFileShortcut, this);
  connect(saveAction, &QAction::triggered, this, QOverload<>::of(&MainWindow::Save));
  fileMenu->addAction(saveAction);
  
  saveAsAction = new ActionWithConfigurableShortcut(tr("Save As..."), saveAsFileShortcut, this);
  connect(saveAsAction, &QAction::triggered, this, QOverload<>::of(&MainWindow::SaveAs));
  fileMenu->addAction(saveAsAction);
  
  fileMenu->addAction(reloadFileAction);
  
  closeAction = new ActionWithConfigurableShortcut(tr("Close"), closeFileShortcut, this);
  connect(closeAction, &QAction::triggered, this, QOverload<>::of(&MainWindow::CloseDocument));
  fileMenu->addAction(closeAction);
  
  fileMenu->addSeparator();
  
  QAction* quitAction = new ActionWithConfigurableShortcut(tr("Quit"), quitShortcut, this);
  connect(quitAction, &QAction::triggered, this, &MainWindow::close);
  fileMenu->addAction(quitAction);
  
  menuBar->addMenu(fileMenu);
  
  QMenu* editMenu = new QMenu(tr("Edit"));
  editMenu->addAction(findAndReplaceInFilesAction);
  menuBar->addMenu(editMenu);
  
  QMenu* viewMenu = new QMenu(tr("View"));
  showProjectFilesDockAction = new ActionWithConfigurableShortcut(tr("Show project files dock"), showProjectFilesDockShortcut, this);
  connect(showProjectFilesDockAction, &QAction::triggered, this, &MainWindow::ShowProjectFilesDock);
  viewMenu->addAction(showProjectFilesDockAction);
  showProjectFilesDockAction->setCheckable(true);
  showProjectFilesDockAction->setChecked(true);
  menuBar->addMenu(viewMenu);
  
  QMenu* projectMenu = new QMenu(tr("Project"));
  projectMenu->addAction(buildAction);
  projectMenu->addAction(tr("Reconfigure"), this, &MainWindow::Reconfigure);
  projectMenu->addAction(tr("Project settings..."), this, &MainWindow::ShowProjectSettings);
  projectMenu->addSeparator();
  currentFileParseSettingsAction = projectMenu->addAction(tr("Parse settings for current file..."), this, &MainWindow::ParseSettingsForCurrentFile);
  projectMenu->addSeparator();
  newProjectAction = projectMenu->addAction(tr("New project..."), [&]() { NewProject(this); });
  openProjectAction = projectMenu->addAction(tr("Open project..."), [&]() { OpenProject(this); });
  closeProjectAction = projectMenu->addAction(tr("Close project"), this, &MainWindow::CloseProject);
  menuBar->addMenu(projectMenu);
  
#ifndef WIN32
  QMenu* runMenu = new QMenu(tr("Run"));
  runMenu->addAction(debugAction);
  showRunDockAction = new ActionWithConfigurableShortcut(tr("Show run dock"), showRunDockShortcut, this);
  connect(showRunDockAction, &QAction::triggered, this, &MainWindow::ShowRunDock);
  runMenu->addAction(showRunDockAction);
  showRunDockAction->setCheckable(true);
  showRunDockAction->setChecked(false);
  menuBar->addMenu(runMenu);
#endif
  
  QMenu* toolsMenu = new QMenu(tr("Tools"));
  QAction* runGitkAction = new ActionWithConfigurableShortcut(tr("Run gitk..."), runGitkShortcut, this);
  connect(runGitkAction, &QAction::triggered, this, &MainWindow::RunGitk);
  toolsMenu->addAction(runGitkAction);
  toolsMenu->addAction(tr("Program settings..."), this, &MainWindow::ShowProgramSettings);
  menuBar->addMenu(toolsMenu);
  
  QMenu* helpMenu = new QMenu(tr("Help"));
  helpMenu->addAction(tr("About CIDE..."), this, &MainWindow::ShowAboutDialog);
  menuBar->addMenu(helpMenu);
  
  setMenuBar(menuBar);
  
  // Project tree view
  projectTreeView.Initialize(this, showProjectFilesDockAction);
  
  // Build output dock widget
  buildOutputWidget = new DockWidgetWithClosedSignal(tr("Build"));
  buildOutputWidget->setFeatures(
      QDockWidget::DockWidgetClosable |
      QDockWidget::DockWidgetMovable |
      QDockWidget::DockWidgetFloatable |
      QDockWidget::DockWidgetVerticalTitleBar);
  connect(buildOutputWidget, &DockWidgetWithClosedSignal::closed, this, &MainWindow::BuildOutputWidgetClosed);
  
  buildIssuesWidget = nullptr;
  buildIssuesRichTextScrollArea = new QScrollArea();
  buildIssuesRichTextScrollArea->setWidgetResizable(true);
  buildIssuesRichTextScrollArea->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  ClearBuildIssues();
  
  buildOutputWidget->setWidget(buildIssuesRichTextScrollArea);
  
  viewAsTextMenu = new QMenu();
  
  QAction* viewAsTextAction = viewAsTextMenu->addAction(tr("View as text"));
  connect(viewAsTextAction, &QAction::triggered, this, &MainWindow::ViewBuildOutputAsText);
  
  // Run dock widget
  runWidget = new DockWidgetWithClosedSignal(tr("Run"));
  runWidget->setFeatures(
      QDockWidget::DockWidgetClosable |
      QDockWidget::DockWidgetMovable |
      QDockWidget::DockWidgetFloatable |
      QDockWidget::DockWidgetVerticalTitleBar);
  connect(runWidget, &DockWidgetWithClosedSignal::closed, this, &MainWindow::RunWidgetClosed);
  
  threadDropdown = new QComboBox();
  connect(threadDropdown, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::ThreadChanged);
  threadDropdown->setEnabled(false);
  
//   QPlainTextEdit* outputDisplay = new QPlainTextEdit();
//   outputDisplay->setFont(Settings::Instance().GetDefaultFont());
  
  stackFramesList = new QListWidget();
  connect(stackFramesList, &QListWidget::itemActivated, this, &MainWindow::ProgramFrameActivated);
  stackFramesList->setEnabled(false);
  
  expressionEdit = new QLineEdit();
  expressionEdit->setEnabled(false);
  connect(expressionEdit, &QLineEdit::returnPressed, this, &MainWindow::EvaluateExpression);
  evaluateExpressionButton = new QPushButton(tr("Evaluate"));
  connect(evaluateExpressionButton, &QPushButton::clicked, this, &MainWindow::EvaluateExpression);
  evaluateExpressionButton->setEnabled(false);
  
  cwdButton = new QPushButton(tr("Cwd"));
  connect(cwdButton, &QPushButton::clicked, this, &MainWindow::CwdButtonClicked);
  
  callEdit = new QLineEdit();
  callEdit->setFont(Settings::Instance().GetDefaultFont());
  
  QPushButton* callEditButton = new QPushButton(tr("Edit..."));
  connect(callEditButton, &QPushButton::clicked, this, &MainWindow::EditCallClicked);
  
  runPauseButton = new QPushButton(tr("Run"));
  connect(runPauseButton, &QPushButton::clicked, this, &MainWindow::RunPauseClicked);
  
  stopButton = new QPushButton(tr("Stop"));
  connect(stopButton, &QPushButton::clicked, this, &MainWindow::StopClicked);
  stopButton->setEnabled(false);
  
  QHBoxLayout* evaluateExpressionLayout = new QHBoxLayout();
  evaluateExpressionLayout->setContentsMargins(0, 0, 0, 0);
  evaluateExpressionLayout->setSpacing(0);
  evaluateExpressionLayout->addWidget(expressionEdit);
  evaluateExpressionLayout->addWidget(evaluateExpressionButton);
  
  QHBoxLayout* runBottomLayout = new QHBoxLayout();
  runBottomLayout->setContentsMargins(0, 0, 0, 0);
  runBottomLayout->setSpacing(0);
  runBottomLayout->addWidget(cwdButton);
  runBottomLayout->addSpacing(16);
  runBottomLayout->addWidget(callEdit);
  runBottomLayout->addWidget(callEditButton);
  runBottomLayout->addSpacing(16);
  runBottomLayout->addWidget(runPauseButton);
  runBottomLayout->addWidget(stopButton);
  
  QVBoxLayout* runLayout = new QVBoxLayout();
  runLayout->setContentsMargins(0, 0, 0, 0);
  runLayout->setSpacing(0);
  runLayout->addWidget(threadDropdown);
//   runLayout->addWidget(outputDisplay);
  runLayout->addWidget(stackFramesList);
  runLayout->addLayout(evaluateExpressionLayout);
  runLayout->addLayout(runBottomLayout);
  
  QWidget* runWidgetContainer = new QWidget();
  runWidgetContainer->setLayout(runLayout);
  runWidget->setWidget(runWidgetContainer);
  
  // Status bar
  statusTextLabel = new QLabel();
  statusBar()->addWidget(statusTextLabel, 1);
  
  lineColLabel = new QLabel();
  lineColLabel->setAlignment(Qt::AlignRight);
  statusBar()->addPermanentWidget(lineColLabel, 0);
  
  statusBar()->setSizeGripEnabled(true);
  
  resize(1024, 800);
  
  // Others
  connect(&ParseThreadPool::Instance(), &ParseThreadPool::IndexingRequestFinished, this, &MainWindow::UpdateIndexingStatus);
  
  LoadSession();
  
  connect(this, &MainWindow::OpenProjectsChanged, this, &MainWindow::OpenProjectsChangedSlot);
  OpenProjectsChangedSlot();
  
  connect(&gdbRunner, &GDBRunner::Started, this, &MainWindow::ProgramStarted);
  connect(&gdbRunner, &GDBRunner::Interrupted, this, &MainWindow::ProgramInterrupted);
  connect(&gdbRunner, &GDBRunner::Resumed, this, &MainWindow::ProgramResumed);
  connect(&gdbRunner, &GDBRunner::Stopped, this, &MainWindow::ProgramStopped);
  connect(&gdbRunner, &GDBRunner::ThreadListUpdated, this, &MainWindow::ProgramThreadListUpdated);
  connect(&gdbRunner, &GDBRunner::StackTraceUpdated, this, &MainWindow::ProgramStackTraceUpdated);
  connect(&gdbRunner, &GDBRunner::ResponseReceived, [&](const QString& message) {
    // TODO: This signal does not clearly indicate that it belongs to expression evaluation; should we re-name it to be more specific, or change it to be more general?
    QMessageBox::information(this, tr("Expression evaluation"), message);
  });
  
  QTimer::singleShot(0, this, [&](){
    if (!tabs.empty()) {
      tabs.begin()->second.widget->setFocus();
    }
  });
}

bool MainWindow::LoadProject(const QString& path, QWidget* parent) {
  Project* newProject = new Project();
  if (!newProject->Load(path)) {
    if (isVisible()) {
      QMessageBox::warning(parent, tr("Error"), tr("Could not load project file: %1").arg(path));
    }
    return false;
  }
  
  projects.emplace_back(newProject);
  
  // Initialize run settings
  SetRunCwd(projects.back()->GetRunDir().canonicalPath());
  callEdit->setText(projects.back()->GetRunCmd());
  
  // Update the list of recent projects
  QSettings settings;
  std::vector<QString> recentProjects;
  int numRecentProjects = settings.beginReadArray("recentProjects");
  recentProjects.reserve(numRecentProjects + 1);
  for (int i = 0; i < numRecentProjects; ++ i) {
    settings.setArrayIndex(i);
    QString recentProjectPath = settings.value("path").toString();
    if (recentProjectPath != path) {
      recentProjects.push_back(recentProjectPath);
    }
  }
  settings.endArray();
  recentProjects.push_back(QFileInfo(path).absoluteFilePath());
  settings.beginWriteArray("recentProjects");
  settings.remove("");  // remove previous entries in this group
  for (int i = 0; i < recentProjects.size(); ++ i) {
    settings.setArrayIndex(i);
    settings.setValue("path", recentProjects[i]);
  }
  settings.endArray();
  
  // Emit project lifetime signals
  emit ProjectOpened();
  emit OpenProjectsChanged();
  
  // Configure the project
  QString errorReason;
  if (!newProject->Configure(&errorReason, parent)) {
    if (isVisible()) {
      QMessageBox::warning(parent, tr("Error"), tr("The project failed to configure. Reason:\n\n%1").arg(errorReason));
    }
    return true;
  }
  
  UpdateBuildTargetCombo();
  
  // Start indexing
  if (newProject->GetIndexAllProjectFiles()) {
    numIndexingRequestsCreated += newProject->IndexAllNewFiles(this);
    UpdateIndexingStatus();
  }
  
  return true;
}

std::shared_ptr<Document> MainWindow::GetCurrentDocument() const {
  const auto* tabData = GetCurrentTabData();
  if (!tabData) {
    return nullptr;
  }
  return tabData->document;
}

DocumentWidget* MainWindow::GetCurrentDocumentWidget() const {
  const auto* tabData = GetCurrentTabData();
  if (!tabData) {
    return nullptr;
  }
  return tabData->widget;
}

DocumentWidget* MainWindow::GetWidgetForDocument(Document* document) const {
  for (const std::pair<int, TabData>& item : tabs) {
    if (item.second.document.get() == document) {
      return item.second.widget;
    }
  }
  return nullptr;
}

std::shared_ptr<Project> MainWindow::GetCurrentProject() {
  if (projects.empty()) {
    return nullptr;
  }
  
  auto currentDocument = GetCurrentDocument();
  if (currentDocument == nullptr) {
    return projects.front();
  } else {
    for (auto& project : projects) {
      if (project->ContainsFileOrInclude(currentDocument->path())) {
        return project;
      }
    }
    return projects.front();
  }
}

std::shared_ptr<Document> MainWindow::GetDocument(int index) {
  return tabs.at(tabBar->tabData(index).toInt()).document;
}

bool MainWindow::IsFileOpen(const QString& canonicalPath) const {
  for (const std::pair<int, TabData>& item : tabs) {
    if (item.second.document->path() == canonicalPath) {
      return true;
    }
  }
  return false;
}

bool MainWindow::GetDocumentAndWidgetForPath(const QString& canonicalPath, Document** document, DocumentWidget** widget) const {
  for (const std::pair<int, TabData>& item : tabs) {
    if (item.second.document->path() == canonicalPath) {
      *document = item.second.document.get();
      *widget = item.second.widget;
      return true;
    }
  }
  return false;
}

void MainWindow::DocumentParsed(Document* document) {
  for (const std::pair<int, TabData>& item : tabs) {
    if (item.second.document.get() == document) {
      int tabIndex = FindTabIndexForTabDataIndex(item.first);
      if (tabIndex >= 0) {
        QColor tabColor = qRgb(0, 0, 0);
        for (const std::shared_ptr<Problem>& problem : document->problems()) {
          if (problem->type() == Problem::Type::Warning) {
            tabColor = qRgb(0, 200, 0);
          } else {
            tabColor = qRgb(200, 0, 0);
            break;
          }
        }
        
        tabBar->setTabTextColor(tabIndex, tabColor);
      }
      return;
    }
  }
}

void MainWindow::New() {
  AddTab(new Document(), tr("New document"));
}

void MainWindow::Open() {
  QString defaultPath;
  TabData* tabData = GetCurrentTabData();
  if (tabData && !tabData->document->path().isEmpty()) {
    defaultPath = QFileInfo(tabData->document->path()).dir().path();
  } else {
    QSettings settings;
    defaultPath = settings.value("last_file_dir").toString();
  }
  
  QString path = QFileDialog::getOpenFileName(
      this,
      tr("Load file"),
      defaultPath,
      tr("Text files (*)"));
  if (path.isEmpty()) {
    return;
  }
  
  Open(path);
}

void MainWindow::Open(const QString& path) {
  QSettings settings;
  settings.setValue("last_file_dir", QFileInfo(path).absoluteDir().absolutePath());
  
  // Search for the document among the already open tabs
  QString canonicalFilePath = QFileInfo(path).canonicalFilePath();
  for (const std::pair<int, TabData>& item : tabs) {
    if (item.second.document->path() == canonicalFilePath) {
      tabBar->setCurrentIndex(FindTabIndexForTabDataIndex(item.first));
      return;
    }
  }
  
  // Open the document as a new tab
  Document* newDocument = new Document();
  if (!newDocument->Open(canonicalFilePath)) {
    QMessageBox::warning(this, tr("Error"), tr("Cannot open file: %1").arg(path));
    delete newDocument;
    return;
  }
  
  AddTab(newDocument, QFileInfo(path).fileName());
}

void MainWindow::Save() {
  TabData* tabData = GetCurrentTabData();
  if (!tabData) {
    return;
  }
  
  Save(tabData, tabData->document->path());
}

void MainWindow::SaveAs() {
  TabData* tabData = GetCurrentTabData();
  if (!tabData) {
    return;
  }
  
  SaveAs(tabData);
}

void MainWindow::CloseDocument() {
  CloseDocument(tabBar->currentIndex());
}

void MainWindow::CloseDocument(int tabIndex) {
  int tabDataIndex = tabBar->tabData(tabIndex).toInt();
  auto tabIt = tabs.find(tabDataIndex);
  if (tabIt == tabs.end()) {
    qDebug() << "Error: MainWindow::CloseDocument(): Did not find the tab data of the document to be closed.";
    return;
  }
  TabData& tabData = tabIt->second;
  
  // Check for unsaved changes, ask if they should be discarded
  if (tabData.document->HasUnsavedChanges()) {
    QMessageBox::StandardButton response =
        QMessageBox::warning(
            this,
            tr("Close %1").arg(tabData.document->fileName().isEmpty() ? tr("Unnamed document") : tabData.document->fileName()),
            tr("This document has unsaved changes. Would you like to save them? File path:\n\n%1").arg(tabData.document->path().isEmpty() ? tr("(Unnamed document)") : tabData.document->path()),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (response == QMessageBox::Cancel) {
      return;
    } else if (response == QMessageBox::Yes) {
      if (!Save(&tabData, tabData.document->path())) {
        return;
      }
    }
  }
  
  CrashBackup::Instance().RemoveBackup(tabData.document->path());
  
  bool hadFocus = tabData.widget->hasFocus();
  // Prevent the focus from going to the search bar (triggering a pop-up list)
  projectTreeView.SetFocus();
  
  documentLayout->removeWidget(tabData.container);
  tabBar->removeTab(tabIndex);
  tabData.document.reset();
  delete tabData.container;
  tabs.erase(tabIt);
  
  if (hadFocus) {
    QWidget* currentDocumentWidget = GetCurrentDocumentWidget();
    if (currentDocumentWidget) {
      currentDocumentWidget->setFocus();
    }
  }
  
  CurrentOrOpenDocumentsChanged();
  emit DocumentClosed();
}

void MainWindow::CloseAllOtherDocuments(int tabIndex) {
  for (int index = tabBar->count() - 1; index >= 0; -- index) {
    if (index != tabIndex) {
      CloseDocument(index);
    }
  }
}

void MainWindow::CloseAllDocuments() {
  CloseAllOtherDocuments(-1);
}

void MainWindow::DisplayCursorPosition(int line, int col) {
  lineColLabel->setText(tr("L: %1 C: %2").arg(line + 1).arg(col + 1));
}

void MainWindow::GoToLeftTab() {
  if (tabBar->currentIndex() > 0) {
    tabBar->setCurrentIndex(tabBar->currentIndex() - 1);
  }
}

void MainWindow::Reconfigure() {
  for (const auto& project : projects) {
    ReconfigureProject(project, this);
  }
}

void MainWindow::ReconfigureProject(const std::shared_ptr<Project>& project, QWidget* parent) {
  QString errorReason;
  if (!project->Configure(&errorReason, parent)) {
    QMessageBox::warning(parent, tr("Error"), tr("The project failed to configure. Reason:\n\n%1").arg(errorReason));
    return;
  }
  
  if (project->GetIndexAllProjectFiles()) {
    numIndexingRequestsCreated += project->IndexAllNewFiles(this);
    UpdateIndexingStatus();
  }
  
  UpdateBuildTargetCombo();
}

void MainWindow::ShowProjectSettings() {
  // TODO: Use current project
  if (projects.empty()) {
    return;
  }
  std::shared_ptr<Project> currentProject = projects.front();
  
  ProjectSettingsDialog dialog(currentProject, this);
  dialog.exec();
  
  currentProject->Save(currentProject->GetYAMLFilePath());
}

void MainWindow::ParseSettingsForCurrentFile() {
  TabData* tab = GetCurrentTabData();
  if (!tab) {
    return;
  }
  
  bool settingsAreGuessed;
  std::shared_ptr<Project> usedProject;
  CompileSettings* settings = FindParseSettingsForFile(tab->document->path(), GetProjects(), &usedProject, &settingsAreGuessed);
  
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Parse settings for: %1").arg(tab->document->path()));
  dialog.setWindowIcon(QIcon(":/cide/cide.png"));
  
  QVBoxLayout* layout = new QVBoxLayout();
  QLabel* projectLabel = new QLabel(tr("Settings from project: <b>%1</b>").arg(usedProject ? usedProject->GetName() : tr("<no project>")));
  layout->addWidget(projectLabel);
  QLabel* guessedLabel = new QLabel(tr("Settings are guessed: <b>%1</b>").arg(settingsAreGuessed ? tr("yes") : tr("no")));
  layout->addWidget(guessedLabel);
  
  if (settings) {
    QLabel* languageLabel = new QLabel(tr("Language: <b>%1</b>").arg(CompileSettings::LanguageToString(settings->language)));
    layout->addWidget(languageLabel);
    
    std::vector<QByteArray> args = settings->BuildCommandLineArgs(true, tab->document->path(), usedProject.get());
    
    QLabel* argsLabel = new QLabel(tr("Command-line arguments:"));
    layout->addWidget(argsLabel);
    QString argsString;
    for (const QByteArray& item : args) {
      if (!argsString.isEmpty()) {
        argsString += QStringLiteral(" ");
      }
      argsString += item;
    }
    QPlainTextEdit* argsEdit = new QPlainTextEdit(argsString);
    argsEdit->setReadOnly(true);
    layout->addWidget(argsEdit);
  } else {
    QLabel* noSettingsFoundLabel = new QLabel(tr("No parse settings found."));
    layout->addWidget(noSettingsFoundLabel);
  }
  
  QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
  connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  layout->addWidget(buttonBox);
  
  dialog.setLayout(layout);
  
  dialog.exec();
}

bool MainWindow::NewProject(QWidget* parent) {
  NewProjectDialog dialog(QString(), parent);
  if (dialog.exec() == QDialog::Rejected) {
    return false;
  }
  
  dialog.CreateProject();
  if (LoadProject(dialog.GetProjectFilePath(), parent)) {
    return true;
  }
  return false;
}

bool MainWindow::OpenProject(QWidget* parent) {
  QString path = QFileDialog::getOpenFileName(
      parent,
      tr("Open project or CMakeLists.txt"),
      "",
      tr("CIDE or CMakeLists.txt files (*.cide *.txt)"));
  if (path.isEmpty()) {
    return false;
  }
  
  if (path.endsWith(".cide", Qt::CaseInsensitive)) {
    if (LoadProject(path, parent)) {
      return true;
    }
  } else if (path.endsWith(".txt", Qt::CaseInsensitive)) {
    NewProjectDialog dialog(path, parent);
    if (dialog.exec() == QDialog::Rejected) {
      return false;
    }
    
    dialog.CreateProject();
    if (LoadProject(dialog.GetProjectFilePath(), parent)) {
      return true;
    }
  } else {
    QMessageBox::warning(parent, tr("Open project"), tr("The selected file %1 is not a CIDE project file (*.cide) or CMakeLists.txt file (*.txt).").arg(path));
  }
  return false;
}

void MainWindow::CloseProject() {
  if (projects.empty()) {
    return;
  }
  
  projects.clear();
  
  emit OpenProjectsChanged();
}

void MainWindow::ShowProjectFilesDock() {
  if (!projectTreeView.GetDockWidget()->isVisible()) {
    addDockWidget(Qt::LeftDockWidgetArea, projectTreeView.GetDockWidget());
    projectTreeView.GetDockWidget()->show();
  } else {
    removeDockWidget(projectTreeView.GetDockWidget());
    projectTreeView.GetDockWidget()->hide();
  }
  showProjectFilesDockAction->setChecked(projectTreeView.GetDockWidget()->isVisible());
}

void MainWindow::DebugCurrentProject() {
  // Show run dock
  if (!runWidget->isVisible()) {
    addDockWidget(Qt::BottomDockWidgetArea, runWidget);
    runWidget->show();
  }
#ifndef WIN32
  showRunDockAction->setChecked(true);
#endif
  
  // Start the application
  if (gdbRunner.IsRunning()) {
    if (QMessageBox::question(nullptr, tr("Start debugging"), tr("The debugger is already running. Exit it and start anew?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
      return;
    }
    gdbRunner.Stop();
    gdbRunner.WaitForExit();
  }
  
  RunPauseClicked();
}

void MainWindow::ShowRunDock() {
  if (!runWidget->isVisible()) {
    addDockWidget(Qt::BottomDockWidgetArea, runWidget);
    runWidget->show();
  } else {
    removeDockWidget(runWidget);
    runWidget->hide();
  }
#ifndef WIN32
  showRunDockAction->setChecked(runWidget->isVisible());
#endif
}

void MainWindow::RunWidgetClosed() {
  showRunDockAction->setChecked(false);
}

void MainWindow::CwdButtonClicked() {
  QString dir = QFileDialog::getExistingDirectory(
      this,
      tr("Choose working directory"),
      runCwd,
      QFileDialog::DontUseNativeDialog);
  if (!dir.isEmpty()) {
    SetRunCwd(dir);
  }
}

void MainWindow::SetRunCwd(const QString& cwd) {
  runCwd = cwd;
  cwdButton->setText(tr("Cwd: %1").arg(QDir(runCwd).dirName()));
}

void MainWindow::EditCallClicked() {
  // Show a dialog with a multi-line text edit that shows each parameter on a
  // separate line, allowing efficient editing of the call parameters.
  QDialog editCallDialog(this);
  editCallDialog.setWindowTitle(tr("Edit call"));
  editCallDialog.setWindowIcon(QIcon(":/cide/cide.png"));
  
  // TODO: Properly parse the call such as not split "a b c" onto separate lines
  QPlainTextEdit* edit = new QPlainTextEdit(
      callEdit->text().replace(' ', '\n'));
  
  QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttonBox, &QDialogButtonBox::accepted, &editCallDialog, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, &editCallDialog, &QDialog::reject);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(edit);
  layout->addWidget(buttonBox);
  editCallDialog.setLayout(layout);
  
  if (editCallDialog.exec() == QDialog::Rejected) {
    return;
  }
  
  callEdit->setText(edit->toPlainText().replace('\n', ' '));
}

void MainWindow::RunPauseClicked() {
  if (gdbRunner.IsInterrupted()) {
    // Resume the program.
    
    // Disable all run buttons, waiting for the run state to get updated
    runPauseButton->setEnabled(false);
    stopButton->setEnabled(false);
    
    gdbRunner.Resume();
  } else if (gdbRunner.IsRunning()) {
    // Interrupt the program.
    
    // Disable all run buttons, waiting for the run state to get updated
    runPauseButton->setEnabled(false);
    stopButton->setEnabled(false);
    
    gdbRunner.Interrupt();
  } else {
    // Start the program.
    
    // TODO: Properly parse the call such as not split "a b c" onto separate lines
    QStringList arguments = callEdit->text().split(' ');
    if (arguments.empty()) {
      QMessageBox::warning(this, tr("Start program"), tr("No program specified."));
      return;
    }
    
    // TODO: Should the run config be stored in the session instead?
    if (!projects.empty()) {
      projects.back()->SetRunConfiguration(runCwd, callEdit->text());
    }
    
    // Disable all run buttons, waiting for the run state to get updated
    runPauseButton->setEnabled(false);
    stopButton->setEnabled(false);
    
    gdbRunner.Start(runCwd, arguments);
  }
}

void MainWindow::StopClicked() {
  // Disable all run buttons, waiting for the run state to get updated
  runPauseButton->setEnabled(false);
  stopButton->setEnabled(false);
  
  gdbRunner.Stop();
}

void MainWindow::ThreadChanged(int index) {
  stackFramesList->setEnabled(false);
  
  int threadId = threadDropdown->itemData(index).toInt();
  gdbRunner.GetStackTrace(threadId);
}

void MainWindow::ProgramFrameActivated(QListWidgetItem* item) {
  int frameIndex = item->data(Qt::UserRole).toInt();
  if (frameIndex < 0 || frameIndex >= currentStackFrames.size()) {
    QMessageBox::warning(this, tr("Error"), tr("Index of activated list item is out-of-bounds of the cached stack trace."));
    return;
  }
  
  const StackFrame& frame = currentStackFrames[frameIndex];
  if (!frame.path.isEmpty()) {
    GotoDocumentLocation(QStringLiteral("file://") + frame.path + ((frame.line >= 0) ? (QStringLiteral(":") + QString::number(frame.line)) : ""));
  }
  
  DocumentWidget* currentDocumentWidget = GetCurrentDocumentWidget();
  bool needsUpdate = currentDocumentWidget && currentDocumentWidget->GetDocument()->path() == currentFrameCanonicalPath;
  currentFrameCanonicalPath = QFileInfo(frame.path).canonicalFilePath();
  currentFrameLine = frame.line - 1;
  currentFrameAddr = frame.address;
  currentFrameLevel = frame.level;
  if (needsUpdate) {
    currentDocumentWidget->update(currentDocumentWidget->rect());
  }
}

void MainWindow::EvaluateExpression() {
  if (expressionEdit->text().isEmpty()) {
    QMessageBox::warning(this, tr("Evaluate expression"), tr("No expression entered."));
    return;
  }
  
  if (currentFrameAddr.isEmpty()) {
    QMessageBox::warning(this, tr("Evaluate expression"), tr("No frame selected."));
    return;
  }
  
  int threadId = threadDropdown->itemData(threadDropdown->currentIndex()).toInt();
  gdbRunner.EvaluateExpression(expressionEdit->text(), threadId, currentFrameLevel);
}

void MainWindow::ProgramStarted() {
  statusTextLabel->setVisible(true);
  statusTextLabel->setText(tr("Program started"));
  
  runPauseButton->setEnabled(true);
  runPauseButton->setText(tr("Interrupt"));
  stopButton->setEnabled(true);
}

void MainWindow::ProgramInterrupted() {
  statusTextLabel->setVisible(true);
  statusTextLabel->setText(tr("Program interrupted"));
  
  runPauseButton->setEnabled(true);
  runPauseButton->setText(tr("Resume"));
  stopButton->setEnabled(true);
  expressionEdit->setEnabled(true);
  evaluateExpressionButton->setEnabled(true);
  
  // Request updated thread and frame info
  // TODO: Disable the thread combo box here and enable it again once we got the update
  gdbRunner.GetThreadList();
  gdbRunner.GetStackTrace();
}

void MainWindow::ProgramResumed() {
  statusTextLabel->setVisible(true);
  statusTextLabel->setText(tr("Program resumed"));
  
  runPauseButton->setEnabled(true);
  runPauseButton->setText(tr("Interrupt"));
  stopButton->setEnabled(true);
  expressionEdit->setEnabled(false);
  evaluateExpressionButton->setEnabled(false);
  
  DocumentWidget* currentDocumentWidget = GetCurrentDocumentWidget();
  bool needsUpdate = currentDocumentWidget && currentDocumentWidget->GetDocument()->path() == currentFrameCanonicalPath;
  currentFrameCanonicalPath = "";
  currentFrameLine = -1;
  currentFrameAddr = "";
  currentFrameLevel = -1;
  if (needsUpdate) {
    currentDocumentWidget->update(currentDocumentWidget->rect());
  }
}

void MainWindow::ProgramStopped(int exitCode) {
  statusTextLabel->setVisible(true);
  if (exitCode == 0) {
    statusTextLabel->setText(tr("Program exited normally (exit code 0)"));
  } else if (exitCode == -1) {
    statusTextLabel->setText(tr("Program exited"));
  } else {
    statusTextLabel->setText(tr("Program exited with code %1").arg(exitCode));
  }
  
  runPauseButton->setEnabled(true);
  runPauseButton->setText(tr("Run"));
  stopButton->setEnabled(false);
  expressionEdit->setEnabled(false);
  evaluateExpressionButton->setEnabled(false);
  
  threadDropdown->setEnabled(false);
  stackFramesList->setEnabled(false);
  
  DocumentWidget* currentDocumentWidget = GetCurrentDocumentWidget();
  bool needsUpdate = currentDocumentWidget && currentDocumentWidget->GetDocument()->path() == currentFrameCanonicalPath;
  currentFrameCanonicalPath = "";
  currentFrameLine = -1;
  currentFrameAddr = "";
  currentFrameLevel = -1;
  if (needsUpdate) {
    currentDocumentWidget->update(currentDocumentWidget->rect());
  }
}

void MainWindow::ProgramThreadListUpdated() {
  threadDropdown->clear();
  
  for (const auto& threadIdAndFrame : gdbRunner.GetThreadIdAndFrames()) {
    // qDebug() << "Thread" << threadIdAndFrame.first << ":" << threadIdAndFrame.second;
    threadDropdown->addItem(tr("Thread %1").arg(threadIdAndFrame.second), QVariant(threadIdAndFrame.first));
  }
  
  // qDebug() << "Current thread ID:" << gdbRunner.GetCurrentThreadId();
  for (int index = 0; index < threadDropdown->count(); ++ index) {
    if (threadDropdown->itemData(index).toInt() == gdbRunner.GetCurrentThreadId()) {
      threadDropdown->setCurrentIndex(index);
      break;
    }
  }
  
  threadDropdown->setEnabled(true);
}

void MainWindow::ProgramStackTraceUpdated() {
  currentStackFrames = gdbRunner.GetStackTraceResult();
  
  stackFramesList->clear();
  
  for (int stackFrameIndex = 0; stackFrameIndex < currentStackFrames.size(); ++ stackFrameIndex) {
    const StackFrame& frame = currentStackFrames[stackFrameIndex];
    
    QListWidgetItem* newItem = new QListWidgetItem(frame.shortDescription);
    newItem->setData(Qt::UserRole, QVariant(stackFrameIndex));
    
    bool hasFilePath = !frame.path.isEmpty();
    bool fileIsInProject = false;
    
    if (hasFilePath) {
      // Check whether this file belongs to an opened project.
      // If yes, highlight the list item in a different color.
      QString canonicalPath = QFileInfo(frame.path).canonicalFilePath();
      for (const auto& project : projects) {
        if (project->ContainsFile(canonicalPath)) {
          fileIsInProject = true;
          break;
        }
      }
    }
    
    if (fileIsInProject) {
      // Keep default background color
    } else if (hasFilePath) {
      // File has a path, but is not in the project --> darken a bit
      newItem->setBackgroundColor(qRgb(220, 220, 220));
    } else {
      // File does not even have a path --> darken more
      newItem->setBackgroundColor(qRgb(190, 190, 190));
    }
    
    stackFramesList->addItem(newItem);
  }
  
  stackFramesList->setCurrentItem(nullptr);
  stackFramesList->setEnabled(true);
}

void MainWindow::ReloadFile() {
  TabData* tabData = GetCurrentTabData();
  if (!tabData) {
    return;
  }
  
  // Check for unsaved changes, ask if they should be discarded
  if (tabData->document->HasUnsavedChanges()) {
    QMessageBox::StandardButton response =
        QMessageBox::warning(
            this,
            tr("Reload %1").arg(tabData->document->fileName().isEmpty() ? tr("Unnamed document") : tabData->document->fileName()),
            tr("This document has unsaved changes. Discard them? File path:\n\n%1").arg(tabData->document->path().isEmpty() ? tr("(Unnamed document)") : tabData->document->path()),
            QMessageBox::Discard | QMessageBox::Cancel);
    if (response == QMessageBox::Cancel) {
      return;
    }
  }
  
  if (!tabData->document->Open(tabData->document->path())) {
    QMessageBox::warning(this, tr("Error"), tr("Cannot read file for re-loading: %1").arg(tabData->document->path()));
    return;
  }
  tabData->container->SetMessage(DocumentWidgetContainer::MessageType::ExternalModificationNotification, QStringLiteral(""));
}

void MainWindow::UpdateIndexingStatus() {
  int progressPercentage = (100 * ParseThreadPool::Instance().GetNumFinishedIndexingRequests()) / numIndexingRequestsCreated;
  
  if (progressPercentage == 100) {
    statusTextLabel->setVisible(false);
  } else {
    statusTextLabel->setVisible(true);
    statusTextLabel->setText(tr("Indexing (%1)").arg(QString::number(progressPercentage) + QStringLiteral("%")));
  }
}

void MainWindow::SetStatusText(const QString& text) {
  statusTextLabel->setVisible(true);
  statusTextLabel->setText(text);
}

void MainWindow::BuildCurrentTarget() {
  if (projects.empty()) {
    QMessageBox::warning(this, tr("Build current target"), tr("No project is open that can be built."));
    return;
  }
  
  if (buildProcess) {
    if (buildProcess->state() != QProcess::NotRunning) {
      if (QMessageBox::question(
          this,
          tr("Build current target"),
          tr("A build process is running already. Abort the old process and start a new one?"),
          QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
        return;
      }
      StopBuilding();
    }
  }
  
  // Save all modified files
  for (const std::pair<int, TabData>& item : tabs) {
    if (item.second.document->HasUnsavedChanges()) {
      Save(&item.second, item.second.document->path());
    }
  }

  // Hide the build dock in case it is currently shown.
  buildOutputWidget->hide();
  
  // Show the build status bar and buttons.
  statusTextLabel->setVisible(false);
  ClearBuildIssues();
  
  buildProgressBar = new QProgressBar();
  buildProgressBar->setRange(0, 0);  // show a generic busy indicator as long as the range is unknown
  statusBar()->addWidget(buildProgressBar, 1);
  
  buildStopButton = new QPushButton(tr("Stop"));
  connect(buildStopButton, &QPushButton::clicked, this, &MainWindow::StopBuilding);
  statusBar()->addWidget(buildStopButton, 0);
  
  buildViewOutputButton = new QPushButton(tr("View output"));
  connect(buildViewOutputButton, &QPushButton::clicked, this, &MainWindow::ViewBuildOutput);
  statusBar()->addWidget(buildViewOutputButton, 0);
  
  // Get the current project.
  // TODO: Add a UI element that allows to select the current project, instead of the current HACK taking always the first
  std::shared_ptr<Project> currentProject = projects.front();
  
  // Create QProcess and connect signals.
  buildProcess.reset(new QProcess());
  
  connect(buildProcess.get(),
          &QProcess::readyReadStandardOutput,
          [&]() {
    buildStdout += buildProcess->readAllStandardOutput();
    
    TryParseBuildStdout();
  });
  
  connect(buildProcess.get(),
          &QProcess::readyReadStandardError,
          [&]() {
    buildStderr += buildProcess->readAllStandardError();
    
    TryParseBuildStderr();
  });
  
  connect(buildProcess.get(),
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          [&](int exitCode, QProcess::ExitStatus exitStatus) {
    switch (exitStatus) {
    case QProcess::NormalExit:
      if (buildErrors > 0 && buildWarnings > 0) {
        FinishedBuilding(tr("<span style=\"color:red\">Errors: %1</span> <span style=\"color:yellow\">Warnings: %2</span>").arg(buildErrors).arg(buildWarnings));
      } else if (buildErrors > 0) {
        FinishedBuilding(tr("<span style=\"color:red\">Errors: %1</span>").arg(buildErrors));
      } else if (buildWarnings > 0) {
        FinishedBuilding(tr("<span style=\"color:yellow\">Built with %1 warning%2</span>").arg(buildWarnings).arg((buildWarnings > 1) ? QStringLiteral("s") : QStringLiteral("")));
      } else if (exitCode == 0) {
        FinishedBuilding(tr("<span style=\"color:green\">Built successfully</span>"));
      } else {
        ViewBuildOutputAsText();
        FinishedBuilding(tr("<span style=\"color:red\">Building returned exit code %1</span>").arg(exitCode));
      }
      break;
    case QProcess::CrashExit:
      FinishedBuilding(tr("<span style=\"color:red\">Building crashed</span>"));
      break;
    }
  });
  
  connect(buildProcess.get(),
          &QProcess::errorOccurred,
          [&](QProcess::ProcessError error) {
    switch (error) {
    case QProcess::FailedToStart:
      FinishedBuilding(tr("<span style=\"color:red\">Failed to start the build process (program missing / insufficient permissions).</span>"));
      break;
    case QProcess::Crashed:
      FinishedBuilding(tr("<span style=\"color:red\">Building crashed</span>"));
      break;
    case QProcess::Timedout:
      FinishedBuilding(tr("<span style=\"color:red\">Building timed out</span>"));
      break;
    case QProcess::WriteError:
      FinishedBuilding(tr("<span style=\"color:red\">Error while writing to the build process</span>"));
      break;
    case QProcess::ReadError:
      FinishedBuilding(tr("<span style=\"color:red\">Error while reading from the build process</span>"));
      break;
    case QProcess::UnknownError:
      FinishedBuilding(tr("<span style=\"color:red\">An unknown error occurred with the build process</span>"));
      break;
    };
  });
  
  // Determine whether to use make or ninja.
  QString binaryPath;
  if (QFileInfo(currentProject->GetBuildDir().filePath("build.ninja")).exists()) {
    binaryPath = "ninja";
    buildParseMode = BuildParseMode::Ninja;
  } else if (QFileInfo(currentProject->GetBuildDir().filePath("Makefile")).exists()) {
    binaryPath = "make";
    buildParseMode = BuildParseMode::Make;
  } else {
    QMessageBox::warning(this, tr("Build current target"), tr("Neither 'Makefile' nor 'build.ninja' found in the build directory, thus cannot proceed. Maybe CMake needs to be run first?"));
    StopBuilding();
    return;
  }
  
  // Compile command-line arguments.
  QStringList arguments;
  if (currentProject->GetBuildThreads() != 0) {
    arguments.push_back("-j");
    arguments.push_back(QString::number(currentProject->GetBuildThreads()));
  }
  if (!buildTargetCombo->currentText().isEmpty()) {
    arguments.push_back(buildTargetCombo->currentText());
  }
  
  // Start the process.
  buildProcess->setWorkingDirectory(currentProject->GetBuildDir().path());
  buildProcess->start(binaryPath, arguments);
}

void MainWindow::TryParseBuildStdout() {
  QByteArray lineBytes;
  for (int i = 0; i < buildStdout.size(); ++ i) {
    if (buildStdout[i] != '\n') {
      lineBytes += buildStdout[i];
      continue;
    }
    
    QString line = QString::fromUtf8(lineBytes);
    lineBytes.clear();
    
    buildOutputText += line + QStringLiteral("\n");
    if (buildOutputTextLabel) {
      buildOutputTextLabel->setText(buildOutputText);
      buildIssuesWidget->resize(buildIssuesWidget->sizeHint());
    }
    
    // Remove the line from buildStdout.
    buildStdout.remove(0, i + 1);
    i = -1;
    
    // Parse the line.
    bool hasBeenParsed = false;
    
    // Ignore messages like "ninja: build stopped: subcommand failed."
    if (!hasBeenParsed && buildParseMode != BuildParseMode::Make && line.startsWith(QStringLiteral("ninja: "))) {
      hasBeenParsed = true;
    }
    
    // Ignore messages like "FAILED: CMakeFiles/CIDEBaseLib.dir/src/cide/main.cc.o"
    // TODO: Does ninja always print the failed command afterwards? Then we could skip that line safely.
    if (!hasBeenParsed && buildParseMode != BuildParseMode::Make && line.startsWith(QStringLiteral("FAILED: "))) {
      hasBeenParsed = true;
    }
    
    // Ignore "collect2: error: ld returned 1 exit status"
    if (!hasBeenParsed && line == QStringLiteral("collect2: error: ld returned 1 exit status")) {
      hasBeenParsed = true;
    }
    
    // Format of ninja progress output: "[X/Y] Message ...\n"
    if (!hasBeenParsed && buildParseMode != BuildParseMode::Make && line.size() >= 6 && line[0] == '[') {
      int slashPos = line.indexOf('/', 2);
      if (slashPos >= 0) {
        int closingBracketPos = line.indexOf(']', slashPos + 1);
        if (closingBracketPos >= 0) {
          bool ok;
          int currentItem = line.midRef(1, slashPos - 1).toInt(&ok);
          if (ok) {
            int itemCount = line.midRef(slashPos + 1, closingBracketPos - slashPos - 1).toInt(&ok);
            if (ok) {
              buildProgressBar->setRange(0, itemCount);
              buildProgressBar->setValue(currentItem);
              hasBeenParsed = true;
            }
          }
        }
      }
    }
    
    // Format of make progress output: "[ 50%] Message ...\n"
    if (!hasBeenParsed && buildParseMode != BuildParseMode::Ninja && line.size() >= 6 && line[0] == '[') {
      int closingBracketPos = line.indexOf(']', 1);
      if (closingBracketPos >= 3 && line[closingBracketPos - 1] == '%') {
        bool ok;
        int percentage = line.midRef(1, closingBracketPos - 2).toInt(&ok);
        if (ok) {
          buildProgressBar->setRange(0, 100);
          buildProgressBar->setValue(percentage);
          hasBeenParsed = true;
        }
      }
    }
    
    if (!hasBeenParsed) {
      ParseBuildOutputForError(line);
    }
  }
}

void MainWindow::TryParseBuildStderr() {
  QByteArray lineBytes;
  for (int i = 0; i < buildStderr.size(); ++ i) {
    if (buildStderr[i] != '\n') {
      lineBytes += buildStderr[i];
      continue;
    }
    
    QString line = QString::fromUtf8(lineBytes);
    lineBytes.clear();
    
    buildOutputText += line + QStringLiteral("\n");
    if (buildOutputTextLabel) {
      buildOutputTextLabel->setText(buildOutputText);
      buildIssuesWidget->resize(buildIssuesWidget->sizeHint());
    }
    
    // Remove the line from buildStderr.
    buildStderr.remove(0, i + 1);
    i = -1;
    
    // Parse the line.
    ParseBuildOutputForError(line);
  }
}

void MainWindow::ParseBuildOutputForError(const QString& line) {
  bool hasBeenParsed = false;
  
  // Format of GCC 5 build error output:
  // "<path>:line:column: error: <description>"
  // "<line with the error>"
  // "               ^     "  (pointing to the error)
  // 
  // ../src/cide/main.cc: In instantiation of ‘void Test<T>::Foo() [with T = int]’:
  // ../src/cide/main.cc:22:12:   required from here
  // ../src/cide/main.cc:11:10: error: invalid conversion from ‘const char*’ to ‘int’ [-fpermissive]
  //      test = "bar";
  //           ^
  // 
  // Format of clang-cl build error output:
  // "<path>"(line,column): error: <description>"
  // "<line with the error>"
  // "               ^     "  (pointing to the error)
  if (!hasBeenParsed &&
      (line.endsWith(QStringLiteral("required from here")) ||
        line.indexOf("In instantiation of") > 0)) {
    int firstSpace = line.indexOf(' ');
    if (firstSpace >= 0) {
      cachedPartialIssueText += QStringLiteral(
          "<a href=\"%1\">%1</a> %2<br/>")
              .arg(line.midRef(0, firstSpace))
              .arg(line.midRef(firstSpace + 1));
      hasBeenParsed = true;
    }
  }
  
  if (!hasBeenParsed && line.size() >= 12) {
    bool isError = true;
    bool isNote = false;
    int prefixLen = 9;
    int errorOrWarningStringPos = line.indexOf(QStringLiteral(": error: "));
    if (errorOrWarningStringPos <= 0) {
      isError = true;
      prefixLen = 15;
      errorOrWarningStringPos = line.indexOf(QStringLiteral(": fatal error: "));
      if (errorOrWarningStringPos <= 0) {
        isError = false;
        prefixLen = 11;
        errorOrWarningStringPos = line.indexOf(QStringLiteral(": warning: "));
        if (errorOrWarningStringPos <= 0) {
          isNote = true;
          prefixLen = 8;
          errorOrWarningStringPos = line.indexOf(QStringLiteral(": note: "));
        }
      }
    }
    if (errorOrWarningStringPos >= 5) {
      int lineNumber = -1;
      int columnNumber = -1;
      QString path;
      QString problemText;
      
      if (line[errorOrWarningStringPos - 1] == ')') {
        // The output seems to be in clang-cl format.
        int openingBracePos = line.lastIndexOf('(', errorOrWarningStringPos - 2);
        if (openingBracePos >= 0) {
          int commaPos = line.indexOf(',', openingBracePos + 1);
          if (commaPos >= 0) {
            bool ok;
            lineNumber = line.midRef(openingBracePos + 1, commaPos - openingBracePos - 1).toInt(&ok);
            if (ok) {
              columnNumber = line.midRef(commaPos + 1, errorOrWarningStringPos - 1 - commaPos - 1).toInt(&ok);
              if (ok) {
                path = line.left(openingBracePos);
                problemText = line.mid(errorOrWarningStringPos + prefixLen);
              }
            }
          }
        }
      } else {
        // The output is assumed to be in gcc/clang format.
        int columnColonPos = line.lastIndexOf(':', errorOrWarningStringPos - 1);
        if (columnColonPos >= 0) {
          int lineColonPos = line.lastIndexOf(':', columnColonPos - 1);
          if (lineColonPos >= 0) {
            bool ok;
            lineNumber = line.midRef(lineColonPos + 1, columnColonPos - lineColonPos - 1).toInt(&ok);
            if (ok) {
              columnNumber = line.midRef(columnColonPos + 1, errorOrWarningStringPos - columnColonPos - 1).toInt(&ok);
              if (ok) {
                path = line.left(lineColonPos);
                problemText = line.mid(errorOrWarningStringPos + prefixLen);
              }
            }
          }
        }
      }
      
      if (!problemText.isEmpty()) {
        // TODO: Also append any following lines that also belong to this error?
        //       It would be easy to recognize lines containing only whitespace and ^, this signals that the line before is a code excerpt.
        //       However, the code can also easily be looked at by clicking the issue link, so maybe we don't need the excerpt.
        QString htmlText = QStringLiteral("%5<a href=\"%1:%2:%3\">%1:%2:%3</a> <b>%4</b>")
            .arg(path)
            .arg(lineNumber)
            .arg(columnNumber)
            .arg(problemText.toHtmlEscaped())
            .arg(cachedPartialIssueText);
        if (isNote) {
          AppendBuildIssue(htmlText);
        } else {
          AddBuildIssue(htmlText, isError);
        }
        cachedPartialIssueText.clear();
        hasBeenParsed = true;
      }
    }
  }
  
  // Format of linker errors:
  // "libCIDEBaseLib.so: undefined reference to `DockWidgetWithClosedSignal::staticMetaObject'"
  if (!hasBeenParsed && line.size() >= 21) {
    int undefinedReferencePos = line.indexOf(QStringLiteral(": undefined reference"));
    if (undefinedReferencePos >= 0) {
      AddBuildIssue(QStringLiteral("<b>%1</b>").arg(line.trimmed().toHtmlEscaped()), true);
      hasBeenParsed = true;
    }
  }
}

void MainWindow::ViewBuildOutput() {
  if (!buildOutputWidget->isVisible()) {
    addDockWidget(Qt::BottomDockWidgetArea, buildOutputWidget);
    buildOutputWidget->show();
  }
  if (buildViewOutputButton) {
    buildViewOutputButton->setVisible(false);
  }
}

void MainWindow::ViewBuildOutputAsText() {
  if (!buildOutputWidget->isVisible()) {
    addDockWidget(Qt::BottomDockWidgetArea, buildOutputWidget);
    buildOutputWidget->show();
  }
  
  if (!buildOutputTextLabel) {
    buildOutputTextLabel = new QLabel(buildOutputText);
    buildOutputTextLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    buildOutputTextLabel->setFont(Settings::Instance().GetDefaultFont());
    buildIssuesLayout->insertWidget(0, buildOutputTextLabel);
    buildIssuesWidget->resize(buildIssuesWidget->sizeHint());
  }
  buildIssuesRichTextScrollArea->ensureVisible(0, 0);
  
  if (buildViewOutputButton) {
    buildViewOutputButton->setVisible(false);
  }
}

void MainWindow::BuildOutputWidgetClosed() {
  if (buildViewOutputButton) {
    buildViewOutputButton->setVisible(true);
  }
}

void MainWindow::StopBuilding() {
  if (buildProcess) {
    // This triggers the "crash" signal, which removes the build widgets from
    // the status bar. So, no need to call FinishedBuilding().
    buildProcess->kill();
  }
}

void MainWindow::FinishedBuilding(const QString& statusMessage) {
  if (buildProgressBar) {
    statusBar()->removeWidget(buildProgressBar);
    delete buildProgressBar;
    buildProgressBar = nullptr;
  }
  if (buildStopButton) {
    statusBar()->removeWidget(buildStopButton);
    delete buildStopButton;
    buildStopButton = nullptr;
  }
  if (buildViewOutputButton) {
    statusBar()->removeWidget(buildViewOutputButton);
    delete buildViewOutputButton;
    buildViewOutputButton = nullptr;
  }
  
  if (!statusMessage.isEmpty()) {
    statusTextLabel->setVisible(true);
    statusTextLabel->setText(statusMessage);
  }
}

void MainWindow::GoToRightTab() {
  if (tabBar->currentIndex() < tabBar->count() - 1) {
    tabBar->setCurrentIndex(tabBar->currentIndex() + 1);
  }
}

void MainWindow::SwitchHeaderSource() {
  // TODO: It can also be that different parts of a source file are defined
  //       in different headers, or different parts of a header are implemented
  //       in different source files. Thus, try to take the current context into
  //       account (i.e., where the cursor position in the document is).
  
  TabData* tabData = GetCurrentTabData();
  if (!tabData) {
    return;
  }
  
  const QString& path = tabData->document->path();
  if (path.isEmpty()) {
    return;
  }
  
  QString correspondingPath = FindCorrespondingHeaderOrSource(path, projects);
  if (!correspondingPath.isEmpty()) {
    Open(correspondingPath);
  }
}

void MainWindow::UpdateBuildTargetCombo() {
  auto project = GetCurrentProject();
  QString oldSelectedTarget;
  if (project) {
    oldSelectedTarget = project->GetBuildTarget();
  } else {
    oldSelectedTarget = buildTargetCombo->currentText();
  }
  
  buildTargetCombo->clear();
  
  if (!project) {
    return;
  }
  
  int numTargets = project->GetNumTargets();
  for (int targetIdx = 0; targetIdx < numTargets; ++ targetIdx) {
    const QString& targetName = project->GetTarget(targetIdx).name;
    buildTargetCombo->addItem(targetName);
    if ((!oldSelectedTarget.isEmpty() && oldSelectedTarget == targetName) ||
        (oldSelectedTarget.isEmpty() && targetName == QStringLiteral("all"))) {
      buildTargetCombo->setCurrentIndex(buildTargetCombo->count() - 1);
    }
  }
}

void MainWindow::BuildTargetChanged(const QString& target) {
  auto project = GetCurrentProject();
  if (project) {
    project->SetBuildTarget(target);
  }
}

void MainWindow::GlobalSymbolSearch() {
  searchBar->SetMode(SearchBar::Mode::GlobalSymbols);
  if (searchBar->hasFocus()) {
    searchBar->ShowListWidget();
  } else {
    searchBar->setFocus();
  }
}

void MainWindow::SearchLocalContexts() {
  searchBar->SetMode(SearchBar::Mode::LocalContexts);
  if (searchBar->hasFocus()) {
    searchBar->ShowListWidget();
  } else {
    searchBar->setFocus();
  }
}

void MainWindow::SearchFiles() {
  searchBar->SetMode(SearchBar::Mode::Files);
  if (searchBar->hasFocus()) {
    searchBar->ShowListWidget();
  } else {
    searchBar->setFocus();
  }
}

void MainWindow::UpdateSearchClickActionLabels() {
  QString shortcutString = Settings::Instance().GetConfiguredShortcut(searchInFilesShortcut).sequence.toString(QKeySequence::NativeText);
  searchFilesClickAction->setText(tr("Search project files%1").arg(
      shortcutString.isEmpty() ? QStringLiteral("") : QStringLiteral(" (%1)").arg(shortcutString)));
  
  shortcutString = Settings::Instance().GetConfiguredShortcut(searchLocalContextsShortcut).sequence.toString(QKeySequence::NativeText);
  searchLocalContextsClickAction->setText(tr("Search local contexts%1").arg(
      shortcutString.isEmpty() ? tr(" (click search bar)") : tr(" (%1 / click search bar)").arg(shortcutString)));
  
  shortcutString = Settings::Instance().GetConfiguredShortcut(searchGlobalSymbolsShortcut).sequence.toString(QKeySequence::NativeText);
  globalSymbolSearchClickAction->setText(tr("Search global symbols%1").arg(
      shortcutString.isEmpty() ? QStringLiteral("") : QStringLiteral(" (%1)").arg(shortcutString)));
}

void MainWindow::CurrentTabChanged(int /*index*/) {
  UpdateWindowTitle();
  
  TabData* tabData = GetCurrentTabData();
  if (tabData) {
    documentLayout->setCurrentWidget(tabData->container);
    
    // Try to have the search bar always behind the current editor widget. This
    // is an attempt to avoid the problem that sometimes, when the focus went
    // away from other widgets (e.g., the find bar, or the stop button for
    // building), it went to the search bar instead of the editor.
    setTabOrder(tabData->widget, searchBar);
  }
  
  saveAction->setEnabled(tabData != nullptr);
  saveAsAction->setEnabled(tabData != nullptr);
  closeAction->setEnabled(tabData != nullptr);
  currentFileParseSettingsAction->setEnabled(tabData != nullptr);
  
  CurrentOrOpenDocumentsChanged();
  
  emit CurrentDocumentChanged();
}

void MainWindow::ShowDocumentationDock(const QUrl& url) {
  constexpr int kDockInitialWidth = 600;
  bool initialShow = false;
  
  if (!documentationDock) {
    initialShow = true;
    documentationDock = new QDockWidget(tr("Documentation"));
    
    documentationBackButton = new QPushButton(tr("Back"));
    documentationForwardButton = new QPushButton(tr("Forward"));
    documentationBrowser = new HelpBrowser();
    
    QHBoxLayout* buttonsLayout = new QHBoxLayout();
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->addWidget(documentationBackButton);
    buttonsLayout->addStretch(1);
    buttonsLayout->addWidget(documentationForwardButton);
    
    QVBoxLayout* dockLayout = new QVBoxLayout();
    dockLayout->addLayout(buttonsLayout);
    dockLayout->addWidget(documentationBrowser, 1);
    QWidget* container = new QWidget();
    container->setLayout(dockLayout);
    documentationDock->setWidget(container);
    
    connect(documentationBrowser, &HelpBrowser::backwardAvailable, [&](bool available) {
      documentationBackButton->setEnabled(available);
    });
    connect(documentationBrowser, &HelpBrowser::forwardAvailable, [&](bool available) {
      documentationForwardButton->setEnabled(available);
    });
    connect(documentationBackButton, &QPushButton::clicked, [&]() {
      documentationBrowser->backward();
    });
    connect(documentationForwardButton, &QPushButton::clicked, [&]() {
      documentationBrowser->forward();
    });
  }
  
  documentationBrowser->setSource(url);
  
  if (!documentationDock->isVisible()) {
    addDockWidget(Qt::RightDockWidgetArea, documentationDock);
    documentationDock->show();
    if (initialShow) {
      resizeDocks({documentationDock}, {kDockInitialWidth}, Qt::Horizontal);
    }
  }
}

void MainWindow::RunGitk() {
  // TODO: Allow choosing the project
  if (projects.empty()) {
    return;
  }
  const Project& currentProject = *(*projects.begin()).get();
  
  gitkProcess.reset(new QProcess());
  gitkProcess->setWorkingDirectory(currentProject.GetDir());
  gitkProcess->start("gitk");
}

void MainWindow::ShowProgramSettings() {
  Settings::Instance().ShowSettingsWindow(this);
  
  UpdateSearchClickActionLabels();
  projectTreeView.UpdateHighlighting();
}

void MainWindow::ShowAboutDialog() {
  AboutDialog dialog(this);
  dialog.exec();
}

void MainWindow::GotoDocumentLocation(const QString& url) {
  // Verify that the URL is of the expected format
  if (!url.startsWith(QStringLiteral("file://"))) {
    QMessageBox::warning(this, tr("Error"), tr("Invalid URL to jump to (does not start with file://)."));
    return;
  }
  
  // Extract path and offset from the URL
  QString path;
  int jumpLine;
  int jumpCol;
  SplitPathAndLineAndColumn(url.mid(7), &path, &jumpLine, &jumpCol);
  path = QFileInfo(path).canonicalFilePath();
  
  // If we can find this file among the open tabs, activate that tab.
  DocumentWidget* widget = nullptr;
  for (const std::pair<int, TabData>& item : tabs) {
    if (item.second.document->path() == path) {
      tabBar->setCurrentIndex(FindTabIndexForTabDataIndex(item.first));
      widget = item.second.widget;
      break;
    }
  }
  
  // If the file is not open in a tab, try to open the file.
  if (!widget) {
    Document* newDocument = new Document();
    if (!newDocument->Open(path)) {
      QMessageBox::warning(this, tr("Error"), tr("Cannot open file: %1").arg(path));
      delete newDocument;
      return;
    }
    
    AddTab(newDocument, QFileInfo(path).fileName(), &widget);
  }
  
  widget->setFocus();
  
  // Set the cursor to the given document offset.
  if (jumpLine >= 0) {
    widget->SetCursor(widget->MapLineColToDocumentLocation(jumpLine - 1, (jumpCol < 0) ? 0 : (jumpCol - 1)), false);
    widget->EnsureCursorIsInView(100);
  }
}

void MainWindow::GotoDocumentLocationFromBuildDir(const QString& url) {
  // TODO: Use the correct project
  std::shared_ptr<Project> currentProject = projects.front();
  
  GotoDocumentLocation(
      QStringLiteral("file://") +
      currentProject->GetBuildDir().absoluteFilePath(url));
}

void MainWindow::DocumentChanged() {
  Document* changedDocument = qobject_cast<Document*>(sender());
  if (changedDocument) {
    DocumentChanged(changedDocument);
  } else {
    qDebug() << "Error: Cannot cast the sender of a DocumentChanged() signal to Document*";
  }
}

void MainWindow::DocumentChanged(Document* changedDocument) {
  // Update tab bar items
  for (int i = 0; i < tabBar->count(); ++ i) {
    int tabDataIndex = tabBar->tabData(i).toInt();
    const TabData& tabData = tabs.at(tabDataIndex);
    
    QString tabText;
    if (tabData.document->HasUnsavedChanges()) {
      tabText = QStringLiteral("(*) ");
    }
    tabText += tabData.document->fileName();
    
    if (tabText != tabBar->tabText(i)) {
      tabBar->setTabText(i, tabText);
    }
  }
  
  statusTextLabel->setVisible(false);
  
  // Go through all other open files. For each one that includes the changed file,
  // mark it as to be reparsed on next activation.
  for (const std::pair<int, TabData>& item : tabs) {
    if (item.second.document.get() == changedDocument) {
      continue;
    }
    
    for (const auto& project : projects) {
      SourceFile* sourceFile = project->GetSourceFile(item.second.document->path());
      if (sourceFile && sourceFile->includedPaths.count(changedDocument->path()) > 0) {
        item.second.widget->SetReparseOnNextActivation();
        break;
      }
    }
  }
}

void MainWindow::CurrentOrOpenDocumentsChanged() {
  // Notify the ParseThreadPool about the current and open documents such that
  // it can prioritize those.
  QString currentDocumentPath;
  std::shared_ptr<Document> currentDocument = GetCurrentDocument();
  if (currentDocument) {
    currentDocumentPath = currentDocument->path();
  }
  
  QStringList openDocumentPaths;
  for (int i = 0, count = GetNumDocuments(); i < count; ++ i) {
    openDocumentPaths.push_back(GetDocument(i)->path());
  }
  
  ParseThreadPool::Instance().SetOpenAndCurrentDocuments(currentDocumentPath, openDocumentPaths);
}

void MainWindow::OpenProjectsChangedSlot() {
  bool isAnyProjectOpen = !projects.empty();
  newProjectAction->setEnabled(!isAnyProjectOpen);
  openProjectAction->setEnabled(!isAnyProjectOpen);
  closeProjectAction->setEnabled(isAnyProjectOpen);
  
  UpdateBuildTargetCombo();
  
  UpdateWindowTitle();
}

void MainWindow::UpdateWindowTitle() {
  QString projectOrApplicationName;
  if (projects.empty()) {
    projectOrApplicationName = tr("CIDE");
  } else {
    projectOrApplicationName = projects.front()->GetName();
  }
  
  TabData* tabData = GetCurrentTabData();
  if (tabData) {
    setWindowTitle(QStringLiteral("%1 - %2").arg(projectOrApplicationName).arg(tabData->document->path()));
  } else {
    setWindowTitle(projectOrApplicationName);
  }
}

void MainWindow::moveEvent(QMoveEvent* /*event*/) {
  TabData* currentTabData = GetCurrentTabData();
  if (currentTabData) {
    currentTabData->widget->Moved();
  }
  searchBar->Moved();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  bool haveUnsavedDocument = false;
  QString unsavedDocuments;
  
  for (int i = 0; i < tabBar->count(); ++ i) {
    int tabDataIndex = tabBar->tabData(i).toInt();
    const TabData& tabData = tabs.at(tabDataIndex);
    if (tabData.document->HasUnsavedChanges()) {
      haveUnsavedDocument = true;
      
      if (!unsavedDocuments.isEmpty()) {
        unsavedDocuments += QStringLiteral("\n");
      }
      
      if (tabData.document->path().isEmpty()) {
        unsavedDocuments += tr("Unnamed document");
      } else {
        unsavedDocuments += tabData.document->path();
      }
    }
  }
  
  if (haveUnsavedDocument) {
    QMessageBox::StandardButton response = QMessageBox::warning(this, tr("Exit CIDE"), tr("The following document(s) have unsaved changes. Would you like to save them?\n\n%1").arg(unsavedDocuments), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (response == QMessageBox::Cancel) {
      event->ignore();
      return;
    } else if (response == QMessageBox::Yes) {
      for (int i = 0; i < tabBar->count(); ++ i) {
        int tabDataIndex = tabBar->tabData(i).toInt();
        const TabData& tabData = tabs.at(tabDataIndex);
        Document* document = tabData.document.get();
        if (document->HasUnsavedChanges()) {
          if (!Save(&tabData, tabData.document->path())) {
            event->ignore();
            return;
          }
        }
      }
    }
  }
  
  // Save the projects (since run configurations, which are a part of the project settings, may have changed)
  for (const auto& project : projects) {
    project->Save(project->GetYAMLFilePath());
  }
  
  // Save the session (list of open documents)
  SaveSession();
  
  // As we are exiting normally, remove all crash backups.
  CrashBackup::Instance().DeleteAllBackups();
  
  event->accept();
}

void MainWindow::AddTab(Document* document, const QString& name, DocumentWidget** newWidget) {
  connect(document, &Document::Changed, this, QOverload<>::of(&MainWindow::DocumentChanged));
  
  TabData newTabData;
  
  newTabData.document.reset(document);
  
  newTabData.container = new DocumentWidgetContainer(newTabData.document, this);
  newTabData.widget = newTabData.container->GetDocumentWidget();
  if (newWidget) {
    *newWidget = newTabData.widget;
  }
  newTabData.widget->setFocus();
  connect(newTabData.widget, &DocumentWidget::CursorMoved, this, &MainWindow::DisplayCursorPosition);
  connect(newTabData.widget, &DocumentWidget::CursorMoved, searchBar, &SearchBar::CursorMoved);
  
  tabs[nextTabDataIndex] = newTabData;
  
  documentLayout->addWidget(newTabData.container);
  documentLayout->setCurrentWidget(newTabData.container);
  
  // Disconnect the currentChanged() handler while adding a new tab. This is
  // because tabBar->addTab() changes the current tab to the new one, but its
  // tabData has not been set at this point yet.
  disconnect(tabBar, &QTabBar::currentChanged, this, &MainWindow::CurrentTabChanged);
  disconnect(tabBar, &QTabBar::currentChanged, searchBar, &SearchBar::CurrentDocumentChanged);
  
  int newTabIndex = -1;
  // If the new tab is the header resp. source file corresponding to a source resp. header
  // file that is already opened, then open it next to this tab.
  QString correspondingPath = QFileInfo(FindCorrespondingHeaderOrSource(document->path(), projects)).canonicalFilePath();
  if (!correspondingPath.isEmpty()) {
    // We found a corresponding path in the file system, search for it among the open tabs
    for (const std::pair<int, TabData>& item : tabs) {
      if (item.second.document->path() == correspondingPath) {
        int correspondingIndex = FindTabIndexForTabDataIndex(item.first);
        if (correspondingIndex >= 0) {
          bool newFileIsHeader = GuessIsHeader(document->path(), nullptr);
          if (Settings::Instance().GetSourceLeftOfHeaderOrdering()) {
            newTabIndex = tabBar->insertTab(newFileIsHeader ? (correspondingIndex + 1) : correspondingIndex, name);
          } else {
            newTabIndex = tabBar->insertTab(newFileIsHeader ? correspondingIndex : (correspondingIndex + 1), name);
          }
          break;
        }
      }
    }
  }
  if (newTabIndex < 0) {
    newTabIndex = tabBar->addTab(name);
  }
  tabBar->setTabToolTip(newTabIndex, document->path());
  tabBar->setTabData(newTabIndex, QVariant::fromValue<int>(nextTabDataIndex));
  tabBar->setCurrentIndex(newTabIndex);
  
  connect(tabBar, &QTabBar::currentChanged, this, &MainWindow::CurrentTabChanged);
  connect(tabBar, &QTabBar::currentChanged, searchBar, &SearchBar::CurrentDocumentChanged);
  CurrentTabChanged(tabBar->currentIndex());
  searchBar->CurrentDocumentChanged();
  
  ++ nextTabDataIndex;
}

MainWindow::TabData* MainWindow::GetCurrentTabData() {
  if (tabBar->currentIndex() == -1) {
    return nullptr;
  } else {
    return &tabs.at(tabBar->tabData(tabBar->currentIndex()).toInt());
  }
}

const MainWindow::TabData* MainWindow::GetCurrentTabData() const {
  if (tabBar->currentIndex() == -1) {
    return nullptr;
  } else {
    return &tabs.at(tabBar->tabData(tabBar->currentIndex()).toInt());
  }
}

int MainWindow::FindTabIndexForTabDataIndex(int tabDataIndex) {
  for (int i = 0; i < tabBar->count(); ++ i) {
    if (tabBar->tabData(i).toInt() == tabDataIndex) {
      return i;
    }
  }
  return -1;
}

int MainWindow::FindTabIndexForTabData(const TabData* tabData) {
  for (int i = 0; i < tabBar->count(); ++ i) {
    if (tabs.at(tabBar->tabData(i).toInt()).widget == tabData->widget) {
      return i;
    }
  }
  return -1;
}

bool MainWindow::Save(const TabData* tabData, const QString& oldPath) {
  Document* document = tabData->document.get();
  if (document->path().isEmpty()) {
    return SaveAs(tabData);
  } else {
    if (!document->Save(document->path())) {
      QMessageBox::warning(this, tr("Error"), tr("Failed to write file: %1").arg(document->path()));
      return false;
    }
    if (!oldPath.isEmpty()) {
      CrashBackup::Instance().RemoveBackup(oldPath);
    }
    tabData->container->SetMessage(DocumentWidgetContainer::MessageType::ExternalModificationNotification, QStringLiteral(""));
    tabBar->setTabToolTip(FindTabIndexForTabData(tabData), document->path());
    tabData->widget->CheckFileType();
    DocumentChanged(document);
    emit DocumentSaved();
    return true;
  }
}

bool MainWindow::SaveAs(const TabData* tabData) {
  QSettings settings;
  QString defaultPath;
  if (tabData && !tabData->document->path().isEmpty()) {
    defaultPath = QFileInfo(tabData->document->path()).dir().path();
  } else {
    defaultPath = settings.value("last_file_dir").toString();
  }
  
  QString path = QFileDialog::getSaveFileName(
      this,
      tr("Save document"),
      defaultPath,
      tr("Text files (*)"));
  if (path.isEmpty()) {
    return false;
  }
  settings.setValue("last_file_dir", QFileInfo(path).absoluteDir().absolutePath());
  
  if (QFileInfo(path).exists()) {
    if (QMessageBox::question(
        this,
        tr("Save document"),
        tr("The target file stated below exists already. Do you want to replace it?\n\n%1").arg(path),
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
      return false;
    }
  }
  
  Document* document = tabData->document.get();
  QString oldPath = document->path();
  document->setPath(path);
  int tabIndex = FindTabIndexForTabData(tabData);
  if (tabIndex >= 0) {
    tabBar->setTabText(tabIndex, QFileInfo(path).fileName());
  }
  
  return Save(tabData, oldPath);
}

void MainWindow::SaveSession() {
  QSettings settings;
  settings.beginWriteArray("session");
  settings.remove("");  // remove previous entries in this group
  for (int i = 0; i < tabBar->count(); ++ i) {
    int tabDataIndex = tabBar->tabData(i).toInt();
    const TabData& tabData = tabs.at(tabDataIndex);
    Document* document = tabData.document.get();
    
    settings.setArrayIndex(i);
    settings.setValue("path", document->path());
    settings.setValue("yScroll", tabData.widget->GetYScroll());
    
    QList<QVariant> bookmarkLines;
    for (int line = 0, lineCount = document->LineCount(); line < lineCount; ++ line) {
      if (document->lineAttributes(line) & static_cast<int>(LineAttribute::Bookmark)) {
        bookmarkLines.push_back(line);
      }
    }
    settings.setValue("bookmarks", bookmarkLines);
  }
  settings.endArray();
}

void MainWindow::LoadSession() {
  QSettings settings;
  int size = settings.beginReadArray("session");
  for (int i = 0; i < size; ++ i) {
    settings.setArrayIndex(i);
    
    QString path = settings.value("path").toString();
    if (!path.isEmpty()) {
      Open(path);
      
      QString canonicalFilePath = QFileInfo(path).canonicalFilePath();
      for (const std::pair<int, TabData>& item : tabs) {
        Document* document = item.second.document.get();
        if (document->path() == canonicalFilePath) {
          item.second.widget->SetYScroll(settings.value("yScroll").toInt());
          
          int lineCount = document->LineCount();
          QList<QVariant> bookmarks = settings.value("bookmarks").toList();
          for (QVariant& bookmark : bookmarks) {
            int line = bookmark.toInt();
            if (line < lineCount) {
              document->AddLineAttributes(line, static_cast<int>(LineAttribute::Bookmark));
            }
          }
          
          break;
        }
      }
    }
  }
  settings.endArray();
}

void MainWindow::ClearBuildIssues() {
  buildStdout.clear();
  buildStderr.clear();
  buildOutputText.clear();
  cachedPartialIssueText.clear();
  buildErrors = 0;
  buildWarnings = 0;
  
  delete buildOutputTextLabel;
  buildOutputTextLabel = nullptr;
  
  lastAddedBuildIssueLabel = nullptr;
  
  delete buildIssuesWidget;
  buildIssuesWidget = new WidgetWithRightClickSignal();
  connect(buildIssuesWidget, &WidgetWithRightClickSignal::rightClicked, [&](QPoint /*pos*/, QPoint globalPos) {
    viewAsTextMenu->popup(globalPos);
  });
  QPalette palette = buildIssuesWidget->palette();
  palette.setColor(QPalette::Background, qRgb(255, 255, 255));
  buildIssuesWidget->setPalette(palette);
  buildIssuesLayout = new QVBoxLayout();
  buildIssuesLayout->setContentsMargins(0, 0, 0, 0);
  buildIssuesLayout->setSpacing(16);
  buildIssuesLayout->addStretch(1);
  buildIssuesWidget->setLayout(buildIssuesLayout);
  
  buildIssuesRichTextScrollArea->setWidget(buildIssuesWidget);
}

void MainWindow::AddBuildIssue(const QString& text, bool isError) {
  QLabel* issueLabel = new QLabel(text);
  issueLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
  issueLabel->setFont(Settings::Instance().GetDefaultFont());
  issueLabel->setStyleSheet(
      QStringLiteral("QLabel{border-radius:5px;background-color:%1;padding:5px;}")
          .arg(isError ? QStringLiteral("#ffe6e6") : QStringLiteral("#e6ffe6")));
  issueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  connect(issueLabel, &QLabel::linkActivated, this, &MainWindow::GotoDocumentLocationFromBuildDir);
  // Insert the widget before the stretch at the end
  buildIssuesLayout->insertWidget(buildIssuesLayout->count() - 1, issueLabel);
  
  buildIssuesWidget->resize(buildIssuesWidget->sizeHint());
  
  if (buildErrors == 0 && buildWarnings == 0) {
    ViewBuildOutput();
  }
  if (isError) {
    ++ buildErrors;
  } else {
    ++ buildWarnings;
  }
  
  lastAddedBuildIssueLabel = issueLabel;
}

void MainWindow::AppendBuildIssue(const QString& text) {
  if (!lastAddedBuildIssueLabel) {
    qDebug() << "Error: attempting to append to a build issue, but no issue has been created yet";
    AddBuildIssue(text, true);
    return;
  }
  
  lastAddedBuildIssueLabel->setText(lastAddedBuildIssueLabel->text() + QStringLiteral("<br/>") + text);
  
  buildIssuesWidget->resize(buildIssuesWidget->sizeHint());
}
