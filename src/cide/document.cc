// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/document.h"

#include <iostream>
#include <unordered_set>

#include <QFile>

#include "cide/qt_thread.h"
#include "cide/settings.h"
#include "cide/text_utils.h"


Document::LineIterator::LineIterator(Document* document)
    : document(document),
      blockIndex(0),
      blockStartOffset(0),
      lineInBlockIndex(0) {}

Document::LineIterator::LineIterator(Document* document, int initialOffset)
    : document(document) {
  document->EnsureOffsetCacheIsUpToDate();
  
  int l = 0;
  unsigned int lOffset = document->mBlocks[l]->GetCachedStartLine();
  int r = static_cast<int>(document->mBlocks.size()) - 1;
  unsigned int rLine = document->mBlocks[r]->GetCachedEndLine();
  
  while (l <= r) {
    blockIndex = l + (initialOffset - lOffset) / static_cast<float>(rLine - lOffset) * (r - l) + 0.5f;
    if (blockIndex < 0 || blockIndex >= document->mBlocks.size()) {
      break;
    }
    
    int blockStartLine = document->mBlocks[blockIndex]->GetCachedStartLine();
    int blockEndLine = document->mBlocks[blockIndex]->GetCachedEndLine();
    if (blockStartLine <= initialOffset && blockEndLine > initialOffset) {
      blockStartOffset = document->mBlocks[blockIndex]->GetCachedStartOffset();
      lineInBlockIndex = initialOffset - blockStartLine;
      return;
    }
    
    if (initialOffset >= blockEndLine) {
      l = blockIndex + 1;
      if (l >= document->mBlocks.size()) {
        break;
      }
      lOffset = document->mBlocks[l]->GetCachedStartLine();
    } else {
      r = blockIndex - 1;
      if (r < 0) {
        break;
      }
      rLine = document->mBlocks[r]->GetCachedEndLine();
    }
  }
  
  blockIndex = document->mBlocks.size();
  // qDebug() << "Error: LineIterator initializing constructor did not find the given initialOffset (" << initialOffset << ")";
}

Document::LineIterator::LineIterator(const LineIterator& other)
    : document(other.document),
      blockIndex(other.blockIndex),
      blockStartOffset(other.blockStartOffset),
      lineInBlockIndex(other.lineInBlockIndex) {}

bool Document::LineIterator::IsValid() const {
  return blockIndex < document->mBlocks.size();
}

DocumentLocation Document::LineIterator::GetLineStart() const {
  return blockStartOffset + document->mBlocks[blockIndex]->lineAttributes()[lineInBlockIndex].offset + 1;
}

DocumentRange Document::LineIterator::GetLineRange() const {
  TextBlock& block = *document->mBlocks[blockIndex];
  
  DocumentRange result;
  result.start.offset = blockStartOffset + block.lineAttributes()[lineInBlockIndex].offset + 1;
  
  if (lineInBlockIndex < static_cast<int>(block.lineAttributes().size()) - 1) {
    result.end.offset = blockStartOffset + block.lineAttributes()[lineInBlockIndex + 1].offset;
  } else {
    bool found = false;
    int startOffset = blockStartOffset + block.text().size();
    for (int block = blockIndex + 1, size = document->mBlocks.size(); block < size; ++ block) {
      if (!document->mBlocks[block]->lineAttributes().empty()) {
        result.end.offset = startOffset + document->mBlocks[block]->lineAttributes().front().offset;
        found = true;
        break;
      }
      startOffset += document->mBlocks[block]->text().size();
    }
    if (!found) {
      result.end.offset = startOffset;  // end of document
    }
  }
  
  return result;
}

int Document::LineIterator::GetAttributes() {
  return document->mBlocks[blockIndex]->lineAttributes()[lineInBlockIndex].attributes;
}

void Document::LineIterator::SetAttributes(int attributes) {
  document->mBlocks[blockIndex]->lineAttributes()[lineInBlockIndex].attributes = attributes;
}

void Document::LineIterator::AddAttributes(int attributes) {
  document->mBlocks[blockIndex]->lineAttributes()[lineInBlockIndex].attributes |= attributes;
}

void Document::LineIterator::RemoveAttributes(int attributes) {
  document->mBlocks[blockIndex]->lineAttributes()[lineInBlockIndex].attributes &= ~attributes;
}

Document::CharacterIterator Document::LineIterator::GetCharacterIterator() const {
  if (document->mBlocks[blockIndex]->lineAttributes()[lineInBlockIndex].offset < document->mBlocks[blockIndex]->text().size() - 1) {
    /// The new iterator starts in the current block
    int charInBlockIndex = document->mBlocks[blockIndex]->lineAttributes()[lineInBlockIndex].offset + 1;
    return CharacterIterator(
        document,
        blockIndex,
        blockStartOffset,
        charInBlockIndex);
  } else {
    /// The new iterator starts in the next block (if that exists)
    constexpr int charInBlockIndex = 0;
    return CharacterIterator(
        document,
        blockIndex + 1,
        blockStartOffset + document->mBlocks[blockIndex]->text().size(),
        charInBlockIndex);
  }
}

Document::CharacterAndStyleIterator Document::LineIterator::GetCharacterAndStyleIterator() const {
  int styleInBlockIndex[TextBlock::kLayerCount];
  if (document->mBlocks[blockIndex]->lineAttributes()[lineInBlockIndex].offset < document->mBlocks[blockIndex]->text().size() - 1) {
    /// The new iterator starts in the current block
    int charInBlockIndex = document->mBlocks[blockIndex]->lineAttributes()[lineInBlockIndex].offset + 1;
    for (int layer = 0; layer < TextBlock::kLayerCount; ++ layer) {
      styleInBlockIndex[layer] = document->mBlocks[blockIndex]->FindStyleIndexForCharacter(charInBlockIndex, layer);
    }
    return CharacterAndStyleIterator(
        document,
        blockIndex,
        blockStartOffset,
        charInBlockIndex,
        styleInBlockIndex,
        true);
  } else {
    /// The new iterator starts in the next block (if that exists)
    constexpr int charInBlockIndex = 0;
    for (int layer = 0; layer < TextBlock::kLayerCount; ++ layer) {
      styleInBlockIndex[layer] = document->mBlocks[blockIndex]->FindStyleIndexForCharacter(charInBlockIndex, layer);
    }
    return CharacterAndStyleIterator(
        document,
        blockIndex + 1,
        blockStartOffset + document->mBlocks[blockIndex]->text().size(),
        charInBlockIndex,
        styleInBlockIndex,
        true);
  }
}

void Document::LineIterator::operator++() {
  ++ lineInBlockIndex;
  while (lineInBlockIndex >= document->mBlocks[blockIndex]->lineAttributes().size()) {
    blockStartOffset += document->mBlocks[blockIndex]->text().size();
    ++ blockIndex;
    if (!IsValid()) {
      break;
    }
    lineInBlockIndex = 0;
  }
}


Document::CharacterIterator::CharacterIterator(Document* document)
    : document(document),
      blockIndex(0),
      blockStartOffset(0),
      charInBlockIndex(0) {}

Document::CharacterIterator::CharacterIterator(Document* document, int characterOffset)
    : document(document) {
  blockIndex = document->BlockForCharacter(characterOffset, &blockStartOffset);
  charInBlockIndex = characterOffset - blockStartOffset;
}

