// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/project_tree_view.h"

#include <unordered_set>

#include <git2.h>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QInputDialog>
#include <QItemDelegate>
#include <QMessageBox>
#include <QMetaType>
#include <QTreeWidget>

#include "cide/create_class.h"
#include "cide/git_diff.h"
#include "cide/main_window.h"
#include "cide/project_settings.h"
#include "cide/util.h"

Q_DECLARE_METATYPE(ProjectGitStatusMap);
struct MetaTypeRegistrator {
  MetaTypeRegistrator() {
    qRegisterMetaType<ProjectGitStatusMap>();
  }
};

/// Item delegate that uses a compact sizeHint() to avoid excessive item spacing.
class ItemDelegate : public QItemDelegate {
 Q_OBJECT
 public:
  ItemDelegate(const QFontMetrics& fontMetrics, QObject* parent = Q_NULLPTR) :
      QItemDelegate(parent),
      fontMetrics(fontMetrics) {}
  
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QSize oSize = QItemDelegate::sizeHint(option, index);
    constexpr int lineSpacing = 2;
    oSize.setHeight(fontMetrics.ascent() + fontMetrics.descent() + lineSpacing);
    return oSize;
  }
  
 private:
  QFontMetrics fontMetrics;
};


/// Worker thread to query the git status for the open projects
class GitStatusWorkerThread : public QThread {
 Q_OBJECT
 public:
  GitStatusWorkerThread(const std::vector<QString>& projectYAMLFilePaths, QObject* parent = nullptr)
      : QThread(parent),
        projectYAMLFilePaths(projectYAMLFilePaths) {
    static MetaTypeRegistrator typeRegistrator;
  }
  
  void run() override {
    ProjectGitStatusMap projectGitStatuses;
    
    for (const auto& projectYAMLFilePath : projectYAMLFilePaths) {
      QString projectPath = QFileInfo(projectYAMLFilePath).dir().path();
      
      // Open repository
      git_repository* repo = nullptr;
      int result = git_repository_open_ext(&repo, projectPath.toLocal8Bit(), GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr);
      std::shared_ptr<git_repository> repo_deleter(repo, [&](git_repository* repo){ git_repository_free(repo); });
      if (result == GIT_ENOTFOUND) {
        // There is no git repository at the project path.
        continue;
      } else if (result != 0) {
        qDebug() << "Failed to open the git repository at" << projectPath << "(some possible reasons: repo corruption or system errors), libgit2 error code:" << result;
        continue;
      }
      
      if (git_repository_is_bare(repo)) {
        continue;
      }
      
      std::shared_ptr<ProjectGitStatus> projectStatus(new ProjectGitStatus());
      projectGitStatuses[projectPath] = projectStatus;
      
      // Get the branch name for HEAD
      git_reference* head = nullptr;
      result = git_repository_head(&head, repo);
      std::shared_ptr<git_reference> head_deleter(head, [&](git_reference* ref){ git_reference_free(ref); });
      
      if (result == GIT_EUNBORNBRANCH || result == GIT_ENOTFOUND) {
        projectStatus->branchName = tr("(not on any branch)");
      } else if (result != 0) {
        qDebug() << "There was an error getting the branch for the git repository at" << projectPath << ", libgit2 error code:" << result;
        continue;
      } else {
        projectStatus->branchName = QString::fromUtf8(git_reference_shorthand(head));
      }
      
      // Get the working directory of the repository. Returned paths will be relative to this directory.
      QDir workDir(QString::fromLocal8Bit(git_repository_workdir(repo)));
      
      // Query repository status (i.e.: lists of untracked files in working copy, and modified files between HEAD->index and index->worktree).
      // We display the merged HEAD<->index and index<->worktree differences here.
      git_status_options opts;
      memset(&opts, 0, sizeof(git_status_options));
      opts.version = GIT_STATUS_OPTIONS_INIT;
      opts.show  = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
      opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                   GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;
      git_status_list* status = nullptr;
      result = git_status_list_new(&status, repo, &opts);
      std::shared_ptr<git_status_list> status_deleter(status, [&](git_status_list* status){ git_status_list_free(status); });
      if (result != 0) {
        qDebug() << "There was an error getting the status list for the git repository at" << projectPath << ", libgit2 error code:" << result;
        continue;
      }
      
      int numStatusItems = git_status_list_entrycount(status);
      for (int i = 0; i < numStatusItems; ++ i) {
        const git_status_entry* s = git_status_byindex(status, i);
        
        if (s->index_to_workdir && s->status == GIT_STATUS_WT_NEW) {
          // Untracked file.
          QString filePath = QFileInfo(workDir.filePath(QString::fromLocal8Bit(s->index_to_workdir->old_file.path))).canonicalFilePath();
          projectStatus->fileStatuses[filePath] = ProjectGitStatus::FileStatus::Untracked;
        } else if (s->status == GIT_STATUS_CURRENT) {
          // No changes to this file.
          continue;
        } else if ((s->status & GIT_STATUS_INDEX_NEW) ||
                   (s->status & GIT_STATUS_INDEX_MODIFIED) ||
                   (s->status & GIT_STATUS_INDEX_TYPECHANGE)) {
          // Modified file (HEAD<->index).
          const char* old_path = s->head_to_index->old_file.path;
          const char* new_path = s->head_to_index->new_file.path;
          QString filePath = QFileInfo(workDir.filePath(QString::fromLocal8Bit(old_path ? old_path : new_path))).canonicalFilePath();
          projectStatus->fileStatuses[filePath] = ProjectGitStatus::FileStatus::Modified;
        } else if (s->index_to_workdir &&
                  ((s->status & GIT_STATUS_WT_MODIFIED) ||
                   (s->status & GIT_STATUS_WT_TYPECHANGE) ||
                   (s->status & GIT_STATUS_WT_RENAMED))) {
          // Modified file (index<->worktree).
          const char* old_path = s->index_to_workdir->old_file.path;
          const char* new_path = s->index_to_workdir->new_file.path;
          QString filePath = QFileInfo(workDir.filePath(QString::fromLocal8Bit(old_path ? old_path : new_path))).canonicalFilePath();
          projectStatus->fileStatuses[filePath] = ProjectGitStatus::FileStatus::Modified;
        }
      }
    }
    
    emit ResultReady(projectGitStatuses);
  }
  
