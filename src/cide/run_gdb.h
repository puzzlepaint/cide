// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>

struct GDBResult;

struct StackFrame {
  /// A short description of the frame, intended for display.
  QString shortDescription;
  
  /// Line in the source file, or -1 if unknown.
  int line;
  
  /// Path to the source file, or an empty string if unknown.
  QString path;
  
  /// Address of the frame, required to reference it in gdb.
  QString address;
  
  /// Level of the frame, required to reference it in gdb.
  int level;
};

class GDBRunner : public QObject {
 Q_OBJECT
 public:
  GDBRunner();
  
  void Start(const QString& workingDir, const QStringList& programAndArguments);
  
  void Interrupt();
  void Resume();
  
  void Stop();
  
  void WaitForExit();
  
  /// Returns whether the program is running (this includes situations where it
  /// is interrupted). This reflects what has been requested and may not reflect
  /// the actual asynchronous state yet.
  bool IsRunning();
  
  /// Returns whether the program has been run and is currently interrupted.
  /// This reflects what has been requested and may not reflect the actual
  /// asynchronous state yet.
  bool IsInterrupted();
  
  /// Calling this causes an asynchronous request. The result is ready when the
  /// ThreadListUpdated() signal is emitted and can be obtained by calling
  /// GetCurrentThreadId() and GetThreadIdAndFrames().
  void GetThreadList();
  inline int GetCurrentThreadId() const { return currentThreadId; }
  inline const std::vector<std::pair<int, QString>>& GetThreadIdAndFrames() const { return threadIdAndFrame; }
  
  /// Requests a stack trace for a given thread, or for the current thread if
  /// threadId is -1.
  /// Calling this causes an asynchronous request. The result is ready when the
  /// StackTraceUpdated() signal is emitted and can be obtained by calling
  /// GetStackTraceResult().
  void GetStackTrace(int threadId = -1);
  inline const std::vector<StackFrame>& GetStackTraceResult() const { return stackFrames; }
  
  void EvaluateExpression(const QString& expression, int threadId, int frameIndex);
  
 signals:
  void Started();
  void Interrupted();
  void Resumed();
  void Stopped(int exitCode);
  
  void ThreadListUpdated();
  void StackTraceUpdated();
  void ResponseReceived(QString value);
  
 private slots:
  void ReadyReadStdOut();
  void ReadyReadStdErr();
  
 private:
  void ParseLine(const QByteArray& line);
  
  void WaitForOutput(char type, const QString& status);
  
  void ParseThreadInfo(
      const std::vector<GDBResult>& results,
      int* currentThreadId,
      std::vector<std::pair<int, QString>>* threadIdAndFrame);
  void ParseStackTrace(
      const std::vector<GDBResult>& results,
      std::vector<StackFrame>* frames);
  QString GetShortFrameDescription(const std::vector<GDBResult>& frameAttributes);
  
  
  /// Cached thread info results
  int currentThreadId;
  std::vector<std::pair<int, QString>> threadIdAndFrame;
  
  /// Cached stack trace results
  std::vector<StackFrame> stackFrames;
  
  /// Attributes for waiting for confirmation messages from gdb
  char waitingForType;
  QString waitingForStatus;
  bool waitingDone;
  
  int waitingForToken = -1;
  
  /// Caches for gdb output until a newline is encountered
  QByteArray stdoutCache;
  QByteArray stderrCache;
  
  /// The gdb process
  QProcess process;
  
  /// The last known thread group (= process) ID of the program that is run.
  /// SIGINT for interrupting the program must be sent to this process ID. This
  /// is initialized with the gdb process ID and then updated based on gdb output.
  qint64 lastThreadGroupId;
  
  /// Attributes storing the requested state
  bool running;
  bool interrupted;
  
  bool emitStateChanges;
  
  int nextToken = 1;
};
