// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/text_block.h"

#include <QStringBuilder>


TextBlock::TextBlock() {
  mLineAttributes.emplace_back(-1, 0);
  for (int layer = 0; layer < kLayerCount; ++ layer) {
    mStyleRanges[layer].emplace_back(0, 0);
  }
}

TextBlock::TextBlock(const QString& text, bool isFirst) {
  mText = text;
  
  if (isFirst) {
    mLineAttributes.emplace_back(-1, 0);
  }
  
  for (int c = 0, size = mText.size(); c < size; ++ c) {
    if (mText[c] == '\n') {
      mLineAttributes.emplace_back(c, 0);
    }
  }
  
  for (int layer = 0; layer < kLayerCount; ++ layer) {
    mStyleRanges[layer].emplace_back(0, 0);
  }
}

void TextBlock::Replace(const DocumentRange& range, const QString& newText, TextBlock* prevBlock, TextBlock* nextBlock) {
  // If the number of newlines in the old and new text differs, update the lineAttributes.
  // Since we don't know how the lines move in case the replacement represents some form
  // of movement (we might try to guess it with some heuristics though ...), we always reset
  // all affected line attributes to zero in case the number of lines changes.
  
  // Example sketch:
  //   (newline1)(newline2)|oldLineRangeStart|(newline3)|oldLineRangeEnd|(newline4)
  // Values for the sketch:
  //   mLineAttributes.size() == 4
  //   oldLineRangeStart = 2 (between element 2 and 3)
  //   oldLineRangeEnd = 3 (between element 3 and 4)
  //   The element which is replaced is element 3.
  int numOldNewlines = 0;  // within the replaced range
  int oldLineRangeStart = -1;  // index of a place *between* two elements
  int oldLineRangeEnd = -1;  // index of a place *between* two elements
  for (int a = 0, size = mLineAttributes.size(); a < size; ++ a) {
    if (mLineAttributes[a].offset >= range.end.offset) {
      if (oldLineRangeEnd == -1) {
        oldLineRangeEnd = a;
        if (oldLineRangeStart == -1) {
          oldLineRangeStart = a;
        }
      }
//       qDebug() << "Found line after range: mLineAttributes[a (" << a << ")].offset (" << mLineAttributes[a].offset << ") >= range.end.offset (" << range.end.offset << ")";
      break;
    }
    if (mLineAttributes[a].offset < range.start.offset) {
      continue;
    }
    
    if (oldLineRangeStart == -1) {
      oldLineRangeStart = a;
    }
//     qDebug() << "Old newline:" << a << " (mLineAttributes[a].offset:" << mLineAttributes[a].offset << ")";
    ++ numOldNewlines;
  }
  if (oldLineRangeEnd == -1) {
    oldLineRangeEnd = mLineAttributes.size();
    if (oldLineRangeStart == -1) {
      oldLineRangeStart = mLineAttributes.size();
    }
//     qDebug() << "Found no line after range.";
  }
  if (numOldNewlines != oldLineRangeEnd - oldLineRangeStart) {
    qFatal("numOldNewlines (%i) != oldLineRangeEnd - oldLineRangeStart (%i)", numOldNewlines, oldLineRangeEnd - oldLineRangeStart);
  }
  
//   qDebug() << "oldLineRangeStart" << oldLineRangeStart;
//   qDebug() << "oldLineRangeEnd" << oldLineRangeEnd;
  
  int numNewNewlines = newText.count('\n');
  
//   qDebug() << "numOldNewlines" << numOldNewlines;
//   qDebug() << "numNewNewlines" << numNewNewlines;
  
  // Update the mLineAttributes count
  if (numOldNewlines < numNewNewlines) {
    mLineAttributes.insert(
        mLineAttributes.begin() + oldLineRangeEnd,
        numNewNewlines - numOldNewlines,
        NewlineAttributes(-1, -1));
  } else if (numOldNewlines > numNewNewlines) {
    mLineAttributes.erase(
        mLineAttributes.begin() + (oldLineRangeEnd - numOldNewlines + numNewNewlines),
        mLineAttributes.begin() + oldLineRangeEnd);
  }
  
  // Go over all lineAttributes within the replaced range and update their values.
  int lastResultingLine = oldLineRangeStart + numNewNewlines - 1;
//   qDebug() << "lastResultingLine:" << lastResultingLine;
  if (numNewNewlines > 0) {
    int cursor = 0;
    for (int a = oldLineRangeStart; a <= lastResultingLine; ++ a) {
      // Find the next newline in the replaced text.
      while (true) {
        if (newText[cursor] == '\n') {
          mLineAttributes[a].offset = cursor + range.start.offset;
//           qDebug() << "Newline" << a << "within replaced range has been set to offset" << mLineAttributes[a].offset;
          if (numOldNewlines != numNewNewlines) {
            mLineAttributes[a].attributes = 0;
          }
          ++ cursor;
          break;
        }
        ++ cursor;
      }
    }
  }
  
  // Update the newline offsets of all lineAttributes after the replaced range.
  int shift = newText.size() - range.size();
  for (int a = lastResultingLine + 1, size = mLineAttributes.size(); a < size; ++ a) {
//     qDebug() << "Shifting newline" << a << "from" << mLineAttributes[a].offset << "to" << (mLineAttributes[a].offset + shift);
    mLineAttributes[a].offset += shift;
  }
  
//   qDebug() << "Newlines in block after replacement:";
//   for (int a = 0, size = mLineAttributes.size(); a < size; ++ a) {
//     qDebug() << "[" << a << "] at offset " << mLineAttributes[a].offset;
//   }
  
  // Update the style ranges with the following heuristics:
  // * Delete any styles that are in the replaced range.
  // * By default, fill the new text range with the default style.
  // * Non-default ranges that end directly at the border of the new text get
  //   extended into the new text range as far as there are only latin
  //   characters.
  // * If this applies to both sides and there are only latin characters in the
  //   new text, always prefer the style from the left side to resolve the
  //   ambiguity.
  for (int layer = 0; layer < kLayerCount; ++ layer) {
    auto& styleRanges = mStyleRanges[layer];
    
    // qDebug() << "=== Debugging style update on replacement ===";
    // qDebug() << "Initial block text: " << mText;
    
    // First step: Adjust ranges after the replaced range, and delete ranges
    // within it.
    int minStyleToDelete = std::numeric_limits<int>::max();
    int maxStyleToDelete = -1;
    
    int styleBeforeReplacement = -1;
    int styleAfterReplacement = -1;
    
    DocumentLocation currentStyleEnd = mText.size();
    for (int s = styleRanges.size() - 1; s >= 0; -- s) {
      StyleRange& style = styleRanges[s];
      DocumentLocation prevStyleEnd = style.start;
      
      if (style.start >= range.end) {
        // The style starts after the replaced region, move it.
  //       qDebug() << "Style" << s << " (highlight" << style.rangeIndex << ") starts after or at the end of the replaced region, move it.";
        if (style.start == range.end) {
          styleAfterReplacement = s;
        }
        if (style.start == range.start) {
          style.start += shift;
          if (s > 0) {
            styleBeforeReplacement = s - 1;
          }
          break;
        }
        style.start += shift;
      } else if (style.start <= range.start) {
        // The style starts before the replaced region, break.
  //       qDebug() << "Style" << s << " (highlight" << style.rangeIndex << ") starts before the replaced region";
        styleBeforeReplacement = s;
        if (currentStyleEnd > range.end) {
          styleAfterReplacement = s;
        }
        break;
      } else if (currentStyleEnd > range.end) {
        // The style starts in the replaced region and overlaps the replaced
        // region's right border. Cut it such that it starts on the right border
        // after replacement.
  //       qDebug() << "Style" << s << " (highlight" << style.rangeIndex << ") starts in the replaced region and overlaps the replaced region's right border. Cut it.";
        style.start = range.start + newText.size();
        styleAfterReplacement = s;
      } else {
        // The style starts in the replaced region and does not overlap its right
        // border. Delete it.
  //       qDebug() << "Style" << s << " (highlight" << style.rangeIndex << ") starts in the replaced region and does not overlap its right border. Delete it.";
        minStyleToDelete = std::min(minStyleToDelete, s);
        maxStyleToDelete = std::max(maxStyleToDelete, s);
      }
      
      currentStyleEnd = prevStyleEnd;
    }
    if (maxStyleToDelete >= 0) {
      styleRanges.erase(styleRanges.begin() + minStyleToDelete,
                        styleRanges.begin() + (maxStyleToDelete + 1));
      if (styleAfterReplacement != -1) {
        styleAfterReplacement -= (maxStyleToDelete - minStyleToDelete + 1);
      }
      if (styleBeforeReplacement >= 0 &&
          styleAfterReplacement == styleBeforeReplacement + 1 &&
          styleRanges[styleBeforeReplacement].rangeIndex == styleRanges[styleAfterReplacement].rangeIndex) {
        styleRanges.erase(styleRanges.begin() + styleAfterReplacement);
        styleAfterReplacement = styleBeforeReplacement;
      }
    }
    
  //   qDebug() << "styleBeforeReplacement" << styleBeforeReplacement << "styleAfterReplacement" << styleAfterReplacement;
    
    // Second step: Handle the new text region.
    // Get the previous style from the previous block if necessary; if there is
    // no previous style, insert the default style (there always must be a style
    // starting at block offset 0).
    if (styleBeforeReplacement == -1 && (styleRanges.empty() || styleRanges.front().start > 0)) {
      int prevHighlightIndex = 0;
      if (prevBlock) {
        prevHighlightIndex = prevBlock->mStyleRanges[layer].back().rangeIndex;
      }
      
      if (!styleRanges.empty() && styleRanges.front().rangeIndex == prevHighlightIndex) {
  //       qDebug() << "Moving the first style range to the start since its highlight range index matches";
        
        styleRanges.front().start = 0;
      } else {
  //       qDebug() << "Inserting a new style start at position 0 with highlight range index" << prevHighlightIndex;
        
        styleRanges.insert(styleRanges.begin(), StyleRange(0, prevHighlightIndex));
        if (styleAfterReplacement != -1) {
          ++ styleAfterReplacement;
        }
      }
      styleBeforeReplacement = 0;
    }
    
    if (!newText.isEmpty()) {
      // If necessary, insert a region of the default style for inserted non-latin
      // characters.
      int nonLatinStart = -1;
      int nonLatinEnd = -1;
      for (int i = 0, size = newText.size(); i < size; ++ i) {
        if (!newText[i].isLetterOrNumber()) {
          if (nonLatinStart == -1) {
            nonLatinStart = i;
          }
          nonLatinEnd = i;
        }
      }
      
  //     qDebug() << "nonLatinStart" << nonLatinStart << "nonLatinEnd" << nonLatinEnd;
      
      if (nonLatinStart == -1) {
        // If the style to the right is a non-default style and the style to the
        // left is the default style, move the right one to the start of the
        // replacement.
        bool rightIsDefaultStyle;
        if (styleAfterReplacement >= 0) {
          rightIsDefaultStyle = (styleRanges[styleAfterReplacement].rangeIndex == 0);
        } else if (nextBlock) {
          rightIsDefaultStyle = nextBlock->mStyleRanges[layer].front().rangeIndex == 0;
        } else {
          rightIsDefaultStyle = true;
        }
        
        bool leftIsDefaultStyle;
        if (styleBeforeReplacement >= 0) {
          leftIsDefaultStyle = (styleRanges[styleBeforeReplacement].rangeIndex == 0);
        } else if (prevBlock) {
          leftIsDefaultStyle = prevBlock->mStyleRanges[layer].back().rangeIndex == 0;
        } else {
          leftIsDefaultStyle = true;
        }
        
        if (leftIsDefaultStyle && !rightIsDefaultStyle) {
          if (styleAfterReplacement >= 0) {
  //           qDebug() << "leftIsDefaultStyle && !rightIsDefaultStyle (case A)";
            styleRanges[styleAfterReplacement].start = range.start;
            if (styleBeforeReplacement >= 0 &&
                styleRanges[styleBeforeReplacement].start == styleRanges[styleAfterReplacement].start) {
              styleRanges.erase(styleRanges.begin() + styleBeforeReplacement);
            }
          } else if (nextBlock) {
  //           qDebug() << "leftIsDefaultStyle && !rightIsDefaultStyle (case B)";
            if (styleRanges.back().start == range.start) {
              styleRanges.back().rangeIndex = nextBlock->mStyleRanges[layer].front().rangeIndex;
            } else {
              styleRanges.emplace_back(range.start, nextBlock->mStyleRanges[layer].front().rangeIndex);
            }
            rightIsDefaultStyle = nextBlock->mStyleRanges[layer].front().rangeIndex == 0;
          }
        }
      } else {
        // Adjust the next style. The next style always starts at the end of the new
        // text range (but possibly in the next block).
        DocumentLocation nextStyleStart = range.start + nonLatinEnd + 1;
        if (styleAfterReplacement != -1 &&
            styleAfterReplacement == styleBeforeReplacement) {
          if (styleRanges[styleAfterReplacement].rangeIndex != 0) {
  //           qDebug() << "End style added (case A)";
            styleRanges.insert(styleRanges.begin() + (styleAfterReplacement + 1), StyleRange(nextStyleStart, styleRanges[styleAfterReplacement].rangeIndex));
          }
        } else if (styleAfterReplacement >= 0) {
  //         qDebug() << "End style adjusted (case B)";
          styleRanges[styleAfterReplacement].start = nextStyleStart;
        } else if (nextBlock) {
  //         qDebug() << "End style adjusted (case C)";
          int nextHighlightIndex = nextBlock->mStyleRanges[layer].front().rangeIndex;
          if (styleRanges.back().rangeIndex != nextHighlightIndex) {
            styleRanges.emplace_back(nextStyleStart, nextHighlightIndex);
          }
        } else {
          // Nothing to do, we simply let the default style expand until the end
        }
        
        // Insert a region of the default style (if the style there is not already
        // the default).
        if (styleBeforeReplacement < 0) {
  //         qDebug() << "Error: styleBeforeReplacement < 0, I think that this is not supposed to happen";
        } else {
          if (styleRanges[styleBeforeReplacement].rangeIndex != 0) {
            if (styleAfterReplacement >= 0 && styleRanges[styleAfterReplacement].rangeIndex == 0) {
  //             qDebug() << "Moving the style after the replacement to the left to cover the default style in the middle";
              styleRanges[styleAfterReplacement].start = range.start + nonLatinStart;
              if (styleBeforeReplacement >= 0 &&
                  styleRanges[styleBeforeReplacement].start == styleRanges[styleAfterReplacement].start) {
                styleRanges.erase(styleRanges.begin() + styleBeforeReplacement);
              }
            } else {
              if (styleRanges[styleBeforeReplacement].start == range.start + nonLatinStart) {
  //               qDebug() << "Inserting a default style in the middle by changing the style before the replacement";
                styleRanges[styleBeforeReplacement].rangeIndex = 0;
              } else {
  //               qDebug() << "Inserting a default style in the middle";
                styleRanges.insert(styleRanges.begin() + (styleBeforeReplacement + 1), StyleRange(range.start + nonLatinStart, 0));
              }
            }
          }
        }
      }
    } else {  // TODO: only on else, or do that always? are the variables styleBeforeReplacement and styleAfterReplacement always up to date then? or do a generic check for merging / deleting successive styles?
      if (styleBeforeReplacement >= 0 &&
          styleAfterReplacement >= 0 &&
          styleBeforeReplacement + 1 == styleAfterReplacement &&
          styleRanges[styleBeforeReplacement].start == styleRanges[styleAfterReplacement].start) {
        styleRanges.erase(styleRanges.begin() + styleBeforeReplacement);
      }
    }
    
  //   for (int i = 1; i < styleRanges.size(); ++ i) {
  //     if (styleRanges[i - 1].rangeIndex == styleRanges[i].rangeIndex) {
  //       qDebug() << "Warning: redundant style range start!";
  //     }
  //     if (styleRanges[i - 1].start == styleRanges[i].start) {
  //       qDebug() << "Error: two style ranges start at the same location!";
  //     }
  //     if (styleRanges[i - 1].start > styleRanges[i].start) {
  //       qDebug() << "Error: style range odering broken!";
  //     }
  //   }
  //   
  //   qDebug() << "Final block text: " << (mText.left(range.start.offset) % newText % mText.right(mText.size() - range.end.offset));
  //   qDebug() << "Final style starts:";
  //   for (const auto& style : styleRanges) {
  //     qDebug() << " " << style.start.offset << "  (highlight index" << style.rangeIndex << ")";
  //   }
  //   qDebug() << "=============================================";
  }
  
  // Update the text
  mText = mText.left(range.start.offset) % newText % mText.right(mText.size() - range.end.offset);
}

