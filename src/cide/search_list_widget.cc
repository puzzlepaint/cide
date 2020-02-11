// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/search_list_widget.h"

#include <QDebug>
#include <QPainter>
#include <QPaintEvent>

#include "cide/document_widget.h"
#include "cide/main_window.h"
#include "cide/search_bar.h"
#include "cide/settings.h"

struct SearchBarItemSorter {
  inline SearchBarItemSorter(SearchListItem* items)
      : items(items) {}
  
  inline bool operator() (int indexA, int indexB) const {
    const SearchListItem& itemA = items[indexA];
    const SearchListItem& itemB = items[indexB];
    
    // Sort based on the match quality between the items' filter texts and the
    // text input by the user.
    int scoreComparison = itemA.matchScore.Compare(itemB.matchScore);
    if (scoreComparison != -1) {
      return scoreComparison;
    }
    
    // Sort based on text length.
    // TODO: Should this be part of the matchScore?
    if (itemA.filterText.size() != itemB.filterText.size()) {
      return itemA.filterText.size() < itemB.filterText.size();
    }
    
    // If the items are otherwise equal, use their indices to get an unambiguous
    // ordering as a last resort
    return indexA < indexB;
  }
  
  SearchListItem* items;
};


SearchListWidget::SearchListWidget(SearchBar* searchBarWidget, QWidget* parent)
    : QWidget(parent, GetCustomTooltipWindowFlags()) {
  setFocusPolicy(Qt::NoFocus);
  setAutoFillBackground(false);
  
  scrollBar = new QScrollBar(Qt::Vertical, this);
  connect(scrollBar, &QScrollBar::valueChanged, this, &SearchListWidget::ScrollChanged);
  
  this->searchBarWidget = searchBarWidget;
}

SearchListWidget::~SearchListWidget() {}

void SearchListWidget::SetItems(const std::vector<SearchListItem>&& items) {
  mItems = items;
  mSortOrder.resize(mItems.size());
  for (int i = 0, size = mItems.size(); i < size; ++ i) {
    mSortOrder[i] = i;
  }
  numShownItems = mItems.size();
  
  selectedItem = 0;
  yScroll = 0;
}

void SearchListWidget::SetFilterText(const QString& text) {
  QString defaultFilterText = text.trimmed();
  
  // Try to parse the text as "filepath:line:column" or "filepath:line".
  // Items of type ProjectFile will be filtered with the filepath only.
  QString filepathFilterText;
  int line, column;
  SplitPathAndLineAndColumn(defaultFilterText, &filepathFilterText, &line, &column);
  // Further clean up the filepath filter text by changing redundant "/./" path elements
  // to "/".
  while (true) {
    QString oldFilepathFilterText = filepathFilterText;
    filepathFilterText.replace("/./", "/");
    if (oldFilepathFilterText == filepathFilterText) {
      break;
    }
  }
  
  // Score each item according to how well it matches the new filter text.
  constexpr int kMaxNonMatchedCharacters = 2;
  int minMatchedCharactersDefault = std::max(0, defaultFilterText.size() - kMaxNonMatchedCharacters);
  int minMatchedCharactersFilepath = std::max(0, filepathFilterText.size() - kMaxNonMatchedCharacters);
  numShownItems = 0;
  for (int i = 0, numItems = mItems.size(); i < numItems; ++ i) {
    SearchListItem& item = mItems[i];
    
    QString* filterText;
    int minMatchedCharacters;
    if (item.type == SearchListItem::Type::ProjectFile) {
      filterText = &filepathFilterText;
      minMatchedCharacters = minMatchedCharactersFilepath;
    } else {
      filterText = &defaultFilterText;
      minMatchedCharacters = minMatchedCharactersDefault;
    }
    
    ComputeFuzzyTextMatch(*filterText, item.filterText, &item.matchScore);
    if (item.matchScore.matchedCharacters >= minMatchedCharacters) {
      ++ numShownItems;
    }
  }
  
  // Sort the items.
  numSortedItems = std::min<std::size_t>(maxNumVisibleItems, numShownItems);
  std::partial_sort(mSortOrder.begin(), mSortOrder.begin() + numSortedItems, mSortOrder.end(), SearchBarItemSorter(mItems.data()));
  
  // We currently never preserve the selection when the filter text changes.
  selectedItem = 0;
  yScroll = 0;
  
  filterText = defaultFilterText;
  filterTextFilepath = filepathFilterText;
  
  Relayout();
}