 signals:
  void ResultReady(ProjectGitStatusMap projectGitStatuses);
  
 private:
  std::vector<QString> projectYAMLFilePaths;
};


ProjectTreeView::~ProjectTreeView() {
  delete contextMenu;
}

void ProjectTreeView::Initialize(MainWindow* mainWindow, QAction* showProjectFilesDockAction, FindAndReplaceInFiles* findAndReplaceInFiles) {
  this->findAndReplaceInFiles = findAndReplaceInFiles;
  this->mainWindow = mainWindow;
  
  iconProvider.setOptions(QFileIconProvider::DontUseCustomDirectoryIcons);
  
  contextMenu = new QMenu();
  reconfigureAction = contextMenu->addAction(tr("Reconfigure"), this, &ProjectTreeView::Reconfigure);
  projectSettingsAction = contextMenu->addAction(tr("Project settings..."), this, &ProjectTreeView::ProjectSettings);
  closeProjectAction = contextMenu->addAction(tr("Close project"), this, &ProjectTreeView::CloseProject);
  contextMenu->addSeparator();
  contextMenu->addAction(tr("Select current document"), this, &ProjectTreeView::SelectCurrent);
  contextMenu->addSeparator();
  createClassAction = contextMenu->addAction(tr("Create class..."), this, &ProjectTreeView::CreateClass);
  createFileAction = contextMenu->addAction(tr("Create file..."), this, &ProjectTreeView::CreateFile);
  createFolderAction = contextMenu->addAction(tr("Create folder..."), this, &ProjectTreeView::CreateFolder);
  searchInFolderAction = contextMenu->addAction(tr("Search in folder..."), this, &ProjectTreeView::SearchInFolder);
  renameAction = contextMenu->addAction(tr("Rename..."), this, &ProjectTreeView::Rename);
  deleteAction = contextMenu->addAction(tr("Delete"), this, &ProjectTreeView::DeleteSelectedItems);
  
  dock = new DockWidgetWithClosedSignal(tr("Project files"));
  connect(dock, &DockWidgetWithClosedSignal::closed, [=]() {
    showProjectFilesDockAction->setChecked(false);
  });
  
  tree = new TreeWidgetWithRightClickSignal();
  tree->setColumnCount(1);
  tree->setHeaderHidden(true);
  tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  #ifndef _WIN32
    tree->setItemDelegate(new ItemDelegate(tree->fontMetrics(), tree));
  #endif
  
  connect(tree, &QTreeWidget::itemExpanded, this, &ProjectTreeView::ItemExpanded);
  connect(tree, &QTreeWidget::itemCollapsed, this, &ProjectTreeView::ItemCollapsed);
  connect(tree, &QTreeWidget::itemDoubleClicked, this, &ProjectTreeView::ItemDoubleClicked);
  connect(tree, &TreeWidgetWithRightClickSignal::itemRightClicked, this, &ProjectTreeView::ItemRightClicked);
  connect(tree, &TreeWidgetWithRightClickSignal::rightClicked, this, &ProjectTreeView::RightClicked);
  
  QWidget* container = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(tree);
  container->setLayout(layout);
  dock->setWidget(container);
  mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);
  
  connect(&watcher, &QFileSystemWatcher::directoryChanged, this, &ProjectTreeView::FileWatcherNotification);
  gitUpdateTimer.setSingleShot(true);
  connect(&gitUpdateTimer, &QTimer::timeout, this, &ProjectTreeView::GitUpdate);
  connect(&gitWatcher, &QFileSystemWatcher::fileChanged, this, &ProjectTreeView::GitWatcherNotification);
  
  connect(mainWindow, &MainWindow::OpenProjectsChanged, this, &ProjectTreeView::UpdateProjects);
  connect(mainWindow, &MainWindow::CurrentDocumentChanged, this, &ProjectTreeView::UpdateHighlighting);
  connect(mainWindow, &MainWindow::DocumentSaved, this, &ProjectTreeView::UpdateGitStatus);
  connect(mainWindow, &MainWindow::DocumentClosed, this, &ProjectTreeView::UpdateHighlighting);
}

