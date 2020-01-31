// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <clang-c/Index.h>
#include <QString>

#include "cide/clang_index.h"

/// Wraps a libclang translation unit together with the settings that have been
/// used to create it.
class ClangTU {
 public:
  struct IncludeWithModificationTime {
    inline IncludeWithModificationTime(const QByteArray& path, time_t lastModificationTime)
        : path(path),
          lastModificationTime(lastModificationTime) {}
    
    QByteArray path;
    time_t lastModificationTime;
  };
  
  
  ClangTU();
  ~ClangTU();
  
  /// Returns true if this ClangTU instance contains a TU that has been parsed
  /// with the given path and commandLineArgs before, such that it can be
  /// reparsed to obtain an up-to-date TU. If this returns false, the TU has
  /// to be created from scratch instead.
  bool CanBeReparsed(
      const QString& path,
      const std::vector<QByteArray>& commandLineArgs);
  
  /// Sets the contents of this ClangTU instance.
  void Set(
      CXTranslationUnit TU,
      const std::vector<QByteArray>& commandLineArgs);
  
  QString GetPath();
  
  inline const CXTranslationUnit& TU() const { return mTU; }
  inline CXIndex index() const { return mIndex.index(); }
  
  inline unsigned int GetParseStamp() const { return parseStamp; }
  inline void SetParseStamp(unsigned int value) { parseStamp = value; }
  
  inline bool isInitialized() const { return initialized; }
  
  inline std::vector<IncludeWithModificationTime>& GetIncludes() { return includesWithModificationTimes; }
  inline const std::vector<QByteArray>& GetCommandLineArgs() const { return mCommandLineArgs; }
  
 private:
  /// List of included files and their last modification times as given by
  /// libclang. This may be used to (approximately) check whether the preamble
  /// has been modified or not (it does not account for adding/removing macros
  /// in front of includes that may change their behavior).
  std::vector<IncludeWithModificationTime> includesWithModificationTimes;
  
  /// Command-line arguments that were used to parse the TU
  std::vector<QByteArray> mCommandLineArgs;
  unsigned int parseStamp;
  CXTranslationUnit mTU;
  bool initialized;
  
  // We create a CXIndex for every TU in the hope that this avoids issues
  // with multithreaded access to libclang functionality.
  ClangIndex mIndex;
};

/// Stores a pool of libclang translation units (TUs). At least two TUs should
/// be used for a document that is edited, such that one remains available for
/// code completion / AST queries while the other one is being used for re-parsing.
class ClangTUPool {
 public:
  ClangTUPool(int numTUs);
  
  ~ClangTUPool();
  
  /// If any free TU is available, takes it out of the pool and returns it.
  /// Prefers the least up to date TU in case multiple ones are available.
  /// 
  /// The idea is to use the least up to date ones for re-parsing.
  std::shared_ptr<ClangTU> TakeLeastUpToDateTU();
  
  /// If any free TU is available, takes it out of the pool and returns it.
  /// Prefers the most up to date TU in case multiple ones are available.
  /// 
  /// The idea is to use the most up to date ones for code completion and AST
  /// queries.
  std::shared_ptr<ClangTU> TakeMostUpToDateTU();
  
  /// Inserts the TU into the pool, making it available to the Take...()
  /// functions again.
  void PutTU(const std::shared_ptr<ClangTU>& TU, bool reparsed);
  
 private:
  std::mutex accessMutex;
  unsigned int parseCounter;
  std::vector<std::shared_ptr<ClangTU>> mTUs;
};
