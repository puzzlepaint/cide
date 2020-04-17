// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/code_completion_widget.h"

#include <QDebug>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>

#include "cide/clang_utils.h"
#include "cide/document_widget.h"
#include "cide/settings.h"
#include "cide/text_utils.h"

CompletionItem::CompletionItem() {}

CompletionItem::CompletionItem(const CXCodeCompleteResults* libclangResults, int index) {
  const CXCompletionString& completion = libclangResults->Results[index].CompletionString;
  
  clangCompletionIndex = index;
  priority = clang_getCompletionPriority(completion);
  numFixits = clang_getCompletionNumFixIts(const_cast<CXCodeCompleteResults*>(libclangResults), index);
  
  // Note: This also classified deprecated items as "not available".
  isAvailable = clang_getCompletionAvailability(completion) == CXAvailability_Available;
  
  DisplayStyle currentStyle = DisplayStyle::Default;
  if (numFixits > 0) {
    displayStyles.emplace_back(std::make_pair(0, DisplayStyle::Fixit));
    currentStyle = DisplayStyle::Fixit;
    
    for (int i = 0; i < numFixits; ++ i) {
      CXSourceRange range;
      CXString clangReplacement = clang_getCompletionFixIt(const_cast<CXCodeCompleteResults*>(libclangResults), index, i, &range);
      QString replacement = QString::fromUtf8(clang_getCString(clangReplacement));
      clang_disposeString(clangReplacement);
      if (!replacement.isEmpty()) {
        displayText = replacement;
        break;
      }
    }
    if (displayText.isEmpty()) {
      displayText = QStringLiteral("(fix)");
    }
  }
  AppendCompletionString(completion, &currentStyle);
}

void CompletionItem::AppendCompletionString(const CXCompletionString& completion, DisplayStyle* currentStyle) {
  auto setStyle = [&](DisplayStyle style) {
    if (*currentStyle != style) {
      displayStyles.emplace_back(std::make_pair(displayText.size(), style));
      *currentStyle = style;
    }
  };
  
  unsigned numChunks = clang_getNumCompletionChunks(completion);
  for (int chunkIndex = 0; chunkIndex < numChunks; ++ chunkIndex) {
    CXCompletionChunkKind kind = clang_getCompletionChunkKind(completion, chunkIndex);
    
    if (kind == CXCompletionChunk_Optional) {
      CXCompletionString childString = clang_getCompletionChunkCompletionString(completion, chunkIndex);
      AppendCompletionString(childString, currentStyle);
    } else {
      CXString clangText = clang_getCompletionChunkText(completion, chunkIndex);
      QString text = QString::fromUtf8(clang_getCString(clangText));
      clang_disposeString(clangText);
      
      if (kind == CXCompletionChunk_TypedText) {
        filterText = text;
        setStyle(DisplayStyle::FilterText);
      } else if (kind == CXCompletionChunk_Placeholder) {
        setStyle(DisplayStyle::Placeholder);
      } else if (kind == CXCompletionChunk_Informative) {
        setStyle(DisplayStyle::Extra);
      } else if (kind == CXCompletionChunk_ResultType) {
        returnTypeText = text;
        continue;
      } else {
        setStyle(DisplayStyle::Default);
      }
      
      displayText += text;
    }
  }
}


struct CompletionItemSorter {
  inline CompletionItemSorter(CompletionItem* items)
      : items(items) {}
  
  inline bool operator() (int indexA, int indexB) const {
    const CompletionItem& itemA = items[indexA];
    const CompletionItem& itemB = items[indexB];
    
    // Sort based on the match quality between the items' filter texts and the
    // text input by the user.
    int scoreComparison = itemA.matchScore.Compare(itemB.matchScore);
    if (scoreComparison != -1) {
      return scoreComparison;
    }
    
    // If the text match quality is equal, sort by how well the completion items
    // match the insertion place.
    // if ((itemA.numFixits == 0) != (itemB.numFixits == 0)) { return itemA.numFixits == 0; }
    
    // Note that we assume that the compatibility between the expected type for
    // a function parameter and the type of the completion items is already
    // included in the priority below. Otherwise, we should check for that here
    // first.
    if (itemA.priority != itemB.priority) { return itemA.priority < itemB.priority; }
    
    // If the items are otherwise equal, use their indices to get an unambiguous
    // ordering as a last resort
    return indexA < indexB;
  }
  