Document::CharacterIterator::CharacterIterator(const CharacterIterator& other)
    : document(other.document),
      blockIndex(other.blockIndex),
      blockStartOffset(other.blockStartOffset),
      charInBlockIndex(other.charInBlockIndex) {}

bool Document::CharacterIterator::IsValid() const {
  return blockIndex >= 0 && blockIndex < document->mBlocks.size();
}

QChar Document::CharacterIterator::GetChar() const {
  return document->mBlocks[blockIndex]->text()[charInBlockIndex];
}

void Document::CharacterIterator::operator--() {
  -- charInBlockIndex;
  while (charInBlockIndex < 0) {
    -- blockIndex;
    if (!IsValid()) {
      // Make GetCharacterOffset() return -1 for iterators that point before the
      // start of the document.
      charInBlockIndex = -1;
      break;
    }
    int intSize = document->mBlocks[blockIndex]->text().size();
    blockStartOffset -= intSize;
    charInBlockIndex = intSize - 1;
  }
}

void Document::CharacterIterator::operator++() {
  if (blockIndex < 0) {
    blockIndex = 0;
    blockStartOffset = 0;
    charInBlockIndex = 0;
    return;
  }
  
  ++ charInBlockIndex;
  while (charInBlockIndex >= document->mBlocks[blockIndex]->text().size()) {
    blockStartOffset += document->mBlocks[blockIndex]->text().size();
    ++ blockIndex;
    // charInBlockIndex must be set to 0 before exiting such that GetCharacterOffset()
    // returns the correct offset after reaching the end of the document.
    charInBlockIndex = 0;
    if (!IsValid()) {
      break;
    }
  }
}


Document::CharacterAndStyleIterator::CharacterAndStyleIterator(Document* document)
    : document(document),
      blockIndex(0),
      blockStartOffset(0),
      charInBlockIndex(0),
      styleInBlockIndex{0, 0},
      styleChanged(true) {}

Document::CharacterAndStyleIterator::CharacterAndStyleIterator(Document* document, int characterOffset)
    : document(document),
      styleChanged(true) {
  blockIndex = document->BlockForCharacter(characterOffset, &blockStartOffset);
  charInBlockIndex = characterOffset - blockStartOffset;
  if (blockIndex >= 0) {
    for (int layer = 0; layer < TextBlock::kLayerCount; ++ layer) {
      styleInBlockIndex[layer] = document->mBlocks[blockIndex]->FindStyleIndexForCharacter(charInBlockIndex, layer);
    }
  }
}

Document::CharacterAndStyleIterator::CharacterAndStyleIterator(const CharacterAndStyleIterator& other)
    : document(other.document),
      blockIndex(other.blockIndex),
      blockStartOffset(other.blockStartOffset),
      charInBlockIndex(other.charInBlockIndex),
      styleInBlockIndex{other.styleInBlockIndex[0], other.styleInBlockIndex[1]},
      styleChanged(other.styleChanged) {}

bool Document::CharacterAndStyleIterator::IsValid() const {
  return blockIndex >= 0 && blockIndex < document->mBlocks.size();
}

QChar Document::CharacterAndStyleIterator::GetChar() const {
  return document->mBlocks[blockIndex]->text()[charInBlockIndex];
}

HighlightRange Document::CharacterAndStyleIterator::GetStyle() const {
  const auto& block = document->mBlocks[blockIndex];
  
  // Apply all layers of highlight ranges on top of each other.
  int layer = 0;
  int rangeIndex = block->styleRanges(layer)[styleInBlockIndex[layer]].rangeIndex;
  HighlightRange result = document->mRanges[layer][rangeIndex];
  
  for (layer = 1; layer < TextBlock::kLayerCount; ++ layer) {
    rangeIndex = block->styleRanges(layer)[styleInBlockIndex[layer]].rangeIndex;
    const HighlightRange& highlight = document->mRanges[layer][rangeIndex];
    
    if (highlight.affectsText) {
      result.affectsText = true;
      result.textColor = highlight.textColor;
      result.bold = highlight.bold;
    }
    if (highlight.affectsBackground) {
      result.affectsBackground = true;
      result.backgroundColor = highlight.backgroundColor;
    }
  }
  
  return result;
}

const HighlightRange& Document::CharacterAndStyleIterator::GetStyleOfLayer(int layer) const {
  const auto& block = document->mBlocks[blockIndex];
  int rangeIndex = block->styleRanges(layer)[styleInBlockIndex[layer]].rangeIndex;
  return document->mRanges[layer][rangeIndex];
}

void Document::CharacterAndStyleIterator::operator--() {
  styleChanged = false;
  -- charInBlockIndex;
  for (int layer = 0; layer < TextBlock::kLayerCount; ++ layer) {
    if (document->mBlocks[blockIndex]->styleRanges(layer)[styleInBlockIndex[layer]].start.offset == charInBlockIndex + 1) {
      -- styleInBlockIndex[layer];
      styleChanged = true;
    }
  }
  
  while (charInBlockIndex < 0) {
    -- blockIndex;
    if (!IsValid()) {
      // Make GetCharacterOffset() return -1 for iterators that point before the
      // start of the document.
      charInBlockIndex = -1;
      break;
    }
    int intSize = document->mBlocks[blockIndex]->text().size();
    blockStartOffset -= intSize;
    charInBlockIndex = intSize - 1;
    for (int layer = 0; layer < TextBlock::kLayerCount; ++ layer) {
      styleInBlockIndex[layer] = document->mBlocks[blockIndex]->styleRanges(layer).size() - 1;
    }
    styleChanged = true;
  }
}

void Document::CharacterAndStyleIterator::operator++() {
  styleChanged = false;
  ++ charInBlockIndex;
  const auto& block = document->mBlocks[blockIndex];
  for (int layer = 0; layer < TextBlock::kLayerCount; ++ layer) {
    if (block->styleRanges(layer).size() > styleInBlockIndex[layer] + 1
        && block->styleRanges(layer)[styleInBlockIndex[layer] + 1].start.offset == charInBlockIndex) {
      ++ styleInBlockIndex[layer];
      styleChanged = true;
    }
  }
  
  while (charInBlockIndex >= block->text().size()) {
    blockStartOffset += block->text().size();
    ++ blockIndex;
    // charInBlockIndex must be set to 0 before exiting such that GetCharacterOffset()
    // returns the correct offset after reaching the end of the document.
    charInBlockIndex = 0;
    if (!IsValid()) {
      break;
    }
    for (int layer = 0; layer < TextBlock::kLayerCount; ++ layer) {
      styleInBlockIndex[layer] = 0;
    }
    styleChanged = true;
  }
}


Document::Document(int desiredBlockSize)
    : mVersion(0),
      mSavedVersion(0),
      mOffsetCacheVersion(-1),
      desiredBlockSize(desiredBlockSize) {
  mBlocks = {std::shared_ptr<TextBlock>(new TextBlock())};
  
  versionGraphRoot = new DocumentVersion(mVersion, nullptr);
  
  creatingCombinedUndoStep = false;
  
  // Add default font style
  const auto& defaultStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::Default);
  for (int layer = 0; layer < TextBlock::kLayerCount; ++ layer) {
    mRanges[layer].emplace_back(
        /*range*/ DocumentRange::Invalid(),
        /*affectsText*/ layer == 0,
        /*textColor*/ defaultStyle.textColor,
        /*bold*/ defaultStyle.bold,
        /*affectsBackground*/ defaultStyle.affectsBackground,
        /*backgroundColor*/ defaultStyle.backgroundColor,
        /*isNonCodeRange*/ false);
  }
}

