// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/problem.h"

#include "cide/clang_utils.h"

Problem::Problem(CXDiagnostic diagnostic, CXTranslationUnit tu, const std::vector<unsigned>& lineOffsets) {
  // Get severity
  CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diagnostic);
  if (severity == CXDiagnostic_Warning) {
    mType = Type::Warning;
  } else {
    mType = Type::Error;
  }
  
  // If this diagnostic is a warning, check whether there is a flag to disable it.
  if (severity == CXDiagnostic_Warning) {
    CXString disableFlag;
    CXString enableFlag = clang_getDiagnosticOption(diagnostic, &disableFlag);
    if (clang_getCString(disableFlag)[0] != 0) {
      flagToDisable = QString::fromUtf8(clang_getCString(disableFlag));
    }
    clang_disposeString(disableFlag);
    clang_disposeString(enableFlag);
  }
  
  ExtractItem(diagnostic, tu, lineOffsets, &mItems);
}

void Problem::AddNote(CXDiagnostic diagnostic, CXTranslationUnit tu, const std::vector<unsigned>& lineOffsets) {
  ExtractItem(diagnostic, tu, lineOffsets, &mItems);
}

void Problem::SetIsRequestedHere() {
  if (!mItems.empty()) {
    mItems.front().text = QObject::tr("[Requested here] %1").arg(mItems.front().text);
  }
}

QString Problem::GetFormattedDescription(const QString& forFile, int forLine) {
  QString text = QStringLiteral("<b>%1:</b>").arg((mType == Problem::Type::Error) ? QObject::tr("Error") : QObject::tr("Warning"));
  AppendItemsToDescription(mItems, forFile, forLine, &text);
  return text;
}

void Problem::AppendItemsToDescription(const std::vector<Item>& items, const QString& forFile, int forLine, QString* text) {
  if (&items == &mItems && items.size() == 1 && items[0].children.empty()) {
    *text += " " + items[0].text.toHtmlEscaped();
    return;
  }
  
  *text += QStringLiteral("<ul style=\"margin: 0px;padding: 0px;\">");
  
  for (int i = 0; i < items.size(); ++ i) {
    const Problem::Item& item = items[i];
    
    bool itemHovered = (forLine + 1 == item.line) && (forFile == item.filePath);
    
    *text += QStringLiteral("<li style=\"margin-left: -30px;padding-left: 0px;\">");
    if (!itemHovered) {
      *text += QStringLiteral("<a href=\"file://%1:%2:%3\">").arg(item.filePath).arg(item.line).arg(item.col);
    }
    *text += QFileInfo(item.filePath).fileName() + QStringLiteral(":") + QString::number(items[i].line);
    if (!itemHovered) {
      *text += QStringLiteral("</a>");
    }
    *text += QStringLiteral("&nbsp;&nbsp;");
    *text += item.text.toHtmlEscaped();
    
    if (!item.children.empty()) {
      AppendItemsToDescription(item.children, forFile, forLine, text);
    }
    
    *text += QStringLiteral("</li>");
  }
  
  *text += QStringLiteral("</ul>");
}

void Problem::ExtractItem(CXDiagnostic diagnostic, CXTranslationUnit tu, const std::vector<unsigned>& lineOffsets, std::vector<Item>* items) {
  // Get fix-its
  unsigned numFixIts = clang_getDiagnosticNumFixIts(diagnostic);
  fixIts.reserve(fixIts.size() + numFixIts);
  for (int fixitIndex = 0; fixitIndex < numFixIts; ++ fixitIndex) {
    CXSourceRange range;
    CXString replacement = clang_getDiagnosticFixIt(diagnostic, fixitIndex, &range);
    
    fixIts.emplace_back();
    FixIt& newFixit = fixIts.back();
    newFixit.range = CXSourceRangeToDocumentRange(range, lineOffsets);
    newFixit.oldText = GetClangText(range, tu);
    newFixit.newText = QString::fromUtf8(clang_getCString(replacement));
    
    clang_disposeString(replacement);
  }
  
  // Build the item
  items->emplace_back();
  CXFile diagnosticFile;
  clang_getFileLocation(
      clang_getDiagnosticLocation(diagnostic),
      &diagnosticFile,
      &items->back().line,
      &items->back().col,
      &items->back().offset);
  items->back().filePath = GetClangFilePath(diagnosticFile);
  
  items->back().text = ClangString(clang_getDiagnosticSpelling(diagnostic)).ToQString();
  
  CXDiagnosticSet children = clang_getChildDiagnostics(diagnostic);
  unsigned numChildren = clang_getNumDiagnosticsInSet(children);
  for (unsigned i = 0; i < numChildren; ++ i) {
    CXDiagnostic child = clang_getDiagnosticInSet(children, i);
    ExtractItem(child, tu, lineOffsets, &items->back().children);
    
    clang_disposeDiagnostic(child);
  }
}