void SearchListWidget::Relayout() {
  // Get font metrics
  QFontMetrics fontMetrics(Settings::Instance().GetDefaultFont());
  lineHeight = fontMetrics.ascent() + fontMetrics.descent();
  charWidth = fontMetrics./*horizontalAdvance*/ width(' ');
  
  int goodHeight = 2;
  
  int visibleItems = std::min<int>(numShownItems, maxNumVisibleItems);
  goodHeight += visibleItems * lineHeight;
  
  QPoint leftPoint = searchBarWidget->mapToGlobal(searchBarWidget->rect().bottomLeft());
  QPoint rightPoint = searchBarWidget->mapToGlobal(searchBarWidget->rect().bottomRight());
  int goodWidth = rightPoint.x() - leftPoint.x();
  
  if (leftPoint != pos() || width() != goodWidth || height() != goodHeight) {
    setGeometry(leftPoint.x(), leftPoint.y(), goodWidth, goodHeight);
    
    int scrollBarWidth = scrollBar->sizeHint().width();
    scrollBar->setGeometry(width() - scrollBarWidth - 1, 1, scrollBarWidth, height() - 2);
    // Note that maxScroll needs to be recomputed here since setGeometry() might
    // have changed the widget height().
    int maxScroll = numShownItems * lineHeight - (height() - 2);
    if (maxScroll <= 0) {
      scrollBar->setVisible(false);
    } else {
      scrollBar->setVisible(true);
      scrollBar->setRange(0, maxScroll);
    }
  } else if (scrollBar->isVisible()) {
    int maxScroll = numShownItems * lineHeight - (height() - 2);
    scrollBar->setRange(0, maxScroll);
  }
  update(rect());
}

void SearchListWidget::Accept() {
  if (numShownItems == 0) {
    return;
  }
  if (selectedItem < 0 || selectedItem >= numShownItems) {
    qDebug() << "Error: SearchListWidget::Accept(): Invalid value of selectedItem";
    return;
  }
  const SearchListItem& item = mItems[mSortOrder[selectedItem]];
  if (item.type == SearchListItem::Type::LocalContext) {
    DocumentWidget* widget = searchBarWidget->GetMainWindow()->GetCurrentDocumentWidget();
    if (widget) {
      widget->SetCursor(item.jumpLocation, false);
      widget->ScrollTo(item.jumpLocation);
      widget->setFocus();
    }
  } else if (item.type == SearchListItem::Type::ProjectFile) {
    QString dummyFilepath;
    int line, column;
    SplitPathAndLineAndColumn(filterText, &dummyFilepath, &line, &column);
    
    QString jumpUrl = QStringLiteral("file://") + item.displayText;
    if (line >= 0) {
      jumpUrl += QStringLiteral(":") + QString::number(line);
    }
    if (column >= 0) {
      jumpUrl += QStringLiteral(":") + QString::number(column);
    }
    searchBarWidget->GetMainWindow()->GotoDocumentLocation(jumpUrl);
  } else if (item.type == SearchListItem::Type::GlobalSymbol) {
    QStringList words = item.displayText.split(" ");
    if (words.empty()) {
      qDebug() << "Error: Cannot parse jump location for a GlobalSymbol from display string:" << item.displayText;
    } else {
      searchBarWidget->GetMainWindow()->GotoDocumentLocation(QStringLiteral("file://") + words.back());
    }
  } else {
    qDebug() << "Error: SearchListWidget::Accept(): Item type not handled.";
  }
  
  searchBarWidget->clear();
  searchBarWidget->CloseListWidget();
}