Document::~Document() {
  if (watcher) {
    RunInQtThreadBlocking([&]() {
      watcher.reset();
    });
  }
  ClearVersionGraph();
  delete versionGraphRoot;
}

void Document::AssignTextAndStyles(const Document& other) {
  // Copy blocks
  mBlocks.resize(other.mBlocks.size());
  for (int i = 0; i < mBlocks.size(); ++ i) {
    mBlocks[i].reset(new TextBlock(*other.mBlocks[i]));
  }
  
  // Copy style ranges
  for (int layer = 0; layer < TextBlock::kLayerCount; ++ layer) {
    mRanges[layer] = other.mRanges[layer];
  }
}

bool Document::Open(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }
  setPath(QFileInfo(path).canonicalFilePath());
  mFileName = QFileInfo(path).fileName();
  
  ReadTextFromFile(&file);
  
  ++ mVersion;
  mSavedVersion = mVersion;
  ClearVersionGraph();
  emit Changed();
  return true;
}

bool Document::Save(const QString& path) {
  QString pathCopy = path;  // Copy the path for the case that the passed-in reference goes to mPath
  setPath(QStringLiteral(""));  // stop watching any old file
  
  QFile file(pathCopy);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return false;
  }
  
  for (int b = 0; b < mBlocks.size(); ++ b) {
    QByteArray utf8Data = mBlocks[b]->text().toUtf8();
    if (file.write(utf8Data) != utf8Data.size()) {
      return false;
    }
  }
  
  file.close();
  setPath(QFileInfo(pathCopy).canonicalFilePath());
  mFileName = QFileInfo(pathCopy).fileName();
  mSavedVersion = mVersion;
  return true;
}

bool Document::OpenBackup(const QString& backupPath, QString* originalPath) {
  QFile file(backupPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }
  
  *originalPath = QString::fromUtf8(file.readLine().chopped(1));
  
  ReadTextFromFile(&file);
  
  return true;
}

bool Document::SaveBackup(const QString& backupPath, const QString& originalPath) {
  QFile file(backupPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return false;
  }
  
  file.write(originalPath.toUtf8() + "\n");
  
  for (int b = 0; b < mBlocks.size(); ++ b) {
    QByteArray utf8Data = mBlocks[b]->text().toUtf8();
    if (file.write(utf8Data) != utf8Data.size()) {
      return false;
    }
  }
  
  file.close();
  return true;
}

