// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/qt_thread.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

#include <QApplication>
#include <QDebug>
#include <QThread>
#include <QTimer>

bool RunInQtThreadBlocking(
    const std::function<void()>& f) {
  // If there is no qApp, we cannot run the function.
  if (!qApp) {
    qDebug() << "Error: RunInQtThreadBlocking(): No qApp exists. Not running the function.";
    return false;
  }
  
  // If the current thread is the Qt thread, we can run the function directly.
  if (QThread::currentThread() == qApp->thread()) {
    f();
    return true;
  }
  
  // Use a queued connection to run the function.
  std::mutex done_mutex;
  std::condition_variable done_condition;
  std::atomic<bool> done;
  done = false;
  
  QTimer* timer = new QTimer();
  timer->moveToThread(qApp->thread());
  timer->setSingleShot(true);
  QObject::connect(timer, &QTimer::timeout, [&]() {
    f();
    timer->deleteLater();
    
    std::lock_guard<std::mutex> lock(done_mutex);
    done = true;
    done_condition.notify_all();
  });
  QMetaObject::invokeMethod(timer, "start", Qt::QueuedConnection, Q_ARG(int, 0));
  
  std::unique_lock<std::mutex> lock(done_mutex);
  while (!done) {
    done_condition.wait(lock);
  }
  return true;
}

bool RunInQtThreadBlocking(
    const std::function<void()>& f,
    std::mutex* abortedMutex,
    std::atomic<bool>* aborted,
    std::condition_variable* abortedCondition) {
  // If aborted is already true, exit right away.
  if (aborted && *aborted) {
    return false;
  }
  
  // If there is no qApp, we cannot run the function.
  if (!qApp) {
    qDebug() << "Error: RunInQtThreadBlocking(): No qApp exists. Not running the function.";
    return false;
  }
  
  // If the current thread is the Qt thread, we can run the function directly.
  if (QThread::currentThread() == qApp->thread()) {
    f();
    return true;
  }
  
  // Use a queued connection to run the function.
  std::mutex done_mutex;
  std::mutex* mutexToUse = abortedMutex ? abortedMutex : &done_mutex;
  std::condition_variable done_condition;
  std::condition_variable* conditionToUse = (abortedCondition != nullptr) ? abortedCondition : &done_condition;
  std::atomic<bool> done;
  done = false;
  
  std::shared_ptr<std::mutex> abortExecutionMutex(new std::mutex());
  std::shared_ptr<std::atomic<bool>> abortExecution(new std::atomic<bool>());
  *abortExecution = false;
  std::shared_ptr<std::atomic<bool>> lambdaExited(new std::atomic<bool>());
  *lambdaExited = false;
  
  QTimer* timer = new QTimer();
  timer->moveToThread(qApp->thread());
  timer->setSingleShot(true);
  QObject::connect(timer, &QTimer::timeout, [&, timer, abortExecutionMutex, abortExecution, lambdaExited]() {
    std::lock_guard<std::mutex> abortExecutionLock(*abortExecutionMutex);
    if (*abortExecution) {
      timer->deleteLater();
      *lambdaExited = true;
      return;
    }
    
    std::lock_guard<std::mutex> lock(*mutexToUse);
    if (*abortExecution) {
      timer->deleteLater();
      *lambdaExited = true;
      return;
    }
    
    f();
    
    timer->deleteLater();
    
    done = true;
    conditionToUse->notify_all();
  });
  QMetaObject::invokeMethod(timer, "start", Qt::QueuedConnection, Q_ARG(int, 0));
  
  // Wait for the function to finish, or for RunInQtThreadBlocking() to be aborted.
  std::unique_lock<std::mutex> lock(*mutexToUse);
  while (!done && (aborted == nullptr || !*aborted)) {
    conditionToUse->wait(lock);
  }
  
  // If the function was aborted, some more handling is required to ensure that the
  // lambda (if called later) does not access invalid objects.
  if (!done) {
    // Tell the lambda to exit instead of running the function.
    *abortExecution = true;
    
    // At this point, we must make sure that the lambda will not access mutexToUse anymore
    // since it might get deleted once this function exits.
    if (abortExecutionMutex->try_lock()) {
      // The lambda did not start yet. It is safe to exit this function,
      // since once the lambda starts, it will abort, since *abortExecution == true.
      abortExecutionMutex->unlock();
    } else {
      // The lambda started and locked the abortExecutionMutex already.
      // We must wait for it to reach a safe state.
      lock.unlock();  // allow the lambda to continue in case it waits for the *mutexToUse lock
      
      // Wait for the lambda to exit to ensure that mutexToUse is not accessed by it anymore.
      while (!*lambdaExited) {
        QThread::yieldCurrentThread();
      }
    }
  }
  return done;
}