  CompletionItem* items;
};


CodeCompletionWidget::CodeCompletionWidget(std::vector<CompletionItem>&& items, CXCodeCompleteResults* libclangResults, QPoint invocationPoint, QWidget* parentWidget, QWidget* parent)
    : QWidget(parent, GetCustomTooltipWindowFlags()) {
  mLibclangResults = libclangResults;
  items.swap(mItems);
  mSortOrder.resize(mItems.size());
  for (int i = 0, size = mItems.size(); i < size; ++ i) {
    mSortOrder[i] = i;
  }
  
  setFocusPolicy(Qt::NoFocus);
  setAutoFillBackground(false);
  
  scrollBar = new QScrollBar(Qt::Vertical, this);
  connect(scrollBar, &QScrollBar::valueChanged, this, &CodeCompletionWidget::ScrollChanged);
  
  this->parentWidget = parentWidget;
  invocationPosition = invocationPoint;
}

CodeCompletionWidget::~CodeCompletionWidget() {
  clang_disposeCodeCompleteResults(mLibclangResults);
}

void CodeCompletionWidget::SetFilterText(const QString& text) {
  // Score each item according to how well it matches the new filter text. Note
  // that this must be very fast since the number of items may be huge (and we
  // currently perform the sorting in the foreground thread, i.e., the UI will
  // lock up for the duration this takes). Even with only some Qt headers
  // included, the item count was in the range of 10'000 items already!
  for (int i = 0, numItems = mItems.size(); i < numItems; ++ i) {
    CompletionItem& item = mItems[i];
    ComputeFuzzyTextMatch(text, item.filterText, &item.matchScore);
  }
  
  // Sort the items.
  numSortedItems = std::min<std::size_t>(maxNumVisibleItems, mSortOrder.size());
  std::partial_sort(mSortOrder.begin(), mSortOrder.begin() + numSortedItems, mSortOrder.end(), CompletionItemSorter(mItems.data()));
  
  filterText = text;
  
  // We currently never preserve the selection when the filter text changes.
  selectedItem = 0;
  yScroll = 0;
  
  if (parentWidget) {
    Relayout();
  }
}