void Document::Replace(const DocumentRange& range, const QString& newText, bool createUndoStep, Replacement* undoReplacement, bool forceNewUndoStep) {
  constexpr bool kDebug = false;
  
  // Debug: Pretty-print the blocks and the replacement
  if (kDebug) {
    qDebug() << "--- REPLACEMENT ---";
    QString debugBlocks = "|";
    QString debugReplacement = " ";
    int prevLen = 0;
    for (int b = 0; b < mBlocks.size(); ++ b) {
      for (int c = 0; c < mBlocks[b]->text().size(); ++ c) {
        if (prevLen + c == range.start.offset) {
          debugBlocks += "[";
          debugReplacement += "[";
        }
        if (prevLen + c == range.end.offset) {
          debugBlocks += "]";
          debugReplacement += "]";
        }
        
        QChar character = mBlocks[b]->text()[c];
        if (character == '\n') {
          debugBlocks += "\\n";
          debugReplacement += "  ";
        } else {
          debugBlocks += character;
          debugReplacement += " ";
        }
      }
      prevLen += mBlocks[b]->text().size();
      
      if (b == mBlocks.size() - 1) {
        if (prevLen == range.start.offset) {
          debugBlocks += "[";
          debugReplacement += "[";
        }
        if (prevLen == range.end.offset) {
          debugBlocks += "]";
          debugReplacement += "]";
        }
      }
      
      debugBlocks += "|";
      debugReplacement += " ";
    }
    std::cout << debugBlocks.toStdString() << std::endl;
    std::cout << debugReplacement.toStdString() << std::endl;
    qDebug() << "Replacement: " << newText;
  }
  
  int firstBlockOffset;
  int firstBlock = BlockForLocation(range.start, true, &firstBlockOffset);
  int lastBlockOffset;
  int lastBlock;
  if (range.size() == 0) {
    lastBlock = firstBlock;
    lastBlockOffset = firstBlockOffset;
  } else {
    lastBlock = BlockForLocation(range.end, false, &lastBlockOffset);
  }
  
  QString oldText;
  
  if (firstBlock == lastBlock) {
    TextBlock& block = *mBlocks[firstBlock];
    DocumentRange localRange = DocumentRange(range.start.offset - firstBlockOffset,
                                             range.end.offset - lastBlockOffset);
    oldText = block.TextForRange(localRange);
    block.Replace(
        localRange, newText,
        (firstBlock == 0) ? nullptr : mBlocks[firstBlock - 1].get(),
        (firstBlock == mBlocks.size() - 1) ? nullptr : mBlocks[firstBlock + 1].get());
    
    CheckBlockSplitOrMerge(firstBlock);
  } else {
    // Get the old text to make the undo step later
    oldText += mBlocks[firstBlock]->TextForRange(
        DocumentRange(range.start.offset - firstBlockOffset,
                      mBlocks[firstBlock]->text().size()));
    for (int block = firstBlock + 1; block < lastBlock; ++ block) {
      oldText += mBlocks[block]->text();
    }
    oldText += mBlocks[lastBlock]->TextForRange(
        DocumentRange(0,
                      range.end.offset - lastBlockOffset));
    
    // Replace the partial range in the last block with an empty string. This is
    // done before inserting the new text in the first block to handle style
    // updates properly (it makes the correct subsequent style available that
    // may need to be extended into the replaced region).
    mBlocks[lastBlock]->Replace(
        DocumentRange(0,
                      range.end.offset - lastBlockOffset),
        QStringLiteral(""),
        mBlocks[lastBlock - 1].get(),
        (lastBlock == mBlocks.size() - 1) ? nullptr : mBlocks[lastBlock + 1].get());
    
    // Replace the partial range in the first block with the whole newText. Note
    // that we tell Replace() here that the lastBlock is the following one
    // already (although we only delete the in-between blocks below). This way,
    // styles can be updated correctly.
    mBlocks[firstBlock]->Replace(
        DocumentRange(range.start.offset - firstBlockOffset,
                      mBlocks[firstBlock]->text().size()),
        newText,
        (firstBlock == 0) ? nullptr : mBlocks[firstBlock - 1].get(),
        mBlocks[lastBlock].get());
    
    // Delete the blocks in the middle
    if (lastBlock > firstBlock + 1) {
      mBlocks.erase(mBlocks.begin() + (firstBlock + 1), mBlocks.begin() + lastBlock);
    }
    
    if (kDebug) {
      qDebug() << "Blocks before split/merge:";
      QString debugBlocks = "|";
      for (int b = 0; b < mBlocks.size(); ++ b) {
        for (int c = 0; c < mBlocks[b]->text().size(); ++ c) {
          QChar character = mBlocks[b]->text()[c];
          if (character == '\n') {
            debugBlocks += "\\n";
          } else {
            debugBlocks += character;
          }
        }
        
        debugBlocks += "|";
      }
      std::cout << debugBlocks.toStdString() << std::endl;
    }
    
    // Split/merge both remaining blocks. We call this on the second block
    // first, since regardless of splitting or merging, this leaves the first
    // block at the same index, such that we can also call the function on it
    // later. The other way round, the indices would be likely to change, and
    // we would lose track of the second block.
    CheckBlockSplitOrMerge(firstBlock + 1);
    CheckBlockSplitOrMerge(firstBlock);
  }
  
  // Debug: Pretty-print the resulting blocks
  if (kDebug) {
    qDebug() << "Final blocks:";
    QString debugBlocks = "|";
    for (int b = 0; b < mBlocks.size(); ++ b) {
      for (int c = 0; c < mBlocks[b]->text().size(); ++ c) {
        QChar character = mBlocks[b]->text()[c];
        if (character == '\n') {
          debugBlocks += "\\n";
        } else {
          debugBlocks += character;
        }
      }
      
      debugBlocks += "|";
    }
    std::cout << debugBlocks.toStdString() << std::endl;
    qDebug() << "-------------------";
  }
  
  int shift = newText.size() - range.size();
  DocumentLocation newRangeEnd = range.start + newText.size();
  
  // Adjust the problem ranges
  // TODO: Would it be better to use a vector for the problem ranges to make it
  //       faster to modify them?
  std::set<ProblemRange> newProblemRanges;
  
  for (auto it = mProblemRanges.begin(), end = mProblemRanges.end(); it != end; ++ it) {
    if (it->range.start >= range.end) {
      newProblemRanges.insert(ProblemRange(DocumentRange(it->range.start + shift, it->range.end + shift), it->problemIndex));
    } else if (it->range.start >= range.start) {
      if (it->range.end <= range.end) {
        // Delete the problem range
      } else {
        newProblemRanges.insert(ProblemRange(DocumentRange(newRangeEnd, it->range.end + shift), it->problemIndex));
      }
    } else if (it->range.end >= range.start) {
      if (it->range.end > range.end) {
        newProblemRanges.insert(ProblemRange(DocumentRange(it->range.start, it->range.end + shift), it->problemIndex));
      } else {
        newProblemRanges.insert(ProblemRange(DocumentRange(it->range.start, range.start), it->problemIndex));
      }
    } else {
      // The problem range lies before the edit range, take it over unmodified.
      newProblemRanges.insert(ProblemRange(it->range, it->problemIndex));
    }
  }
  newProblemRanges.swap(mProblemRanges);
  
  // Adjust the fix-it ranges
  for (const std::shared_ptr<Problem>& problem : mProblems) {
    for (int i = 0; i < static_cast<int>(problem->fixits().size()); ++ i) {
      Problem::FixIt& fixit = problem->fixits()[i];
      
      // TODO: This duplicates the logic above
      if (fixit.range.start >= range.end) {
        fixit.range = DocumentRange(fixit.range.start + shift, fixit.range.end + shift);
      } else if (fixit.range.start >= range.start) {
        if (fixit.range.end <= range.end) {
          // Delete the fixit
          problem->fixits().erase(problem->fixits().begin() + i);
          -- i;
          continue;
        } else {
          fixit.range = DocumentRange(newRangeEnd, fixit.range.end + shift);
        }
      } else if (fixit.range.end >= range.start) {
        if (fixit.range.end > range.end) {
          fixit.range = DocumentRange(fixit.range.start, fixit.range.end + shift);
        } else {
          fixit.range = DocumentRange(fixit.range.start, range.start);
        }
      } else {
        // The problem range lies before the edit range, take it over unmodified.
      }
    }
  }
  
  // Adjust the context ranges
  // TODO: Would storing this as a vector be better to make modifications faster (no need to build a new set)?
  std::set<Context> newContexts;
  for (auto it = mContexts.begin(), end = mContexts.end(); it != end; ++ it) {
    Context newContext = *it;
    
    // TODO: This duplicates the logic above
    if (it->range.start >= range.end) {
      newContext.range = DocumentRange(it->range.start + shift, it->range.end + shift);
    } else if (it->range.start >= range.start) {
      if (it->range.end <= range.end) {
        // Delete the context
        continue;
      } else {
        newContext.range = DocumentRange(newRangeEnd, it->range.end + shift);
      }
    } else if (it->range.end >= range.start) {
      if (it->range.end > range.end) {
        newContext.range = DocumentRange(it->range.start, it->range.end + shift);
      } else {
        newContext.range = DocumentRange(it->range.start, range.start);
      }
    } else {
      // The problem range lies before the edit range, take it over unmodified.
      newContext.range = it->range;
    }
    
    newContexts.insert(newContext);
  }
  newContexts.swap(mContexts);
  
  if (createUndoStep) {
    ++ mVersion;
    
    // Delete any redo steps in case there are any: Go to the highest version,
    // track back to the current version, deleting all versions that are not also
    // referenced by another step.
    std::vector<DocumentVersion*> redoList;
    DocumentVersion* curItem = versionGraphRoot;
    while (!curItem->links.empty()) {
      int latestVersion = -1;
      DocumentVersion* latestVersionPtr = nullptr;
      for (const DocumentVersionLink& link : curItem->links) {
        if (link.linkedVersion->version > latestVersion) {
          latestVersion = link.linkedVersion->version;
          latestVersionPtr = link.linkedVersion;
        }
      }
      if (latestVersion < curItem->version) {
        break;
      }
      if (!latestVersionPtr) {
        qDebug() << "Error: This should never happen (only in case the version counter overflows)";
        break;
      }
      
      curItem = latestVersionPtr;
      redoList.push_back(curItem);
    }
    
    for (int i = static_cast<int>(redoList.size()) - 1; i >= 0; -- i) {
      // TODO: Check whether this version needs to be kept due to being used by parsing
      constexpr bool needsToBeKeptForParsing = false;
      bool needsToBeKeptDueToExternalReference = !redoList[i]->links.empty();
      if (needsToBeKeptForParsing || needsToBeKeptDueToExternalReference) {
        // Finished deleting redo steps.
        break;
      }
      
      // Delete this node since it is not required anymore.
      int backLinkIndex = redoList[i]->FindBackLink();
      if (backLinkIndex >= 0) {
        redoList[i]->towardsCurrentVersion->links.erase(redoList[i]->towardsCurrentVersion->links.begin() + backLinkIndex);
      }
      delete redoList[i];
    }
    
    // Check whether the last undo step should be extended to include the current change.
    // Criteria:
    // * The "direction" of change must be the same, i.e., the final step must be a
    //   combination of only adding or only removing text
    // * The timestamp of the last undo step is not too far in the past
    // * TODO: We currently also require the version graph root to have only one
    //         back-link. This should be avoided with special case handling which
    //         updates the other link(s) as well.
    bool mergedUndoStep = false;
    if (!forceNewUndoStep &&
        !creatingCombinedUndoStep &&
        versionGraphRoot->links.size() == 1 &&
        versionGraphRoot->links[0].replacements.size() == 1) {
      constexpr int kMaxMillisecondsForUndoMerging = 500;  // TODO: Make configurable
      QTime currentTime = QTime::currentTime();
      if (versionGraphRoot->creationTime.msecsTo(currentTime) <= kMaxMillisecondsForUndoMerging) {
        DocumentVersionLink& link = versionGraphRoot->links[0];
        Replacement& replacement = link.replacements[0];
        
        // TODO: Check for consistency of the character types as well?
        if (replacement.text.isEmpty() &&
            replacement.range.end == range.start &&
            !newText.isEmpty()) {
          // Merge two additions
          versionGraphRoot->creationTime = currentTime;
          versionGraphRoot->version = mVersion;
          replacement.range.end = range.start + newText.size();
          mergedUndoStep = true;
        } else if (!replacement.text.isEmpty() &&
                   replacement.range.end == range.start &&
                   newText.isEmpty()) {
          // Merge two removals (old removal at new range start)
          versionGraphRoot->creationTime = currentTime;
          versionGraphRoot->version = mVersion;
          replacement.text = replacement.text + oldText;
          mergedUndoStep = true;
        } else if (!replacement.text.isEmpty() &&
                   replacement.range.end == range.end &&
                   newText.isEmpty()) {
          // Merge two removals (old removal at new range end)
          versionGraphRoot->creationTime = currentTime;
          versionGraphRoot->version = mVersion;
          replacement.range = DocumentRange(range.start, range.start + newText.size());
          replacement.text = oldText + replacement.text;
          mergedUndoStep = true;
        }
      }
    }
    
    // Add a new undo step for the replacement?
    if (!mergedUndoStep) {
      Replacement undoReplacement;
      undoReplacement.range = DocumentRange(range.start, range.start + newText.size());
      undoReplacement.text = oldText;
      
      if (creatingCombinedUndoStep) {
        combinedUndoReplacements.push_back(undoReplacement);
      } else {
        DocumentVersion* newVersion = new DocumentVersion(mVersion, nullptr);
        versionGraphRoot->towardsCurrentVersion = newVersion;
        newVersion->links.emplace_back(versionGraphRoot, undoReplacement);
        versionGraphRoot = newVersion;
      }
    }
    
    emit Changed();
  } else {
    // In this case, we do not create an undo step and do not increase mVersion.
    // As a side effect, the offset cache may be invalidated and would not be
    // updated on the next access. So we force an update here.
    // TODO: Would it be better to instead always increase mVersion?
    UpdateOffsetCache();
  }
  if (undoReplacement) {
    undoReplacement->range = DocumentRange(range.start, range.start + newText.size());
    undoReplacement->text = oldText;
  }
}

