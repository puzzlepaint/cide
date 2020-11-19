// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>
#include <unordered_map>

#include <QAction>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedLayout>

#include "cide/document.h"
#include "cide/document_widget_container.h"
#include "cide/find_and_replace_in_files.h"
#include "cide/project.h"
#include "cide/project_tree_view.h"
#include "cide/run_gdb.h"
#include "cide/tab_bar.h"

class BuildTargetSelector;
class DockWidgetWithClosedSignal;
class QComboBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QToolButton;
class QTreeWidget;
class SearchBar;
class WidgetWithRightClickSignal;

class MainWindow : public QMainWindow {
 Q_OBJECT
 public:
  MainWindow(QWidget* parent = nullptr);
  
  /// Attempts to load a project given the path to its project YAML file.
  /// Returns true if successful.
  bool LoadProject(const QString& path, QWidget* parent);
  
  /// Returns the Document in the current tab. May return null.
  std::shared_ptr<Document> GetCurrentDocument() const;
  
  /// Returns the DocumentWidget in the current tab. May return null.
  DocumentWidget* GetCurrentDocumentWidget() const;
  
  /// Returns the DocumentWidget for the given document, or null if the document
  /// is not open.
  DocumentWidget* GetWidgetForDocument(Document* document) const;
  
  /// Returns the current project. This is the project that the current file
  /// is probably associated with, or an unspecified open project if there is
  /// no current file, or null if no project is open.
  std::shared_ptr<Project> GetCurrentProject();
  
  /// Returns the number of open documents.
  inline int GetNumDocuments() const { return tabBar->count(); }
  
  /// Returns the open document with the given index in [0, GetNumOpenDocuments()[.
  std::shared_ptr<Document> GetDocument(int index);
  
  /// Returns whether the given file is open in any tab.
  bool IsFileOpen(const QString& canonicalPath) const;
  
  /// If the file is open in any tab, returns its Document and DocumentWidget.
  /// Returns true if successful, false otherwise.
  bool GetDocumentAndWidgetForPath(const QString& canonicalPath, Document** document, DocumentWidget** widget) const;
  
  inline std::vector<std::shared_ptr<Project>>& GetProjects() { return projects; }
  
  inline const QString& GetCurrentFrameCanonicalPath() const { return currentFrameCanonicalPath; }
  inline int GetCurrentFrameLine() const { return currentFrameLine; }
  
  /// Notifies the main window about a parse iteration being finished for the document.
  void DocumentParsed(Document* document);
  
 signals:
  void ProjectOpened();
  void OpenProjectsChanged();
  
  void CurrentDocumentChanged();
  
  void DocumentSaved();
  void DocumentClosed();
  
 public slots:
  void New();
  void Open();
  void Open(const QString& path);
  void Save();
  void SaveAs();
  void CloseDocument();
  void CloseDocument(int tabIndex);
  void CloseAllOtherDocuments(int tabIndex);
  void CloseAllDocuments();
  
  void DisplayCursorPosition(int line, int col);
  
  void SwitchHeaderSource();
  
  void UpdateBuildTargetSelector();
  void SelectedBuildTargetsChanged();
  
  void GlobalSymbolSearch();
  void SearchLocalContexts();
  void SearchFiles();
  void UpdateSearchClickActionLabels();
  
  void UpdateIndexingStatus();
  void SetStatusText(const QString& text);
  
  void BuildCurrentTarget();
  void TryParseBuildStdout();
  void TryParseBuildStderr();
  void ParseBuildOutputForError(const QString& line);
  void ViewBuildOutput();
  void ViewBuildOutputAsText();
  void BuildOutputWidgetClosed();
  void StopBuilding();
  void FinishedBuilding(const QString& statusMessage);
  
  void Reconfigure();
  void ReconfigureProject(const std::shared_ptr<Project>& project, QWidget* parent);
  void ShowProjectFilesDock();
  void ShowProjectSettings();
  void ParseSettingsForCurrentFile();
  void ParseIssuesForCurrentFile();
  /// Shows the new-project dialog.
  bool NewProject(QWidget* parent);
  /// Lets the user open a project.
  bool OpenProject(QWidget* parent);
  /// Closes the currently open project.
  void CloseProject();
  
  void DebugCurrentProject();
  void ShowRunDock();
  void RunWidgetClosed();
  void CwdButtonClicked();
  void SetRunCwd(const QString& cwd);
  void EditCallClicked();
  void RunPauseClicked();
  void StopClicked();
  void ThreadChanged(int index);
  void ProgramFrameActivated(QListWidgetItem* item);
  void EvaluateExpression();
  
  void ProgramStarted();
  void ProgramInterrupted();
  void ProgramResumed();
  void ProgramStopped(int exitCode);
  void ProgramThreadListUpdated();
  void ProgramStackTraceUpdated();
  
  void ReloadFile();
  void GoToLeftTab();
  void GoToRightTab();
  void CurrentTabChanged(int index);
  
  void ShowDocumentationDock(const QUrl& url);
  
  void RunGitk();
  void ShowProgramSettings();
  
  void ShowAboutDialog();
  