void SearchListWidget::paintEvent(QPaintEvent* event) {
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
  int maxItem = std::min<int>(numShownItems - 1, (yScroll + (rect.bottom() - 1)) / lineHeight);
  
  int currentY = 1 + minItem * lineHeight - yScroll;
  for (int itemIndex = minItem; itemIndex <= maxItem; ++ itemIndex) {
    const SearchListItem& item = mItems[mSortOrder[itemIndex]];
    
    int visibleHeight = std::min(height() - 1 - currentY, lineHeight);
    
    // Draw the line background
    int intensity = 255 - std::min(80, 20 * std::max(
        item.matchScore.matchErrors,
        ((item.type == SearchListItem::Type::ProjectFile) ? filterTextFilepath.size() : filterText.size()) - item.matchScore.matchedCharacters));
    if (item.matchScore.matchedStartIndex > 0) {
      intensity = std::min(intensity, 255 - 20);
    }
    QColor backgroundColor = qRgb(intensity, intensity, intensity);
    if (itemIndex == selectedItem) {
      backgroundColor.setRed(0.75 * backgroundColor.red());
      backgroundColor.setGreen(0.75 * backgroundColor.green());
    }
    painter.fillRect(1, currentY, (width() - 1) - 1, visibleHeight, backgroundColor);
    
    // Draw the display text
    painter.setPen(qRgb(0, 0, 0));
    painter.setFont(Settings::Instance().GetDefaultFont());
    bool usingBoldFont = false;
    
    int xCoord = 1;
    for (int c = 0, size = item.displayText.size(); c < size; ++ c) {
      int visibleWidth = std::min(width() - 1 - xCoord, charWidth);
      
      bool bold = item.displayTextBoldRange.ContainsCharacter(c);
      if (bold != usingBoldFont) {
        painter.setFont(bold ? Settings::Instance().GetBoldFont() : Settings::Instance().GetDefaultFont());
        usingBoldFont = bold;
      }
      
      // Draw the character
      painter.drawText(QRect(xCoord, currentY, visibleWidth, visibleHeight), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, item.displayText.at(c));
      xCoord += charWidth;
    }
    
    currentY += lineHeight;
  }
  
  painter.end();
}

void SearchListWidget::mousePressEvent(QMouseEvent* event) {
  selectedItem = std::min(numShownItems - 1, std::max(0, (yScroll + (event->y() - 1)) / lineHeight));
  update(rect());
  Accept();
}

void SearchListWidget::wheelEvent(QWheelEvent* event) {
  double degrees = event->delta() / 8.0;
  double numSteps = degrees / 15.0;
  
  int newYScroll = yScroll - 3 * numSteps * lineHeight;
  newYScroll = std::max(0, newYScroll);
  newYScroll = std::min(scrollBar->isVisible() ? scrollBar->maximum() : 0, newYScroll);
  
  if (newYScroll != yScroll) {
    ScrollChanged(newYScroll);
  }
}

void SearchListWidget::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Up) {
    if (selectedItem > 0) {
      -- selectedItem;
      EnsureSelectionIsVisible();
    }
    update(rect());
    
    event->accept();
    return;
  } else if (event->key() == Qt::Key_Down) {
    if (selectedItem < numShownItems - 1) {
      ++ selectedItem;
      if (selectedItem >= numSortedItems) {
        ExtendItemSort(selectedItem);
      }
      EnsureSelectionIsVisible();
    }
    update(rect());
    
    event->accept();
    return;
  } else if (event->key() == Qt::Key_Return ||
             event->key() == Qt::Key_Tab) {
    Accept();
    event->accept();
    return;
  }
  
  event->ignore();
}

void SearchListWidget::ScrollChanged(int value) {
  yScroll = value;
  if (scrollBar->value() != value) {
    scrollBar->setValue(value);
  }
  
  int maxItem = std::min<int>(numShownItems - 1, (yScroll + (height() - 1)) / lineHeight);
  ExtendItemSort(maxItem);
  
  update(rect());
}

void SearchListWidget::EnsureSelectionIsVisible() {
  int selectionMinY = selectedItem * lineHeight;
  int selectionMaxY = (selectedItem + 1) * lineHeight - 1;
  
  int oldYScroll = yScroll;
  yScroll = std::max(yScroll, selectionMaxY - height() + 3);
  yScroll = std::min(yScroll, selectionMinY);
  if (oldYScroll != yScroll) {
    scrollBar->setValue(yScroll);
    update(rect());
  }
}

void SearchListWidget::ExtendItemSort(int itemIndex) {
  if (itemIndex < numSortedItems) {
    return;
  }
  
  // Note: we arbitrarily add maxNumVisibleItems to itemIndex here such that we
  // won't need to sort again until this new index is reached.
  int newNumSortedItems = std::min<std::size_t>(itemIndex + maxNumVisibleItems, numShownItems);
  std::partial_sort(mSortOrder.begin() + numSortedItems, mSortOrder.begin() + newNumSortedItems, mSortOrder.end(), SearchBarItemSorter(mItems.data()));
  numSortedItems = newNumSortedItems;
}