void Document::StartUndoStep() {
  if (creatingCombinedUndoStep) {
    qDebug() << "ERROR: Called StartUndoStep() when creatingCombinedUndoStep was already true";
  }
  creatingCombinedUndoStep = true;
}

void Document::EndUndoStep() {
  if (!creatingCombinedUndoStep) {
    qDebug() << "ERROR: Called EndUndoStep() when creatingCombinedUndoStep was false";
  }
  creatingCombinedUndoStep = false;
  
  // If no undo steps have been added since the call to StartUndoStep(), abort.
  if (combinedUndoReplacements.empty()) {
    return;
  }
  
  // Bring accumulated undo steps into the correct order
  std::reverse(combinedUndoReplacements.begin(), combinedUndoReplacements.end());
  
  // Add the new document version
  DocumentVersion* newVersion = new DocumentVersion(mVersion, nullptr);
  versionGraphRoot->towardsCurrentVersion = newVersion;
  newVersion->links.emplace_back(versionGraphRoot, std::vector<Replacement>());
  newVersion->links.back().replacements.swap(combinedUndoReplacements);
  versionGraphRoot = newVersion;
}

QString Document::TextForRange(const DocumentRange& range) {
  int firstBlockOffset;
  int firstBlock = BlockForLocation(range.start, true, &firstBlockOffset);
  int lastBlockOffset;
  int lastBlock;
  if (range.size() == 0) {
    lastBlock = firstBlock;
    lastBlockOffset = firstBlockOffset;
  } else {
    lastBlock = BlockForLocation(range.end, false, &lastBlockOffset);
  }
  
  if (firstBlock < 0 || lastBlock < 0) {
    qDebug() << "ERROR: TextForRange() got invalid blocks for the start/end of the given range";
    return QString();
  }
  
  if (firstBlock == lastBlock) {
    TextBlock& block = *mBlocks[firstBlock];
    DocumentRange localRange = DocumentRange(range.start.offset - firstBlockOffset,
                                             range.end.offset - lastBlockOffset);
    return block.TextForRange(localRange);
  } else {
    QString result;
    
    result += mBlocks[firstBlock]->TextForRange(
        DocumentRange(range.start.offset - firstBlockOffset,
                      mBlocks[firstBlock]->text().size()));
    
    for (int block = firstBlock + 1; block < lastBlock; ++ block) {
      result += mBlocks[block]->text();
    }
    
    result += mBlocks[lastBlock]->TextForRange(
        DocumentRange(0,
                      range.end.offset - lastBlockOffset));
    
    return result;
  }
}

void Document::CheckBlockSplitOrMerge(int index) {
  TextBlock& block = *mBlocks[index];
  int blockSize = block.text().size();
  
  if (blockSize < std::max(1, desiredBlockSize / 2)) {
    // The block is too small. Merge it with the smaller one of the previous and
    // next blocks if possible.
    if (mBlocks.size() <= 1) {
      // No other block exists that this one could be merged with.
      return;
    }
    
    int prevBlockSize = std::numeric_limits<int>::max();
    int nextBlockSize = std::numeric_limits<int>::max();
    
    if (index > 0) {
      prevBlockSize = mBlocks[index - 1]->text().size();
    }
    if (index < mBlocks.size() - 1) {
      nextBlockSize = mBlocks[index + 1]->text().size();
    }
    
    if (prevBlockSize < nextBlockSize) {
      // Merge with previous block
      mBlocks[index - 1]->Append(block);
      mBlocks.erase(mBlocks.begin() + index);
    } else {
      // Merge with next block
      block.Append(*mBlocks[index + 1]);
      mBlocks.erase(mBlocks.begin() + (index + 1));
    }
  } else if (blockSize >= 2 * desiredBlockSize) {
    // The block is too large. Split it.
    std::vector<std::shared_ptr<TextBlock>> newBlocks = block.Split(desiredBlockSize);
    mBlocks.insert(mBlocks.begin() + (index + 1), newBlocks.begin(), newBlocks.end());
  }
}

bool Document::Undo(DocumentRange* newTextRange) {
  return UndoRedoImpl(false, newTextRange);
}

bool Document::Redo(DocumentRange* newTextRange) {
  return UndoRedoImpl(true, newTextRange);
}

void Document::setPath(const QString &path) {
  // Lazily create the file watcher if it does not exist yet
  if (!watcher) {
    RunInQtThreadBlocking([&]() {
      watcher.reset(new QFileSystemWatcher());
    });
    connect(watcher.get(), &QFileSystemWatcher::fileChanged, this, &Document::FileWatcherNotification);
  }
  
  // Change the path.
  if (!mPath.isEmpty()) {
    watcher->removePath(mPath);
  }
  mPath = QFileInfo(path).canonicalFilePath();
  if (!mPath.isEmpty()) {
    watcher->addPath(mPath);
  }
  
  mFileName = QFileInfo(mPath).fileName();
}

