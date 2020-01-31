// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>
#include <vector>

#include <QString>

#include "cide/document_range.h"


/// A small block of text within a Document. The block is kept small such that
/// edit operations can be done quickly, without causing changes to other
/// blocks. This also means that all persistent references to characters (for
/// example newlines) should use relative indexing within a block rather than
/// absolute indexing within the document.
class TextBlock {
 public:
  struct NewlineAttributes {
    inline NewlineAttributes(int offset, int attributes)
        : offset(offset), attributes(attributes) {}
    
    /// Offset of the newline character within @a text, or -1 in the special case
    /// of representing the very first line in the document, for which there is
    /// no newline character.
    int offset;
    
    /// A combination of LineAttribute flags combined by logical or.
    int attributes;
  };
  
  struct StyleRange {
    inline StyleRange(const DocumentLocation& start, int rangeIndex)
        : start(start), rangeIndex(rangeIndex) {}
    
    /// Start of this range. The end is equal to the start of the following
    /// range.
    DocumentLocation start;
    
    /// Index of the highlight range (in the Document that contains this
    /// TextBlock) which created this style range. The font style properties can
    /// be looked up there. Non-negative indices are looked up in mRanges,
    /// negative indices are looked up at position [-rangeIndex - 1] in mTopRanges.
    int rangeIndex;
  };
  
  /// Creates a block representing an empty document.
  TextBlock();
  
  /// Copy constructor.
  TextBlock(const TextBlock& other) = default;
  
  /// Creates a block from the given text fragment. isFirst must be set to true
  /// if this is the first fragment of the text.
  TextBlock(const QString& text, bool isFirst);
  
  void Replace(const DocumentRange& range, const QString& newText, TextBlock* prevBlock, TextBlock* nextBlock);
  
  /// Inserts the StyleRange for a highlight range in the document.
  void InsertStyleRange(const DocumentRange& range, int highlightRangeIndex, int layer);
  
  int FindStyleIndexForCharacter(int characterOffset, int layer);
  
  void ClearStyleRanges(int layer);
  
  /// Returns the document text for the given range.
  QString TextForRange(const DocumentRange& range);
  
  /// Splits this block into two or more. The first part stays within this object, while
  /// the second (and potential other) parts are returned as new blocks by this function.
  std::vector<std::shared_ptr<TextBlock>> Split(int desiredBlockSize);
  
  /// Merges the other block into this block by appending the other block.
  void Append(const TextBlock& other);
  
  bool DebugCheckNewlineoffsets(bool isFirst) const;
  
  inline const QString& text() const { return mText; }
  
  inline const std::vector<NewlineAttributes>& lineAttributes() const { return mLineAttributes; }
  inline std::vector<NewlineAttributes>& lineAttributes() { return mLineAttributes; }
  
  inline const std::vector<StyleRange>& styleRanges(int layer) const { return mStyleRanges[layer]; }
  
  inline void SetCachedOffsets(unsigned int startOffset, unsigned int startLine) {
    cachedStartOffset = startOffset;
    cachedStartLine = startLine;
  }
  
  inline unsigned int GetCachedStartOffset() const { return cachedStartOffset; }
  inline unsigned int GetCachedEndOffset() const { return cachedStartOffset + mText.size(); }
  
  inline unsigned int GetCachedStartLine() const { return cachedStartLine; }
  inline unsigned int GetCachedEndLine() const { return cachedStartLine + mLineAttributes.size(); }
  
  
  /// The number of style layers.
  static constexpr int kLayerCount = 2;
  
 private:
  /// The text in this block.
  QString mText;
  
  /// Contains one element (offset, attributes) for each newline character within this block,
  /// where offset is the offset of the newline character within @a text, and attributes is
  /// a combination of LineAttribute flags combined by logical or. The attributes apply to
  /// the line that follows the newline character. In addition, there is one special entry for the
  /// very first line in the document (for which there is no newline character) with offset -1.
  /// The entires are ordered by increasing offset.
  std::vector<NewlineAttributes> mLineAttributes;
  
  /// Partitions the text into ranges, where each range has a consistent (font)
  /// style. The entries are ordered by increasing offset, and always cover the
  /// complete text block. There is also always at least one entry, starting at
  /// the beginning of the text block. There are two layers, a bottom layer [0]
  /// and a top layer [1].
  std::vector<StyleRange> mStyleRanges[kLayerCount];
  
  /// The cached absolute offset of the block's first character in the document.
  /// This is only valid if it has been (re)calculated after the latest change.
  /// The Document stores whether that is the case.
  unsigned int cachedStartOffset;
  
  /// The cached absolute offset of the block's first new line in the document.
  /// This is only valid if it has been (re)calculated after the latest change.
  /// The Document stores whether that is the case.
  unsigned int cachedStartLine;
};
