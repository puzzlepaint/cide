// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/argument_hint_widget.h"

#include <QDebug>
#include <QPainter>
#include <QPaintEvent>

#include "cide/settings.h"

ArgumentHintItem::ArgumentHintItem() {}

ArgumentHintItem::ArgumentHintItem(const CXCodeCompleteResults* libclangResults, int index, int* activeParameter) {
  CXCompletionString& completion = libclangResults->Results[index].CompletionString;
  
  // qDebug() << "- Overload priority: " << clang_getCompletionPriority(completion);;
  // qDebug() << "  Overload available: " << (clang_getCompletionAvailability(completion) == CXAvailability_Available);
  
  int currentParameter = 0;
  *activeParameter = -1;
  AppendCompletionString(completion, &currentParameter, activeParameter);
}

void ArgumentHintItem::AppendCompletionString(const CXCompletionString& completion, int* currentParameter, int* activeParameter) {
  unsigned numChunks = clang_getNumCompletionChunks(completion);
  for (int chunkIndex = 0; chunkIndex < numChunks; ++ chunkIndex) {
    CXCompletionChunkKind kind = clang_getCompletionChunkKind(completion, chunkIndex);
    
    if (kind == CXCompletionChunk_Optional) {
      CXCompletionString childString = clang_getCompletionChunkCompletionString(completion, chunkIndex);
      AppendCompletionString(childString, currentParameter, activeParameter);
    } else {
      CXString clangText = clang_getCompletionChunkText(completion, chunkIndex);
      QString text = QString::fromUtf8(clang_getCString(clangText));
      clang_disposeString(clangText);
      
      DisplayStyle style = DisplayStyle::Default;
      if (kind == CXCompletionChunk_Informative) {
        style = DisplayStyle::Extra;
      } else if (kind == CXCompletionChunk_ResultType) {
        text += " ";
        style = DisplayStyle::ReturnType;
      } else if (kind == CXCompletionChunk_CurrentParameter) {
        *activeParameter = *currentParameter;
        ++ *currentParameter;
        style = DisplayStyle::Parameter;
      } else if (kind == CXCompletionChunk_Placeholder) {
        ++ *currentParameter;
        style = DisplayStyle::Parameter;
      }
      
      // qDebug() << "  - Chunk:" << text << " (kind:" << kind << " --> style " << static_cast<int>(style) << ")";
      strings.emplace_back(text, style);
    }
  }
}


ArgumentHintWidget::ArgumentHintWidget(int currentParameter, std::vector<ArgumentHintItem>&& items, QPoint invocationPoint, QWidget* parentWidget, QWidget* parent)
    : QWidget(parent, GetCustomTooltipWindowFlags()) {
  this->currentParameter = currentParameter;
  
  items.swap(mItems);
  
  setFocusPolicy(Qt::NoFocus);
  setAutoFillBackground(false);
  
  scrollBar = new QScrollBar(Qt::Vertical, this);
  connect(scrollBar, &QScrollBar::valueChanged, this, &ArgumentHintWidget::ScrollChanged);
  
  this->parentWidget = parentWidget;
  invocationPosition = invocationPoint;
}

ArgumentHintWidget::~ArgumentHintWidget() {}

void ArgumentHintWidget::SetCurrentParameter(int index) {
  if (currentParameter == index) {
    return;
  }
  
  currentParameter = index;
  Relayout();
}

void ArgumentHintWidget::SetInvocationPoint(const QPoint& point) {
  invocationPosition = point;
}

void ArgumentHintWidget::Relayout() {
  int scrollBarWidth = scrollBar->sizeHint().width();
  
  // Get font metrics
  QFontMetrics fontMetrics(Settings::Instance().GetDefaultFont());
  lineHeight = fontMetrics.ascent() + fontMetrics.descent();
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
  charWidth = fontMetrics.horizontalAdvance(' ');
#else
  charWidth = fontMetrics.width(' ');
#endif
  
  // Compute good widget size and position.
  // Initialize the size with the frame size.
  int goodWidth = 2 + scrollBarWidth;
  int goodHeight = 2;
  
  int visibleItems = std::min<int>(mItems.size(), maxNumVisibleItems);
  goodHeight += visibleItems * lineHeight;
  
  // Loop over visible items, analogous to the drawing code
  int minItem = std::max(0, (yScroll - 1) / lineHeight);
  int maxItem = std::min<int>(static_cast<int>(mItems.size()) - 1, (yScroll + (goodHeight - 1)) / lineHeight);
  
  int maxLeftCharacters = 0;
  int maxRightCharacters = 0;
  
  for (int itemIndex = minItem; itemIndex <= maxItem; ++ itemIndex) {
    const ArgumentHintItem& item = mItems[itemIndex];
    
    int leftCharacters = 0;
    int rightCharacters = 0;
    int parameterIndex = 0;
    for (const std::pair<QString, ArgumentHintItem::DisplayStyle>& stringPair : item.strings) {
      if (parameterIndex < currentParameter) {
        leftCharacters += stringPair.first.size();
      } else {
        rightCharacters += stringPair.first.size();
      }
      
      if (stringPair.second == ArgumentHintItem::DisplayStyle::Parameter) {
        ++ parameterIndex;
      }
    }
    maxLeftCharacters = std::max(maxLeftCharacters, leftCharacters);
    maxRightCharacters = std::max(maxRightCharacters, rightCharacters);
  }
  
  constexpr int maxVisibleLeftCharacters = 21;
  constexpr int maxVisibleRightCharacters = 100;
  // Prevent flickering by using a fixed left width, see CodeCompletionWidget::Relayout().
  // int visibleLeftCharacters = maxVisibleLeftCharacters;  // std::min(maxVisibleLeftCharacters, maxLeftCharacters);
  int visibleRightCharacters = std::min(maxVisibleRightCharacters, maxRightCharacters);
  leftTextAreaWidth = maxVisibleLeftCharacters * charWidth;
  goodWidth += leftTextAreaWidth + visibleRightCharacters * charWidth;
  
  // Resize and move the widget accordingly.
  QPoint globalInvocationPosition = parentWidget->mapToGlobal(invocationPosition);
  QPoint goodPosition = QPoint(std::max(0, globalInvocationPosition.x() - 1 - maxVisibleLeftCharacters * charWidth), globalInvocationPosition.y() - goodHeight);
  if (width() != goodWidth || height() != goodHeight || goodPosition != pos()) {
    setGeometry(goodPosition.x(), goodPosition.y(), goodWidth, goodHeight);
    
    scrollBar->setGeometry(width() - scrollBarWidth - 1, 1, scrollBarWidth, height() - 2);
    int maxScroll = mItems.size() * lineHeight - (height() - 2);
    if (maxScroll <= 0) {
      scrollBar->setVisible(false);
    } else {
      scrollBar->setVisible(true);
      scrollBar->setRange(0, maxScroll);
    }
  }
  update(rect());
}

