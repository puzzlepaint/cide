// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QtCore>

#include "cide/document_location.h"

/// Represents a range of text within a document.
struct DocumentRange {
  inline DocumentRange() = default;
  
  inline DocumentRange(
      const DocumentLocation& start,
      const DocumentLocation& end)
      : start(start),
        end(end) {
    if (end < start) {
      qFatal("Range start (%i) after its end (%i)", start.offset, end.offset);
    }
  }
  
  inline DocumentRange(
      int startOffset,
      int endOffset)
      : start(startOffset),
        end(endOffset) {
    if (end < start) {
      qFatal("Range start (%i) after its end (%i)", start.offset, end.offset);
    }
  }
  
  static DocumentRange Invalid() {
    return DocumentRange(-1, -1);
  }
  
  /// Makes this range encompass both ranges (potentially also covering any
  /// possible empty space between the ranges).
  void Add(const DocumentRange& other);
  
  /// Enlarges this range (if needed) to encompass also the given location.
  void Add(const DocumentLocation& other);
  
  /// Returns whether the range contains the given character.
  /// Note that the concept of the character index is different from a column
  /// (columns are between the characters).
  inline bool ContainsCharacter(int characterOffset) const {
    if (characterOffset < start.offset) {
      return false;
    }
    if (characterOffset >= end.offset) {
      return false;
    }
    return true;
  }
  
  /// Returns whether the range contains the given location.
  /// TODO: An invalid range will "contain" invalid locations. Should this be
  ///       changed (e.g., by using -2 for invalid locations)?
  inline bool Contains(const DocumentLocation& location) const {
    return location >= start && location <= end;
  }
  
  /// Returns whether this range is valid.
  inline bool IsValid() const {
    return end.IsValid();
  }
  
  /// Returns whether this range is invalid.
  inline bool IsInvalid() const {
    return !IsValid();
  }
  
  /// Returns whether this range is empty.
  /// This returns true for invalid ranges.
  inline bool IsEmpty() const {
    return start.offset == end.offset;
  }
  
  inline int size() const {
    return end.offset - start.offset;
  }
  
  DocumentLocation start;
  DocumentLocation end;
};
