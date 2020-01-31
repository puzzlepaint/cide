// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <clang-c/Index.h>
#include <QColor>

#include "cide/document_range.h"

class Document;


/// Data that needs to be passed to the visitor function visiting libclang's AST,
/// VisitClangAST_AddHighlightingAndContexts() (and related functions).
struct HighlightingASTVisitorData {
  /// The document being parsed.
  Document* document;
  
  CXTranslationUnit TU;
  CXFile file;
  std::vector<unsigned>* lineOffsets;
  
  /// Ranges of all comments in the document.
  std::vector<DocumentRange> commentRanges;
  
  /// Ranges of all macro expansions, storing the start and end offsets returned
  /// by libclang.
  std::vector<std::pair<unsigned, unsigned>> macroExpansionRanges;
  
  /// State for highlighting "#pragma once" (consisting of a sequence of tokens)
  int pragmaOnceState = 0;
  
  // Per-variable coloring for local variables
  bool perVariableColoring;
  int variableCounterPerFunction = 0;
  /// Maps file offsets of variable definitions to their assigned colors.
  std::unordered_map<unsigned, QColor> perVariableColorMap;
  
  // NOTE: For debug printing only:
  CXCursor prevCursor;
  std::string indent;
  std::vector<CXCursor> parentCursors;
};


/// Finds comment keywords within comment tokens.
void FindCommentMarkerRanges(CXToken* tokens, unsigned numTokens, HighlightingASTVisitorData* visitorData, std::vector<DocumentRange>* ranges);

/// Adds highlight ranges for the given comment marker ranges found by FindCommentMarkerRanges().
void ApplyCommentMarkerRanges(Document* document, const std::vector<DocumentRange>& ranges);

/// Adds highlighting ranges to the document based on the given tokens.
void AddTokenHighlighting(Document* document, CXToken* tokens, unsigned numTokens, HighlightingASTVisitorData* visitorData);

/// AST visitor function for libclang to add syntax highlighting ranges and extract "contexts" for navigation.
CXChildVisitResult VisitClangAST_AddHighlightingAndContexts(CXCursor cursor, CXCursor parent, CXClientData client_data);