void CodeCompletionWidget::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  QRect rect = event->rect();
  painter.setClipRect(rect);
  
  // Draw widget frame
  painter.setPen(qRgb(0, 0, 0));
  painter.drawLine(0, 0, width() - 1, 0);  // top
  painter.drawLine(0, height() - 1, width() - 1, height() - 1);  // bottom
  painter.drawLine(0, 0, 0, height() - 1);  // left
  painter.drawLine(width() - 1, 0, width() - 1, height() - 1);  // right
  
  painter.setClipRect(rect.intersected(QRect(1, 1, width() - 2, height() - 2)));
  
  // Draw all visible items
  int maxReturnTypeCharacters = (returnTypeTextAreaWidth / charWidth) - 1;
  
  int minItem = std::max(0, (yScroll + (rect.top() - 1)) / lineHeight);
  int maxItem = std::min<int>(static_cast<int>(mItems.size()) - 1, (yScroll + (rect.bottom() - 1)) / lineHeight);
  
  int currentY = 1 + minItem * lineHeight - yScroll;
  for (int itemIndex = minItem; itemIndex <= maxItem; ++ itemIndex) {
    const CompletionItem& item = mItems[mSortOrder[itemIndex]];
    
    int visibleHeight = std::min(height() - 1 - currentY, lineHeight);
    
    // Draw the line background
    int intensity = 255 - std::min(80, 20 * std::max(item.matchScore.matchErrors, filterText.size() - item.matchScore.matchedCharacters));
    if (item.matchScore.matchedStartIndex > 0) {
      intensity = std::min(intensity, 255 - 20);
    }
    QColor backgroundColor = qRgb(intensity, intensity, intensity);
    if (!item.isAvailable) {
      // TODO: Use a lock icon to signal this instead?
      backgroundColor = qRgb(255, 80 * intensity / 255, 80 * intensity / 255);
    }
    if (itemIndex == selectedItem) {
      backgroundColor.setRed(0.75 * backgroundColor.red());
      backgroundColor.setGreen(0.75 * backgroundColor.green());
    }
    painter.fillRect(1, currentY, (width() - 1) - 1, visibleHeight, backgroundColor);
    
    // Draw the return type text in the x-range [1; 1 + returnTypeTextAreaWidth[
    painter.setPen(qRgb(0, 127, 0));
    painter.setFont(Settings::Instance().GetDefaultFont());
    
    QString returnTypeText = item.returnTypeText;
    if (returnTypeText.size() > maxReturnTypeCharacters) {
      returnTypeText.replace(maxReturnTypeCharacters - 3, returnTypeText.size() - (maxReturnTypeCharacters - 3), QStringLiteral("..."));
    }
    
    int xCoord = 1 + (maxReturnTypeCharacters - returnTypeText.size()) * charWidth;
    for (int c = 0, size = returnTypeText.size(); c < size; ++ c) {
      // Draw the background color
      // painter.fillRect(xCoord, currentY, charWidth, visibleHeight, backgroundColor);
      
      // Draw the character
      painter.drawText(QRect(xCoord, currentY, charWidth, visibleHeight), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, returnTypeText.at(c));
      
      xCoord += charWidth;
    }
    
    // Draw the display text
    int currentStyleIndex = 0;
    CompletionItem::DisplayStyle currentStyle = CompletionItem::DisplayStyle::Default;
    painter.setPen(qRgb(0, 0, 0));
    painter.setFont(Settings::Instance().GetDefaultFont());
    
    xCoord = 1 + returnTypeTextAreaWidth;
    for (int c = 0, size = item.displayText.size(); c < size; ++ c) {
      int visibleWidth = std::min(width() - 1 - xCoord, charWidth);
      
      // Update the current style
      while (currentStyleIndex < item.displayStyles.size() &&
             item.displayStyles[currentStyleIndex].first <= c) {
        if (currentStyle != item.displayStyles[currentStyleIndex].second) {
          currentStyle = item.displayStyles[currentStyleIndex].second;
          if (currentStyle == CompletionItem::DisplayStyle::Default) {
            painter.setPen(qRgb(0, 0, 0));
            painter.setFont(Settings::Instance().GetDefaultFont());
          } else if (currentStyle == CompletionItem::DisplayStyle::FilterText) {
            if (item.clangCompletionIndex >= 0) {
              const CXCursorKind& kind = mLibclangResults->Results[item.clangCompletionIndex].CursorKind;
              // TODO: Get these colors from program settings
              if (IsFunctionDeclLikeCursorKind(kind)) {
                // Function color
                painter.setPen(qRgb(0, 0, 127));
              } else if (IsClassDeclLikeCursorKind(kind)) {
                // Class color
                painter.setPen(qRgb(220, 80, 2));
              } else if (IsVarDeclLikeCursorKind(kind)) {
                // Attribute color
                painter.setPen(qRgb(0, 127, 0));
              } else if (kind == CXCursor_TypedefDecl) {
                painter.setPen(qRgb(200, 0, 180));
              } else if (kind == CXCursor_EnumConstantDecl) {
                painter.setPen(qRgb(0, 127, 0));
              } else {
                painter.setPen(qRgb(0, 0, 0));
              }
            } else {
              painter.setPen(qRgb(0, 0, 0));
            }
            painter.setFont(Settings::Instance().GetBoldFont());
          } else if (currentStyle == CompletionItem::DisplayStyle::Placeholder) {
            painter.setPen(qRgb(0, 0, 127));
            painter.setFont(Settings::Instance().GetDefaultFont());
          } else if (currentStyle == CompletionItem::DisplayStyle::Extra) {
            painter.setPen(qRgb(127, 127, 127));
            painter.setFont(Settings::Instance().GetDefaultFont());
          } else if (currentStyle == CompletionItem::DisplayStyle::Fixit) {
            painter.setPen(qRgb(0, 0, 0));
            painter.setFont(Settings::Instance().GetDefaultFont());
          } else {
            qDebug() << "Error: Unhandled completion item style";
          }
        }
        ++ currentStyleIndex;
      }
      
      // Draw the background color
      if (currentStyle == CompletionItem::DisplayStyle::Fixit) {
        painter.fillRect(xCoord, currentY, visibleWidth, visibleHeight, qRgb(255, 100, 100));
      }
      
      // Draw the character
      painter.drawText(QRect(xCoord, currentY, visibleWidth, visibleHeight), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, item.displayText.at(c));
      xCoord += charWidth;
    }
    
    // Draw line background color to the right of the item
    // if (xCoord <= width() - 1) {
    //   painter.fillRect(xCoord, currentY, (width() - 1) - xCoord, visibleHeight, backgroundColor);
    // }
    
    currentY += lineHeight;
  }
  
  painter.end();
}

