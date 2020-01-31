// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

struct DocumentLocation {
  inline DocumentLocation() = default;
  
  inline DocumentLocation(int offset)
      : offset(offset) {}
  
  static DocumentLocation Invalid() {
    return DocumentLocation(-1);
  }
  
  inline bool IsInvalid() const {
    return offset < 0;
  }
  
  inline bool IsValid() const {
    return !IsInvalid();
  }
  
  /// Returns the location which is ealier in the document.
  /// Assumes that both locations are valid.
  DocumentLocation Min(const DocumentLocation& other) {
    if (offset < other.offset) {
      return *this;
    } else {
      return other;
    }
  }
  
  /// Returns the location which is later in the document.
  /// Assumes that both locations are valid.
  DocumentLocation Max(const DocumentLocation& other) {
    if (offset > other.offset) {
      return *this;
    } else {
      return other;
    }
  }
  
  inline DocumentLocation operator+ (int other) const {
    return DocumentLocation(offset + other);
  }
  
  inline DocumentLocation& operator+= (int other) {
    offset += other;
    return *this;
  }
  
  inline DocumentLocation operator- (int other) const {
    return DocumentLocation(offset - other);
  }
  
  inline DocumentLocation& operator-= (int other) {
    offset -= other;
    return *this;
  }
  
  inline bool operator< (const DocumentLocation& other) const {
    return offset < other.offset;
  }
  
  inline bool operator<= (const DocumentLocation& other) const {
    return offset <= other.offset;
  }
  
  inline bool operator> (const DocumentLocation& other) const {
    return offset > other.offset;
  }
  
  inline bool operator>= (const DocumentLocation& other) const {
    return offset >= other.offset;
  }
  
  inline bool operator== (const DocumentLocation& other) const {
    return offset == other.offset;
  }
  
  inline bool operator!= (const DocumentLocation& other) const {
    return offset != other.offset;
  }
  
  inline DocumentLocation& operator++ () {
    ++ offset;
    return *this;
  }
  
  inline DocumentLocation& operator-- () {
    -- offset;
    return *this;
  }
  
  int offset;
};