void TextBlock::InsertStyleRange(const DocumentRange& range, int highlightRangeIndex, int layer) {
  auto& styleRanges = mStyleRanges[layer];
  
  for (std::size_t i = 0, end = styleRanges.size(); i < end; ++ i) {
    // Skip over existing ranges that end before the new range starts
    DocumentLocation otherRangeEnd = (i == end - 1) ? mText.size() : styleRanges[i + 1].start;
    if (otherRangeEnd <= range.start) {
      continue;
    }
    
    if (styleRanges[i].start == range.start) {
      // Insert the new range before the current one
      styleRanges.insert(styleRanges.begin() + i, StyleRange(range.start, highlightRangeIndex));
      ++ i;
    } else {
      // Insert the new range after the current one.
      styleRanges.insert(styleRanges.begin() + (i + 1), StyleRange(range.start, highlightRangeIndex));
      if (otherRangeEnd > range.end) {
        // A part of the other range remains on the right side. Insert a new
        // range for this.
        styleRanges.insert(styleRanges.begin() + (i + 2), StyleRange(range.end, styleRanges[i].rangeIndex));
        break;
      }
      i += 2;
    }
    
    // i is now at the first range following the inserted one. Check whether any
    // following ranges need to be deleted or shrunk.
    std::size_t firstFollowingRange = i;
    ++ end;
    for (; i < end; ++ i) {
      if (styleRanges[i].start >= range.end) {
        break;
      }
      otherRangeEnd = (i == end - 1) ? mText.size() : styleRanges[i + 1].start;
      
      if (otherRangeEnd == range.end) {
        styleRanges.erase(styleRanges.begin() + firstFollowingRange,
                          styleRanges.begin() + (i + 1));
        break;
      } else if (otherRangeEnd > range.end) {
        styleRanges[i].start = range.end;
        styleRanges.erase(styleRanges.begin() + firstFollowingRange,
                          styleRanges.begin() + i);
        break;
      }
    }
    
    break;
  }
}

