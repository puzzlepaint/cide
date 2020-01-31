// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <clang-c/Index.h>

/// Wraps a CXIndex instance.
class ClangIndex {
 public:
  ClangIndex();
  ~ClangIndex();
  
  inline CXIndex index() const {
    return mIndex;
  }
  
 private:
  CXIndex mIndex;
};
