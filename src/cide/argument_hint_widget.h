// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>

#include <clang-c/Index.h>
#include <QScrollBar>
#include <QWidget>

struct DocumentLocation;
struct DocumentRange;
class DocumentWidget;

struct ArgumentHintItem {
  enum class DisplayStyle {
    /// Black.
    Default = 0,
    
    /// Green.
    ReturnType,
    
    /// Blue.
    Parameter,
    
    /// Gray.
    Extra,
  };
  
  
  /// Creates an empty argument hint item.
  ArgumentHintItem();
  
  /// Creates a argument hint item from the given libclang completion result
  /// (that must have CursorKind == CXCursor_OverloadCandidate).
  /// The index of the currently active parameter is written into @p activeParameter,
  /// if it can be determined, otherwise it is set to -1.
  ArgumentHintItem(const CXCodeCompleteResults* libclangResults, int index, int* activeParameter);
  
  
  /// Components that make up the displayed text.
  std::vector<std::pair<QString, DisplayStyle>> strings;
  
 private:
  void AppendCompletionString(const CXCompletionString& completion, int* currentParameter, int* activeParameter);
};


class ArgumentHintWidget : public QWidget {
 Q_OBJECT
 public:
  /// Creates an argument hint widget with the given items.
  ArgumentHintWidget(int currentParameter, std::vector<ArgumentHintItem>&& items, QPoint invocationPoint, QWidget* parentWidget, QWidget* parent = nullptr);
  
  /// Destructor.
  ~ArgumentHintWidget();
  
  /// Sets the index of the current parameter in the function call. This
  /// highlights the corresponding parameters in the argument hint. Calls
  /// Relayout().
  void SetCurrentParameter(int index);
  
  /// Re-positions the widget to the given invocation point (relative to the
  /// containing widget). Relayout() should be called afterwards (potentially
  /// implicitly via SetFilterText()).
  void SetInvocationPoint(const QPoint& point);
  
  /// Re-computes the widget size and potentially moves it to adapt to the new
  /// size. Normally there is no need to call this, with the exception of the
  /// parent document widget having moved.
  void Relayout();
  
  inline int GetCurrentParameter() const { return currentParameter; }
  
 protected:
  void paintEvent(QPaintEvent* event) override;
  
 private slots:
  void ScrollChanged(int value);
  
 private:
  /// Stores all argument hint items.
  std::vector<ArgumentHintItem> mItems;
  
  /// Index of the current parameter.
  int currentParameter;
  
  
  /// Containing widget, used to re-compute the global tooltip position after
  /// widget movements.
  QWidget* parentWidget;
  
  /// Relative position of where code completion was invoked within the
  /// containing widget.
  QPoint invocationPosition;
  
  /// Vertical scroll bar widget.
  QScrollBar* scrollBar;
  
  /// Width of the left side of the argument hint widget.
  int leftTextAreaWidth = 0;
  
  /// Vertical scroll in pixels.
  int yScroll = 0;
  
  /// Maximum number of visible items in this widget.
  int maxNumVisibleItems = 15;
  
  
  int lineHeight;
  int charWidth;
};
