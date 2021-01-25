// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>
#include <unordered_map>

#include <QFileIconProvider>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

#include "cide/find_and_replace_in_files.h"
#include "cide/util.h"

class DockWidgetWithClosedSignal;
class MainWindow;
class Project;
class QAction;
class QItemDelegate;
class QMenu;
class QTreeWidgetItem;
class TreeWidgetWithRightClickSignal;

struct ProjectGitStatus {
  enum class FileStatus {
    Modified = 0,
    Untracked,

    NotModified,
    Invalid
  };

  QString branchName;

  /// Maps the full canonical path of the file to its status
  std::unordered_map<QString, FileStatus> fileStatuses;
};

typedef std::unordered_map<QString, std::shared_ptr<ProjectGitStatus>> ProjectGitStatusMap;

/// Displays a file tree of the opened projects.
class ProjectTreeView : public QObject {
 Q_OBJECT
 friend class GitStatusWorkerThread;
 public:
  ~ProjectTreeView();
  void Initialize(MainWindow* mainWindow, QAction* showProjectFilesDockAction, FindAndReplaceInFiles* findAndReplaceInFiles);
  
  inline DockWidgetWithClosedSignal* GetDockWidget() { return dock; }
  
 public slots:
  void SetFocus();
  
  void Reconfigure();
  void ProjectSettings();
  void CloseProject();
  
  bool SelectCurrent();
  
  void UpdateProjects();
  void UpdateHighlighting();
  void ReloadDirectory(QTreeWidgetItem* dirItem);
  
  void ProjectMayRequireReconfiguration();
  void ProjectConfigured();
  
  void FileWatcherNotification(const QString& path);
  void GitWatcherNotification(const QString& path);
  void GitUpdate();
  
  void ItemExpanded(QTreeWidgetItem* item);
  void ItemCollapsed(QTreeWidgetItem* item);
  void ItemDoubleClicked(QTreeWidgetItem* item);
  void ItemRightClicked(QTreeWidgetItem* item, QPoint pos);
  void RightClicked(QPoint pos);
  
  void CreateClass();
  void CreateFile();
  void CreateFolder();
  void SearchInFolder();
  void Rename();
  void DeleteSelectedItems();
  
  void UpdateGitStatus();
  
 private slots:
  void HandleGitStatusResults(const ProjectGitStatusMap& projectGitStatuses);

 private:
  QTreeWidgetItem* InsertItemFor(QTreeWidgetItem* parentFolder, const QString& path, QTreeWidgetItem* prevItem = nullptr);
  QString GetItemPath(QTreeWidgetItem* item);
  QTreeWidgetItem* GetItemForPath(const QString& path, bool expandCollapsedDirs);
  std::shared_ptr<Project> GetProjectForItem(QTreeWidgetItem* item);
  void ApplyItemStyles(QTreeWidgetItem* item, const QString& canonicalPath);
  void SetProjectMayRequireReconfiguration(QTreeWidgetItem* item, bool enable);
  
  ProjectGitStatus::FileStatus GetFileStatusFor(QTreeWidgetItem* item);
  
  
  QMenu* contextMenu;
  QAction* reconfigureAction;
  QAction* projectSettingsAction;
  QAction* closeProjectAction;
  QAction* createClassAction;
  QAction* createFileAction;
  QAction* createFolderAction;
  QAction* searchInFolderAction;
  QAction* renameAction;
  QAction* deleteAction;
  
  QTreeWidgetItem* rightClickedItem = nullptr;
  
  DockWidgetWithClosedSignal* dock;
  TreeWidgetWithRightClickSignal* tree;
  QFileIconProvider iconProvider;
  
  QColor projectNeedingReconfigurationColor = qRgb(255, 255, 180);
  
  ProjectGitStatusMap projectGitStatuses;
  QFileSystemWatcher gitWatcher;
  QTimer gitUpdateTimer;
  
  QFileSystemWatcher watcher;
  
  FindAndReplaceInFiles* findAndReplaceInFiles;
  MainWindow* mainWindow;
};
