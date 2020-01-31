// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/document_range.h"

void DocumentRange::Add(const DocumentRange& other) {
  if (IsInvalid()) {
    *this = other;
  } else if (other.IsInvalid()) {
    // No action required.
  } else {
    start = start.Min(other.start);
    end = end.Max(other.end);
  }
}

void DocumentRange::Add(const DocumentLocation& other) {
  if (IsInvalid()) {
    *this = DocumentRange(other.offset, other.offset);
  } else if (other < start) {
    start = other;
  } else if (other > end) {
    end = other;
  }
}