DocumentRange Document::FullDocumentRange() const {
  int size = 0;
  for (int b = 0; b < mBlocks.size(); ++ b) {
    size += mBlocks[b]->text().size();
  }
  return DocumentRange(0, size);
}

DocumentRange Document::RangeForWordAt(int characterOffset, const std::function<int(QChar)>& charClassifier, int noWordType) const {
  CharacterIterator it(const_cast<Document*>(this), characterOffset);  // TODO: Avoid const_cast
  if (!it.IsValid()) {
    return DocumentRange::Invalid();
  }
  
  int wordType = charClassifier(it.GetChar());
  if (wordType == noWordType) {
    return DocumentRange(characterOffset, characterOffset + 1);
  }
  
  CharacterIterator prevIt(it);
  int firstCharacter;
  while (true) {
    -- prevIt;
    if (!prevIt.IsValid()) {
      firstCharacter = 0;
      break;
    }
    
    QChar c = prevIt.GetChar();
    if (c == '\n' || charClassifier(c) != wordType) {
      firstCharacter = prevIt.GetCharacterOffset() + 1;
      break;
    }
  }
  
  int lastCharacter;
  while (true) {
    ++ it;
    if (!it.IsValid()) {
      lastCharacter = FullDocumentRange().end.offset - 1;
      break;
    }
    
    QChar c = it.GetChar();
    if (c == '\n' || charClassifier(c) != wordType) {
      lastCharacter = it.GetCharacterOffset() - 1;
      break;
    }
  }
  
  return DocumentRange(firstCharacter, lastCharacter + 1);
}

int Document::FindMatchingBracket(const CharacterAndStyleIterator& pos) {
  QChar c = pos.GetChar();
  if (IsOpeningBracket(c)) {
    // Find the matching closing bracket to the right
    const QChar& openBracket = c;
    const QChar closeBracket = GetMatchingBracketCharacter(openBracket);
    int bracketCounter = 1;
    
    Document::CharacterAndStyleIterator it = pos;
    ++ it;
    while (it.IsValid()) {
      if (!it.GetStyleOfLayer(0).isNonCodeRange) {
        QChar c = it.GetChar();
        if (c == openBracket) {
          ++ bracketCounter;
        } else if (c == closeBracket) {
          -- bracketCounter;
          if (bracketCounter == 0) {
            // Found the matching bracket.
            return it.GetCharacterOffset();
          }
        }
      }
      
      ++ it;
    }
    // Did not find a matching bracket.
  } else if (IsClosingBracket(c)) {
    // Find the matching opening bracket to the left
    const QChar& closeBracket = c;
    const QChar openBracket = GetMatchingBracketCharacter(closeBracket);
    int bracketCounter = 1;
    
    Document::CharacterAndStyleIterator it = pos;
    -- it;
    while (it.IsValid()) {
      if (!it.GetStyleOfLayer(0).isNonCodeRange) {
        QChar c = it.GetChar();
        if (c == closeBracket) {
          ++ bracketCounter;
        } else if (c == openBracket) {
          -- bracketCounter;
          if (bracketCounter == 0) {
            // Found the matching bracket.
            return it.GetCharacterOffset();
          }
        }
      }
      
      -- it;
    }
    // Did not find a matching bracket.
  }
  
  return -1;
}

int Document::LineCount() const {
  int count = 0;
  for (int b = 0; b < mBlocks.size(); ++ b) {
    count += mBlocks[b]->lineAttributes().size();
  }
  return count;
}

bool Document::DebugCheckNewlineoffsets() const {
  for (int b = 0; b < mBlocks.size(); ++ b) {
    if (!mBlocks[b]->DebugCheckNewlineoffsets(b == 0)) {
      qDebug() << "ERROR: DebugCheckNewlineoffsets() failed for block" << b;
      return false;
    }
  }
  return true;
}

bool Document::DebugCheckVersionGraph() const {
//   {
//     qDebug() << "=== Debug print of version graph ===";
//     std::vector<DocumentVersion*> workList = {versionGraphRoot};
//     std::unordered_set<DocumentVersion*> visited;
//     while (!workList.empty()) {
//       DocumentVersion* curItem = workList.back();
//       workList.pop_back();
//       visited.insert(curItem);
//       qDebug() << "- Version" << curItem->version << (versionGraphRoot == curItem ? "(root == current version)" : "");
//       for (const DocumentVersionLink& link : curItem->links) {
//         qDebug() << "  - linked to" << link.linkedVersion->version;
//         if (visited.count(link.linkedVersion) == 0) {
//           workList.push_back(link.linkedVersion);
//         }
//       }
//     }
//     qDebug() << "====================================";
//   }
  
  std::unordered_set<DocumentVersion*> visited;
  std::vector<DocumentVersion*> workList = {versionGraphRoot};
  while (!workList.empty()) {
    DocumentVersion* curItem = workList.back();
    workList.pop_back();
    
    for (const DocumentVersionLink& link : curItem->links) {
      workList.push_back(link.linkedVersion);
    }
    
    if (visited.count(curItem) > 0) {
      qDebug() << "Debug check error: Version graph has a cycle! Printing the graph below.";
      workList = {versionGraphRoot};
      visited.clear();
      while (!workList.empty()) {
        DocumentVersion* curItem = workList.back();
        workList.pop_back();
        visited.insert(curItem);
        qDebug() << "- Version" << curItem->version << (versionGraphRoot == curItem ? "(root == current version)" : "");
        for (const DocumentVersionLink& link : curItem->links) {
          qDebug() << "  - linked to" << link.linkedVersion->version;
          if (visited.count(link.linkedVersion) == 0) {
            workList.push_back(link.linkedVersion);
          }
        }
      }
      return false;
    }
    visited.insert(curItem);
  }
  return true;
}

void Document::DebugGetBlockStatistics(int* blockCount, float* avgBlockSize, int* maxBlockSize, float* avgStyleRanges) {
  *blockCount = mBlocks.size();
  *avgBlockSize = 0;
  *maxBlockSize = 0;
  *avgStyleRanges = 0;
  
  for (int b = 0; b < mBlocks.size(); ++ b) {
    int size = mBlocks[b]->text().size();
    *avgBlockSize += size;
    *maxBlockSize = std::max(*maxBlockSize, size);
    for (int layer = 0; layer < TextBlock::kLayerCount; ++ layer) {
      *avgStyleRanges += mBlocks[b]->styleRanges(layer).size();
    }
  }
  
  *avgBlockSize /= mBlocks.size();
  *avgStyleRanges /= (TextBlock::kLayerCount * mBlocks.size());
}

QString Document::GetDocumentText() const {
  QString text = "";
  for (int b = 0; b < mBlocks.size(); ++ b) {
    text += mBlocks[b]->text();
  }
  return text;
}

int Document::BlockForLocation(const DocumentLocation& loc, bool forwards, int* blockStartOffset) const {
  if (loc.offset < 0) {
    return -1;
  }
  
  EnsureOffsetCacheIsUpToDate();
  
  if (loc.offset > mBlocks.back()->GetCachedEndOffset()) {
    return -1;
  }
  // We handle this case by looking for the offset after (if forwards == true) or before (if forwards == false)
  // the given DocumentLocation. However, this does not work with empty blocks. This should however only
  // occur if the document is empty. Thus, handle this as a special case.
  if (mBlocks.back()->GetCachedEndOffset() == 0) {
    // We already bounds-checked the given location, so just return the empty block.
    *blockStartOffset = 0;
    return 0;
  }
  int searchOffset = std::max(0, std::min(static_cast<int>(mBlocks.back()->GetCachedEndOffset()) - 1, forwards ? loc.offset : (loc.offset - 1)));
  
  int result = BlockForCharacter(searchOffset, blockStartOffset);
  if (result < 0) {
    qDebug() << "Error: BlockForLocation() unexpectedly got an error from BlockForCharacter()";
  }
  return result;
}

