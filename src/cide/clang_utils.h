// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <clang-c/Index.h>
#include <QByteArray>
#include <QString>

#include "cide/document_location.h"
#include "cide/document_range.h"

class MainWindow;

class ClangString {
 public:
  inline ClangString(CXString str)
      : str(str) {}
  
  inline ~ClangString() {
    clang_disposeString(str);
  }
  
  inline QString ToQString() const {
    return QString::fromUtf8(clang_getCString(str));
  }
  
  inline QByteArray ToQByteArray() const {
    return QByteArray(clang_getCString(str));
  }
  
 private:
  CXString str;
};

inline bool IsClassDeclLikeCursorKind(CXCursorKind kind) {
  return kind == CXCursor_StructDecl ||
         kind == CXCursor_UnionDecl ||
         kind == CXCursor_ClassDecl ||
         kind == CXCursor_EnumDecl ||
         kind == CXCursor_ClassTemplate;
}

inline bool IsFunctionDeclLikeCursorKind(CXCursorKind kind) {
  return kind == CXCursor_FunctionDecl ||
         kind == CXCursor_FunctionTemplate ||
         kind == CXCursor_CXXMethod ||
         kind == CXCursor_Constructor ||
         kind == CXCursor_Destructor;
}

inline bool IsVarDeclLikeCursorKind(CXCursorKind kind) {
  return kind == CXCursor_ParmDecl ||
         kind == CXCursor_VarDecl ||
         kind == CXCursor_FieldDecl ||
         kind == CXCursor_TemplateTypeParameter ||
         kind == CXCursor_NonTypeTemplateParameter ||
         kind == CXCursor_TemplateTemplateParameter;
}

inline DocumentLocation CXSourceLocationToDocumentLocation(const CXSourceLocation& location, const std::vector<unsigned> lineOffsets) {
  unsigned line, column;
  clang_getFileLocation(
      location,
      /*CXFile *file*/ nullptr,
      /*unsigned line*/ &line,
      /*unsigned column*/ &column,
      /*unsigned offset*/ nullptr);
  return DocumentLocation((line == 0) ? 0 : (lineOffsets[line - 1] + column - 1));
}

inline DocumentRange CXSourceRangeToDocumentRange(const CXSourceRange& range, const std::vector<unsigned> lineOffsets) {
  if (clang_Range_isNull(range)) {
    return DocumentRange::Invalid();
  }
  
  CXSourceLocation rangeStart = clang_getRangeStart(range);
  CXSourceLocation rangeEnd = clang_getRangeEnd(range);
  
  CXFile startFile;
  unsigned startLine;
  unsigned startColumn;
  clang_getFileLocation(
      rangeStart,
      &startFile,
      /*unsigned line*/ &startLine,
      /*unsigned column*/ &startColumn,
      /*unsigned offset*/ nullptr);
  DocumentLocation startLocation((startLine == 0) ? -1 : (lineOffsets[startLine - 1] + startColumn - 1));
  
  CXFile endFile;
  unsigned endLine;
  unsigned endColumn;
  clang_getFileLocation(
      rangeEnd,
      &endFile,
      /*unsigned line*/ &endLine,
      /*unsigned column*/ &endColumn,
      /*unsigned offset*/ nullptr);
  DocumentLocation endLocation((endLine == 0) ? -1 : (lineOffsets[endLine - 1] + endColumn - 1));
  
  if (!clang_File_isEqual(startFile, endFile)) {
    // This happened in a case where it seemed that only the start of libclang's
    // range was valid, while the end was at offset 0 in a file without a path
    // (apparently clang_getNullLocation()).
    return DocumentRange::Invalid();
  }
  
  return DocumentRange(startLocation, endLocation);
}

// NOTE: This would need to take into account UTF8-UTF16 differences
// inline CXSourceRange DocumentRangeToCXSourceRange(const DocumentRange& range, CXFile file, CXTranslationUnit tu) {
//   CXSourceLocation startLocation = clang_getLocationForOffset(
//       tu, file, range.start.offset);
//   CXSourceLocation endLocation = clang_getLocationForOffset(
//       tu, file, range.end.offset);
//   return clang_getRange(startLocation, endLocation);
// }

/// Wraps clang_getFileName() while ensuring that we consistently get '/' as directory
/// separators, even on Windows, allowing to compare the resulting paths to others
/// without calling Qt's canonicalFilePath() on them.
inline QString GetClangFilePath(CXFile file) {
  QString path = ClangString(clang_getFileName(file)).ToQString();
#ifdef WIN32
  path.replace('\\', '/');
#endif
  return path;
}

inline QByteArray GetClangFilePathAsByteArray(CXFile file) {
  QByteArray path = ClangString(clang_getFileName(file)).ToQByteArray();
#ifdef WIN32
  path.replace('\\', '/');
#endif
  return path;
}

inline QString GetClangText(const CXSourceRange& range, CXTranslationUnit tu) {
  CXFile file;
  CXSourceLocation rangeStart = clang_getRangeStart(range);
  CXSourceLocation rangeEnd = clang_getRangeEnd(range);
  
  unsigned startOffset;
  clang_getFileLocation(
      rangeStart,
      &file,
      /*unsigned line*/ nullptr,
      /*unsigned column*/ nullptr,
      &startOffset);
  if (file == nullptr) {
    return QStringLiteral("");
  }
  
  unsigned endOffset;
  clang_getFileLocation(
      rangeEnd,
      /*CXFile *file*/ nullptr,
      /*unsigned line*/ nullptr,
      /*unsigned column*/ nullptr,
      &endOffset);
  
  const char* text = clang_getFileContents(tu, file, nullptr);
  return QString::fromUtf8(text + startOffset, endOffset - startOffset);
}

void GetAllUnsavedFiles(
    MainWindow* mainWindow,
    std::vector<CXUnsavedFile>* unsavedFiles,
    std::vector<std::string>* unsavedFileContents,
    std::vector<std::string>* unsavedFilePaths);

/// Attempts to find the while, do, for, or switch statement that the given
/// break or continue statement cursor refers to. Returns true if successful,
/// false otherwise. If successful, the cursor to the while, do, etc. statement
/// is returned in @p containerCursor.
bool FindContainerStatementForContinueOrBreak(
    CXCursor continueOrBreakCursor,
    CXCursor* containerCursor);

/// Retrieves the libclang version without the unnecessary "clang version " prefix.
QString GetLibclangVersion();