void ProjectTreeView::SetFocus() {
  tree->setFocus();
}

void ProjectTreeView::Reconfigure() {
  if (rightClickedItem) {
    std::shared_ptr<Project> project = GetProjectForItem(rightClickedItem);
    if (project) {
      mainWindow->ReconfigureProject(project, mainWindow);
    }
  }
}

void ProjectTreeView::ProjectSettings() {
  if (rightClickedItem) {
    std::shared_ptr<Project> project = GetProjectForItem(rightClickedItem);
    if (project) {
      ProjectSettingsDialog dialog(project, mainWindow);
      dialog.exec();
    }
  }
}

void ProjectTreeView::CloseProject() {
  if (rightClickedItem) {
    std::shared_ptr<Project> project = GetProjectForItem(rightClickedItem);
    if (project) {
      if (QMessageBox::question(mainWindow, tr("Close project"), tr("Are you sure that you want to close project %1?").arg(project->GetName()), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
        return;
      }
      
      mainWindow->CloseProject();
    }
  }
}

bool ProjectTreeView::SelectCurrent() {
  const auto& document = mainWindow->GetCurrentDocument();
  if (!document) {
    return false;
  }
  
  QTreeWidgetItem* item = GetItemForPath(document->path(), true);
  if (item) {
    tree->setCurrentItem(item);
  }
  return item != nullptr;
}

void ProjectTreeView::UpdateProjects() {
  tree->clear();
  if (!watcher.files().isEmpty()) {
    watcher.removePaths(watcher.files());
  }
  if (!gitWatcher.files().isEmpty()) {
    gitWatcher.removePaths(gitWatcher.files());
  }
  
  QTreeWidgetItem* lastItem = nullptr;
  for (const auto& project : mainWindow->GetProjects()) {
    connect(project.get(), &Project::ProjectMayRequireReconfiguration, this, &ProjectTreeView::ProjectMayRequireReconfiguration);
    connect(project.get(), &Project::ProjectConfigured, this, &ProjectTreeView::ProjectConfigured);
    
    QDir rootDir = QFileInfo(project->GetYAMLFilePath()).dir();
    
    QDir gitDir = rootDir;
    if (gitDir.cd(".git")) {
      QString gitIndexPath = gitDir.filePath("index");
      if (QFile::exists(gitIndexPath)) {
        gitWatcher.addPath(gitIndexPath);
      }
    }
    
    watcher.addPath(rootDir.path());
    
    lastItem = new QTreeWidgetItem(tree, lastItem);
    lastItem->setText(0, rootDir.dirName());
    SetProjectMayRequireReconfiguration(lastItem, project->MayRequireReconfiguration());
    lastItem->setIcon(0, QIcon(":/cide/cide.png") /*iconProvider.icon(QFileInfo(rootDir.path()))*/);
    lastItem->setData(0, Qt::UserRole, QFileInfo(rootDir.path()).canonicalFilePath());
    lastItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    
    lastItem->setExpanded(true);
  }
  
  UpdateGitStatus();
}

void ProjectTreeView::UpdateHighlighting() {
  std::vector<QTreeWidgetItem*> workList = {tree->invisibleRootItem()};
  while (!workList.empty()) {
    QTreeWidgetItem* item = workList.back();
    workList.pop_back();
    
    for (int i = 0, count = item->childCount(); i < count; ++ i) {
      QTreeWidgetItem* child = item->child(i);
      if (child->childCount() > 0) {
        workList.push_back(child);
      } else {
        // This might be a file or an (empty or not-expanded-yet) directory.
        // It should not hurt to check directories in GetHighlightColor(),
        // it should never return anything else than the default for them.
        QString canonicalPath = QFileInfo(GetItemPath(child)).canonicalFilePath();
        ApplyItemStyles(child, canonicalPath);
      }
      
      if (child->parent() == nullptr ||
          child->parent() == tree->invisibleRootItem()) {
        QString projectPath = child->data(0, Qt::UserRole).toString();
        
        auto gitStatusIt = projectGitStatuses.find(projectPath);
        if (gitStatusIt != projectGitStatuses.end()) {
          ProjectGitStatus* status = gitStatusIt->second.get();
          int numModifications = 0;
          int numUntracked = 0;
          for (const auto& item : status->fileStatuses) {
            if (item.second == ProjectGitStatus::FileStatus::Modified) {
              ++ numModifications;
            } else if (item.second == ProjectGitStatus::FileStatus::Untracked) {
              ++ numUntracked;
            }
          }
          
          if (numModifications == 0 && numUntracked == 0) {
            child->setText(0, tr("%1 (branch: %2, clean)").arg(QDir(projectPath).dirName()).arg(status->branchName));
          } else {
            child->setText(0, tr("%1 (branch: %2, %3 %4, %5 untracked)")
                .arg(QDir(projectPath).dirName())
                .arg(status->branchName)
                .arg(numModifications)
                .arg((numModifications == 1) ? tr("modification") : tr("modifications"))
                .arg(numUntracked));
          }
        }
      }
    }
  }
}

void ProjectTreeView::ReloadDirectory(QTreeWidgetItem* dirItem) {
  std::unordered_set<std::string> expandedDirs;
  
  // Delete all children and remove all watchers related to them (and
  // grand-children etc.). Remember expanded directories such that they can be
  // expanded again after the reload (at least if they still exist with the
  // same name then).
  std::vector<QTreeWidgetItem*> workList = {dirItem};
  std::vector<QTreeWidgetItem*> doneList;
  while (!workList.empty()) {
    QTreeWidgetItem* item = workList.back();
    workList.pop_back();
    
    // Add all children to the work list
    for (int i = item->childCount() - 1; i >= 0; -- i) {
      workList.push_back(item->child(i));
    }
    
    // If this item was expanded, remember it
    if (item->isExpanded()) {
      expandedDirs.insert(GetItemPath(item).toStdString());
    }
    
    if (item != dirItem) {
      doneList.push_back(item);
    }
  }
  
  for (int i = static_cast<int>(doneList.size()) - 1; i >= 0; -- i) {
    watcher.removePath(GetItemPath(doneList[i]));
    delete doneList[i];
  }
  
  // Collapse the item
  dirItem->setExpanded(false);
  
  // Expand the item(s) again, which will add the updated children
  workList = {dirItem};
  while (!workList.empty()) {
    QTreeWidgetItem* item = workList.back();
    workList.pop_back();
    
    if (expandedDirs.count(GetItemPath(item).toStdString()) > 0) {
      item->setExpanded(true);
      
      // Add all children to the work list
      for (int i = item->childCount() - 1; i >= 0; -- i) {
        workList.push_back(item->child(i));
      }
    }
  }
}

void ProjectTreeView::ProjectMayRequireReconfiguration() {
  Project* project = qobject_cast<Project*>(sender());
  if (!project) {
    qDebug() << "Error: Sender of ProjectMayRequireReconfiguration() signal is not a project";
    return;
  }
  
  QString rootPath = QFileInfo(project->GetYAMLFilePath()).dir().path();
  QTreeWidgetItem* treeRoot = tree->invisibleRootItem();
  for (int i = 0; i < treeRoot->childCount(); ++ i) {
    if (treeRoot->child(i)->data(0, Qt::UserRole).toString() == rootPath) {
      SetProjectMayRequireReconfiguration(treeRoot->child(i), true);
      return;
    }
  }
  qDebug() << "Error: Project sending ProjectMayRequireReconfiguration() not found in ProjectTreeView";
}

void ProjectTreeView::ProjectConfigured() {
  Project* project = qobject_cast<Project*>(sender());
  if (!project) {
    qDebug() << "Error: Sender of ProjectConfigured() signal is not a project";
    return;
  }
  
  QString rootPath = QFileInfo(project->GetYAMLFilePath()).dir().path();
  QTreeWidgetItem* treeRoot = tree->invisibleRootItem();
  for (int i = 0; i < treeRoot->childCount(); ++ i) {
    if (treeRoot->child(i)->data(0, Qt::UserRole).toString() == rootPath) {
      SetProjectMayRequireReconfiguration(treeRoot->child(i), false);
      return;
    }
  }
  qDebug() << "Error: Project sending ProjectConfigured() not found in ProjectTreeView";
}

void ProjectTreeView::FileWatcherNotification(const QString& path) {
  // Re-load the contents of the given directory.
  QTreeWidgetItem* dirItem = GetItemForPath(path, false);
  if (!dirItem) {
    qDebug() << "ERROR: ProjectTreeView got a watcher notification for path" << path << ", however no corresponding tree view item can be found.";
    return;
  }
  
  ReloadDirectory(dirItem);
}

void ProjectTreeView::GitWatcherNotification(const QString& path) {
  // Wait a bit before checking the git state. This might help to perform the
  // update at a time where the external changes have completed, rather than
  // checking too early.
  gitUpdateTimer.start(100);
  
  // Re-adding the path seemed to be necessary to get further notifications
  gitWatcher.addPath(path);
}

void ProjectTreeView::GitUpdate() {
  UpdateGitStatus();
  
  // Schedule all open files for a git diff update.
  // TODO: This logic seems like it would better be placed on a higher level,
  //       as it has nothing to do with the ProjectTreeView.
  for (int i = 0; i < mainWindow->GetNumDocuments(); ++ i) {
    std::shared_ptr<Document> document = mainWindow->GetDocument(i);
    DocumentWidget* widget = mainWindow->GetWidgetForDocument(document.get());
    GitDiff::Instance().RequestDiff(document, widget, mainWindow);
  }
}

void ProjectTreeView::ItemExpanded(QTreeWidgetItem* item) {
  if (item->childCount() != 0) {
    return;
  }
  
  // Get the path of this directory.
  QDir dir(GetItemPath(item));
  
  // Add a watcher for this directory.
  watcher.addPath(dir.path());
  
  // Add the directory items as children.
  QStringList entryList = dir.entryList(
      QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs | QDir::Hidden,
      QDir::Name | QDir::DirsFirst | QDir::IgnoreCase);
  QTreeWidgetItem* lastItem = nullptr;
  for (const QString& entry : entryList) {
    lastItem = InsertItemFor(item, dir.filePath(entry), lastItem);
  }
  
  item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
}

void ProjectTreeView::ItemCollapsed(QTreeWidgetItem* item) {
  // Get the path of this directory.
  QDir dir(GetItemPath(item));
  
  // Remove the watcher for this directory.
  watcher.removePath(dir.path());
  
  // Delete all children and remove any watchers for them.
  std::vector<QTreeWidgetItem*> workList = {item};
  std::vector<QTreeWidgetItem*> doneList;
  while (!workList.empty()) {
    QTreeWidgetItem* curItem = workList.back();
    workList.pop_back();
    
    // Add all children to the work list
    for (int i = curItem->childCount() - 1; i >= 0; -- i) {
      workList.push_back(curItem->child(i));
    }
    
    if (curItem != item) {
      doneList.push_back(curItem);
    }
  }
  
  for (int i = static_cast<int>(doneList.size()) - 1; i >= 0; -- i) {
    watcher.removePath(GetItemPath(doneList[i]));
    delete doneList[i];
  }
  
  item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
}

void ProjectTreeView::ItemDoubleClicked(QTreeWidgetItem* item) {
  QString path = GetItemPath(item);
  if (QFileInfo(path).isDir()) {
    return;
  }
  
  mainWindow->Open(path);
}

void ProjectTreeView::ItemRightClicked(QTreeWidgetItem* item, QPoint pos) {
  rightClickedItem = item;
  
  bool isDir = QFileInfo(GetItemPath(item)).isDir();
  bool isProject = item->parent() == nullptr || item->parent() == tree->invisibleRootItem();
  
  reconfigureAction->setVisible(isProject);
  projectSettingsAction->setVisible(isProject);
  closeProjectAction->setVisible(isProject);
  createClassAction->setVisible(isDir);
  createFileAction->setVisible(isDir);
  createFolderAction->setVisible(isDir);
  renameAction->setVisible(true);
  deleteAction->setVisible(true);
  
  contextMenu->popup(tree->mapToGlobal(pos));
}

void ProjectTreeView::RightClicked(QPoint pos) {
  rightClickedItem = nullptr;
  
  reconfigureAction->setVisible(false);
  projectSettingsAction->setVisible(false);
  closeProjectAction->setVisible(false);
  createClassAction->setVisible(false);
  createFileAction->setVisible(false);
  createFolderAction->setVisible(false);
  renameAction->setVisible(false);
  deleteAction->setVisible(false);
  
  contextMenu->popup(tree->mapToGlobal(pos));
}

void ProjectTreeView::CreateClass() {
  if (!rightClickedItem) {
    return;
  }
  QDir folder(GetItemPath(rightClickedItem));
  
  std::shared_ptr<Project> project = GetProjectForItem(rightClickedItem);
  if (project) {
    CreateClassDialog dialog(folder, project, mainWindow);
    dialog.exec();
  }
}

void ProjectTreeView::CreateFile() {
  if (!rightClickedItem) {
    return;
  }
  QDir folder(GetItemPath(rightClickedItem));
  
  bool ok;
  QString fileName = QInputDialog::getText(
      mainWindow,
      tr("Create file in folder %1").arg(folder.dirName()),
      tr("Filename:"),
      QLineEdit::Normal,
      QStringLiteral(""),
      &ok);
  if (!ok) {
    return;
  }
  if (fileName.isEmpty()) {
    QMessageBox::warning(mainWindow, tr("Create file"), tr("The file name cannot be empty."));
    return;
  }
  
  // Create file
  QString path = folder.filePath(fileName);
  if (QFileInfo(path).exists()) {
    QMessageBox::warning(mainWindow, tr("Create file"), tr("There already exists a file or folder with this name."));
    return;
  }
  QFile(path).open(QIODevice::WriteOnly | QIODevice::Text);
  
  // Open file
  mainWindow->Open(path);
  
  UpdateGitStatus();
}

void ProjectTreeView::CreateFolder() {
  if (!rightClickedItem) {
    return;
  }
  QDir folder(GetItemPath(rightClickedItem));
  
  bool ok;
  QString folderName = QInputDialog::getText(
      mainWindow,
      tr("Create folder in folder %1").arg(folder.dirName()),
      tr("Folder name:"),
      QLineEdit::Normal,
      QStringLiteral(""),
      &ok);
  if (!ok) {
    return;
  }
  if (folderName.isEmpty()) {
    QMessageBox::warning(mainWindow, tr("Create folder"), tr("The folder name cannot be empty."));
    return;
  }
  
  // Create folder
  QString path = folder.filePath(folderName);
  if (QFileInfo(path).exists()) {
    QMessageBox::warning(mainWindow, tr("Create folder"), tr("There already exists a file or folder with this name."));
    return;
  }
  folder.mkdir(folderName);
  
  UpdateGitStatus();
}

void ProjectTreeView::SearchInFolder() {
  findAndReplaceInFiles->ShowDialog(GetItemPath(rightClickedItem));
}

void ProjectTreeView::Rename() {
  if (!rightClickedItem) {
    return;
  }
  
  bool ok;
  QString oldName = rightClickedItem->data(0, Qt::UserRole).toString();
  QString newName = QInputDialog::getText(
      mainWindow,
      tr("Rename %1").arg(oldName),
      tr("New name:"),
      QLineEdit::Normal,
      oldName,
      &ok);
  if (!ok) {
    return;
  }
  if (newName.isEmpty()) {
    QMessageBox::warning(mainWindow, tr("Rename"), tr("The new name cannot be empty."));
    return;
  }
  
  QDir folder(GetItemPath(rightClickedItem));
  folder.cdUp();
  QString newPath = folder.filePath(newName);
  if (QFileInfo(newPath).exists()) {
    QMessageBox::warning(mainWindow, tr("Rename"), tr("A file or folder with the new name already exists."));
    return;
  }
  
  // Rename the file or folder
  if (!folder.rename(oldName, newName)) {
    QMessageBox::warning(mainWindow, tr("Rename"), tr("Renaming failed."));
    return;
  }
}

void ProjectTreeView::DeleteSelectedItems() {
  QList<QTreeWidgetItem*> items = tree->selectedItems();
  
  // Ask safety question.
  QString fileList;
  for (QTreeWidgetItem* item : items) {
    if (!fileList.isEmpty()) {
      fileList += "\n";
    }
    fileList += QFileInfo(GetItemPath(item)).absoluteFilePath();
  }
  if (QMessageBox::question(
      mainWindow,
      tr("Remove files/folders"),
      tr("Are you sure to remove the following files/folder(s):\n\n") + fileList,
      QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
    return;
  }
  
  // Clear up the list of selected items: if an item has any other item from the
  // list as (grand-, ...)parent, erase it from the list, since removing the
  // parent will automatically remove its child as well.
  for (int i = items.size() - 1; i >= 0; -- i) {
    QTreeWidgetItem* testItem = items[i]->parent();
    bool isRedundant = false;
    while (testItem) {
      if (items.contains(testItem)) {
        isRedundant = true;
        break;
      }
      testItem = testItem->parent();
    }
    
    if (isRedundant) {
      items.takeAt(i);
    }
  }
  
  // Remove the remaining items.
  for (QTreeWidgetItem* item : items) {
    QString path = GetItemPath(item);
    if (QFileInfo(path).isDir()) {
      QDir(path).removeRecursively();
    } else {
      QFile::remove(path);
    }
  }
}

void ProjectTreeView::UpdateGitStatus() {
  std::vector<QString> projectYAMLFilePaths(mainWindow->GetProjects().size());
  for (std::size_t i = 0; i < mainWindow->GetProjects().size(); ++ i) {
    projectYAMLFilePaths[i] = mainWindow->GetProjects()[i]->GetYAMLFilePath();
  }
  
  GitStatusWorkerThread* workerThread = new GitStatusWorkerThread(projectYAMLFilePaths, this);
  connect(workerThread, &GitStatusWorkerThread::ResultReady, this, &ProjectTreeView::HandleGitStatusResults);
  connect(workerThread, &GitStatusWorkerThread::finished, workerThread, &QObject::deleteLater);
  workerThread->start();
}

void ProjectTreeView::HandleGitStatusResults(const ProjectGitStatusMap& projectGitStatuses) {
  this->projectGitStatuses = projectGitStatuses;
  UpdateHighlighting();
}

QTreeWidgetItem* ProjectTreeView::InsertItemFor(QTreeWidgetItem* parentFolder, const QString& path, QTreeWidgetItem* prevItem) {
  QString parentFolderPath = GetItemPath(parentFolder);
  QDir parentDir(parentFolderPath);
  QFileInfo fileInfo(path);
  bool isDir = fileInfo.isDir();
  QString itemName = fileInfo.fileName();
  
  if (prevItem == nullptr) {
    for (int i = 0, count = parentFolder->childCount(); i < count; ++ i) {
      QTreeWidgetItem* child = parentFolder->child(i);
      if (isDir) {
        if (!QFileInfo(parentDir.filePath(child->data(0, Qt::UserRole).toString())).isDir() ||
            child->text(0) > itemName) {  // TODO: Does this comparison correspond to the file sorting of dir.entryList()?
          break;
        }
      } else {
        if (!QFileInfo(parentDir.filePath(child->data(0, Qt::UserRole).toString())).isDir() &&
            child->text(0) > itemName) {  // TODO: Does this comparison correspond to the file sorting of dir.entryList()?
          break;
        }
      }
      prevItem = child;
    }
  }
  
  QTreeWidgetItem* newItem = new QTreeWidgetItem(parentFolder, prevItem);
  newItem->setText(0, itemName);
  newItem->setIcon(0, iconProvider.icon(path));
  newItem->setData(0, Qt::UserRole, itemName);
  newItem->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
  
  if (isDir) {
    if (!QDir(fileInfo.filePath()).isEmpty()) {
      newItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
  } else {
    QString canonicalPath = fileInfo.canonicalFilePath();
    ApplyItemStyles(newItem, canonicalPath);
  }
  
  return newItem;
}

QString ProjectTreeView::GetItemPath(QTreeWidgetItem* item) {
  if (item->parent() == nullptr ||
      item->parent() == tree->invisibleRootItem()) {
    return item->data(0, Qt::UserRole).toString();
  }
  
  std::vector<QString> pathParts;
  QTreeWidgetItem* currentItem = item;
  while (currentItem && currentItem != tree->invisibleRootItem()) {
    pathParts.push_back(currentItem->data(0, Qt::UserRole).toString());
    currentItem = currentItem->parent();
  }
  QDir dir = QDir(pathParts.back());
  for (int i = static_cast<int>(pathParts.size()) - 1; i >= 1; -- i) {
    if (!dir.cd(pathParts[i])) {
      qDebug() << "ERROR: Failed to cd() into directory in ProjectTreeView";
    }
  }
  return dir.filePath(pathParts[0]);
}

QTreeWidgetItem* ProjectTreeView::GetItemForPath(const QString& path, bool expandCollapsedDirs) {
  QString remainingPath = path;
  
  QTreeWidgetItem* item = tree->invisibleRootItem();
  while (true) {
    // Find the next child to descend to
    bool childFound = false;
    for (int i = 0, count = item->childCount(); i < count; ++ i) {
      QTreeWidgetItem* child = item->child(i);
      
      QString childPath = child->data(0, Qt::UserRole).toString();
      if (remainingPath.startsWith(childPath) &&
          (remainingPath.size() == childPath.size() ||
           (remainingPath.size() > childPath.size() && remainingPath[childPath.size()] == '/'))) {
        if (remainingPath.size() == childPath.size()) {
          return child;
        }
        
        // Descend into this child.
        item = child;
        if (!item->isExpanded()) {
          if (!expandCollapsedDirs) {
            return nullptr;
          }
          
          item->setExpanded(true);
        }
        childFound = true;
        remainingPath.remove(0, childPath.size());
        while (remainingPath.startsWith('/')) {
          remainingPath.remove(0, 1);
        }
        break;
      }
    }
    
    if (!childFound) {
      // No child found to descend into. Abort.
      return nullptr;
    }
  }
}

std::shared_ptr<Project> ProjectTreeView::GetProjectForItem(QTreeWidgetItem* item) {
  QTreeWidgetItem* projectItem = item;
  while (projectItem->parent() && projectItem->parent() != tree->invisibleRootItem()) {
    projectItem = projectItem->parent();
  }
  
  QString path = projectItem->data(0, Qt::UserRole).toString();
  for (const auto& project : mainWindow->GetProjects()) {
    if (QDir(project->GetDir()).canonicalPath() == path) {
      return project;
    }
  }
  return nullptr;
}

void ProjectTreeView::ApplyItemStyles(QTreeWidgetItem* item, const QString& canonicalPath) {
  // Check open / current state
  bool isOpened = false;
  bool isCurrent = false;
  if (mainWindow->IsFileOpen(canonicalPath)) {
    Document* currentDocument = mainWindow->GetCurrentDocument().get();
    if (currentDocument && currentDocument->path() == canonicalPath) {
      isCurrent = true;
    } else {
      isOpened = true;
    }
  }
  
  // Check git state
  bool isModified = false;
  bool isUntracked = false;
  ProjectGitStatus::FileStatus fileStatus = GetFileStatusFor(item);
  switch (fileStatus) {
  case ProjectGitStatus::FileStatus::Modified:
    isModified = true;
    break;
  case ProjectGitStatus::FileStatus::Untracked:
    isUntracked = true;
    break;
  case ProjectGitStatus::FileStatus::NotModified:
    break;
  case ProjectGitStatus::FileStatus::Invalid:
    break;
  }
  
  // Apply styles.
  auto applyStyle = [&](const Settings::ConfigurableTextStyle& style, bool force = false) {
    if (style.affectsText || force) {
      item->setForeground(0, QBrush(style.textColor));
      QFont font = item->font(0);
      font.setBold(style.bold);
      item->setFont(0, font);
    }
    if (style.affectsBackground || force) {
      item->setBackground(0, QBrush(style.backgroundColor));
    }
  };
  
  applyStyle(Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ProjectTreeViewDefault), true);
  if (isUntracked) {
    applyStyle(Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ProjectTreeViewUntrackedItem));
  }
  if (isModified) {
    applyStyle(Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ProjectTreeViewModifiedItem));
  }
  if (isOpened) {
    applyStyle(Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ProjectTreeViewOpenedItem));
  }
  if (isCurrent) {
    applyStyle(Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ProjectTreeViewCurrentItem));
  }
}