void CodeCompletionWidget::mousePressEvent(QMouseEvent* event) {
  selectedItem = std::min(static_cast<int>(mSortOrder.size()) - 1, std::max(0, (yScroll + (event->y() - 1)) / lineHeight));
  update(rect());
}

void CodeCompletionWidget::mouseDoubleClickEvent(QMouseEvent* /*event*/) {
  emit Accepted();
}

void CodeCompletionWidget::wheelEvent(QWheelEvent* event) {
  double degrees = event->delta() / 8.0;
  double numSteps = degrees / 15.0;
  
  int newYScroll = yScroll - 3 * numSteps * lineHeight;
  newYScroll = std::max(0, newYScroll);
  newYScroll = std::min(scrollBar->isVisible() ? scrollBar->maximum() : 0, newYScroll);
  
  if (newYScroll != yScroll) {
    ScrollChanged(newYScroll);
  }
}

void CodeCompletionWidget::keyPressEvent(QKeyEvent* event) {
  bool shiftHeld = event->modifiers() & Qt::ShiftModifier;
  bool ctrlHeld = event->modifiers() & Qt::ControlModifier;
  
  if (event->key() == Qt::Key_Up && !shiftHeld && !ctrlHeld) {
    if (selectedItem > 0) {
      -- selectedItem;
      EnsureSelectionIsVisible();
    }
    update(rect());
    
    event->accept();
    return;
  } else if (event->key() == Qt::Key_Down && !shiftHeld && !ctrlHeld) {
    if (selectedItem < mSortOrder.size() - 1) {
      ++ selectedItem;
      if (selectedItem >= numSortedItems) {
        ExtendItemSort(selectedItem);
      }
      EnsureSelectionIsVisible();
    }
    update(rect());
    
    event->accept();
    return;
  }
  
  event->ignore();
}

void CodeCompletionWidget::ScrollChanged(int value) {
  yScroll = value;
  if (scrollBar->value() != value) {
    scrollBar->setValue(value);
  }
  
  int maxItem = std::min<int>(static_cast<int>(mItems.size()) - 1, (yScroll + (height() - 1)) / lineHeight);
  ExtendItemSort(maxItem);
  
  Relayout();
}

void CodeCompletionWidget::AppendCompletionString(const CXCompletionString& completion, QString* text, std::vector<DocumentRange>* placeholders, bool skipBracketAndFollowing, bool skipAngleBracketAndFollowing, bool mayAppendSemicolon) {
  bool haveResultType = false;
  // bool resultTypeIsVoid = false;
  unsigned numChunks = clang_getNumCompletionChunks(completion);
  
  for (int chunkIndex = 0; chunkIndex < numChunks; ++ chunkIndex) {
    CXCompletionChunkKind kind = clang_getCompletionChunkKind(completion, chunkIndex);
    
    if (kind == CXCompletionChunk_Optional) {
      CXCompletionString childString = clang_getCompletionChunkCompletionString(completion, chunkIndex);
      AppendCompletionString(childString, text, placeholders, skipBracketAndFollowing, skipAngleBracketAndFollowing, mayAppendSemicolon);
    } else if ((kind == CXCompletionChunk_LeftParen && skipBracketAndFollowing) ||
               (kind == CXCompletionChunk_LeftAngle && skipAngleBracketAndFollowing)) {
      break;
    } else {
      QString chunkText = ClangString(clang_getCompletionChunkText(completion, chunkIndex)).ToQString();
      
      if (kind == CXCompletionChunk_Placeholder) {
        placeholders->emplace_back(text->size(), text->size() + chunkText.size());
      } else if (kind == CXCompletionChunk_Informative) {
        continue;
      } else if (kind == CXCompletionChunk_ResultType) {
        haveResultType = true;
        // if (chunkText == QStringLiteral("void")) {
        //   resultTypeIsVoid = true;
        // }
        continue;
      }
      
      *text += chunkText;
    }
  }
  
  // TODO: Testing whether it would be good to always append ';', even if the return type is not 'void'.
  if (mayAppendSemicolon && haveResultType && /*resultTypeIsVoid &&*/ !text->endsWith(';') && !skipBracketAndFollowing && !skipAngleBracketAndFollowing) {
    *text += QStringLiteral(";");
  }
}