int TextBlock::FindStyleIndexForCharacter(int characterOffset, int layer) {
  auto& styleRanges = mStyleRanges[layer];
  for (int i = styleRanges.size() - 1; i >= 0; -- i) {
    if (styleRanges[i].start.offset <= characterOffset) {
      return i;
    }
  }
  qDebug() << "Error: FindStyleIndexForCharacter() did not find a range for character offset" << characterOffset << ", this should never happen.";
  return -1;
}

void TextBlock::ClearStyleRanges(int layer) {
  mStyleRanges[layer].clear();
  mStyleRanges[layer].emplace_back(0, 0);
}

QString TextBlock::TextForRange(const DocumentRange& range) {
  return mText.mid(range.start.offset, range.end.offset - range.start.offset);
}

std::vector<std::shared_ptr<TextBlock>> TextBlock::Split(int desiredBlockSize) {
  int oldSize = mText.size();
  int numBlocks = std::max(2, (mText.size() + desiredBlockSize / 2) / desiredBlockSize);
  
  std::vector<std::shared_ptr<TextBlock>> result(numBlocks - 1);
  for (int i = result.size() - 1; i >= 0; -- i) {
    int pos = ((i + 1) * oldSize) / numBlocks;
    int posNext = ((i + 2) * oldSize) / numBlocks;
    
    result[i].reset(new TextBlock(QStringLiteral(""), false));
    TextBlock* block = result[i].get();
    
    block->mText = mText.mid(pos, posNext - pos);
    
    int firstLine = 0;
    for (int a = static_cast<int>(mLineAttributes.size()) - 1; a >= 0; -- a) {
      if (mLineAttributes[a].offset < pos) {
        firstLine = a + 1;
        break;
      }
    }
    if (firstLine < mLineAttributes.size()) {
      block->mLineAttributes.assign(mLineAttributes.begin() + firstLine, mLineAttributes.end());
      for (NewlineAttributes& att : block->mLineAttributes) {
        att.offset -= pos;
      }
      mLineAttributes.erase(mLineAttributes.begin() + firstLine, mLineAttributes.end());
    }
    
    for (int layer = 0; layer < kLayerCount; ++ layer) {
      auto& styleRanges = mStyleRanges[layer];
      auto& blockStyleRanges = block->mStyleRanges[layer];
      
      int firstStyle = 0;
      for (int s = static_cast<int>(styleRanges.size()) - 1; s >= 0; -- s) {
        if (styleRanges[s].start < pos) {
          firstStyle = s + 1;
          break;
        }
      }
      if (firstStyle < styleRanges.size()) {
        blockStyleRanges.assign(styleRanges.begin() + firstStyle, styleRanges.end());
        for (StyleRange& style : blockStyleRanges) {
          style.start -= pos;
        }
        styleRanges.erase(styleRanges.begin() + firstStyle, styleRanges.end());
        
        if (blockStyleRanges.front().start.offset > 0) {
          // Insert last style from previous block
          blockStyleRanges.insert(blockStyleRanges.begin(), StyleRange(0, styleRanges.back().rangeIndex));
        }
      } else {
        // Use last style from previous block
        blockStyleRanges.assign({StyleRange(0, styleRanges.back().rangeIndex)});
      }
    }
  }
  
  int posNext = (1 * oldSize) / numBlocks;
  mText = mText.left(posNext);
  return result;
}

