// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/run_gdb.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QThread>

#include "cide/settings.h"


/// Stores results given by the GDB/MI interface. The key attribute is always
/// valid. In addition, *one of* the following must be valid:
/// - value
/// - resultList
/// However, it is possible that a valid list is empty.
struct GDBResult {
  QByteArray key;
  
  QByteArray value;
  std::vector<GDBResult> resultList;
  
  
  /// When calling this function, the cursor must be set on the first character
  /// of the result. After the function returns, the cursor will be set
  /// on the character after the result.
  bool Read(const QByteArray& line, int* cursor) {
    // Read variable name
    int equalsIdx = line.indexOf('=', *cursor);
    if (equalsIdx < 0) {
      qDebug() << "ERROR: Failed to read result in line ('=' character not found):" << line << "  remaining to parse:" << line.mid(*cursor);
      return false;
    }
    key = line.mid(*cursor, equalsIdx - *cursor);
    *cursor = equalsIdx + 1;
    
    // Read value
    return ReadValue(line, cursor);
  }
  
  bool ReadValue(const QByteArray& line, int* cursor) {
    if (line[*cursor] == '"') {
      // Read C string into value
      ++ *cursor;
      while (*cursor < line.size() && line[*cursor] != '"') {
        if (line[*cursor] == '\\' && *cursor < line.size() - 1) {
          // TODO: Are there some codes that must be handled specifically, for example \t to insert a tab character?
          value += line[*cursor + 1];
          *cursor += 2;
          continue;
        }
        value += line[*cursor];
        ++ *cursor;
      }
      if (*cursor >= line.size()) {
        qDebug() << "ERROR: Failed to read result in line (unexpected end of C string):" << line;
        return false;
      }
      ++ *cursor;
    } else if (line[*cursor] == '{' || line[*cursor] == '[') {
      char endChar = (line[*cursor] == '{') ? '}' : ']';
      
      // Read valueList or resultList
      ++ *cursor;
      bool isValueList = true;
      bool endRead = false;
      while (*cursor < line.size()) {
        if (line[*cursor] == endChar) {
          // End of list
          endRead = true;
          ++ *cursor;
          break;
        } else if (line[*cursor] == ',') {
          // Read the next item in a list
          ++ *cursor;
        } else if (line[*cursor] == '"' || line[*cursor] == '{' || line[*cursor] == '[') {
          isValueList = true;
        } else if (QChar::fromLatin1(line[*cursor]).isLetter()) {
          isValueList = false;
        } else {
          qDebug() << "ERROR: Failed to read result in line (unexpected character while reading list):" << line;
          return false;
        }
        
        if (isValueList) {
          // Read a valueList item
          resultList.emplace_back();
          if (!resultList.back().ReadValue(line, cursor)) {
            return false;
          }
        } else {
          // Read a resultList item
          resultList.emplace_back();
          if (!resultList.back().Read(line, cursor)) {
            return false;
          }
        }
      }
      if (!endRead) {
        qDebug() << "ERROR: Failed to read result in line (end of list/tuple missing):" << line;
        return false;
      }
    } else {
      qDebug() << "ERROR: Failed to read result in line (unknown character at start of result value):" << line;
      qDebug() << "       character:" << line[*cursor] << ", cursor:" << *cursor;
      return false;
    }
    
    return true;
  }
};