void CodeCompletionWidget::EnsureSelectionIsVisible() {
  int selectionMinY = selectedItem * lineHeight;
  int selectionMaxY = (selectedItem + 1) * lineHeight - 1;
  
  int oldYScroll = yScroll;
  yScroll = std::max(yScroll, selectionMaxY - height() + 3);
  yScroll = std::min(yScroll, selectionMinY);
  if (oldYScroll != yScroll) {
    scrollBar->setValue(yScroll);
    Relayout();
  }
}

void CodeCompletionWidget::ExtendItemSort(int itemIndex) {
  if (itemIndex < numSortedItems) {
    return;
  }
  
  // Note: we arbitrarily add maxNumVisibleItems to itemIndex here such that we
  // won't need to sort again until this new index is reached.
  int newNumSortedItems = std::min<std::size_t>(itemIndex + maxNumVisibleItems, mSortOrder.size());
  std::partial_sort(mSortOrder.begin() + numSortedItems, mSortOrder.begin() + newNumSortedItems, mSortOrder.end(), CompletionItemSorter(mItems.data()));
  numSortedItems = newNumSortedItems;
}

void CodeCompletionWidget::SetInvocationPoint(const QPoint& point) {
  invocationPosition = point;
}

bool CodeCompletionWidget::HasSingleExactMatch() {
  if (numSortedItems < 2) {
    ExtendItemSort(2);
    if (numSortedItems < 2) {
      return false;
    }
  }
  
  // Check whether the best item matches exactly.
  CompletionItem& bestItem = mItems[mSortOrder[0]];
  if (bestItem.matchScore.matchedCharacters < bestItem.filterText.size() ||
      bestItem.matchScore.matchedCharacters < filterText.size() ||
      !bestItem.matchScore.matchedCase) {
    return false;
  }
  
  // Verify that there is no other item matching exactly.
  if (mItems.size() > 1 &&
      mItems[mSortOrder[1]].matchScore.matchedCharacters == filterText.size() &&
      mItems[mSortOrder[1]].matchScore.matchedCase) {
    return false;
  }
  
  // Verify that the insertion text of the best item is equal to the already
  // typed text.
  QString insertionText;
  if (bestItem.clangCompletionIndex < 0) {
    insertionText = bestItem.filterText;
  } else {
    CXCompletionResult& clangResult = mLibclangResults->Results[bestItem.clangCompletionIndex];
    CXCompletionString& completion = clangResult.CompletionString;
    std::vector<DocumentRange> placeholders;
    AppendCompletionString(completion, &insertionText, &placeholders, false, false, false);
  }
  return insertionText == filterText;
}

