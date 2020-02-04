// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include <clang-c/Index.h>

#include <git2.h>
#include <QtWidgets>
#include <QString>

#include "cide/clang_utils.h"
#include "cide/crash_backup.h"
#include "cide/code_info.h"
#include "cide/git_diff.h"
#include "cide/main_window.h"
#include "cide/parse_thread_pool.h"
#include "cide/settings.h"
#include "cide/startup_dialog.h"
#include "cide/util.h"


void CleanUp() {
  // We need to keep the QApplication alive until all other threads exited,
  // otherwise RunInQtThreadBlocking() will not execute anymore, potentially
  // leading to issues. However, we need to run a Qt event loop while waiting
  // for the threads to exit, otherwise RunInQtThreadBlocking() will block
  // forever. So, we invoke the cleanup in a separate thread.
  std::atomic<bool> exitFinished;
  exitFinished = false;
  std::thread exitThread([&]() {
    ParseThreadPool::Instance().ExitAllThreads();
    CodeInfo::Instance().Exit();
    CrashBackup::Instance().Exit();
    GitDiff::Instance().Exit();
    exitFinished = true;
  });
  QEventLoop exitEventLoop;
  while (!exitFinished) {
    exitEventLoop.processEvents();
  }
  exitThread.join();
}

void CheckForLeftoverPreambles() {
  QDir backupDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (!backupDir.exists()) {
    return;
  }
  
  QStringList preambleFiles;
  for (const QString& item : backupDir.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
    if (item.startsWith("preamble-") && item.endsWith(".pch")) {
      preambleFiles.push_back(item);
    }
  }
  
  if (!preambleFiles.isEmpty()) {
    if (QMessageBox::question(
        nullptr,
        QObject::tr("Preamble files detected"),
        QObject::tr("Found existing preamble files (listed below). These might stem from a previous run of CIDE that crashed."
                    " However, they could also stem from other programs or a concurrently running instance of CIDE. Delete those files?\n\n%1").arg(preambleFiles.join('\n')),
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
      for (const QString& file : preambleFiles) {
        QFile::remove(backupDir.filePath(file));
      }
    }
  }
}

int main(int argc, char** argv) {
  // Initialize libgit2
  git_libgit2_init();
  
  // Initialize Qt
  QApplication qapp(argc, argv);
  QCoreApplication::setOrganizationName("PuzzlePaint");
  QCoreApplication::setOrganizationDomain("puzzlepaint.net");
  QCoreApplication::setApplicationName("CIDE");
  // This was required to have all wheelEvent()s reported immediately.
  // Otherwise, they were sometimes buffered together and only reported with
  // the next wheelEvent() that "got through", which could have been a long
  // time after the first one, resulting in choppy scrolling.
  qapp.setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
  
  // Print used libclang version
  qDebug() << "CIDE using libclang" << GetLibclangVersion();
  
  // If the program starts for the first time, ask the user to configure it.
  if (Settings::Instance().GetDefaultCompiler().isEmpty()) {
    QMessageBox::information(
        nullptr,
        QObject::tr("Initial startup"),
        QObject::tr("It seems that CIDE is running for the first time (the default compiler path setting is empty)."
                    " Please configure the application to your preferences, and in particular verify the default compiler path."));
    Settings::Instance().SetDefaultCompiler(FindDefaultClangBinaryPath());
    Settings::Instance().ShowSettingsWindow(nullptr);
  } else {
    // Look for preamble files that might be left over from a previous run that crashed.
    CheckForLeftoverPreambles();
  }
  
  // Create main window
  MainWindow* mainWindow = new MainWindow();
  mainWindow->show();
  
  // Parse command-line arguments
  bool loadedProject = false;
  bool openedFile = false;
  int firstFileArg = 1;
  if (argc >= 3 && std::string(argv[1]) == "-p") {
    // Load project
    if (mainWindow->LoadProject(QString::fromLocal8Bit(argv[2]), mainWindow)) {
      loadedProject = true;
    }
    
    firstFileArg += 2;
  }
  for (int i = firstFileArg; i < argc; ++ i) {
    mainWindow->Open(QString::fromLocal8Bit(argv[i]));
    openedFile = true;
  }
  
  // Restore backups if there are any
  if (CrashBackup::Instance().DoBackupsExist()) {
    if (QMessageBox::question(
        mainWindow,
        QObject::tr("Restore backup"),
        QObject::tr("Backup files exist for the files below. Restore them?\n\n%1").arg(CrashBackup::Instance().GetAllBackedUpFilePaths().join("\n")),
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
      CrashBackup::Instance().RestoreBackups(mainWindow);
    } else {
      CrashBackup::Instance().DeleteAllBackups();
    }
  }
  
  if (!loadedProject && !openedFile) {
    // Show the startup dialog.
    StartupDialog startupDialog(mainWindow);
    if (startupDialog.exec() == QDialog::Rejected) {
      CleanUp();
      return 0;
    }
  }
  
  // Run the main event loop.
  qapp.exec();
  
  // Clean up.
  CleanUp();
  
  delete mainWindow;
  return 0;
}