GDBRunner::GDBRunner() {
  emitStateChanges = false;
  waitingForType = 0;
  waitingForStatus = "";
  
  connect(&process,
          &QProcess::readyReadStandardOutput,
          this,
          &GDBRunner::ReadyReadStdOut);
  
  connect(&process,
          &QProcess::readyReadStandardError,
          this,
          &GDBRunner::ReadyReadStdErr);
/*  
  connect(&process,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          [&](int exitCode, QProcess::ExitStatus exitStatus) {
    switch (exitStatus) {
    case QProcess::NormalExit:
      if (compileErrors > 0 && compileWarnings > 0) {
        FinishedCompiling(tr("<span style=\"color:red\">Errors: %1</span> <span style=\"color:yellow\">Warnings: %2</span>").arg(compileErrors).arg(compileWarnings));
      } else if (compileErrors > 0) {
        FinishedCompiling(tr("<span style=\"color:red\">Errors: %1</span>").arg(compileErrors));
      } else if (compileWarnings > 0) {
        FinishedCompiling(tr("<span style=\"color:yellow\">Compiled with %1 warning%2</span>").arg(compileWarnings).arg((compileWarnings > 1) ? QStringLiteral("s") : QStringLiteral("")));
      } else if (exitCode == 0) {
        FinishedCompiling(tr("<span style=\"color:green\">Compiled successfully</span>"));
      } else {
        ViewCompileOutputAsText();
        FinishedCompiling(tr("<span style=\"color:red\">Compiling returned exit code %1</span>").arg(exitCode));
      }
      break;
    case QProcess::CrashExit:
      FinishedCompiling(tr("<span style=\"color:red\">Compiling crashed</span>"));
      break;
    }
  });
  
  connect(&process,
          &QProcess::errorOccurred,
          [&](QProcess::ProcessError error) {
    switch (error) {
    case QProcess::FailedToStart:
      FinishedCompiling(tr("<span style=\"color:red\">Failed to start the compile process (program missing / insufficient permissions).</span>"));
      break;
    case QProcess::Crashed:
      FinishedCompiling(tr("<span style=\"color:red\">Compiling crashed</span>"));
      break;
    case QProcess::Timedout:
      FinishedCompiling(tr("<span style=\"color:red\">Compiling timed out</span>"));
      break;
    case QProcess::WriteError:
      FinishedCompiling(tr("<span style=\"color:red\">Error while writing to the compile process</span>"));
      break;
    case QProcess::ReadError:
      FinishedCompiling(tr("<span style=\"color:red\">Error while reading from the compile process</span>"));
      break;
    case QProcess::UnknownError:
      FinishedCompiling(tr("<span style=\"color:red\">An unknown error occurred with the compile process</span>"));
      break;
    };
  });*/

  running = false;
  interrupted = false;
}