void CodeCompletionWidget::Accept(DocumentWidget* widget, const DocumentLocation& invocationLoc) {
  CompletionItem& item = mItems[mSortOrder[selectedItem]];
  
  // Get the line start offsets, required for CXSourceRangeToDocumentRange.
  // NOTE: Here, we probably only need a single line, so this could be mostly avoided.
  std::vector<unsigned> lineOffsets(widget->GetDocument()->LineCount());
  Document::LineIterator lineIt(widget->GetDocument().get());
  int lineIndex = 0;
  while (lineIt.IsValid()) {
    lineOffsets[lineIndex] = lineIt.GetLineStart().offset;
    
    ++ lineIndex;
    ++ lineIt;
  }
  if (lineIndex != lineOffsets.size()) {
    qDebug() << "Error: Line iterator returned a different line count than Document::LineCount().";
  }
  
  // Determine the range of existing text that shall be replaced. We start from
  // the invocation location and go right until hitting a non-letter character.
  Document::CharacterIterator it(widget->GetDocument().get(), invocationLoc.offset);
  while (it.IsValid()) {
    QChar character = it.GetChar();
    if (!IsIdentifierChar(character)) {
      break;
    }
    ++ it;
  }
  DocumentRange replacementRange(invocationLoc, it.GetCharacterOffset());
  
  // Apply any fix-its associated with the item, while potentially adapting the
  // replacement range
  std::vector<std::pair<DocumentRange, int>> shifts;  // Stores: (replacement range, length of new text - replacement range size) for all fix-its applied so far.
  if (item.numFixits > 0) {
    for (int fixitIndex = 0; fixitIndex < item.numFixits; ++ fixitIndex) {
      CXSourceRange fixitRange;
      CXString replacement = clang_getCompletionFixIt(mLibclangResults, item.clangCompletionIndex, fixitIndex, &fixitRange);
      
      // Transform the range through the replacements applied so far
      DocumentRange docRange = CXSourceRangeToDocumentRange(fixitRange, lineOffsets);
      for (const std::pair<DocumentRange, int>& shift : shifts) {
        if (shift.first.end <= docRange.start) {
          docRange.start += shift.second;
          docRange.end += shift.second;
        }
      }
      
      // Apply the fix-it.
      QString newText = QString::fromUtf8(clang_getCString(replacement));
      int shift = newText.size() - docRange.size();
      widget->GetDocument()->Replace(docRange, newText);
      shifts.emplace_back(docRange, shift);
      
      // Transform replacementRange.
      if (replacementRange.start >= docRange.end) {
        replacementRange.start += shift;
        replacementRange.end += shift;
      }
      
      clang_disposeString(replacement);
    }
  }
  
  // Insert the completion text
  if (item.clangCompletionIndex < 0) {
    // The completion item does not come from libclang. Insert the item's filter text directly.
    widget->GetDocument()->Replace(replacementRange, item.filterText);
    widget->SetSelection(DocumentRange::Invalid());
    widget->SetCursor(replacementRange.start + item.filterText.size(), false);
    widget->update(widget->rect());  // TODO: Smaller update rect?
  } else {
    // The completion item comes from libclang. Use its semantic string to insert it.
    // First, check whether there is an opening bracket ('(' or '<') to the right of the replacement range.
    // If yes, do not insert '(' or '<' and further text from the completion string.
    bool haveOpeningBracketAlready = false;
    bool haveOpeningAngleBracketAlready = false;
    bool haveNonWhitespaceAfterInsertionPoint = false;
    Document::CharacterIterator charIt(widget->GetDocument().get(), replacementRange.end.offset);
    while (charIt.IsValid()) {
      QChar c = charIt.GetChar();
      ++ charIt;
      
      if (c == '\n') {
        break;
      } else if (IsWhitespace(c)) {
        continue;
      } else {
        if (!haveNonWhitespaceAfterInsertionPoint) {
          if (c == '(') {
            haveOpeningBracketAlready = true;
          } else if (c == '<') {
            haveOpeningAngleBracketAlready = true;
          }
        }
        haveNonWhitespaceAfterInsertionPoint = true;
        break;
      }
    }
    
    int numOpenBracketsBeforeInsertion = 0;
    int numCloseBracketsBeforeInsertion = 0;
    // TODO: We do not account for brackets in comments or strings here, which should not count towards the balance
    charIt = Document::CharacterIterator(widget->GetDocument().get(), replacementRange.start.offset - 1);
    while (charIt.IsValid()) {
      QChar c = charIt.GetChar();
      -- charIt;
      
      if (c == '\n') {
        break;
      } else if (c == '(') {
        ++ numOpenBracketsBeforeInsertion;
      } else if (c == ')') {
        ++ numCloseBracketsBeforeInsertion;
      }
    }
    
    CXCompletionResult& clangResult = mLibclangResults->Results[item.clangCompletionIndex];
    CXCompletionString& completion = clangResult.CompletionString;
    bool isFunction = IsFunctionDeclLikeCursorKind(clangResult.CursorKind);
    
    // If we complete a path for an inclusion directive, extend the replacement range to the end of the current line.
    // libclang gives a "CXCursor_NotImplemented" for these kinds of completions currently, so instead check
    // for the #include at the start of the line.
    charIt = Document::CharacterIterator(widget->GetDocument().get(), invocationLoc.offset);
    while (charIt.IsValid() && charIt.GetChar() != '\n') {
      -- charIt;
    }
    if (charIt.IsValid()) {
      ++ charIt;
    } else {
      charIt = Document::CharacterIterator(widget->GetDocument().get(), 0);
    }
    while (charIt.IsValid() && IsWhitespace(charIt.GetChar())) {
      ++ charIt;
    }
    QString include = QStringLiteral("#include");
    int pos = 0;
    while (pos < include.size() && charIt.IsValid()) {
      if (charIt.GetChar() == include[pos]) {
        ++ charIt;
        ++ pos;
      } else {
        break;
      }
    }
    bool lineStartsWithInclude = pos == include.size();
    if (lineStartsWithInclude) {
      charIt = Document::CharacterIterator(widget->GetDocument().get(), replacementRange.end.offset);
      while (charIt.IsValid() && charIt.GetChar() != '\n') {
        ++ charIt;
      }
      replacementRange.end = charIt.GetCharacterOffset();
    }
    
    QString completionString;
    std::vector<DocumentRange> placeholders;
    AppendCompletionString(
        completion,
        &completionString,
        &placeholders,
        haveOpeningBracketAlready,
        haveOpeningAngleBracketAlready,
        isFunction && !haveNonWhitespaceAfterInsertionPoint && numOpenBracketsBeforeInsertion == numCloseBracketsBeforeInsertion);
    
    // TODO: Mark the placeholders in the document
    
    widget->GetDocument()->Replace(replacementRange, completionString);
    if (placeholders.empty()) {
      widget->SetSelection(DocumentRange::Invalid());
      widget->SetCursor(replacementRange.start + completionString.size(), false);
    } else {
      widget->SetSelection(DocumentRange(replacementRange.start.offset + placeholders.front().start.offset,
                                         replacementRange.start.offset + placeholders.front().end.offset));
    }
    widget->update(widget->rect());  // TODO: Smaller update rect?
  }
}

