// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/clang_index.h"

ClangIndex::ClangIndex() {
  // excludeDeclarationsFromPCH must be set to 0, otherwise the use of the
  // CXTranslationUnit_PrecompiledPreamble flag for parsing will lead to
  // preprocessor cursors being omitted.
  // 
  // displayDiagnostics does not seem to be explained in the documentation, but
  // seems to control whether parse errors are printed to stdout/stderr.
  mIndex = clang_createIndex(/*excludeDeclarationsFromPCH*/ 0, /*displayDiagnostics*/ 0);
  
  clang_CXIndex_setGlobalOptions(mIndex,
      clang_CXIndex_getGlobalOptions(mIndex) |
      CXGlobalOpt_ThreadBackgroundPriorityForIndexing);
}

ClangIndex::~ClangIndex() {
  clang_disposeIndex(mIndex);
}
