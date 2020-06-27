// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/crash_backup.h"

#include "cide/document.h"
#include "cide/main_window.h"

CrashBackup& CrashBackup::Instance() {
  static CrashBackup instance;
  return instance;
}

void CrashBackup::MakeBackup(const QString& path, std::shared_ptr<Document>& constantDocumentCopy) {
  if (path.isEmpty()) {
    // TODO: Make this work for documents without saved path
    return;
  }
  
  backupMutex.lock();
  backupRequests.push_back(BackupRequest(path, constantDocumentCopy));
  backupMutex.unlock();
  newBackupRequestCondition.notify_one();
}

void CrashBackup::RemoveBackup(const QString& path) {
  if (path.isEmpty()) {
    // TODO: Make this work for documents without saved path
    return;
  }
  
  // Erase all backup requests for this path from the queue
  backupMutex.lock();
  for (int i = static_cast<int>(backupRequests.size()) - 1; i >= 0; -- i) {
    if (backupRequests[i].path == path) {
      backupRequests.erase(backupRequests.begin() + i);
    }
  }
  backupMutex.unlock();
  
  // If the document for this path is currently being backed up, wait for this
  // to finish. Note that this should usually be very quick, so we use a simple
  // wait scheme here
  while (pathBeingBackedUp == path) {
    std::this_thread::yield();
  }
  
  // Delete the backup file for this path
  pathToBackupFilenameMutex.lock();
  auto it = pathToBackupFilename.find(path);
  if (it != pathToBackupFilename.end()) {
    QFile::remove(it->second);
    pathToBackupFilename.erase(it);
  }
  pathToBackupFilenameMutex.unlock();
}

bool CrashBackup::DoBackupsExist() {
  QDir backupQDir(backupDir);
  return backupQDir.exists() && !backupQDir.isEmpty();
}

QStringList CrashBackup::GetAllBackedUpFilePaths() {
  QDir backupQDir(backupDir);
  QStringList backupFiles = backupQDir.entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Readable);
  QStringList originalPaths;
  for (const QString& backupFilename : backupFiles) {
    QString backupPath = backupQDir.filePath(backupFilename);
    
    QFile file(backupPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qDebug() << "Error: Cannot read backup file:" << backupPath;
      continue;
    }
    
    originalPaths.push_back(QString::fromUtf8(file.readLine().chopped(1)));
  }
  return originalPaths;
}

void CrashBackup::RestoreBackups(MainWindow* mainWindow) {
  QDir backupQDir(backupDir);
  QStringList backupFiles = backupQDir.entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Readable);
  QStringList originalPaths;
  for (const QString& backupFilename : backupFiles) {
    QString backupPath = backupQDir.filePath(backupFilename);
    
    // Read the backup file
    std::shared_ptr<Document> backupDocument(new Document());
    QString originalFilePath;
    if (!backupDocument->OpenBackup(backupPath, &originalFilePath)) {
      qDebug() << "Error: Cannot read backup file:" << backupPath;
      continue;
    }
    
    // Make sure that the original file is open in the main window
    // TODO: Currently we assume that the original file exists. Make this also work if the original file does not exist anymore.
    Document* openDocument = nullptr;
    DocumentWidget* widget = nullptr;
    if (!mainWindow->IsFileOpen(originalFilePath)) {
      mainWindow->Open(originalFilePath);
    }
    if (!mainWindow->GetDocumentAndWidgetForPath(originalFilePath, &openDocument, &widget)) {
      qDebug() << "Error: Failed to open file to restore backup:" << originalFilePath;
      continue;
    }
    
    // Replace the file text with the backed up text
    openDocument->Replace(openDocument->FullDocumentRange(), backupDocument->GetDocumentText());
    
    // Remove the old backup file
    // TODO: To be safe, it would be best to wait until a new backup for the
    //       backup-restore change has been saved before deleting this
    QFile::remove(backupPath);
  }
}

void CrashBackup::DeleteAllBackups() {
  // Wait for the backup thread to be idle
  while (!pathBeingBackedUp.isEmpty()) {
    std::this_thread::yield();
  }
  
  // Delete all backup files
  QDir backupQDir(backupDir);
  QStringList backupFiles = backupQDir.entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Readable);
  for (const QString& backupFilename : backupFiles) {
    QString backupPath = backupQDir.filePath(backupFilename);
    QFile::remove(backupPath);
  }
  
  // Clear backup file map
  pathToBackupFilenameMutex.lock();
  pathToBackupFilename.clear();
  pathToBackupFilenameMutex.unlock();
}

void CrashBackup::Exit() {
  backupMutex.lock();
  mExit = true;
  backupMutex.unlock();
  newBackupRequestCondition.notify_all();
  if (mThread) {
    mThread->join();
    mThread.reset();
  }
}

CrashBackup::CrashBackup() {
  QString backupPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QDir dir(backupPath);
  dir.setPath(dir.filePath("cide_backup"));
  dir.mkpath(".");
  backupDir = dir.path();
  
  mExit = false;
  mThread.reset(new std::thread(&CrashBackup::ThreadMain, this));
}

int CrashBackup::GetNextBackupRequest() {
  if (backupRequests.empty()) {
    return -1;
  }
  
  // Find all requests with the same document path as the first one
  const QString& path = backupRequests.front().path;
  std::vector<int> requestsForPath = {0};
  for (int i = 1; i < backupRequests.size(); ++ i) {
    if (backupRequests[i].path == path) {
      requestsForPath.push_back(i);
    }
  }
  
  // Delete all but the latest request, as they are outdated
  int numDeleted = 0;
  for (int i = 0; i < requestsForPath.size() - 1; ++ i) {
    backupRequests.erase(backupRequests.begin() + requestsForPath[i] - numDeleted);
    ++ numDeleted;
  }
  
  // Return the latest request
  return requestsForPath.back() - numDeleted;
}

void CrashBackup::CreateBackup(const BackupRequest& request) {
  // Ensure that the backup folder exists
  QDir backupQDir(backupDir);
  if (!backupQDir.exists()) {
    backupQDir.mkpath(".");
  }
  
  // Generate a filename for the new backup file
  QString backupPath;
  do {
    backupPath = backupQDir.filePath(QString::number(nextBackupNumber));
    ++ nextBackupNumber;
  } while (QFile::exists(backupPath));
  
  // Write the backup file
  request.document->SaveBackup(backupPath, request.path);
  
  // Delete the old backup file for this path, if any, and remember the new filename
  pathToBackupFilenameMutex.lock();
  QString& storedBackupPath = pathToBackupFilename[request.path];
  if (!storedBackupPath.isEmpty()) {
    QFile::remove(storedBackupPath);
  }
  storedBackupPath = backupPath;
  pathToBackupFilenameMutex.unlock();
}

void CrashBackup::ThreadMain() {
  while (true) {
    std::unique_lock<std::mutex> lock(backupMutex);
    if (mExit) {
      return;
    }
    pathBeingBackedUp = "";
    int requestIndex;
    while ((requestIndex = GetNextBackupRequest()) == -1) {
      newBackupRequestCondition.wait(lock);
      if (mExit) {
        return;
      }
    }
    BackupRequest request = backupRequests[requestIndex];
    backupRequests.erase(backupRequests.begin() + requestIndex);
    pathBeingBackedUp = request.path;
    lock.unlock();
    
    // Make the backup
    CreateBackup(request);
  }
}
