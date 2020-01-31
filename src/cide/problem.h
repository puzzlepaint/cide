// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <vector>

#include <clang-c/Index.h>
#include <QString>

#include "cide/document_range.h"

struct ProblemRange {
  inline ProblemRange(const DocumentRange& range, int problemIndex)
      : range(range), problemIndex(problemIndex) {}
  
  inline bool operator< (const ProblemRange& other) const {
    return range.start < other.range.start;
  }
  
  DocumentRange range;
  int problemIndex;
};

class Problem {
 public:
  enum class Type {
    Warning = 0,
    Error
  };
  
  /// Represents a part of the problem description.
  struct Item {
    /// Description of this item.
    QString text;
    
    /// Path of the file that this item refers to.
    QString filePath;
    
    /// Location that this item refers to (line, column, offset).
    /// Line and column are 1-based.
    unsigned line;
    unsigned col;
    unsigned offset;
    
    /// Child items.
    std::vector<Item> children;
  };
  
  /// Stores a fix-it item that was suggested by libclang. To apply the item,
  /// the given range must be replaced with the given text.
  struct FixIt {
    /// Original text within the range.
    QString oldText;
    
    /// Text that the range must be replaced with.
    QString newText;
    
    /// Range that must be replaced. Note that this applies to the version of the
    /// document that has been parsed and may need to be adapted if the document
    /// has changed.
    DocumentRange range;
  };
  
  /// Creates a problem for a libclang diagnostic.
  Problem(CXDiagnostic diagnostic, CXTranslationUnit tu, const std::vector<unsigned>& lineOffsets);
  
  /// Appends a libclang diagnostic of type CXDiagnostic_Note to this problem.
  void AddNote(CXDiagnostic diagnostic, CXTranslationUnit tu, const std::vector<unsigned>& lineOffsets);
  
  /// Marks this problem as a problem that is "requested here" (indicating that
  /// the actual problem occurs somewhere else, but is requested by a template
  /// instantiation here).
  void SetIsRequestedHere();
  
  QString GetFormattedDescription(const QString& forFile, int forLine);
  
  inline Type type() const { return mType; }
  
  inline const std::vector<Item>& items() const { return mItems; }
  
  inline const std::vector<FixIt>& fixits() const { return fixIts; }
  
 private:
  void AppendItemsToDescription(const std::vector<Item>& items, const QString& forFile, int forLine, QString* text);
  
  void ExtractItem(CXDiagnostic diagnostic, CXTranslationUnit tu, const std::vector<unsigned>& lineOffsets, std::vector<Item>* items);
  
  
  /// Problem type (warning or error)
  Type mType;
  
  /// If not empty, holds the compiler flag to disable this problem (for warnings)
  QString flagToDisable;
  
  /// List of items that describe the problem. Each item holds a document location
  /// and a description text, and may contain children.
  std::vector<Item> mItems;
  
  /// List of fix-it items.
  std::vector<FixIt> fixIts;
};
