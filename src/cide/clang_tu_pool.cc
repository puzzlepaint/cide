// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/clang_tu_pool.h"

#include "cide/clang_utils.h"

ClangTU::ClangTU()
    : parseStamp(0),
      initialized(false) {}

ClangTU::~ClangTU() {
  if (initialized) {
    clang_disposeTranslationUnit(mTU);
  }
}

bool ClangTU::CanBeReparsed(const QString& path, const std::vector<QByteArray>& commandLineArgs) {
  if (!initialized) {
    return false;
  }
  if (path != GetPath()) {
    return false;
  }
  if (commandLineArgs.size() != mCommandLineArgs.size()) {
    return false;
  }
  for (int i = 0; i < mCommandLineArgs.size(); ++ i) {
    if (mCommandLineArgs[i] != commandLineArgs[i]) {
      return false;
    }
  }
  
  return true;
}

void ClangTU::Set(CXTranslationUnit TU, const std::vector<QByteArray>& commandLineArgs) {
  if (initialized) {
    clang_disposeTranslationUnit(mTU);
  }
  
  mTU = TU;
  mCommandLineArgs = commandLineArgs;
  initialized = true;
}

QString ClangTU::GetPath() {
  return ClangString(clang_getTranslationUnitSpelling(mTU)).ToQString();
}


ClangTUPool::ClangTUPool(int numTUs)
    : parseCounter(1),
      mTUs(numTUs) {
  for (int i = 0; i < numTUs; ++ i) {
    mTUs[i].reset(new ClangTU());
  }
}

ClangTUPool::~ClangTUPool() {}

std::shared_ptr<ClangTU> ClangTUPool::TakeLeastUpToDateTU() {
  std::unique_lock<std::mutex> lock(accessMutex);
  
  unsigned int minParseStamp = std::numeric_limits<unsigned int>::max();
  int resultIndex = -1;
  
  for (int i = 0; i < mTUs.size(); ++ i) {
    if (resultIndex == -1 || mTUs[i]->GetParseStamp() < minParseStamp) {
      minParseStamp = mTUs[i]->GetParseStamp();
      resultIndex = i;
    }
  }
  
  if (resultIndex >= 0) {
    std::shared_ptr<ClangTU> result = mTUs[resultIndex];
    mTUs.erase(mTUs.begin() + resultIndex);
    return result;
  } else {
    return nullptr;
  }
}

std::shared_ptr<ClangTU> ClangTUPool::TakeMostUpToDateTU() {
  std::unique_lock<std::mutex> lock(accessMutex);
  
  unsigned int maxParseStamp = 0;
  int resultIndex = -1;
  
  for (int i = 0; i < mTUs.size(); ++ i) {
    if (resultIndex == -1 || mTUs[i]->GetParseStamp() > maxParseStamp) {
      maxParseStamp = mTUs[i]->GetParseStamp();
      resultIndex = i;
    }
  }
  
  if (resultIndex >= 0) {
    std::shared_ptr<ClangTU> result = mTUs[resultIndex];
    mTUs.erase(mTUs.begin() + resultIndex);
    return result;
  } else {
    return nullptr;
  }
}

void ClangTUPool::PutTU(const std::shared_ptr<ClangTU>& TU, bool reparsed) {
  if (reparsed) {
    TU->SetParseStamp(parseCounter);
    ++ parseCounter;
  }
  
  std::unique_lock<std::mutex> lock(accessMutex);
  mTUs.push_back(TU);
}