void TextBlock::Append(const TextBlock& other) {
  std::size_t oldAttributesSize = mLineAttributes.size();
  std::size_t oldStylesSize[kLayerCount] = {mStyleRanges[0].size(), mStyleRanges[1].size()};
  int oldLength = mText.size();
  
  mText += other.mText;
  mLineAttributes.insert(mLineAttributes.end(), other.mLineAttributes.begin(), other.mLineAttributes.end());
  for (int layer = 0; layer < kLayerCount; ++ layer) {
    bool sameBorderStyle = (mStyleRanges[layer].back().rangeIndex == other.mStyleRanges[layer].front().rangeIndex);
    mStyleRanges[layer].insert(mStyleRanges[layer].end(), other.mStyleRanges[layer].begin() + (sameBorderStyle ? 1 : 0), other.mStyleRanges[layer].end());
  }
  
  for (std::size_t i = oldAttributesSize, size = mLineAttributes.size(); i < size; ++ i) {
    mLineAttributes[i].offset += oldLength;
  }
  for (int layer = 0; layer < kLayerCount; ++ layer) {
    for (std::size_t i = oldStylesSize[layer], size = mStyleRanges[layer].size(); i < size; ++ i) {
      mStyleRanges[layer][i].start += oldLength;
    }
  }
}