void ArgumentHintWidget::paintEvent(QPaintEvent* event) {
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
  int minItem = std::max(0, (yScroll + (rect.top() - 1)) / lineHeight);
  int maxItem = std::min<int>(static_cast<int>(mItems.size()) - 1, (yScroll + (rect.bottom() - 1)) / lineHeight);
  
  int currentY = 1 + minItem * lineHeight - yScroll;
  for (int itemIndex = minItem; itemIndex <= maxItem; ++ itemIndex) {
    const ArgumentHintItem& item = mItems[itemIndex];
    
    int visibleHeight = std::min(height() - 1 - currentY, lineHeight);
    
    // Draw the line background
    QColor backgroundColor = qRgb(255, 255, 255);
    painter.fillRect(1, currentY, (width() - 1) - 1, visibleHeight, backgroundColor);
    
    // Count the characters left of the current parameter to align the text.
    int leftCharacters = 0;
    int parameterIndex = 0;
    for (int pairIndex = 0, size = item.strings.size(); pairIndex < size; ++ pairIndex) {
      const std::pair<QString, ArgumentHintItem::DisplayStyle>& stringPair = item.strings[pairIndex];
      if (stringPair.second == ArgumentHintItem::DisplayStyle::Parameter) {
        if (parameterIndex == currentParameter) {
          break;
        }
        ++ parameterIndex;
      }
      leftCharacters += stringPair.first.size();
    }
    if (currentParameter > 0) {
      // If a ',' is the invocation point (which is the case for currentParameter > 0),
      // then align the first parameter name start such that it starts one character
      // more to the right compared to if '(' is the invocation point. This accounts for
      // the fact that we leave a space after a comma, but not after an opening bracket.
      -- leftCharacters;
    }
    
    // Draw the text in the line
    int xCoord = 1 + leftTextAreaWidth - leftCharacters * charWidth;
    parameterIndex = 0;
    for (int pairIndex = 0, size = item.strings.size(); pairIndex < size; ++ pairIndex) {
      const std::pair<QString, ArgumentHintItem::DisplayStyle>& stringPair = item.strings[pairIndex];
      if (stringPair.second == ArgumentHintItem::DisplayStyle::Default) {
        painter.setPen(qRgb(0, 0, 0));
        painter.setFont(Settings::Instance().GetDefaultFont());
      } else if (stringPair.second == ArgumentHintItem::DisplayStyle::ReturnType) {
        painter.setPen(qRgb(0, 127, 0));
        painter.setFont(Settings::Instance().GetDefaultFont());
      } else if (stringPair.second == ArgumentHintItem::DisplayStyle::Parameter) {
        painter.setPen(qRgb(0, 0, 127));
        if (currentParameter == parameterIndex) {
          painter.setFont(Settings::Instance().GetBoldFont());
        } else {
          painter.setFont(Settings::Instance().GetDefaultFont());
        }
        ++ parameterIndex;
      } else if (stringPair.second == ArgumentHintItem::DisplayStyle::Extra) {
        painter.setPen(qRgb(127, 127, 127));
        painter.setFont(Settings::Instance().GetDefaultFont());
      } else {
        qDebug() << "Error: Unhandled argument hint item style";
      }
      
      for (int c = 0, size = stringPair.first.size(); c < size; ++ c) {
        if (xCoord + charWidth > 0) {
          painter.drawText(QRect(xCoord, currentY, charWidth, visibleHeight), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, stringPair.first.at(c));
        }
        
        xCoord += charWidth;
      }
    }
    
    currentY += lineHeight;
  }
}

void ArgumentHintWidget::ScrollChanged(int value) {
  yScroll = value;
  if (scrollBar->value() != value) {
    scrollBar->setValue(value);
  }
  
  Relayout();
}