void ProjectTreeView::SetProjectMayRequireReconfiguration(QTreeWidgetItem* item, bool enable) {
  if (enable) {
    item->setBackground(0, QBrush(projectNeedingReconfigurationColor));
    item->setToolTip(0, tr("A CMake file of this project changed, thus it may require reconfiguration to be up-to-date."));
  } else {
    item->setBackground(0, QBrush(qRgb(255, 255, 255)));
    item->setToolTip(0, QStringLiteral(""));
  }
}

ProjectGitStatus::FileStatus ProjectTreeView::GetFileStatusFor(QTreeWidgetItem* item) {
  QTreeWidgetItem* projectItem = item;
  while (projectItem->parent() && projectItem->parent() != tree->invisibleRootItem()) {
    projectItem = projectItem->parent();
  }
  
  QString projectPath = projectItem->data(0, Qt::UserRole).toString();
  auto projectIt = projectGitStatuses.find(projectPath);
  if (projectIt == projectGitStatuses.end()) {
    return ProjectGitStatus::FileStatus::Invalid;
  }
  
  auto fileIt = projectIt->second->fileStatuses.find(GetItemPath(item));
  if (fileIt == projectIt->second->fileStatuses.end()) {
    return ProjectGitStatus::FileStatus::NotModified;
  }
  return fileIt->second;
}

#include "project_tree_view.moc"
