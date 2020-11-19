// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <vector>

class Document;
namespace glslang {
  class TIntermediate;  // glslang AST
}
class QString;

/// Adds highlighting ranges to the document based on the given TIntermediate AST.
void AddGLSLHighlighting(Document* document, const QString& documentContent, glslang::TIntermediate* ast, const std::vector<unsigned>& lineOffsets);