int Document::BlockForCharacter(int characterOffset, int* blockStartOffset) const {
  // TODO: Cache the last used block (for this and similar functions)? Need a benchmark to see whether that would be useful.
  
  if (characterOffset < 0) {
    return -1;
  }
  
  EnsureOffsetCacheIsUpToDate();
  
  int l = 0;
  unsigned int lOffset = mBlocks[l]->GetCachedStartOffset();
  int r = static_cast<int>(mBlocks.size()) - 1;
  unsigned int rOffset = mBlocks[r]->GetCachedEndOffset();
  
  while (l <= r) {
    int blockIndex = l + (characterOffset - lOffset) / static_cast<float>(rOffset - lOffset) * (r - l) + 0.5f;
    if (blockIndex < 0 || blockIndex >= mBlocks.size()) {
      break;
    }
    
    *blockStartOffset = mBlocks[blockIndex]->GetCachedStartOffset();
    int blockEndOffset = mBlocks[blockIndex]->GetCachedEndOffset();
    if (*blockStartOffset <= characterOffset && blockEndOffset > characterOffset) {
      return blockIndex;
    }
    
    if (characterOffset >= blockEndOffset) {
      l = blockIndex + 1;
      if (l >= mBlocks.size()) {
        break;
      }
      lOffset = mBlocks[l]->GetCachedStartOffset();
    } else {
      r = blockIndex - 1;
      if (r < 0) {
        break;
      }
      rOffset = mBlocks[r]->GetCachedEndOffset();
    }
  }
  
  // qDebug() << "BlockForCharacter(): Invalid characterOffset given.";
  return -1;
}

DocumentLocation Document::Find(const QString& searchString, const DocumentLocation& searchStart, bool forwards, bool matchCase) {
  if (searchString.isEmpty()) {
    return DocumentLocation::Invalid();
  }
  
  auto matches = [&](const QChar& a, const QChar& b) {
    if (matchCase) {
      return a == b;
    } else {
      return a.toLower() == b.toLower();
    }
  };
  
  // NOTE: As a possible micro-optimization, could stop searching if the remaining
  //       text in the document is too short to fit the search string into.
  if (forwards) {
    Document::CharacterIterator it(this, searchStart.offset);
    while (it.IsValid()) {
      if (matches(it.GetChar(), searchString[0])) {
        // The first character matches, test if the rest matches as well
        Document::CharacterIterator testIt = it;
        ++ testIt;
        int i = 1;
        for (; testIt.IsValid() && i < searchString.size(); ++ i, ++ testIt) {
          if (!matches(testIt.GetChar(), searchString[i])) {
            break;
          }
        }
        if (i == searchString.size()) {
          return it.GetCharacterOffset();
        }
      }
      ++ it;
    };
  } else {
    if (searchStart.offset == 0) {
      return DocumentLocation::Invalid();
    }
    
    Document::CharacterIterator it(this, searchStart.offset - 1);
    while (it.IsValid()) {
      if (matches(it.GetChar(), searchString[searchString.size() - 1])) {
        // The last character matches, test if the rest matches as well
        Document::CharacterIterator testIt = it;
        -- testIt;
        int i = searchString.size() - 2;
        for (; testIt.IsValid() && i >= 0; -- i, -- testIt) {
          if (!matches(testIt.GetChar(), searchString[i])) {
            break;
          }
        }
        if (i == -1) {
          return testIt.GetCharacterOffset() + 1;
        }
      }
      -- it;
    };
  }
  
  return DocumentLocation::Invalid();
}

DocumentRange Document::GetRangeForLine(int l) {
  LineIterator it(this, l);
  if (it.IsValid()) {
    return it.GetLineRange();
  } else {
    return DocumentRange::Invalid();
  }
}

int Document::lineAttributes(int l) {
  LineIterator it(this, l);
  if (it.IsValid()) {
    return it.GetAttributes();
  } else {
    qDebug() << "Attempting to get lineAttributes() for invalid line " << l;
    return 0;
  }
}

void Document::SetLineAttributes(int l, int attributes) {
  LineIterator it(this, l);
  if (it.IsValid()) {
    it.SetAttributes(attributes);
  } else {
    qDebug() << "Attempting to set lineAttributes() for invalid line " << l;
  }
}

void Document::AddLineAttributes(int l, int attributes) {
  LineIterator it(this, l);
  if (it.IsValid()) {
    it.AddAttributes(attributes);
  } else {
    qDebug() << "Attempting to add lineAttributes() for invalid line " << l;
  }
}

void Document::RemoveLineAttributes(int l, int attributes) {
  LineIterator it(this, l);
  if (it.IsValid()) {
    it.RemoveAttributes(attributes);
  } else {
    qDebug() << "Attempting to remove lineAttributes() for invalid line " << l;
  }
}

void Document::AddHighlightRange(const DocumentRange& range, bool isNonCodeRange, const QColor& textColor, bool bold, bool affectsText, bool affectsBackground, const QColor& backgroundColor, int layer) {
  if (range.IsInvalid() || range.IsEmpty()) {
    return;
  }
  
  constexpr bool kDebug = false;
  if (kDebug) {
    qDebug() << "Highlighting in (" << textColor.red() << "," << textColor.green() << "," << textColor.blue() << "): " << TextForRange(range);
  }
  
  // Add highlight range
  mRanges[layer].emplace_back(
      range,
      affectsText,
      textColor,
      bold,
      affectsBackground,
      backgroundColor,
      isNonCodeRange);
  
  // Update style ranges in blocks
  ApplyHighlightRange(range, mRanges[layer].size() - 1, layer);
}

void Document::ClearHighlightRanges(int layer) {
  // Delete all highlight ranges except the default text style range
  mRanges[layer].erase(mRanges[layer].begin() + 1, mRanges[layer].end());
  
  // Recompute style ranges in blocks
  ReapplyHighlightRanges(layer);
}

void Document::FinishedHighlightingChanges() {
  emit HighlightingChanged();
}

int Document::AddProblem(const std::shared_ptr<Problem>& problem) {
  mProblems.push_back(problem);
  return mProblems.size() - 1;
}

void Document::AddProblemRange(int problemIndex, const DocumentRange& range) {
  if (!range.IsValid()) {
    return;
  }
  mProblemRanges.insert(ProblemRange(range, problemIndex));
}

void Document::RemoveProblem(const std::shared_ptr<Problem>& problem) {
  for (auto it = mProblems.begin(), end = mProblems.end(); it != end; ++ it) {
    if (it->get() == problem.get()) {
      int index = static_cast<int>(it - mProblems.begin());
      mProblems.erase(it);
      
      for (auto it = mProblemRanges.begin(); it != mProblemRanges.end(); ) {
        if (it->problemIndex == index) {
          it = mProblemRanges.erase(it);
        } else {
          if (it->problemIndex > index) {
            // TODO: Avoid const_cast. It should be safe here since these set
            //       items are not sorted by this member.
            *const_cast<int*>(&it->problemIndex) -= 1;
          }
          ++ it;
        }
      }
      
      return;
    }
  }
  qDebug() << "Warning: Tried to remove a problem from a document that does not contain that problem";
}

