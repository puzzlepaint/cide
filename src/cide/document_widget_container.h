// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "cide/document_widget.h"

class EscapeSignalingLineEdit;
class MainWindow;
class ScrollbarMinimap;

/// Container widget that creates both a document widget and supplemental
/// widgets for searching/replacing, goto line, etc.
class DocumentWidgetContainer : public QWidget {
 Q_OBJECT
 public:
  // NOTE: The entries must have sequential numbers, starting from zero.
  enum class MessageType {
    ParseSettingsAreGuessedNotification = 0,
    ParseNotification = 1,
    ExternalModificationNotification = 2,
    
    HighestMessageType = ExternalModificationNotification
  };
  
  DocumentWidgetContainer(const std::shared_ptr<Document>& document, MainWindow* mainWindow, QWidget* parent = nullptr);
  
  void SetMessage(MessageType type, const QString& message);
  
  inline DocumentWidget* GetDocumentWidget() { return mDocumentWidget; }
  inline ScrollbarMinimap* GetMinimap() { return minimap; }
  inline QScrollBar* GetScrollbar() { return scrollbar; }
  
 public slots:
  void ShowGotoLineBar();
  void CloseGotoLineBar();
  void ApplyGotoLine();
  
  void ShowFindBar();
  void CloseFindReplaceBar();
  void FindPrevious();
  void FindNext();
  void FindTextChanged();
  
  void ShowReplaceBar();
  void ReplaceReturnPressed(bool ctrlHeld);
  void Replace();
  void ReplaceAll();
  
  void DocumentCursorMoved();
  void FileChangedExternally();
  
 private slots:
  void GotoLineChanged(const QString& text);
  
 private:
  void FindImpl(bool startFromSelection, bool forwards, bool updateSearchStart);
  
  
  // Message labels (indexed by static_cast<int>(messageType)).
  std::vector<QLabel*> mMessageLabels;
  
  // Document widget
  DocumentWidget* mDocumentWidget;
  
  // Scrollbar mini-map
  ScrollbarMinimap* minimap;
  
  // Horizontal scrollbar
  QScrollBar* scrollbar;
  
  // "Go to line" bar
  QWidget* gotoLineContainer;
  EscapeSignalingLineEdit* gotoLineEdit;
  QPushButton* gotoLineButton;
  
  // "Find" bar
  QWidget* findReplaceContainer;
  EscapeSignalingLineEdit* findEdit;
  QPushButton* findNextButton;
  QPushButton* findPreviousButton;
  QCheckBox* findMatchCase;
  DocumentRange findReplaceStartRange;
  bool doNotSearchOnTextChange = false;
  
  // "Replace" bar (within the "Find" bar)
  QWidget* replaceContainer;
  EscapeSignalingLineEdit* replaceEdit;
  QPushButton* replaceButton;
  QPushButton* replaceAllButton;
  QCheckBox* replaceInSelectionOnly;
};
