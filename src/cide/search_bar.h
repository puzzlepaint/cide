// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QLineEdit>

#include "cide/document_range.h"

class MainWindow;
class SearchListWidget;

class SearchBar : public QLineEdit {
 Q_OBJECT
 public:
  enum class Mode {
    LocalContexts = 0,
    Files,
    GlobalSymbols
  };
  
  
  explicit SearchBar(MainWindow* mainWindow, QWidget* parent = nullptr);
  
  ~SearchBar();
  
  void SetMode(Mode mode);
  
  void ShowListWidget();
  void CloseListWidget();
  
  /// Must be called to notify the widget that its containing window moved.
  void Moved();
  
  inline MainWindow* GetMainWindow() const { return mainWindow; }
  
 public slots:
  /// Must be called after the current document (tab) has changed.
  void CurrentDocumentChanged();
  
  /// Must be called after the cursor moved within the current document.
  void CursorMoved();
  
 private slots:
  void SearchTextChanged();

 protected:
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  bool event(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  
 private:
  void GetCurrentContext();
  
  void ComputeItems();
  
  
  QString currentContexts;
  std::vector<DocumentRange> currentContextBoldRanges;
  
  Mode mode;
  SearchListWidget* mListWidget;
  MainWindow* mainWindow;
};