void Document::ClearProblems() {
  mProblems.clear();
  mProblemRanges.clear();
}

ClangTUPool* Document::GetTUPool() {
  if (!mTUPool) {
    mTUPool.reset(new ClangTUPool(2));
  }
  return mTUPool.get();
}

void Document::FileWatcherNotification() {
  emit FileChangedExternally();
}

void Document::ApplyHighlightRange(const DocumentRange& range, int highlightRangeIndex, int layer) {
  if (range.IsInvalid()) {
    return;
  }
  
  int firstBlockOffset;
  int firstBlock = BlockForLocation(range.start, true, &firstBlockOffset);
  int lastBlockOffset;
  int lastBlock;
  if (range.size() == 0) {
    lastBlock = firstBlock;
    lastBlockOffset = firstBlockOffset;
  } else {
    lastBlock = BlockForLocation(range.end, false, &lastBlockOffset);
  }
  
  if (firstBlock == -1 || lastBlock == -1) {
    qDebug() << "Error: In ApplyHighlightRange(), the first or last retrieved block for the range is invalid ( firstBlock:" << firstBlock << ", lastBlock" << lastBlock << ", range.start: " << range.start.offset << ", range.end: " << range.end.offset << ", document.end.offset: " << FullDocumentRange().end.offset << ")";
    return;
  }
  
  if (firstBlock == lastBlock) {
    TextBlock& block = *mBlocks[firstBlock];
    DocumentRange localRange = DocumentRange(range.start.offset - firstBlockOffset,
                                             range.end.offset - lastBlockOffset);
    block.InsertStyleRange(localRange, highlightRangeIndex, layer);
  } else {
    mBlocks[firstBlock]->InsertStyleRange(
        DocumentRange(range.start.offset - firstBlockOffset,
                      mBlocks[firstBlock]->text().size()), highlightRangeIndex, layer);
    
    for (int block = firstBlock + 1; block < lastBlock; ++ block) {
      mBlocks[block]->InsertStyleRange(DocumentRange(0, mBlocks[block]->text().size()), highlightRangeIndex, layer);
    }
    
    mBlocks[lastBlock]->InsertStyleRange(
        DocumentRange(0,
                      range.end.offset - lastBlockOffset), highlightRangeIndex, layer);
  }
}

void Document::ReapplyHighlightRanges(int layer) {
  // Reset styles
  for (int b = 0, size = mBlocks.size(); b < size; ++ b) {
    mBlocks[b]->ClearStyleRanges(layer);
  }
  
  // Apply all highlight ranges that are in the stack (excluding the default
  // style).
  for (std::size_t i = 1, size = mRanges[layer].size(); i < size; ++ i) {
    ApplyHighlightRange(mRanges[layer][i].range, i, layer);
  }
}

bool Document::UndoRedoImpl(bool redo, DocumentRange* newTextRange) {
  // From the root node of the version graph, find the oldest / newest node
  // which is directly reachable.
  int bestVersion = redo ? -1 : std::numeric_limits<int>::max();
  DocumentVersionLink* doLink = nullptr;
  int undoLinkIndex = -1;
  for (int i = 0; i < versionGraphRoot->links.size(); ++ i) {
    DocumentVersionLink& link = versionGraphRoot->links[i];
    if ((redo && link.linkedVersion->version > bestVersion) ||
        (!redo && link.linkedVersion->version < bestVersion)) {
      bestVersion = link.linkedVersion->version;
      doLink = &link;
      undoLinkIndex = i;
    }
  }
  if (!doLink ||
      ((redo && bestVersion < versionGraphRoot->version) ||
       (!redo && bestVersion > versionGraphRoot->version))) {
    return false;
  }
  
  // Perform the operation.
  std::vector<Replacement> redoReplacements(doLink->replacements.size());
  for (int i = 0, size = doLink->replacements.size(); i < size; ++ i) {
    Replace(doLink->replacements[i].range, doLink->replacements[i].text, false, &redoReplacements[redoReplacements.size() - 1 - i]);
  }
  
  if (newTextRange) {
    if (doLink->replacements.empty()) {
      *newTextRange = DocumentRange::Invalid();
    } else {
      // TODO: This only outputs the data of the last replacement, should we output all?
      const Replacement& lastReplacement = doLink->replacements.back();
      *newTextRange = DocumentRange(lastReplacement.range.start, lastReplacement.range.start + lastReplacement.text.size());
    }
  }
  
  // Update the version graph.
  DocumentVersion* newCurVersion = doLink->linkedVersion;  // need to cache this here since the link will be deleted below
  
  versionGraphRoot->links.erase(versionGraphRoot->links.begin() + undoLinkIndex);
  newCurVersion->links.emplace_back(versionGraphRoot, redoReplacements);
  
  newCurVersion->towardsCurrentVersion = nullptr;
  versionGraphRoot->towardsCurrentVersion = newCurVersion;
  
  versionGraphRoot = newCurVersion;
  mVersion = versionGraphRoot->version;
  emit Changed();
  return true;
}

void Document::ClearVersionGraph() {
  // (Depth-first) deletion of all nodes in the version graph.
  std::vector<DocumentVersion*> workList = {versionGraphRoot};
  while (!workList.empty()) {
    DocumentVersion* curItem = workList.back();
    workList.pop_back();
    
    for (const DocumentVersionLink& link : curItem->links) {
      workList.push_back(link.linkedVersion);
    }
    
    delete curItem;
  }
  
  // Restore the root node.
  versionGraphRoot = new DocumentVersion(mVersion, nullptr);
}

void Document::ReadTextFromFile(QFile* file) {
  // Read lines from file while removing possible unwanted \r characters
  QString fileText = "";
  while (!file->atEnd()) {
    QByteArray line = file->readLine();
    fileText += QString::fromUtf8(line);  // TODO: Allow reading other formats than UTF-8 only?
  }
  
  // Convert text to blocks
  int numBlocks = std::max(1, (fileText.size() + desiredBlockSize / 2) / desiredBlockSize);
  mBlocks.resize(numBlocks);
  // Note: This calculation actually overflows for large files if using int for i.
  for (uint64_t i = 0; i < numBlocks; ++ i) {
    int pos = (i * fileText.size()) / numBlocks;
    int posNext = ((i + 1) * fileText.size()) / numBlocks;
    mBlocks[i].reset(new TextBlock(fileText.mid(pos, posNext - pos), i == 0));
  }
}

void Document::ClearContexts() {
  mContexts.clear();
}

void Document::AddContext(const QString& name, const QString& description, const DocumentRange& nameInDescriptionRange, const DocumentRange& range) {
  mContexts.insert(Context(name, description, nameInDescriptionRange, range));
}

std::vector<Context> Document::GetContextsAt(const DocumentLocation& location) {
  std::vector<Context> result;
  for (const auto& item : mContexts) {
    if (item.range.Contains(location)) {
      result.push_back(item);
    }
  }
  // Since the ranges are ordered by increasing start location in mContexts,
  // we should not need to do any sorting here.
  return result;
}
