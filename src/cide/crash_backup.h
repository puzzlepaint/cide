// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <QString>

#include "cide/util.h"

class Document;
class MainWindow;

/// Saves unsaved versions of documents in a temporary directory such that they
/// can be restored from there after a crash of CIDE.
/// TODO: Make the backup directory configurable.
class CrashBackup {
 public:
  static CrashBackup& Instance();
  
  /// Backs up the given document. Important: As the backup is done in a
  /// background thread, the Document instance passed in here must be a constant
  /// copy of the actual document that will not be modified anymore.
  void MakeBackup(const QString& path, std::shared_ptr<Document>& constantDocumentCopy);
  
  /// Removes the backup for the document with the given path. This should be
  /// called if the document was saved, meaning that no backup is required
  /// anymore.
  void RemoveBackup(const QString& path);
  
  /// Checks whether any backup file exists.
  bool DoBackupsExist();
  
  /// Returns a list of paths to files for which backups exist.
  QStringList GetAllBackedUpFilePaths();
  
  /// Restores all backup files and deletes them.
  void RestoreBackups(MainWindow* mainWindow);
  
  /// Deletes all backup files.
  void DeleteAllBackups();
  
  /// Makes the backup thread stop and waits for it to exit.
  void Exit();
  
 private:
  struct BackupRequest {
    inline BackupRequest(const QString& path, const std::shared_ptr<Document>& document)
        : path(path),
          document(document) {}
    
    QString path;
    std::shared_ptr<Document> document;
  };
  
  
  CrashBackup();
  
  int GetNextBackupRequest();
  void CreateBackup(const BackupRequest& request);
  
  void ThreadMain();
  
  
  // Thread input handling
  std::mutex backupMutex;
  std::condition_variable newBackupRequestCondition;
  std::vector<BackupRequest> backupRequests;
  
  // Thread state
  QString pathBeingBackedUp;
  
  // Backup state
  std::mutex pathToBackupFilenameMutex;
  std::unordered_map<QString, QString> pathToBackupFilename;
  int nextBackupNumber = 0;
  
  // Settings
  QString backupDir;
  
  // Threading
  std::atomic<bool> mExit;
  std::shared_ptr<std::thread> mThread;
};