bool TextBlock::DebugCheckNewlineoffsets(bool isFirst) const {
  int a = 0;
  if (isFirst) {
    if (mLineAttributes.empty()) {
      qDebug() << "isFirst && mLineAttributes.empty()";
      return false;
    }
    if (mLineAttributes[0].offset != -1) {
      qDebug() << "isFirst && mLineAttributes[0].offset != -1";
      return false;
    }
    ++ a;
  }
  
  for (; a < mLineAttributes.size(); ++ a) {
    if (a > 0 && mLineAttributes[a - 1].offset >= mLineAttributes[a].offset) {
      qDebug() << "mLineAttributes[a - 1].offset >= mLineAttributes[a].offset";
      return false;
    }
    if (mLineAttributes[a].offset < 0 ||
        mLineAttributes[a].offset >= mText.size()) {
      qDebug() << "mLineAttributes[a (" << a << ")].offset (" << mLineAttributes[a].offset << ") < 0 || mLineAttributes[a].offset (" << mLineAttributes[a].offset << ") >= mText.size() (" << mText.size() << ")";
      return false;
    }
    if (mText[mLineAttributes[a].offset] != '\n') {
      qDebug() << "mText[mLineAttributes[a (" << a << ")].offset (" << mLineAttributes[a].offset << ")] (" << mText[mLineAttributes[a].offset].toLatin1() << ") != '\\n'";
      return false;
    }
  }
  
  int actualNewlineCount = isFirst ? 1 : 0;
  for (int c = 0; c < mText.size(); ++ c) {
    if (mText[c] == '\n') {
      ++ actualNewlineCount;
    }
  }
  if (actualNewlineCount != mLineAttributes.size()) {
    qDebug() << "actualNewlineCount (" << actualNewlineCount << ") != mLineAttributes.size() (" << mLineAttributes.size() << ")";
    return false;
  }
  
  return true;
}