  /// Expects an URL like "file://filepath:line:column". If the file is open in
  /// a tab, activates the tab and goes to the given file location. If the file
  /// is not open, tries to open it.
  void GotoDocumentLocation(const QString& url);
  
  void GotoDocumentLocationFromBuildDir(const QString& url);
  
  /// Called when any document has changed.
  void DocumentChanged();
  void DocumentChanged(Document* changedDocument);
  
  /// Called when either the current document has changed, or any document got
  /// opened or closed.
  void CurrentOrOpenDocumentsChanged();
  
  /// Called when any project has been opened or closed.
  void OpenProjectsChangedSlot();
  
  void UpdateWindowTitle();
  
 protected:
  void moveEvent(QMoveEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  
 private:
  struct TabData {
    std::shared_ptr<Document> document;
    DocumentWidget* widget;
    DocumentWidgetContainer* container;
  };
  
  void AddTab(Document* document, const QString& name, DocumentWidget** newWidget = nullptr);
  TabData* GetCurrentTabData();
  const TabData* GetCurrentTabData() const;
  int FindTabIndexForTabDataIndex(int tabDataIndex);
  int FindTabIndexForTabData(const TabData* tabData);
  
  bool Save(const TabData* tabData, const QString& oldPath);
  bool SaveAs(const TabData* tabData);
  
  void SaveSession();
  void LoadSession();
  
  void ClearBuildIssues();
  void AddBuildIssue(const QString& text, bool isError);
  void AppendBuildIssue(const QString& text);
  
  
  QAction* saveAction;
  QAction* saveAsAction;
  QAction* reloadFileAction;
  QAction* closeAction;
  
  QAction* currentFileParseSettingsAction;
  QAction* currentFileParseIssuesAction;
  QAction* newProjectAction;
  QAction* openProjectAction;
  QAction* closeProjectAction;
  
  QLabel* statusTextLabel;
  QLabel* lineColLabel;
  
  BuildTargetSelector* buildTargetSelector;
  QToolButton* searchBarModeButton;
  SearchBar* searchBar;
  QAction* searchFilesClickAction;
  QAction* searchLocalContextsClickAction;
  QAction* globalSymbolSearchClickAction;
  
  TabBar* tabBar;
  /// Maps tabDataIndex -> TabData.
  std::unordered_map<int, TabData> tabs;
  int nextTabDataIndex;
  
  std::vector<std::shared_ptr<Project>> projects;
  
  std::shared_ptr<QProcess> gitkProcess;
  
  // Indexing
  int numIndexingRequestsCreated = 0;
  
  // Building
  enum class BuildParseMode {
    Ninja = 0,
    Make,
    Unknown
  };
  std::shared_ptr<QProcess> buildProcess = nullptr;
  BuildParseMode buildParseMode;
  QProgressBar* buildProgressBar = nullptr;
  QPushButton* buildStopButton = nullptr;
  QPushButton* buildViewOutputButton = nullptr;
  QMenu* viewAsTextMenu = nullptr;
  QByteArray buildStdout;
  QByteArray buildStderr;
  QString buildOutputText;
  QString cachedPartialIssueText;
  int buildErrors;
  int buildWarnings;
  
  // Build dock widget
  DockWidgetWithClosedSignal* buildOutputWidget;
  QScrollArea* buildIssuesRichTextScrollArea;
  WidgetWithRightClickSignal* buildIssuesWidget;
  QVBoxLayout* buildIssuesLayout;
  QLabel* buildOutputTextLabel = nullptr;
  QLabel* lastAddedBuildIssueLabel = nullptr;
  
  // Run dock widget
  QAction* showRunDockAction;
  DockWidgetWithClosedSignal* runWidget;
  QComboBox* threadDropdown;
  QListWidget* stackFramesList;
  QLineEdit* expressionEdit;
  QPushButton* evaluateExpressionButton;
  QPushButton* cwdButton;
  QLineEdit* callEdit;
  QPushButton* runPauseButton;
  QPushButton* stopButton;
  QString runCwd;
  
  // Running
  GDBRunner gdbRunner;
  std::vector<StackFrame> currentStackFrames;
  /// Canonical path of the currently selected frame's file. May be empty if unknown or no frame is selected.
  QString currentFrameCanonicalPath;
  /// Line of the currently selected frame (zero-based). May be -1 if unknown or no frame is selected.
  int currentFrameLine;
  /// Address of the current frame (required to reference this frame in gdb).
  QString currentFrameAddr;
  /// Level of the current frame (required to reference this frame in gdb).
  int currentFrameLevel;
  
  // Project tree view
  ProjectTreeView projectTreeView;
  QAction* showProjectFilesDockAction;
  
  // Find-and-replace in files dock widget
  FindAndReplaceInFiles findAndReplaceInFiles;
  
  // Documentation dock widget
  QDockWidget* documentationDock = nullptr;
  HelpBrowser* documentationBrowser;
  QPushButton* documentationBackButton;
  QPushButton* documentationForwardButton;
  
  /// Contains all document widgets. Indices correspond to those in tabBar.
  QStackedLayout* documentLayout;
};
