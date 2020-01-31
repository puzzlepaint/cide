// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "cide/code_info.h"

struct GotoReferencedCursorOperation : public TUOperationBase {
  QString jumpUrl;
  
  void SetJumpLocation(CXSourceLocation location);
  
  Result OperateOnTU(
      const CodeInfoRequest& request,
      const std::shared_ptr<ClangTU>& TU,
      const QString& canonicalFilePath,
      int invocationLine,
      int invocationCol,
      std::vector<CXUnsavedFile>& unsavedFiles) override;
  
  void FinalizeInQtThread(const CodeInfoRequest& request) override;
};