void CodeCompletionWidget::Relayout() {
  int scrollBarWidth = scrollBar->sizeHint().width();
  
  // Get font metrics
  QFontMetrics fontMetrics(Settings::Instance().GetDefaultFont());
  lineHeight = fontMetrics.ascent() + fontMetrics.descent();
  charWidth = fontMetrics./*horizontalAdvance*/ width(' ');
  
  // Compute good widget size and position.
  // Initialize the size with the frame size.
  int goodWidth = 2 + scrollBarWidth;
  int goodHeight = 2;
  
  int visibleItems = std::min<int>(mItems.size(), maxNumVisibleItems);
  goodHeight += visibleItems * lineHeight;
  
  // Loop over visible items, analogous to the drawing code
  int minItem = std::max(0, (yScroll - 1) / lineHeight);
  int maxItem = std::min<int>(static_cast<int>(mItems.size()) - 1, (yScroll + (goodHeight - 1)) / lineHeight);
  
  int maxReturnTypeCharacters = 0;
  int maxDisplayCharacters = 0;
  
  for (int itemIndex = minItem; itemIndex <= maxItem; ++ itemIndex) {
    const CompletionItem& item = mItems[mSortOrder[itemIndex]];
    
    maxReturnTypeCharacters = std::max(maxReturnTypeCharacters, item.returnTypeText.size());
    maxDisplayCharacters = std::max(maxDisplayCharacters, item.displayText.size());
  }
  
  constexpr int maxVisibleReturnTypeCharacters = 20;
  constexpr int maxVisibleDisplayCharacters = 100;
  // Always use the maximum visible return type characters. This is to prevent
  // the widget from moving, which unfortunately creates flickering that might
  // not be easily prevented: The problem is that the widget needs to be both
  // moved and repainted at the same time to prevent flickering, but that might
  // not be possible since the resizing might to some extent be out of our and
  // instead in the window manager’s control.
  int visibleReturnTypeCharacters = maxVisibleReturnTypeCharacters;  // std::min(maxVisibleReturnTypeCharacters, maxReturnTypeCharacters);
  int visibleDisplayCharacters = std::min(maxVisibleDisplayCharacters, maxDisplayCharacters);
  returnTypeTextAreaWidth = (visibleReturnTypeCharacters + 1) * charWidth;
  goodWidth += returnTypeTextAreaWidth + visibleDisplayCharacters * charWidth;
  
  // Resize and move the widget accordingly.
  QPoint globalInvocationPosition = parentWidget->mapToGlobal(invocationPosition);
  QPoint goodPosition = QPoint(std::max(0, globalInvocationPosition.x() - returnTypeTextAreaWidth - 1), globalInvocationPosition.y());
  if (width() != goodWidth || height() != goodHeight || goodPosition != pos()) {
    setGeometry(goodPosition.x(), goodPosition.y(), goodWidth, goodHeight);
    
    scrollBar->setGeometry(width() - scrollBarWidth - 1, 1, scrollBarWidth, height() - 2);
    int maxScroll = mSortOrder.size() * lineHeight - (height() - 2);
    if (maxScroll <= 0) {
      scrollBar->setVisible(false);
    } else {
      scrollBar->setVisible(true);
      scrollBar->setRange(0, maxScroll);
    }
  }
  update(rect());
}
