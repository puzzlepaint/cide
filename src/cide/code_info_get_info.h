// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "cide/code_info.h"

struct GetInfoOperation : public TUOperationBase {
  // The information that we hope to gather
  QString htmlString;
  CXSourceRange infoTokenRange;
  std::vector<CXSourceRange> referenceRanges;
  QUrl helpUrl;
  
  Result OperateOnTU(
      const CodeInfoRequest& request,
      const std::shared_ptr<ClangTU>& TU,
      const QString& canonicalFilePath,
      int invocationLine,
      int invocationCol,
      std::vector<CXUnsavedFile>& unsavedFiles) override;
  
  void FinalizeInQtThread(const CodeInfoRequest& request) override;
};
