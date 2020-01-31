// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "cide/code_info.h"

struct GetRightClickInfoOperation : public TUOperationBase {
  // The information that we hope to gather
  QString clickedCursorUSR;
  QString clickedCursorSpelling;
  QString clickedTokenSpelling;
  CXSourceRange clickedTokenSpellingRange;
  bool cursorHasLocalDefinition;
  
  Result OperateOnTU(
      const CodeInfoRequest& request,
      const std::shared_ptr<ClangTU>& TU,
      const QString& canonicalFilePath,
      int invocationLine,
      int invocationCol,
      std::vector<CXUnsavedFile>& unsavedFiles) override;
  
  void FinalizeInQtThread(const CodeInfoRequest& request) override;
};
