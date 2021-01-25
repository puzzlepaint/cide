// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QScrollBar>
#include <QWidget>

#include "cide/document_location.h"
#include "cide/document_range.h"
#include "cide/text_utils.h"

class SearchBar;

struct SearchListItem {
  enum class Type {
    /// A context (e.g., function, class) in the current document. This is shown
    /// in the search list on clicking the search bar, without user input.
    LocalContext = 0,
    
    /// A source file of an opened project.
    ProjectFile,
    
    /// A symbol listed in global search
    GlobalSymbol
  };
  
  inline SearchListItem(Type type, const QString& displayText, const QString& filterText)
      : type(type),
        displayText(displayText),
        filterText(filterText),
        filterTextLowercase(filterText.toLower()),
        matchScore(FuzzyTextMatchScore(0, 0, true, 0)) {}
  
  /// Type of this item.
  Type type;
  
  /// Text displayed in the list widget
  QString displayText;
  
  /// Range of text within displayText that should be displayed in bold
  DocumentRange displayTextBoldRange;
  
  /// If not empty, text that the user input is matched to
  QString filterText;
  QString filterTextLowercase;
  
  /// For type == LocalContext, the location to jump to on activating the item.
  DocumentLocation jumpLocation;
  
  /// Match score between this item and the text input by the user.
  FuzzyTextMatchScore matchScore;
};

class SearchListWidget : public QWidget {
 Q_OBJECT
 public:
  /// Creates a search list widget.
  SearchListWidget(SearchBar* searchBarWidget, QWidget* parent = nullptr);
  
  /// Destructor.
  ~SearchListWidget();
  
  /// Sets the list of items displayed in the widget.
  void SetItems(const std::vector<SearchListItem>&& items);
  
  /// Updates the filter text (i.e., the text typed by the user) with which the
  /// items are filtered.
  void SetFilterText(const QString& text);
  
  /// (Re)computes the widget size and position. Only needs to be called after
  /// the parent widget has moved.
  void Relayout();
  
  /// Accepts the currently selected item.
  void Accept();
  
  /// Returns whether this widget contains at least one item that can possibly be shown.
  inline bool HasItems() const { return !mItems.empty(); }
  
 protected:
  void paintEvent(QPaintEvent* event) override;
  
  void mousePressEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  
 private slots:
  void ScrollChanged(int value);
  
 private:
  void EnsureSelectionIsVisible();
  
  /// Extends the sorting of items to at least the given index.
  void ExtendItemSort(int itemIndex);
  
  
  /// The text by which the items have been filtered.
  QString filterText;
  /// The filter text for items of type ProjectFile.
  QString filterTextFilepath;
  
  /// Stores all search list items (in arbitrary order). The first displayed
  /// item is mItems[mSortOrder[0]].
  std::vector<SearchListItem> mItems;
  
  /// The order of items in this vector determines the order in which items are
  /// displayed. Indexes into mItems.
  std::vector<int> mSortOrder;
  
  /// Indexes into mSortOrder.
  int selectedItem = 0;
  
  /// For performance reasons, only a part of the item list gets sorted when the
  /// filter text changes. numSortedItems stores the number of items
  /// (of mSortOrder) that have been sorted. If any item with a higher
  /// index gets into the view, the sorting must be extended first.
  int numSortedItems;
  
  /// Number of shown items. Any possible additional items are hidden.
  int numShownItems = 0;
  
  
  /// Vertical scroll bar widget.
  QScrollBar* scrollBar;
  
  /// Vertical scroll in pixels.
  int yScroll = 0;
  
  /// Maximum number of visible items in this widget.
  int maxNumVisibleItems = 15;
  
  /// Search bar which this list belongs to.
  SearchBar* searchBarWidget;
  
  
  int lineHeight;
  int charWidth;
};
