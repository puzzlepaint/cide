// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>

/// Runs function @p f in the Qt thread. Blocks until it completes.
/// If there is no QApplication object, the function cannot be run.
/// In this case, false is returned.
bool RunInQtThreadBlocking(
    const std::function<void()>& f);

struct RunInQtThreadAbortData {
  inline RunInQtThreadAbortData()
      : aborted(false) {}
  
  inline void Abort() {
    abortedMutex.lock();
    aborted = true;
    abortedMutex.unlock();
    abortedCondition.notify_all();
  }
  
  std::mutex abortedMutex;
  std::atomic<bool> aborted;
  std::condition_variable abortedCondition;
};

/// Version of RunInQtThreadBlocking() with abort support.
/// 
/// Sometimes, the Qt threads needs to wait for another thread that
/// may call RunInQtThreadBlocking(). This is an issue, since if the
/// Qt thread blocks, it will not be able to execute the function given
/// to RunInQtThreadBlocking(), leading to a deadlock. One solution to
/// this is to make the Qt thread process Qt events while it waits, using
/// an event loop. However, this can have other unintended consequences,
/// since other "random" events may be processed then. To provide a clean
/// solution that only waits for the single thread while not allowing
/// "random" other events to be processed, this version of
/// RunInQtThreadBlocking() has an abort mechanism. To use it, initialize
/// the value pointed to by aborted to false before calling
/// RunInQtThreadBlocking(). To initiate the abort, call:
/// 
///   AbortRunInQtThreadBlocking(&abortedMutex, &aborted, &abortedCondition);
/// 
///   // respectively:
/// 
///   abortedMutex.lock();
///   aborted = true;
///   abortedMutex.unlock();
///   abortedCondition.notify_all();
/// 
/// RunInQtThreadBlocking() will then abort and return false if @p f
/// was not called.
bool RunInQtThreadBlocking(
    const std::function<void()>& f,
    std::mutex* abortedMutex,
    std::atomic<bool>* aborted,
    std::condition_variable* abortedCondition);

/// Version of RunInQtThreadBlocking() with abort support that takes a
/// RunInQtThreadAbortData struct as parameter for convenience.
inline bool RunInQtThreadBlocking(
    const std::function<void()>& f,
    RunInQtThreadAbortData* abortData) {
  return RunInQtThreadBlocking(f, &abortData->abortedMutex, &abortData->aborted, &abortData->abortedCondition);
}

inline void AbortRunInQtThreadBlocking(
    std::mutex* abortedMutex,
    std::atomic<bool>* aborted,
    std::condition_variable* abortedCondition) {
  abortedMutex->lock();
  *aborted = true;
  abortedMutex->unlock();
  abortedCondition->notify_all();
}
