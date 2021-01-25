// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>

#include <clang-c/Index.h>
#include <QScrollBar>
#include <QWidget>

#include "cide/text_utils.h"

struct DocumentLocation;
struct DocumentRange;
class DocumentWidget;

struct CompletionItem {
  enum class DisplayStyle {
    /// Black.
    Default = 0,
    
    /// Black, bold.
    FilterText,
    
    /// Blue
    Placeholder,
    
    /// Gray
    Extra,
    
    /// Red background
    Fixit,
  };
  
  
  /// Creates an empty completion item.
  CompletionItem();
  
  /// Creates a completion item from the given libclang completion result.
  CompletionItem(const CXCodeCompleteResults* libclangResults, int index);
  
  
  /// Text displayed in the completion list.
  QString displayText;
  
  /// Text displayed on the left side of the completion list.
  QString returnTypeText;
  
  /// Array of (character index, style) pairs for styling displayText. Each item
  /// represents the start of a style range, which is ended by the next item.
  /// The initial style (at character index 0) is DisplayStyle::Default (unless
  /// another style is specified in displayStyles).
  std::vector<std::pair<int, DisplayStyle>> displayStyles;
  
  /// Text used for filtering (and sorting), which is matched with the user input.
  QString filterText;
  QString lowercaseFilterText;
  
  /// Index of the libclang CXCompletionResult, or -1 if this item was not
  /// created from a libclang completion item.
  int clangCompletionIndex;
  
  /// Number of fix-its that must be applied for this completion item to be viable.
  int numFixits;
  
  /// Whether the item to be completed is available. For example, private class
  /// members are not available outside of the class, but the user may still try
  /// to access them, so they may appear in the code completion.
  bool isAvailable;
  
  /// Heuristical priority that may be used for sorting.
  unsigned int priority;
  
  /// Match quality metric for matching filterText with the text input by the user.
  FuzzyTextMatchScore matchScore;
  
 private:
  void AppendCompletionString(const CXCompletionString& completion, DisplayStyle* currentStyle);
};


class CodeCompletionWidget : public QWidget {
 Q_OBJECT
 public:
  /// Creates a completion widget with the given items. The widget takes
  /// ownership over the libclang results.
  CodeCompletionWidget(std::vector<CompletionItem>&& items, CXCodeCompleteResults* libclangResults, QPoint invocationPoint, QWidget* parentWidget, QWidget* parent = nullptr);
  
  /// Destructor. Frees the libclang results.
  ~CodeCompletionWidget();
  
  /// Updates the filter text (i.e., the text typed by the user) with which the
  /// completions are filtered. Automatically recognizes if the new filter text
  /// is an extension of the previous one, which allows for faster (incremental)
  /// filtering.
  void SetFilterText(const QString& text);
  
  /// Re-positions the widget to the given invocation point (relative to the
  /// containing widget). Relayout() should be called afterwards (potentially
  /// implicitly via SetFilterText()).
  void SetInvocationPoint(const QPoint& point);
  
  /// Returns whether there is only one good match which matches filterText
  /// exactly, and for which the insertion text is equal to its filter text.
  bool HasSingleExactMatch();
  
  /// Applies the currently selected completion item to the document text. Does
  /// not close the widget.
  void Accept(DocumentWidget* widget, const DocumentLocation& invocationLoc);
  
  /// Re-computes the widget size and potentially moves it to adapt to the new
  /// size. Normally there is no need to call this, with the exception of the
  /// parent document widget having moved.
  void Relayout();
  
  /// For debugging purposes. Returns the items array in sorted order.
  inline std::vector<CompletionItem> GetSortedItems() {
    std::vector<CompletionItem> result(mItems.size());
    for (int i = 0; i < mItems.size(); ++ i) {
      result[i] = mItems[mSortOrder[i]];
    }
    return result;
  }
  
 signals:
  void Accepted();
  
 protected:
  void paintEvent(QPaintEvent* event) override;
  
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  
 private slots:
  void ScrollChanged(int value);
  
 private:
  void AppendCompletionString(const CXCompletionString& completion, QString* text, std::vector<DocumentRange>* placeholders, bool skipBracketAndFollowing, bool skipAngleBracketAndFollowing, bool mayAppendSemicolon);
  
  void EnsureSelectionIsVisible();
  
  /// Extends the sorting of items to at least the given index.
  void ExtendItemSort(int itemIndex);
  
  
  /// The text by which the items have been filtered. This corresponds to the
  /// text input by the user in the document after the code completion
  /// invocation location.
  QString filterText;
  
  /// Stores all code completion items (in arbitrary order). The first displayed
  /// item is mItems[mSortOrder[0]].
  std::vector<CompletionItem> mItems;
  
  /// The order of items in this vector determines the order in which items are
  /// displayed. Indexes into mItems.
  std::vector<int> mSortOrder;
  
  /// The original code completion results provided by libclang. They are
  /// retained here such that the corresponding completion items can still
  /// access this original data instead of having to copy everything.
  CXCodeCompleteResults* mLibclangResults;
  
  /// Indexes into mSortOrder.
  int selectedItem = 0;
  
  /// For performance reasons, only a part of the item list gets sorted when the
  /// filter text changes. numSortedItems stores the number of items
  /// (of mSortOrder) that have been sorted. If any item with a higher
  /// index gets into the view, the sorting must be extended first.
  int numSortedItems;
  
  
  /// Containing widget, used to re-compute the global tooltip position after
  /// widget movements.
  QWidget* parentWidget;
  
  /// Relative position of where code completion was invoked within the
  /// containing widget.
  QPoint invocationPosition;
  
  /// Vertical scroll bar widget.
  QScrollBar* scrollBar;
  
  /// Width of the left side of the code completion widget.
  int returnTypeTextAreaWidth;
  
  /// Vertical scroll in pixels.
  int yScroll = 0;
  
  /// Maximum number of visible items in this widget.
  int maxNumVisibleItems = 15;
  
  
  int lineHeight;
  int charWidth;
};