void GDBRunner::Start(const QString& workingDir, const QStringList& programAndArguments) {
  emitStateChanges = false;
  running = false;
  interrupted = false;
  
  if (programAndArguments.empty()) {
    qDebug() << "GDBRunner::Start: programAndArguments is empty";
    return;
  }
  
  if (process.state() == QProcess::Running) {
    if (QMessageBox::question(nullptr, tr("Start debugging"), tr("The debugger is already running. Exit it and start anew?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
      return;
    }
    process.kill();
  }
  
  // TODO: This commented-out approach of creating a virtual terminal did not
  //       seem to work. Reading from the terminal only worked when doing something
  //       like echo "test" > /dev/pts/<terminal_number>, but not when using
  //       -inferior-tty-set to set the debugged program's terminal to this.
  //       Probably I missed some setting or something ...
//   // Create a virtual terminal to handle the debugged program's input and output.
//   // We open the "master" /dev/ptmx, which should create a "slave" /dev/pts/<number>.
//   // TODO: Close that descriptor again
//   int fd = posix_openpt(O_RDWR | O_NOCTTY);
//   if (fd == -1) {
//     qDebug() << "posix_openpt() returned an error. errno:" << errno;
//     return;
//   }
//   
//   // Set mode and owner of the slave
//   grantpt(fd);
//   
//   // Must be called before opening the slave
//   unlockpt(fd);
//   
//   // Get the (full) path of the slave
//   char name[256];
//   if (ptsname_r(fd, name, 256) != 0) {
//     qDebug() << "ptsname_r() returned an error";
//     close(fd);
//     return;
//   }
//   
//   qDebug() << "Terminal name: " << name;
  
//   fcntl(fd, F_SETFL, 0);
//   struct termios ts;
// 
//   if (tcgetattr(fd, &ts)) {
//     perror("tcgetattr");
//     exit(1);
//   }
//   cfmakeraw(&ts);
//   tcsetattr(fd, TCSANOW, &ts);
  
//   // Set the debugged program's terminal to the virtual terminal created above
//   process.write(QStringLiteral("-inferior-tty-set %1\n").arg(name).toLocal8Bit());
  
//   // Set the debugged program's path
//   process.write((QStringLiteral("-file-exec-and-symbols ") + programAndArguments[0] + QStringLiteral("\n")).toLocal8Bit());
  
//   // Set the debugged program's arguments
//   process.write((QStringLiteral("-exec-arguments ") + programAndArguments.mid(1).join(' ') + QStringLiteral("\n")).toLocal8Bit());
  
//   // Start the debugged program
//   process.write(QStringLiteral("-exec-run\n").toLocal8Bit());
  
//   // DEBUG TEST
//   constexpr int fdcount = 1;
//   struct pollfd pfd[1];
//   pfd[0].fd = fd;
//   pfd[0].events = POLLIN;
//   
//   while (true) {
//     if (poll(pfd, fdcount, -1) < 0 && errno != EINTR) {
//       qDebug() << "Exiting loop.";
//       break;
//     }
//     
//     char buffer[16];
//     ssize_t numRead = read(fd, buffer, 16);
//     qDebug() << "Read from virtual terminal: " << buffer;
//   }
  
  
  // File where the bash PID will be written into
  const QString startPIDFilePath = QStringLiteral("/tmp/__cide_start_pid");
  // File whose existence confirms that the bash PID file has been completely written
  const QString startPIDWrittenTriggerFilePath = QStringLiteral("/tmp/__cide_start_pid_written_trigger");
  // File whose existence confirms that the gdb process has attached to the bash process
  const QString startTriggerFilePath = QStringLiteral("/tmp/__cide_start_trigger");
  
  QFile(startPIDFilePath).remove();
  QFile(startPIDWrittenTriggerFilePath).remove();
  QFile(startTriggerFilePath).remove();
  
  
  // Start Konsole process with the chosen working directory
  // --hold: Do not close the initial session automatically when it ends.
  // --separate: Run in a separate process.
  // --nofork is necessary such that the process which will later run the
  //   debugged program is seen as a direct child of gdb, so we can attach to
  //   it without root rights.
  // NOTE: We must have gdb (indirectly) start the debugged process, otherwise
  //       attaching to it will usually require sudo
  QString debuggerBinary = Settings::Instance().GetGDBPath();
  process.setWorkingDirectory(workingDir);
  process.start(
      debuggerBinary,
      {QStringLiteral("--interpreter=mi2"),
       QStringLiteral("--ex"),
       QStringLiteral("run"),
       QStringLiteral("--args"),
       QStringLiteral("konsole"),
       QStringLiteral("--hold"),
       QStringLiteral("--separate"),
       QStringLiteral("--nofork"),
       QStringLiteral("--hide-menubar"),
       QStringLiteral("--hide-tabbar"),
       QStringLiteral("--workdir"),
       workingDir,
       QStringLiteral("-e"),
       QStringLiteral("bash"),
       QStringLiteral("-c"),
           QStringLiteral("ps | grep bash > \"%1\";").arg(startPIDFilePath) +  // write bash PID file
           QStringLiteral("echo \"done\" > %1;").arg(startPIDWrittenTriggerFilePath) +  // write another file to confirm that the first file has been fully written
           QStringLiteral("until test -f \"%1\" ; do sleep 0.001; done;").arg(startTriggerFilePath) +  // wait until a third file confirms that the gdb process was attached; TODO: sleep shorter?
           QStringLiteral("exec ") + programAndArguments.join(' ')});  // start the debugged program
  if (!process.waitForStarted()) {
    QMessageBox::warning(nullptr, tr("Start debugging"), tr("Failed to start the debugger process (%1). The debugger executable to use can be configured in the program settings.").arg(debuggerBinary));
    return;
  }
  
  lastThreadGroupId = process.processId();
  
  // qDebug() << "Waiting for gdb to initialize ...";
  // WaitForOutput(0, QStringLiteral("(gdb) "));
  
  qDebug() << "Waiting for gdb to start konsole ...";
  // process.write("-exec-run\n");
  QFile triggerFile(startPIDWrittenTriggerFilePath);
  while (!triggerFile.exists()) {
    QThread::yieldCurrentThread();
    continue;
  }
  // Clear terminal output
  QEventLoop eventLoop;
  eventLoop.processEvents(QEventLoop::ExcludeUserInputEvents);
  
  qDebug() << "Interrupting gdb ...";
  // NOTE: This did not work to interrupt gdb when using konsole with --nofork.
  // // Get the ID of the gdb process' process group
  // pid_t processGroupId = getpgid(process.processId());
  // qDebug() << "gdb process group id: " << processGroupId;
  // // Send SIGINT to this group, ignoring the signal ourselves
  // signal(SIGINT, SIG_IGN);
  // if (killpg(processGroupId, SIGINT) == -1) {
  //   qDebug() << "killpg() gave an error: " << errno;
  // }
  // signal(SIGINT, SIG_DFL);
#ifdef WIN32
  // TODO: Not implemented for Windows
#else
  if (kill(lastThreadGroupId, SIGINT) == -1) {
    qDebug() << "kill() gave an error: " << errno;
  }
#endif
  WaitForOutput('*', QStringLiteral("stopped"));
  
  // Detach from the konsole process. This will continue running it.
  qDebug() << "Detaching from konsole ...";
  process.write("-target-detach\n");
  WaitForOutput('^', QStringLiteral("done"));
  
  // Get the process ID of the bash process run in konsole
  int bashPID = -1;
  while (true) {
    QFile triggerFile(startPIDWrittenTriggerFilePath);
    if (!triggerFile.exists()) {
      QThread::yieldCurrentThread();
      continue;
    }
    
    QFile file(startPIDFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
      qDebug() << "Cannot read temporary PID file";
      process.kill();
      return;
    }
    
    QByteArray pidFileContent = file.readAll();
    QStringList words = QString::fromLocal8Bit(pidFileContent).split(' ');
    for (const QString& word : words) {
      if (word.isEmpty()) {
        continue;
      }
      bool ok;
      bashPID = word.toInt(&ok);
      if (ok) {
        break;
      } else {
        qDebug() << "Cannot parse temporary PID file; file content: " << pidFileContent;
        process.kill();
        return;
      }
    }
    break;
  }
  qDebug() << "D PID: " << bashPID;
  
  qDebug() << "Attaching to bash ...";
  process.write((QStringLiteral("-target-attach %1\n").arg(bashPID)).toLocal8Bit());
  process.write(QStringLiteral("-exec-continue\n").toLocal8Bit());
  WaitForOutput('*', QStringLiteral("running"));
  
  // Start the debugged program in the bash process
  QFile startTriggerFile(startTriggerFilePath);
  startTriggerFile.open(QIODevice::WriteOnly);
  startTriggerFile.close();
  
  
  running = true;
  interrupted = false;
  
  emit Started();
  emitStateChanges = true;
}

void GDBRunner::Interrupt() {
#ifdef WIN32
  // TODO: Not implemented for Windows
#else
  if (kill(lastThreadGroupId, SIGINT) == -1) {
    qDebug() << "kill() gave an error: " << errno;
  }
#endif
  
  interrupted = true;
}

void GDBRunner::Resume() {
  constexpr const char* cmd = "-exec-continue\r\n";
  process.write(cmd);
  
  interrupted = false;
}

void GDBRunner::Stop() {
  if (!interrupted) {
    Interrupt();
  }
  
#ifdef WIN32
  // TODO: Not implemented for Windows
#else
  if (kill(lastThreadGroupId, SIGKILL) == -1) {
    qDebug() << "kill() gave an error: " << errno;
  }
#endif
  
  constexpr const char* cmd = "-gdb-exit\n";
  process.write(cmd);
  
  interrupted = false;
  running = false;
}

void GDBRunner::WaitForExit() {
  if (process.state() == QProcess::NotRunning) {
    return;
  }
  
  if (!process.waitForFinished(1000)) {
    process.kill();
  }
}

bool GDBRunner::IsRunning() {
  return running;
}

bool GDBRunner::IsInterrupted() {
  return interrupted;
}

void GDBRunner::GetThreadList() {
  constexpr const char* cmd = "-thread-info\n";
  process.write(cmd);
}

void GDBRunner::GetStackTrace(int threadId) {
  if (threadId == -1) {
    process.write(QStringLiteral("-stack-list-frames\n").toLocal8Bit());
  } else {
    process.write(QStringLiteral("-stack-list-frames --thread %1\n").arg(threadId).toLocal8Bit());
  }
}

void GDBRunner::EvaluateExpression(const QString& expression, int threadId, int frameIndex) {
  waitingForToken = nextToken;
  process.write(QStringLiteral("%1-data-evaluate-expression --thread %2 --frame %3 \"%4\"\n").arg(nextToken++).arg(threadId).arg(frameIndex).arg(expression).toLocal8Bit());
  
  // NOTE: Did not seem to work:
//   process.write(QStringLiteral("-var-create tempExpr %1 %2\n").arg(frameAddress).arg(expression).toLocal8Bit());
//   process.write(QStringLiteral("-var-evaluate-expression tempExpr\n").toLocal8Bit());
//   process.write(QStringLiteral("-var-delete tempExpr\n").toLocal8Bit());
}

void GDBRunner::ReadyReadStdOut() {
  int oldSize = stdoutCache.size();
  stdoutCache += process.readAllStandardOutput();
  // qDebug() << "STDOUT:" << stdoutCache.mid(oldSize);
  
  for (int i = oldSize; i < stdoutCache.size(); ++ i) {
    if (stdoutCache[i] == '\n' || stdoutCache[i] == '\r') {
      ParseLine(stdoutCache.left(i));
      stdoutCache.remove(0, i + ((stdoutCache[i] == '\n') ? 1 : 2));
      i = -1;
    }
  }
}

void GDBRunner::ReadyReadStdErr() {
  int oldSize= stderrCache.size();
  stderrCache += process.readAllStandardError();
  qDebug() << "STDERR:" << stderrCache.mid(oldSize);
}

void GDBRunner::ParseLine(const QByteArray& line) {
  // See this page for the syntax of lines:
  // https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Output-Syntax.html#GDB_002fMI-Output-Syntax
  
  qDebug() << "GDB: " << line;
  if (line.isEmpty()) {
    return;
  }
  
  // Disregard "(gdb) " lines
  if (line.compare("(gdb) ") == 0) {
    if (waitingForType == 0 && line == waitingForStatus) {
      waitingDone = true;
    }
    return;
  }
  
  // Check for a numerical token (sequence of digits)
  QByteArray cursorString;
  int cursor = 0;
  while (cursor < line.size() && line[cursor] >= '0' && line[cursor] <= '9') {
    cursorString += line[cursor];
    ++ cursor;
  }
  int token = -1;
  if (!cursorString.isEmpty()) {
    token = cursorString.toInt();
  }
  
  // The first character indicates the type of message:
  // Messages followed by a C string:
    // & - GDB internal log
    // @ - target output (only provided in some cases)
    // ~ - output that should be shown in the console
  // Messages followed by structured output:
    // = - asynchronous notifications
    // * - asynchronous execution state changes
    // + - asynchronous status output on the progress of slow operations
    // ^ - responses to commands
  QChar messageTypeChar = line[cursor];
  QByteArray asyncOrResultClass;
  std::vector<GDBResult> results;
  
  if (messageTypeChar == '&' ||
      messageTypeChar == '@' ||
      messageTypeChar == '~') {
    // Read C string
    // TODO
  } else if (messageTypeChar == '=' ||
             messageTypeChar == '*' ||
             messageTypeChar == '+' ||
             messageTypeChar == '^') {
    // Read structured output
    // Read the word until the line-end or comma
    int commaIdx = line.indexOf(',', cursor + 1);
    if (commaIdx >= 0) {
      asyncOrResultClass = line.mid(cursor + 1, commaIdx - (cursor + 1));
    } else {
      asyncOrResultClass = line.mid(cursor + 1);
    }
    
    if (messageTypeChar == '^') {
      // For this type of message, the set of possible result classes is well-known.
      if (asyncOrResultClass != "done" &&
          asyncOrResultClass != "running" &&
          asyncOrResultClass != "connected" &&
          asyncOrResultClass != "error" &&
          asyncOrResultClass != "exit") {
        qDebug() << "ERROR: Failed to parse line (unexpected result class):" << line;
        return;
      }
    } else {
      // For this types of messages, the set of possible result classes is not well-defined.
    }
    
    // Parse the results (variable = value).
    cursor = cursor + 2 + asyncOrResultClass.size();
    while (cursor < line.size()) {
      results.emplace_back();
      if (!results.back().Read(line, &cursor)) {
        return;
      }
      
      // Jump over the (potential) comma separating the next result
      ++ cursor;
    }
  } else {
    qDebug() << "ERROR: Failed to parse line (unexpected initial character):" << line;
    return;
  }
  
  
  // Interpret the parsed message.
  // TODO: Make separate function for this
  
  if (messageTypeChar == waitingForType &&
      asyncOrResultClass == waitingForStatus) {
    waitingDone = true;
  }
  
  if (messageTypeChar == '*') {
    if (asyncOrResultClass == "running" && emitStateChanges) {
      emit Resumed();
    } else if (asyncOrResultClass == "stopped" && emitStateChanges) {
      emit Interrupted();
    }
  } else if (messageTypeChar == '=') {
    if (asyncOrResultClass == "thread-group-started") {
      for (const GDBResult& result : results) {
        if (result.key == "pid") {
          bool ok;
          lastThreadGroupId = result.value.toInt(&ok);
          if (!ok) {
            qDebug() << "ERROR: Cannot parse pid of thread-group-started as int";
          } else {
            qDebug() << "New lastThreadGroupId:" << lastThreadGroupId;
          }
        }
      }
    } else if (asyncOrResultClass == "thread-group-exited" && emitStateChanges) {
      int exitCode = -1;
      if (results.back().key == "exit-code") {
        exitCode = results.back().value.toInt();
      }
      
      // Emit the Stopped signal and exit the debugger.
      running = false;
      interrupted = false;
      emit Stopped(exitCode);
      emitStateChanges = false;
      
      constexpr const char* cmd = "-gdb-exit\n";
      process.write(cmd);
    }
  } else if (messageTypeChar == '^') {
    if (asyncOrResultClass == "done") {
      if (!results.empty() && results.front().key == "threads") {
        // Received list of threads.
        ParseThreadInfo(results, &currentThreadId, &threadIdAndFrame);
        emit ThreadListUpdated();
      } else if (!results.empty() && results.front().key == "stack") {
        // Received stack trace.
        ParseStackTrace(results.front().resultList, &stackFrames);
        emit StackTraceUpdated();
      }
    }
  }
  
  if (waitingForToken != -1 &&
      token == waitingForToken) {
    waitingForToken = -1;
    
    // Extract error message or value.
    QString result;
    if (asyncOrResultClass == "error" && results.size() >= 1 && results[0].key == "msg") {
      result = results[0].value;
    } else if (asyncOrResultClass == "done" && results.size() >= 1 && results[0].key == "value") {
      result = results[0].value;
    } else {
      qDebug() << "ERROR: Failed to extract result from response that GDBRunner waited for";
      return;
    }
    emit ResponseReceived(result);
  }
}

void GDBRunner::WaitForOutput(char type, const QString& status) {
  waitingForType = type;
  waitingForStatus = status;
  waitingDone = false;
  
  QEventLoop eventLoop;
  while (!waitingDone) {
    eventLoop.processEvents(QEventLoop::ExcludeUserInputEvents);
  }
}

void GDBRunner::ParseThreadInfo(
    const std::vector<GDBResult>& results,
    int* currentThreadId,
    std::vector<std::pair<int, QString>>* threadIdAndFrame) {
  // Example (current-thread-id may be omitted):
  // threads=[
  // {id="2",target-id="Thread 0xb7e14b90 (LWP 21257)",
  //    frame={level="0",addr="0xffffe410",func="__kernel_vsyscall",
  //            args=[]},state="running"},
  // {id="1",target-id="Thread 0xb7e156b0 (LWP 21254)",
  //    frame={level="0",addr="0x0804891f",func="foo",
  //            args=[{name="i",value="10"}],
  //            file="/tmp/a.c",fullname="/tmp/a.c",line="158",arch="i386:x86_64"},
  //            state="running"}],
  // current-thread-id="1"
  
  if (results.back().key == "current-thread-id") {
    *currentThreadId = results.back().value.toInt();
  } else {
    *currentThreadId = -1;
  }
  
  const std::vector<GDBResult>& threadList = results.front().resultList;
  for (const GDBResult& threadInfo : threadList) {
    threadIdAndFrame->emplace_back();
    std::pair<int, QString>* newThread = &threadIdAndFrame->back();
    
    QString name;
    QString frame;
    
    for (const GDBResult& attribute : threadInfo.resultList) {
      if (attribute.key == "id") {
        newThread->first = attribute.value.toInt();
      } else if (attribute.key == "name") {
        name = attribute.value;
      } else if (attribute.key == "frame") {
        frame = GetShortFrameDescription(attribute.resultList);
      }
    }
    
    if (name.isEmpty()) {
      newThread->second = tr("[%1] in: %2").arg(newThread->first).arg(frame);
    } else {
      newThread->second = tr("[%1] %2 in: %3").arg(newThread->first).arg(name).arg(frame);
    }
  }
  
  std::sort(threadIdAndFrame->begin(), threadIdAndFrame->end(),
            [](const std::pair<int, QString>& a, const std::pair<int, QString>& b) {
    return a.first < b.first;
  });
}

void GDBRunner::ParseStackTrace(
    const std::vector<GDBResult>& results,
    std::vector<StackFrame>* frames) {
  // Example:
  // frame={level=\"0\",addr=\"0x00007ffff73b5360\",func=\"__read_nocancel\",file=\"../sysdeps/unix/syscall-template.S\",fullname=\"/build/eglibc-xkFqqE/eglibc-2.19/io/../sysdeps/unix/syscall-template.S\",line=\"81\"},
  // frame={level=\"1\",addr=\"0x00007ffff73405b0\",func=\"_IO_new_file_underflow\",file=\"fileops.c\",fullname=\"/build/eglibc-xkFqqE/eglibc-2.19/libio/fileops.c\",line=\"613\"},
  // frame={level=\"2\",addr=\"0x00007ffff734153e\",func=\"__GI__IO_default_uflow\",file=\"genops.c\",fullname=\"/build/eglibc-xkFqqE/eglibc-2.19/libio/genops.c\",line=\"435\"},
  // frame={level=\"3\",addr=\"0x00007ffff7337ace\",func=\"_IO_getc\",file=\"getc.c\",fullname=\"/build/eglibc-xkFqqE/eglibc-2.19/libio/getc.c\",line=\"39\"},
  // frame={level=\"4\",addr=\"0x00007ffff7b5768d\",func=\"__gnu_cxx::stdio_sync_filebuf<char, std::char_traits<char> >::underflow()\",from=\"/usr/lib/x86_64-linux-gnu/libstdc++.so.6\"},
  // frame={level=\"5\",addr=\"0x00007ffff7b6509a\",func=\"std::istream::sentry::sentry(std::istream&, bool)\",from=\"/usr/lib/x86_64-linux-gnu/libstdc++.so.6\"},
  // frame={level=\"6\",addr=\"0x00007ffff7b49393\",func=\"std::basic_istream<char, std::char_traits<char> >& std::operator>><char, std::char_traits<char>, std::allocator<char> >(std::basic_istream<char, std::char_traits<char> >&, std::basic_string<char, std::char_traits<char>, std::allocator<char> >&)\",from=\"/usr/lib/x86_64-linux-gnu/libstdc++.so.6\"},
  // frame={level=\"7\",addr=\"0x0000000000400bbb\",func=\"main\",file=\"/home/thomas/Projects/test-project/src/main.cc\",fullname=\"/home/thomas/Projects/test-project/src/main.cc\",line=\"24\"}"
  
  frames->resize(results.size());
  for (int i = 0; i < frames->size(); ++ i) {
    // Set defaults
    (*frames)[i].level = -1;
    (*frames)[i].line = -1;
    
    // Parse attributes
    const std::vector<GDBResult>& frameAttributes = results[i].resultList;
    for (const GDBResult& attribute : frameAttributes) {
      if (attribute.key == "level") {
        (*frames)[i].level = attribute.value.toInt();
      } else if (attribute.key == "line") {
        (*frames)[i].line = attribute.value.toInt();
      } else if (attribute.key == "fullname") {
        (*frames)[i].path = attribute.value;
      } else if (attribute.key == "addr") {
        (*frames)[i].address = attribute.value;
      }
    }
    
    (*frames)[i].shortDescription = QStringLiteral("(%1) %2").arg((*frames)[i].level).arg(GetShortFrameDescription(frameAttributes));
  }
}

QString GDBRunner::GetShortFrameDescription(const std::vector<GDBResult>& frameAttributes) {
  QString func;  // may be absent
  QString addr;  // always present
  QString file;  // may be absent
  QString line;  // may be absent
  QString from;  // may be absent
  
  for (const GDBResult& attribute : frameAttributes) {
    if (attribute.key == "func") {
      func = attribute.value;
    } else if (attribute.key == "addr") {
      addr = attribute.value;
    } else if (attribute.key == "file") {
      file = attribute.value;
    } else if (attribute.key == "line") {
      line = attribute.value;
    } else if (attribute.key == "from") {
      from = attribute.value;
    }
  }
  
  QString description = (func.isEmpty() ? addr : func);
  if (!file.isEmpty()) {
    description += " (" + file + (line.isEmpty() ? "" : (":" + line)) + ")";
  } else if (!from.isEmpty()) {
    description += " (" + from + ")";
  }
  return description;
}
