// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "cide/code_info.h"

struct CodeCompletionOperation : public TUOperationBase {
  CXCodeCompleteResults* results = nullptr;
  bool success = false;
  
  ~CodeCompletionOperation();
  
  void InitializeInQtThread(
      const CodeInfoRequest& request,
      const std::shared_ptr<ClangTU>& TU,
      const QString& canonicalFilePath,
      int invocationLine,
      int invocationCol,
      std::vector<CXUnsavedFile>& unsavedFiles) override;
  
  Result OperateOnTU(
      const CodeInfoRequest& request,
      const std::shared_ptr<ClangTU>& TU,
      const QString& canonicalFilePath,
      int invocationLine,
      int invocationCol,
      std::vector<CXUnsavedFile>& unsavedFiles) override;
  
  void FinalizeInQtThread(const CodeInfoRequest& request) override;
  
 private:
  void CreateCodeCompletionItems();
  
  void CreateImplementationCompletionItems(
      const CodeInfoRequest& request,
      const std::shared_ptr<ClangTU>& TU,
      const QString& canonicalFilePath,
      int invocationLine,
      int invocationCol);
  
  std::vector<CompletionItem> items;
  std::vector<ArgumentHintItem> hints;
  int currentParameter = -1;
  
  bool cursorIsOutsideOfAnyClassOrFunctionDefinition;
  QString correspondingHeaderPath;
  CXFile correspondingHeader;
};
