// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/document_widget.h"

#include <iostream>
#include <mutex>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopWidget>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>
#include <QScrollArea>
#include <QStringBuilder>
#include <QStyleOption>

#include "cide/clang_parser.h"
#include "cide/code_completion_widget.h"
#include "cide/code_info.h"
#include "cide/cpp_utils.h"
#include "cide/crash_backup.h"
#include "cide/document_widget_container.h"
#include "cide/git_diff.h"
#include "cide/main_window.h"
#include "cide/parse_thread_pool.h"
#include "cide/rename_dialog.h"
#include "cide/scroll_bar_minimap.h"
#include "cide/settings.h"
#include "cide/text_utils.h"
#include "cide/util.h"


DocumentWidget::DocumentWidget(const std::shared_ptr<Document>& document, DocumentWidgetContainer* container, MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent),
      document(document),
      container(container),
      mainWindow(mainWindow) {
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::IBeamCursor);
  setAutoFillBackground(false);
  
  connect(&Settings::Instance(), &Settings::FontChanged, this, &DocumentWidget::FontChanged);
  FontChanged();
  
  QAction* undoAction = new ActionWithConfigurableShortcut(tr("Undo"), undoShortcut, this);
  connect(undoAction, &QAction::triggered, this, &DocumentWidget::Undo);
  addAction(undoAction);
  
  QAction* redoAction = new ActionWithConfigurableShortcut(tr("Redo"), redoShortcut, this);
  connect(redoAction, &QAction::triggered, this, &DocumentWidget::Redo);
  addAction(redoAction);
  
  QAction* cutAction = new ActionWithConfigurableShortcut(tr("Cut"), cutShortcut, this);
  connect(cutAction, &QAction::triggered, this, &DocumentWidget::Cut);
  addAction(cutAction);
  
  QAction* copyAction = new ActionWithConfigurableShortcut(tr("Copy"), copyShortcut, this);
  connect(copyAction, &QAction::triggered, this, &DocumentWidget::Copy);
  addAction(copyAction);
  
  QAction* pasteAction = new ActionWithConfigurableShortcut(tr("Paste"), pasteShortcut, this);
  connect(pasteAction, &QAction::triggered, this, &DocumentWidget::Paste);
  addAction(pasteAction);
  
  QAction* openFindBarAction = new ActionWithConfigurableShortcut(tr("Find"), findShortcut, this);
  connect(openFindBarAction, &QAction::triggered, container, &DocumentWidgetContainer::ShowFindBar);
  addAction(openFindBarAction);
  
  QAction* openReplaceBarAction = new ActionWithConfigurableShortcut(tr("Replace"), replaceShortcut, this);
  connect(openReplaceBarAction, &QAction::triggered, container, &DocumentWidgetContainer::ShowReplaceBar);
  addAction(openReplaceBarAction);
  
  QAction* openGotoLineAction = new ActionWithConfigurableShortcut(tr("Go to line"), gotoLineShortcut, this);
  connect(openGotoLineAction, &QAction::triggered, container, &DocumentWidgetContainer::ShowGotoLineBar);
  addAction(openGotoLineAction);
  
  QAction* toggleBookmarkAction = new ActionWithConfigurableShortcut(tr("Toggle bookmark"), toggleBookmarkShortcut, this);
  connect(toggleBookmarkAction, &QAction::triggered, this, &DocumentWidget::ToggleBookmark);
  addAction(toggleBookmarkAction);
  
  QAction* jumpToPreviousBookmarkAction = new ActionWithConfigurableShortcut(tr("Jump to previous bookmark"), jumpToPreviousBookmarkShortcut, this);
  connect(jumpToPreviousBookmarkAction, &QAction::triggered, this, &DocumentWidget::JumpToPreviousBookmark);
  addAction(jumpToPreviousBookmarkAction);
  
  QAction* jumpToNextBookmarkAction = new ActionWithConfigurableShortcut(tr("Jump to next bookmark"), jumpToNextBookmarkShortcut, this);
  connect(jumpToNextBookmarkAction, &QAction::triggered, this, &DocumentWidget::JumpToNextBookmark);
  addAction(jumpToNextBookmarkAction);
  
  QAction* removeAllBookmarksAction = new ActionWithConfigurableShortcut(tr("Remove all bookmarks"), removeAllBookmarksShortcut, this);
  connect(removeAllBookmarksAction, &QAction::triggered, this, &DocumentWidget::RemoveAllBookmarks);
  addAction(removeAllBookmarksAction);
  
  QAction* commentAction = new ActionWithConfigurableShortcut(tr("Comment out"), commentOutShortcut, this);
  connect(commentAction, &QAction::triggered, this, &DocumentWidget::Comment);
  addAction(commentAction);
  
  QAction* uncommentAction = new ActionWithConfigurableShortcut(tr("Uncomment"), uncommentShortcut, this);
  connect(uncommentAction, &QAction::triggered, this, &DocumentWidget::Uncomment);
  addAction(uncommentAction);
  
  QAction* codeCompleteAction = new ActionWithConfigurableShortcut(tr("Invoke code completion"), invokeCodeCompletionShortcut, this);
  connect(codeCompleteAction, &QAction::triggered, this, &DocumentWidget::InvokeCodeCompletion);
  addAction(codeCompleteAction);
  
  QAction* showDocInDockAction = new ActionWithConfigurableShortcut(tr("Show documentation in dock"), showDocumentationInDockShortcut, this);
  connect(showDocInDockAction, &QAction::triggered, this, &DocumentWidget::ShowDocumentationInDock);
  addAction(showDocInDockAction);
  
  QAction* renameAction = new ActionWithConfigurableShortcut(tr("Rename item at cursor"), renameItemAtCursorShortcut, this);
  connect(renameAction, &QAction::triggered, this, &DocumentWidget::RenameItemAtCursor);
  addAction(renameAction);
  
  QAction* fixAllAction = new ActionWithConfigurableShortcut(tr("Fix all trivial issues"), fixAllVisibleTrivialIssuesShortcut, this);
  connect(fixAllAction, &QAction::triggered, this, &DocumentWidget::FixAll);
  addAction(fixAllAction);
  
  fixitButtonsDocumentVersion = -1;
  
  cursorBlinkTimer = new QTimer(this);
  connect(cursorBlinkTimer, &QTimer::timeout, this, &DocumentWidget::BlinkCursor);
  cursorBlinkTimer->start(cursorBlinkInterval);
  
  warningIcon = QImage(":/cide/warning-icon-16x16.png");
  errorIcon = QImage(":/cide/error-icon-16x16.png");
  
  // Try to load the "spaces per tab" setting from the project for this file
  for (const auto& project : mainWindow->GetProjects()) {
    if (project->ContainsFile(document->path())) {
      if (project->GetSpacesPerTab() != -1) {
        spacesPerTab = project->GetSpacesPerTab();
      }
      break;
    }
  }
  
  // Create the right-click menu
  rightClickMenu = new QMenu(tr("Right-click menu"), this);
  renameClickedItemAction = rightClickMenu->addAction("");
  connect(renameClickedItemAction, &QAction::triggered, this, &DocumentWidget::RenameClickedItem);
  
  // Emit the initial cursor position in a timer, since otherwise, nobody has
  // had a chance to connect any slots to the signal yet.
  QTimer::singleShot(0, [&](){
    emit CursorMoved(cursorLine, cursorCol);
  });
  
  parseTimer = new QTimer(this);
  parseTimer->setSingleShot(true);
  connect(parseTimer, &QTimer::timeout, this, &DocumentWidget::ParseFile);
  connect(document.get(), &Document::Changed, this, &DocumentWidget::StartParseTimer);
  StartParseTimer();
  connect(document.get(), &Document::HighlightingChanged, this, &DocumentWidget::HighlightingChanged);
  
  mouseHoverTimer.setSingleShot(true);
  connect(&mouseHoverTimer, &QTimer::timeout, [&]() {
    if (QCursor::pos() != mouseHoverPosGlobal) {
      return;
    }
    
    int line, character;
    if (!GetCharacterAt(mouseHoverPosLocal.x(), mouseHoverPosLocal.y(), false, &line, &character)) {
      return;
    }
    int offset = layoutLines[line].start.offset + character;
    
    Document::CharacterIterator charIt(this->document.get(), offset);
    if (charIt.IsValid() && IsWhitespace(charIt.GetChar())) {
      return;
    }
    
    codeInfoRequestRect = GetTextRect(GetWordForCharacter(offset));
    
    CodeInfo::Instance().RequestCodeInfo(this, DocumentLocation(offset));
  });
  
  setMouseTracking(true);
  CheckFileType();
}

DocumentWidget::~DocumentWidget() {
  // Ensure that no background thread tries to access this widget anymore
  ParseThreadPool::Instance().WidgetRemoved(this);
  CodeInfo::Instance().WidgetRemoved(this);
  GitDiff::Instance().WidgetRemoved(this);
  
  delete argumentHintWidget;
  delete codeCompletionWidget;
  if (tooltip) {
    tooltip->deleteLater();
  }
}

void DocumentWidget::EnsureCursorIsInView(int marginInPixels) {
  CheckRelayout();
  
  QRect cursorRect = GetCursorRect();
  int newYScroll = yScroll;
  int newXScroll = xScroll;
  
  if (cursorRect.y() - marginInPixels < 0) {
    newYScroll = std::max(0, yScroll + cursorRect.y() - marginInPixels);
  } else if (cursorRect.bottom() + marginInPixels >= height()) {
    newYScroll = std::min(GetMaxYScroll(), yScroll + cursorRect.bottom() + marginInPixels - height() + 1);
  }
  
  if (cursorRect.x() < sidebarWidth) {
    newXScroll += cursorRect.x() - sidebarWidth;
  } else if (cursorRect.right() >= width()) {
    newXScroll += cursorRect.right() - width() + 1;
  }
  
  SetXYScroll(newXScroll, newYScroll);
}

void DocumentWidget::ScrollTo(const DocumentLocation& location) {
  int line, col;
  if (!MapDocumentToLayout(location, &line, &col)) {
    qDebug() << "Warning: DocumentWidget::ScrollTo(): Cannot scroll to the given location since it was not found in the layout.";
    return;
  }
  
  SetYScroll(yScroll + GetLineRect(line).top());
}

void DocumentWidget::SetCursor(int x, int y, bool addToSelection) {
  StartMovingCursor();
  GetDocumentLocationAt(x, y, nullptr, &cursorLine, &cursorCol);
  EndMovingCursor(addToSelection);
}

void DocumentWidget::SetCursor(const DocumentLocation& location, bool addToSelection) {
  StartMovingCursor();
  SetCursorTo(location);
  EndMovingCursor(addToSelection);
}

void DocumentWidget::Replace(const DocumentRange& range, const QString& newText, bool createUndoStep, Replacement* undoReplacement) {
  DocumentRange selectionRange;
  if (selection.IsEmpty()) {
    selectionRange.start = MapCursorToDocument();
    selectionRange.end = selectionRange.start;
  } else {
    selectionRange = selection;
  }
  
  document->Replace(range, newText, createUndoStep, undoReplacement);
  
  auto adaptDocumentLocation = [&](DocumentLocation* loc) {
    if (*loc < range.start) {
      // Nothing to do.
    } else if (*loc < range.end) {
      *loc = range.start + newText.size();
    } else {
      *loc += newText.size() - range.size();
    }
  };
  adaptDocumentLocation(&selectionRange.start);
  adaptDocumentLocation(&selectionRange.end);
  
  if (selection.IsEmpty()) {
    SetCursor(selectionRange.start, false);
  } else {
    SetSelection(selectionRange);
  }
}

void DocumentWidget::ReplaceAll(const QString& find, const QString& replacement, bool matchCase, bool inSelectionOnly) {
  document->StartUndoStep();
  
  std::vector<DocumentLocation> locsToPreserve;
  if (GetSelection().IsEmpty()) {
    locsToPreserve = {MapCursorToDocument()};
  } else {
    locsToPreserve = {GetSelection().start, GetSelection().end};
  }
  DocumentRange selectionRange = GetSelection();
  
  int numReplacements = 0;
  std::vector<DocumentRange> replacedRanges;
  DocumentLocation findStart = inSelectionOnly ? selectionRange.end : document->FullDocumentRange().end;
  while (true) {
    DocumentLocation result = document->Find(find, findStart, false, matchCase);
    if (result.IsInvalid() ||
        (inSelectionOnly && result < selectionRange.start)) {
      break;
    }
    
    for (DocumentLocation& loc : locsToPreserve) {
      if (loc >= result + find.size()) {
        loc += replacement.size() - find.size();
      } else if (loc >= result) {
        loc = result;
      }
    }
    
    document->Replace(DocumentRange(result, result + find.size()), replacement);
    int offset = replacement.size() - find.size();
    for (DocumentRange& range : replacedRanges) {
      range.start += offset;
      range.end += offset;
    }
    replacedRanges.emplace_back(result, result + replacement.size());
    ++ numReplacements;
    
    findStart = result;
  }
  
  document->EndUndoStep();
  
  if (locsToPreserve.size() == 1) {
    SetCursor(locsToPreserve[0], false);
  } else {
    SetSelection(DocumentRange(locsToPreserve[0], locsToPreserve[1]));
  }
  
  GetDocument()->ClearHighlightRanges(kHighlightLayer);
  const auto& justReplacedStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::JustReplaced);
  for (const DocumentRange& range : replacedRanges) {
    GetDocument()->AddHighlightRange(range, false, justReplacedStyle, kHighlightLayer);
  }
  
  update(rect());
  setFocus();
  
  if (numReplacements == 1) {
    GetMainWindow()->SetStatusText(tr("1 replacement done"));
  } else {
    GetMainWindow()->SetStatusText(tr("%1 replacements done").arg(numReplacements));
  }
}

void DocumentWidget::InsertText(const QString& text) {
  QRect updateRect;
  StartMovingCursor();
  
  // Delete current selection, if any
  DocumentRange replacementRange;
  if (selection.IsValid()) {
    updateRect = GetTextRect(selection);
    replacementRange = selection;
    selection = DocumentRange::Invalid();
  } else {
    DocumentLocation cursorOffset = MapCursorToDocument();
    replacementRange = DocumentRange(cursorOffset, cursorOffset);
  }
  
  document->Replace(replacementRange, text);
  SetCursorTo(replacementRange.start + text.size());
  
  if (true) {  // TODO: Only if the number of lines changed
    updateRect.setLeft(0);
    updateRect.setRight(width());
    updateRect.setBottom(height());
  }
  update(updateRect);
  EndMovingCursor(false);
}

QString DocumentWidget::GetSelectedText() const {
  if (selection.IsEmpty()) {
    return QStringLiteral("");
  }
  
  return document->TextForRange(selection);
}

bool DocumentWidget::GetCharacterAt(int x, int y, bool clamp, int* line, int* character, bool* wasClamped) {
  if (wasClamped) {
    *wasClamped = false;
  }
  
  *line = (yScroll + y) / lineHeight;
  if (*line >= layoutLines.size()) {
    if (!clamp) {
      return false;
    }
    if (wasClamped) {
      *wasClamped = true;
    }
    *line = static_cast<int>(layoutLines.size()) - 1;
  }
  
  QString lineText = document->TextForRange(layoutLines[*line]);
  int xCoord = sidebarWidth;
  int column = 0;
  for (int c = 0; c < lineText.size(); ++ c) {
    int charColumns;
    int charWidth = GetTextWidth(lineText.at(c), column, &charColumns);
    column += charColumns;
    if (x + xScroll < xCoord + charWidth) {
      *character = c;
      return true;
    }
    xCoord += charWidth;
  }
  
  if (clamp) {
    if (wasClamped) {
      *wasClamped = true;
    }
    *character = lineText.isEmpty() ? 0 : (lineText.size() - 1);
    return true;
  } else {
    return false;
  }
}

void DocumentWidget::GetDocumentLocationAt(int x, int y, DocumentLocation* loc, int* line, int* col) {
  int localLine;
  if (!line) {
    line = &localLine;
  }
  int localCol;
  if (!col) {
    col = &localCol;
  }
  
  *line = std::max(0, std::min(static_cast<int>(layoutLines.size()) - 1, (yScroll + y) / lineHeight));
  
  QString lineText = document->TextForRange(layoutLines[*line]);
  *col = lineText.size();
  int xCoord = sidebarWidth;
  int column = 0;
  for (int c = 0; c < lineText.size(); ++ c) {
    int charColumns;
    int charWidth = GetTextWidth(lineText.at(c), column, &charColumns);
    column += charColumns;
    if (x + xScroll < xCoord + charWidth / 2) {
      *col = c;
      break;
    }
    xCoord += charWidth;
  }
  
  if (loc) {
    *loc = MapLayoutToDocument(*line, *col);
  }
}

DocumentRange DocumentWidget::GetWordForCharacter(int characterOffset) {
  return document->RangeForWordAt(characterOffset, &GetCharType, static_cast<int>(CharacterType::Symbol));
}

void DocumentWidget::SetSelection(const DocumentRange& range) {
  // NOTE: EndMovingCursor() may also modify the selection (without calling this
  //       function).
  
  if (!range.IsInvalid()) {
    StartMovingCursor();
    SetCursorTo(range.end);
    EndMovingCursor(false);
  }
  
  selection = range;
  preSelectionCursor = range.start;
  
  // Check whether a whole word or phrase is selected. If yes, highlight all
  // occurrences of it.
  RemoveHighlights();
  CheckPhraseHighlight();
  
  update(GetTextRect(selection));
}

void DocumentWidget::CloseCodeCompletion() {
  // Close the widget in case it is open.
  delete codeCompletionWidget;
  codeCompletionWidget = nullptr;
  
  // This prevents the code completion thread from potentially opening the
  // widget again later.
  ++ codeCompletionInvocationCounter;
  
  // This conveys that we do not expect to get a code completion result from
  // the code completion thread.
  codeCompletionInvocationLocation = DocumentLocation::Invalid();
}

void DocumentWidget::ShowCodeCompletion(DocumentLocation invocationLocation, std::vector<CompletionItem>&& items, CXCodeCompleteResults* libclangResults) {
  if (codeCompletionInvocationLocation.IsInvalid()) {
    qDebug() << "ShowCodeCompletion(): codeCompletionInvocationLocation is invalid";
    return;
  }
  
  // Close the widget in case it is open.
  delete codeCompletionWidget;
  
  DocumentLocation cursorLoc = MapCursorToDocument();
  if (cursorLoc < codeCompletionInvocationLocation) {
    // qDebug() << "ShowCodeCompletion(): cursorLoc (" << cursorLoc.offset << ") < codeCompletionInvocationLocation (" << codeCompletionInvocationLocation.offset << ")";
    codeCompletionWidget = nullptr;
    return;
  }
  
  // Open the new widget.
  QPoint invocationPoint = GetTextRect(DocumentRange(invocationLocation, invocationLocation)).bottomLeft() + QPoint(0, 1);
  // NOTE: Ownership of "results" is passed on to the widget here.
  codeCompletionWidget = new CodeCompletionWidget(std::move(items), libclangResults, invocationPoint, this);
  connect(codeCompletionWidget, &CodeCompletionWidget::Accepted, this, &DocumentWidget::AcceptCodeCompletion);
  codeCompletionWidget->SetFilterText(document->TextForRange(DocumentRange(codeCompletionInvocationLocation, cursorLoc)));
  codeCompletionWidget->show();
}

void DocumentWidget::CodeCompletionRequestWasDiscarded() {
  // Set the invocation location to invalid in order not to expect
  // getting code completion results anymore. This enables making
  // a new request (otherwise, we might be stuck waiting for the old
  // request which will never be fulfilled).
  codeCompletionInvocationLocation = DocumentLocation::Invalid();
}

void DocumentWidget::UpdateCodeCompletion(const DocumentLocation& cursorLoc) {
  if (cursorLoc < codeCompletionInvocationLocation) {
    CloseCodeCompletion();
  } else {
    // If there is any non-identifier-character in the range of text between the
    // cursor and the invocation location, close the code completion widget.
    // Otherwise, update its filter text with that range.
    QString filterText;
    bool closeCompletion = false;
    bool isOnlySpace = true;
    bool containsSpace = false;
    
    Document::CharacterIterator it(document.get(), codeCompletionInvocationLocation.offset);
    if (!it.IsValid()) {
      CloseCodeCompletion();
      return;
    }
    
    while (it.IsValid() && it.GetCharacterOffset() < cursorLoc.offset) {
      QChar character = it.GetChar();
      if (character == '\n') {
        CloseCodeCompletion();
        return;
      } else if (isOnlySpace && character.isSpace()) {
        containsSpace = true;
        ++ it;
        continue;
      }
      isOnlySpace = false;
      if (!IsIdentifierChar(character) || (containsSpace && !isOnlySpace)) {
        closeCompletion = true;
        break;
      }
      filterText += character;
      ++ it;
    }
    
    if (isOnlySpace) {
      if (codeCompletionWidget) {
        codeCompletionInvocationLocation = cursorLoc;
        codeCompletionWidget->SetInvocationPoint(GetTextRect(DocumentRange(cursorLoc, cursorLoc)).bottomLeft() + QPoint(0, 1));
        codeCompletionWidget->SetFilterText(QStringLiteral(""));
      }
    } else if (closeCompletion) {
      CloseCodeCompletion();
    } else {
      if (codeCompletionWidget) {
        codeCompletionWidget->SetFilterText(filterText);
        
        // If there is only one good match which matches filterText exactly,
        // then automatically close the completion widget, unless the insertion
        // text of the match is different from its filter text (this is for
        // example the case for functions which insert placeholders for their
        // parameters).
        if (codeCompletionWidget->HasSingleExactMatch()) {
          CloseCodeCompletion();
        }
      }
    }
  }
}

void DocumentWidget::CloseArgumentHint() {
  delete argumentHintWidget;
  argumentHintWidget = nullptr;
}

void DocumentWidget::ShowArgumentHint(DocumentLocation invocationLocation, std::vector<ArgumentHintItem>&& items, int currentParameter) {
  if (argumentHintInvocationLocation.IsInvalid()) {
    qDebug() << "ShowArgumentHint(): argumentHintInvocationLocation is invalid";
    return;
  }
  
  // Close the widget in case it is open.
  delete argumentHintWidget;
  
  // Open the new widget.
  argumentInvocationCurrentParameter = currentParameter;
  
  QPoint invocationPoint = GetTextRect(DocumentRange(invocationLocation, invocationLocation)).topLeft();
  argumentHintWidget = new ArgumentHintWidget(currentParameter, std::move(items), invocationPoint, this);
  
  // Potentially update the current parameter index or close the widget if the
  // cursor moved away since invocation
  DocumentLocation cursorLoc = MapCursorToDocument();
  if (cursorLoc < argumentHintInvocationLocation) {
    UpdateArgumentHintWidget(DocumentRange(cursorLoc, argumentHintInvocationLocation), false);
  } else {
    UpdateArgumentHintWidget(DocumentRange(argumentHintInvocationLocation, cursorLoc), true);
  }
  
  if (argumentHintWidget) {  // UpdateArgumentHintWidget() may delete the widget
    argumentHintWidget->Relayout();
    argumentHintWidget->show();
  }
}

void DocumentWidget::GotoReferencedCursor(DocumentLocation invocationLocation) {
  CodeInfo::Instance().GotoReferencedCursor(this, invocationLocation);
}

void DocumentWidget::SetCodeTooltip(const DocumentRange& tooltipRange, const QString& codeHtml, const QUrl& helpUrl, const std::vector<DocumentRange>& referenceRanges) {
  // Update the tooltip.
  if (showCodeInfoInExistingWidget) {
    if (tooltipCodeHtmlLabel) {
      tooltipCodeHtmlLabel->setText(codeHtml);
      tooltipCodeHtmlLabel->adjustSize();
    }
    if (tooltipHelpBrowser) {
      if (helpUrl.isValid()) {
        tooltipHelpBrowser->setSource(helpUrl);
      }
      tooltipHelpFrame->setVisible(helpUrl.isValid());
    }
    ResizeTooltipToContents(false);
    
    showCodeInfoInExistingWidget = false;
  } else if (isVisible()) {
    tooltipCodeRect = GetTextRect(tooltipRange);
    QPoint cursorPos = mapFromGlobal(QCursor::pos());
    if (!tooltipCodeRect.contains(cursorPos)) {
      tooltipCodeRect = QRect();
      return;
    }
    std::vector<std::shared_ptr<Problem>> hoveredProblems = GetHoveredProblems();
    UpdateTooltip(cursorPos, &hoveredProblems, &codeHtml, &helpUrl, nullptr);
  }
  
  // Update the reference highlighting.
  if (!codeHtml.isEmpty() && selection.IsEmpty()) {
    RemoveHighlights();
    const auto& referenceHighlightStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ReferenceHighlight);
    for (const DocumentRange& range : referenceRanges) {
      document->AddHighlightRange(range, false, referenceHighlightStyle, /*layer*/ kHighlightLayer);
    }
    document->FinishedHighlightingChanges();
    update(rect());
  }
}

void DocumentWidget::HandleMouseMoveEvent() {
  if (!haveMouseMoveEvent) {
    return;
  }
  haveMouseMoveEvent = false;
  
  if (!(lastMouseMoveEventButtons & Qt::LeftButton)) {
    // Use the pointing-hand cursor when hovering over a fix-it button
    bool cursorOverButton = false;
    for (int buttonIndex = 0; buttonIndex < fixitButtons.size(); ++ buttonIndex) {
      const FixitButton& button = fixitButtons[buttonIndex];
      if (button.buttonRect.contains(lastMouseMoveEventPos)) {
        cursorOverButton = true;
      }
    }
    setCursor(cursorOverButton ? Qt::PointingHandCursor : ((lastMouseMoveEventPos.x() < sidebarWidth) ? Qt::ArrowCursor : Qt::IBeamCursor));
    
    if (lastMouseMoveEventPos.x() < sidebarWidth) {
      // Check for hovering over line diffs
      const LineDiff* hoveredDiff = nullptr;
      
      int line = (yScroll + lastMouseMoveEventPos.y()) / lineHeight;
      
      for (const LineDiff& diff : document->diffLines()) {
        if (diff.type != LineDiff::Type::Removed &&
            diff.line <= line &&
            diff.line + diff.numLines > line) {
          hoveredDiff = &diff;
        }
      }
      
      for (const LineDiff& diff : document->diffLines()) {
        if (diff.type == LineDiff::Type::Removed &&
            abs(lastMouseMoveEventPos.y() - (diff.line * lineHeight - yScroll)) <= std::min(sidebarWidth, lineHeight)) {
          hoveredDiff = &diff;
          break;
        }
      }
      
      std::vector<std::shared_ptr<Problem>> noHoveredProblems;
      UpdateTooltip(lastMouseMoveEventPos, &noHoveredProblems, nullptr, nullptr, hoveredDiff);
    } else {
      if (!cursorOverButton) {
        auto hoveredProblems = GetHoveredProblems();
        UpdateTooltip(lastMouseMoveEventPos, hoveredProblems.empty() ? &hoveredProblems : nullptr, nullptr, nullptr, nullptr);
        
        // If hovering over a word and code info has not been requested for that
        // word yet, request code info for it.
        if (isCFile &&
            !tooltipCodeRect.contains(lastMouseMoveEventPos) &&
            !codeInfoRequestRect.contains(lastMouseMoveEventPos)) {
          constexpr int kHoverDelayMilliseconds = 120;
          mouseHoverTimer.start(kHoverDelayMilliseconds);
          mouseHoverPosGlobal = lastMouseMoveEventGlobalPos;
          mouseHoverPosLocal = lastMouseMoveEventPos;
        }
      }
    }
  }
  
  // Handle selection by dragging
  if (lastMouseMoveEventButtons & Qt::LeftButton) {
    CloseTooltip();
    
    if (selectionDoubleClickOffset == -1) {
      // No double click has been done, perform normal selection
      SetCursor(lastMouseMoveEventPos.x(), lastMouseMoveEventPos.y(), true);
    } else {
      // A double click has been done, perform word-wise selection
      DocumentRange initialWordRange = GetWordForCharacter(selectionDoubleClickOffset);
      
      int line, character;
      bool wasClamped;
      GetCharacterAt(lastMouseMoveEventPos.x(), lastMouseMoveEventPos.y(), true, &line, &character, &wasClamped);
      int offset = layoutLines[line].start.offset + character;
      QString lineText = document->TextForRange(layoutLines[line]);
      if (wasClamped || character >= lineText.size()) {
        // If the character got clamped to the last character in the line, move it just beyond the last character
        // (this is relevant for non-empty lines only).
        offset += lineText.size() - character;
        DocumentLocation otherLocation = DocumentLocation(offset);
        initialWordRange.Add(otherLocation);
        SetSelection(initialWordRange);
      } else {
        DocumentRange otherWordRange = GetWordForCharacter(offset);
        initialWordRange.Add(otherWordRange);
        SetSelection(initialWordRange);
      }
      
      StartMovingCursor();
      if (offset < selectionDoubleClickOffset) {
        // Set the cursor to the start of the selection and preSelectionCursor to the end.
        preSelectionCursor = selection.end;
        SetCursorTo(selection.start);
      } else {
        // Set the cursor to the end of the selection and preSelectionCursor to the start.
        preSelectionCursor = selection.start;
        SetCursorTo(selection.end);
      }
      EndMovingCursor(false, true);
    }
  }
}

void DocumentWidget::ShowRightClickMenu(const QString& clickedCursorUSR, const QString& clickedCursorSpelling, bool cursorHasLocalDefinition, const QString& clickedTokenSpelling, const DocumentRange& clickedTokenRange) {
  if (!isVisible() || clickedCursorSpelling.isEmpty() || clickedCursorUSR.isEmpty()) {
    return;
  }
  
  rightClickedCursorUSR = clickedCursorUSR;
  rightClickedCursorSpelling = clickedCursorSpelling;  // TODO: Currently unused, may delete
  rightClickedCursorHasLocalDefinition = cursorHasLocalDefinition;
  rightClickedTokenSpelling = clickedTokenSpelling;
  rightClickedTokenRange = clickedTokenRange;
  renameClickedItemAction->setText(tr("Rename / find uses of \"%1\"").arg(clickedCursorSpelling));
  
  if (renameRequested) {
    QTimer::singleShot(0, this, &DocumentWidget::RenameClickedItem);
    renameRequested = false;
  } else {
    rightClickMenu->popup(mapToGlobal(lastRightClickPoint));
  }
}

void DocumentWidget::RenameClickedItem() {
  // Initialize the cursor / selection in the rename dialog to match the cursor / selection
  // in this DocumentWidget.
  DocumentRange initialCursorOrSelectionRange = DocumentRange(0, rightClickedTokenSpelling.size());
  if (selection.IsEmpty()) {
    DocumentLocation cursorLoc = MapCursorToDocument();
    int cursorOffsetWithinToken = std::max(0, std::min(rightClickedTokenSpelling.size(), cursorLoc.offset - rightClickedTokenRange.start.offset));
    initialCursorOrSelectionRange = DocumentRange(cursorOffsetWithinToken, cursorOffsetWithinToken);
  } else {
    initialCursorOrSelectionRange = DocumentRange(
        std::max(0, std::min(rightClickedTokenSpelling.size(), selection.start.offset - rightClickedTokenRange.start.offset)),
        std::max(0, std::min(rightClickedTokenSpelling.size(), selection.end.offset - rightClickedTokenRange.start.offset)));
  }
  
  RenameDialog dialog(this, rightClickedCursorUSR, rightClickedTokenSpelling, rightClickedCursorHasLocalDefinition, initialCursorOrSelectionRange, mainWindow);
  dialog.exec();
}

void DocumentWidget::RenameItemAtCursor() {
  // TODO: Rename "RequestRightClickInfo"?
  renameRequested = true;
  CodeInfo::Instance().RequestRightClickInfo(this, MapCursorToDocument());
}

void DocumentWidget::SelectAll() {
  DocumentRange fullDocumentRange = document->FullDocumentRange();
  if (fullDocumentRange.size() == 0) {
    SetSelection(DocumentRange::Invalid());
  } else {
    SetSelection(fullDocumentRange);
  }
}

void DocumentWidget::Undo() {
  DocumentRange newTextRange;
  if (document->Undo(&newTextRange)) {
    // Set the selection to the end of the inserted text (if any)
    SetCursor(newTextRange.end, false);
    
    update(rect());  // TODO: More localized update?
  }
}

void DocumentWidget::Redo() {
  DocumentRange newTextRange;
  if (document->Redo(&newTextRange)) {
    // Set the selection to the end of the inserted text (if any)
    SetCursor(newTextRange.end, false);
    
    update(rect());  // TODO: More localized update?
  }
}

void DocumentWidget::Cut() {
  QString selectedText = GetSelectedText();
  if (selectedText.isEmpty()) {
    return;
  }
  
  QClipboard* clipboard = QGuiApplication::clipboard();
  clipboard->setText(selectedText, QClipboard::Clipboard);
  
  InsertText("");
}

void DocumentWidget::Copy() {
  QString selectedText = GetSelectedText();
  if (selectedText.isEmpty()) {
    return;
  }
  
  QClipboard* clipboard = QGuiApplication::clipboard();
  clipboard->setText(selectedText, QClipboard::Clipboard);
}

void DocumentWidget::Paste() {
  QClipboard* clipboard = QGuiApplication::clipboard();
  InsertText(clipboard->text(QClipboard::Clipboard));
}

void DocumentWidget::ToggleBookmark() {
  int lineAttributes = document->lineAttributes(cursorLine);
  if (lineAttributes & static_cast<int>(LineAttribute::Bookmark)) {
    document->SetLineAttributes(cursorLine, lineAttributes & ~static_cast<int>(LineAttribute::Bookmark));
  } else {
    document->SetLineAttributes(cursorLine, lineAttributes | static_cast<int>(LineAttribute::Bookmark));
  }
  update(GetLineRect(cursorLine));
  BookmarksChanged();
}

void DocumentWidget::JumpToPreviousBookmark() {
  for (int line = cursorLine - 1; line >= 0; -- line) {
    if (document->lineAttributes(line) & static_cast<int>(LineAttribute::Bookmark)) {
      StartMovingCursor();
      cursorLine = line;
      EndMovingCursor(false);
      break;
    }
  }
}

void DocumentWidget::JumpToNextBookmark() {
  for (int line = cursorLine + 1, end = document->LineCount(); line < end; ++ line) {
    if (document->lineAttributes(line) & static_cast<int>(LineAttribute::Bookmark)) {
      StartMovingCursor();
      cursorLine = line;
      EndMovingCursor(false);
      break;
    }
  }
}

void DocumentWidget::RemoveAllBookmarks() {
  Document::LineIterator lineIt(document.get());
  while (lineIt.IsValid()) {
    lineIt.SetAttributes(lineIt.GetAttributes() & ~(static_cast<int>(LineAttribute::Bookmark)));
    ++ lineIt;
  }
  update(rect());
  BookmarksChanged();
}

void DocumentWidget::Comment() {
  document->StartUndoStep();
  
  if (selection.IsEmpty()) {
    // Comment out the current line
    DocumentRange lineRange = document->GetRangeForLine(cursorLine);
    DocumentLocation location = lineRange.start;
    
    Document::CharacterIterator it(document.get(), lineRange.start.offset);
    DocumentLocation cursorLocation = MapCursorToDocument();
    while (it.IsValid() && it.GetCharacterOffset() <= cursorLocation.offset) {
      QChar c = it.GetChar();
      if (c == '\n') {
        break;
      } else if (!IsWhitespace(c)) {
        location = it.GetCharacterOffset();
        break;
      }
      ++ it;
    }
    
    document->Replace(DocumentRange(location, location), QStringLiteral("// "));
    SetCursor(cursorLocation + 3, false);
  } else {
    // Decide whether to comment a range of lines with "// ", or whether to
    // make an inline/multiline comment with / * * / (without the spaces).
    
    // Check whether the selection start is at a line start or within the
    // leading whitespace portion of a line.
    int startLine, startCol;
    MapDocumentToLayout(selection.start, &startLine, &startCol);
    DocumentRange startLineRange = document->GetRangeForLine(startLine);
    bool startingAtLineStart = startLineRange.start == selection.start;
    if (!startingAtLineStart) {
      startingAtLineStart = true;
      QString text = document->TextForRange(DocumentRange(startLineRange.start, selection.start));
      for (QChar c : text) {
        if (!IsWhitespace(c)) {
          startingAtLineStart = false;
          break;
        }
      }
    }
    
    // Check whether the selection end is at a line end (or at the start of the following line).
    int endLine, endCol;
    MapDocumentToLayout(selection.end, &endLine, &endCol);
    DocumentRange endLineRange = document->GetRangeForLine(endLine);
    bool endingAtLineEnd = endLineRange.end == selection.end || endLineRange.start == selection.end;
    if (endLineRange.start == selection.end) {
      -- endLine;
    }
    
    if (startingAtLineStart && endingAtLineEnd) {
      // Comment out a line or multiple lines with "// ".
      // Find the largest indent that each line has as a minimum.
      int maxCommonIndent = std::numeric_limits<int>::max();
      for (int line = startLine; line <= endLine; ++ line) {
        DocumentRange lineRange = document->GetRangeForLine(line);
        Document::CharacterIterator it(document.get(), lineRange.start.offset);
        int indent = 0;
        while (it.IsValid() && it.GetChar() != '\n' && IsWhitespace(it.GetChar())) {
          ++ indent;
          ++ it;
        }
        maxCommonIndent = std::min(maxCommonIndent, indent);
      }
      
      // Insert the comments.
      for (int line = endLine; line >= startLine; -- line) {
        DocumentRange lineRange = document->GetRangeForLine(line);
        document->Replace(DocumentRange(lineRange.start + maxCommonIndent, lineRange.start + maxCommonIndent), QStringLiteral("// "));
      }
      
      // Update the selection
      DocumentRange startLineRange = document->GetRangeForLine(startLine);
      DocumentRange endLineRange = document->GetRangeForLine(endLine);
      SetSelection(DocumentRange(startLineRange.start, endLineRange.end));
    } else {
      // Insert an inline/multiline comment.
      document->Replace(DocumentRange(selection.end, selection.end), QStringLiteral("*/"));
      document->Replace(DocumentRange(selection.start, selection.start), QStringLiteral("/*"));
      SetSelection(DocumentRange(selection.start, selection.end + 4));
    }
  }
  
  document->EndUndoStep();
  update(rect());  // TODO: limit update
}

void DocumentWidget::Uncomment() {
  document->StartUndoStep();
  
  if (selection.IsEmpty()) {
    // Un-comment the current line.
    UncommentLine(cursorLine);
  } else {
    // Check for an inline/multiline comment within the selection.
    QString selectionText = document->TextForRange(selection);
    if (selectionText.startsWith("/*") && selectionText.endsWith("*/")) {
      document->Replace(DocumentRange(selection.end - 2, selection.end), QStringLiteral(""));
      document->Replace(DocumentRange(selection.start, selection.start + 2), QStringLiteral(""));
      SetSelection(DocumentRange(selection.start, selection.end - 4));
      document->EndUndoStep();
      update(rect());  // TODO: limit update
      return;
    }
    
    // Check for an inline/multiline comment surrounding the selection.
    if (selection.start >= 2 && selection.end <= document->FullDocumentRange().end - 2) {
      QString surroundingText = document->TextForRange(DocumentRange(selection.start - 2, selection.end + 2));
      if (surroundingText.startsWith("/*") && surroundingText.endsWith("*/")) {
        document->Replace(DocumentRange(selection.end, selection.end + 2), QStringLiteral(""));
        document->Replace(DocumentRange(selection.start - 2, selection.start), QStringLiteral(""));
        SetSelection(DocumentRange(selection.start - 2, selection.end - 2));
        document->EndUndoStep();
        update(rect());  // TODO: limit update
        return;
      }
    }
    
    // If no inline/multiline comment was found, check for comments at the line starts.
    int startLine, startCol;
    MapDocumentToLayout(selection.start, &startLine, &startCol);
    
    int endLine, endCol;
    MapDocumentToLayout(selection.end, &endLine, &endCol);
    
    for (int line = endLine; line >= startLine; -- line) {
      UncommentLine(line);
    }
    
    // Update the selection
    DocumentRange startLineRange = document->GetRangeForLine(startLine);
    DocumentRange endLineRange = document->GetRangeForLine(endLine);
    SetSelection(DocumentRange(startLineRange.start, endLineRange.end));
  }
  
  document->EndUndoStep();
  update(rect());  // TODO: limit update
}

void DocumentWidget::UncommentLine(int line) {
  DocumentRange lineRange = document->GetRangeForLine(line);
  Document::CharacterIterator it(document.get(), lineRange.start.offset);
  DocumentLocation location;
  int charactersToRemove = 0;
  while (it.IsValid()) {
    QChar c = it.GetChar();
    if (IsWhitespace(c)) {
      // skip character
      if (charactersToRemove > 0) {
        // Abort
        return;
      }
    } else if (c == '/') {
      ++ charactersToRemove;
      if (charactersToRemove == 1) {
        location = it.GetCharacterOffset();
      } else if (charactersToRemove == 2) {
        ++ it;
        break;
      }
    } else {
      // Abort
      return;
    }
    ++ it;
    if (it.GetCharacterOffset() >= lineRange.end.offset) {
      return;
    }
  }
  
  // Remove Doxygen-style comments
  if (it.IsValid() && it.GetChar() == '/') {
    ++ charactersToRemove;
    ++ it;
  }
  
  // Remove a single space after comments
  if (it.IsValid() && it.GetChar() == ' ') {
    ++ charactersToRemove;
    ++ it;
  }
  
  DocumentLocation cursorLocation = MapCursorToDocument();
  document->Replace(DocumentRange(location, location + charactersToRemove), QStringLiteral(""));
  if (cursorLocation > location + charactersToRemove) {
    SetCursor(cursorLocation - charactersToRemove, false);
  } else if (cursorLocation > location) {
    SetCursor(location, false);
  }
}

void DocumentWidget::FixAll() {
  if (fixitButtonsDocumentVersion != document->version()) {
    // TODO: Support this by adapting the ranges
    return;
  }
  
  // Find all problems with a single fix-it that are currently visible in the editor
  std::set<std::shared_ptr<Problem>> uniqueTrivialProblems;
  
  for (const FixitButton& button : fixitButtons) {
    if (button.problem->fixits().size() == 1) {
      uniqueTrivialProblems.insert(button.problem);
    }
  }
  
  // Sort the problems by the (start of the) range of their fix-it
  std::vector<std::shared_ptr<Problem>> sortedProblems(uniqueTrivialProblems.size());
  int index = 0;
  for (const std::shared_ptr<Problem> problem : uniqueTrivialProblems) {
    sortedProblems[index] = problem;
    ++ index;
  }
  std::sort(sortedProblems.begin(), sortedProblems.end(), [](const std::shared_ptr<Problem>& a, const std::shared_ptr<Problem>& b) {
    return a->fixits()[0].range.start < b->fixits()[0].range.start;
  });
  
  // Apply back to front such that applying a fix-it does not change the range
  // of the next fix-its to apply.
  for (int i = static_cast<int>(sortedProblems.size()) - 1; i >= 0; -- i) {
    ApplyFixIt(sortedProblems[i], 0, true);
  }
}

void DocumentWidget::CheckFileType() {
  if (GuessIsCFile(document->path())) {
    if (!isCFile) {
      parseTimer->start(0);
    }
    
    isCFile = true;
  } else {
    if (isCFile) {
      document->ClearHighlightRanges(0);
    }
    
    isCFile = false;
  }
}

void DocumentWidget::StartParseTimer() {
  constexpr int parseDelay = 0;
  parseTimer->start(parseDelay);
}

void DocumentWidget::ParseFile() {
  if (isCFile) {
    ParseThreadPool::Instance().RequestParse(document, this, mainWindow);
  }
  
  // TODO: Rename function? ParseFile() does not fit anymore.
  GitDiff::Instance().RequestDiff(document, this, mainWindow);
}

void DocumentWidget::SetReparseOnNextActivation() {
  reparseOnNextActivation = true;
}

void DocumentWidget::InvokeCodeCompletion() {
  ++ codeCompletionInvocationCounter;
  codeCompletionInvocationLocation = CodeInfo::Instance().RequestCodeCompletion(this);
  argumentHintInvocationLocation = codeCompletionInvocationLocation;
}

void DocumentWidget::AcceptCodeCompletion() {
  if (!codeCompletionWidget) {
    return;
  }
  
  codeCompletionWidget->Accept(this, codeCompletionInvocationLocation);
  CloseCodeCompletion();
}

void DocumentWidget::CheckPhraseHighlight() {
  if (selection.IsValid() && selection.start < selection.end) {
    Document::CharacterIterator it(document.get(), selection.start.offset);
    int wordCharType = GetCharType(it.GetChar());
    -- it;
    if (!it.IsValid() || GetCharType(it.GetChar()) != wordCharType || wordCharType == static_cast<int>(CharacterType::Symbol)) {
      Document::CharacterIterator it2(document.get(), selection.end.offset - 1);
      int wordEndCharType = GetCharType(it2.GetChar());
      ++ it2;
      if (!it2.IsValid() || GetCharType(it2.GetChar()) != wordEndCharType || wordEndCharType == static_cast<int>(CharacterType::Symbol)) {
        // A word or phrase is selected. Request highlighting for it.
        RequestPhraseHighlight(document->TextForRange(selection));
      }
    }
  }
}

void DocumentWidget::RequestPhraseHighlight(const QString& phrase) {
  // Find occurrences
  // TODO: Do this in a background thread like the clang parse threads?
  std::vector<DocumentRange> occurrences;
  occurrences.reserve(128);
  
  DocumentLocation findStart = document->FullDocumentRange().end;
  while (true) {
    DocumentLocation result = document->Find(phrase, findStart, false, true);
    if (result.IsInvalid()) {
      break;
    }
    
    DocumentRange occurrence(result, result + phrase.size());
    
    // Is this a word/phrase occurrence, or part of another word? Only highlight
    // it in the former case.
    Document::CharacterIterator it(document.get(), occurrence.start.offset);
    int wordCharType = GetCharType(it.GetChar());
    -- it;
    if (!it.IsValid() || GetCharType(it.GetChar()) != wordCharType || wordCharType == static_cast<int>(CharacterType::Symbol)) {
      Document::CharacterIterator it2(document.get(), occurrence.end.offset - 1);
      int wordEndCharType = GetCharType(it2.GetChar());
      ++ it2;
      if (!it2.IsValid() || GetCharType(it2.GetChar()) != wordEndCharType || wordEndCharType == static_cast<int>(CharacterType::Symbol)) {
        occurrences.emplace_back(occurrence);
      }
    }
    
    findStart = result;
  }
  
  // Add highlight ranges.
  // We should always find at least one occurrence, which is the actual selection.
  // Highlighting this will not be visible because the selection is drawn on top.
  // So, we only add highlights if we have at least two occurrences.
  if (occurrences.empty()) {
    qDebug() << "Warning: RequestPhraseHighlight() found no occurrence, this is not supposed to happen since the selected occurrence should always be found.";
    return;
  } else if (occurrences.size() <= 1) {
    return;
  } else {
    const auto& copyHighlightStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::CopyHighlight);
    for (const DocumentRange& range : occurrences) {
      document->AddHighlightRange(range, false, copyHighlightStyle, /*layer*/ kHighlightLayer);
    }
    document->FinishedHighlightingChanges();
    update(rect());
  }
}

void DocumentWidget::RemoveHighlights() {
  if (document->GetHighlightRanges(kHighlightLayer).size() > 1) {
    document->ClearHighlightRanges(kHighlightLayer);
    document->FinishedHighlightingChanges();
    update(rect());
  }
}

void DocumentWidget::CheckBracketHighlight() {
  if (selection.IsValid() && selection.start != selection.end) {
    return;
  }
  
  DocumentLocation cursorLoc = MapCursorToDocument();
  Document::CharacterAndStyleIterator it(document.get(), cursorLoc.offset);
  
  // Checks for a bracket to the right of pos
  auto checkBracketHighlight = [&](const Document::CharacterAndStyleIterator& pos, const Settings::TextStyle highlightStyleType) {
    if (pos.GetStyleOfLayer(0).isNonCodeRange) {
      // Do not try to match brackets within string literals or comments
      return;
    }
    int matchOffset = document->FindMatchingBracket(pos);
    if (matchOffset != -1) {
      int posOffset = pos.GetCharacterOffset();
      const auto& highlightStyle = Settings::Instance().GetConfiguredTextStyle(highlightStyleType);
      document->AddHighlightRange(DocumentRange(posOffset, posOffset + 1), false, highlightStyle, /*layer*/ kHighlightLayer);
      document->AddHighlightRange(DocumentRange(matchOffset, matchOffset + 1), false, highlightStyle, /*layer*/ kHighlightLayer);
      update(rect());
    }
  };
  
  // Check for a bracket to the right of the cursor
  if (it.IsValid()) {
    checkBracketHighlight(it, Settings::TextStyle::RightBracketHighlight);
    -- it;
  } else {
    it = Document::CharacterAndStyleIterator(document.get(), cursorLoc.offset - 1);
  }
  
  // Check for a bracket to the left of the cursor
  if (it.IsValid()) {
    checkBracketHighlight(it, Settings::TextStyle::LeftBracketHighlight);
  }
}

void DocumentWidget::HighlightingChanged() {
  // Perform a map update if a relayout check does not do that anyway.
  if (!CheckRelayout()) {
    container->GetMinimap()->UpdateMap(layoutLines, nullptr);
  }
}

void DocumentWidget::MoveCursorLeft(bool shiftHeld, bool controlHeld) {
  StartMovingCursor();
  
  DocumentLocation loc = MapCursorToDocument();
  if (loc.offset > 0) {
    -- loc;
    
    if (controlHeld) {
      // Continue jumping over whitespace / text characters until a different character or the line end is encountered.
      Document::CharacterIterator it(document.get(), loc.offset);
      if (it.GetChar() == QChar('\n')) {
        -- it;
      }
      if (it.IsValid()) {
        int jumpOverType = GetCharType(it.GetChar());
        -- it;
        while (it.IsValid()) {
          if (GetCharType(it.GetChar()) != jumpOverType) {
            break;
          }
          -- it;
        }
        loc = it.GetCharacterOffset() + 1;
      }
    }
    
    SetCursorTo(loc);
  }
  
  EndMovingCursor(shiftHeld);
}

void DocumentWidget::MoveCursorRight(bool shiftHeld, bool controlHeld) {
  StartMovingCursor();
  
  DocumentLocation loc = MapCursorToDocument();
  if (loc < document->FullDocumentRange().end) {
    ++ loc;
    
    if (controlHeld) {
      // Continue jumping over whitespace / text characters until a different character or the line end is encountered.
      Document::CharacterIterator it(document.get(), loc.offset - 1);
      if (it.GetChar() == QChar('\n')) {
        ++ it;
      }
      if (it.IsValid()) {
        int jumpOverType = GetCharType(it.GetChar());
        ++ it;
        while (it.IsValid()) {
          if (GetCharType(it.GetChar()) != jumpOverType) {
            break;
          }
          ++ it;
        }
        loc = it.GetCharacterOffset();
      }
    }
    
    SetCursorTo(loc);
  }
  
  EndMovingCursor(shiftHeld);
}

void DocumentWidget::MoveCursorUpDown(int direction, bool shiftHeld) {
  StartMovingCursor();
  
  // Map the cursor to the text
  cursorLine = std::min(static_cast<int>(layoutLines.size()) - 1, cursorLine);
  const DocumentRange& lineRange = layoutLines[cursorLine];
  int col = std::min(cursorCol, lineRange.size());
  
  // Get the x-coordinate of the cursor
  int cursorX = GetTextWidth(document->TextForRange(lineRange).left(col), 0, nullptr);
  
  // If the cursor was to to the right of the text, append this width
  cursorX += charWidth * (cursorCol - col);
  
  // Go to the line above / below
  if (direction < 0) {
    -- cursorLine;
    cursorLine = std::max(0, cursorLine);
  } else if (direction > 0) {
    ++ cursorLine;
    cursorLine = std::min(static_cast<int>(layoutLines.size()) - 1, cursorLine);
  } else {
    qDebug() << "ERROR: MoveCursorUpDown(): direction must not be zero.";
  }
  
  // Find the location that best matches the x-coordinate
  QString lineText = document->TextForRange(layoutLines[cursorLine]);
  int xCoord = 0;
  int column = 0;
  for (int c = 0; c < lineText.size(); ++ c) {
    int charColumns;
    int charWidth = GetTextWidth(lineText.at(c), column, &charColumns);
    column += charColumns;
    if (cursorX < xCoord + charWidth / 2) {
      cursorCol = c;
      EndMovingCursor(shiftHeld);
      return;
    }
    xCoord += charWidth;
  }
  
  cursorCol = lineText.size() + (cursorX - xCoord + charWidth / 2) / charWidth;
  
  EndMovingCursor(shiftHeld);
}

void DocumentWidget::BlinkCursor() {
  cursorBlinkState = !cursorBlinkState;
  update(GetCursorRect());
}

void DocumentWidget::SetXYScroll(int x, int y) {
  if (xScroll == x && yScroll == y) {
    return;
  }
  xScroll = x;
  yScroll = y;
  update(rect());
  
  container->GetScrollbar()->setValue(x);
  
  if (codeCompletionWidget) {
    codeCompletionWidget->SetInvocationPoint(GetTextRect(DocumentRange(codeCompletionInvocationLocation, codeCompletionInvocationLocation)).bottomLeft() + QPoint(0, 1));
    codeCompletionWidget->Relayout();
  }
  if (argumentHintWidget) {
    argumentHintWidget->SetInvocationPoint(GetTextRect(DocumentRange(argumentHintInvocationLocation, argumentHintInvocationLocation)).topLeft());
    argumentHintWidget->Relayout();
  }
  container->GetMinimap()->update(rect());
}

void DocumentWidget::SetXScroll(int value) {
  if (xScroll == value) {
    return;
  }
  xScroll = value;
  update(rect());
  
  container->GetScrollbar()->setValue(value);
  
  if (codeCompletionWidget) {
    codeCompletionWidget->SetInvocationPoint(GetTextRect(DocumentRange(codeCompletionInvocationLocation, codeCompletionInvocationLocation)).bottomLeft() + QPoint(0, 1));
    codeCompletionWidget->Relayout();
  }
  if (argumentHintWidget) {
    argumentHintWidget->SetInvocationPoint(GetTextRect(DocumentRange(argumentHintInvocationLocation, argumentHintInvocationLocation)).topLeft());
    argumentHintWidget->Relayout();
  }
}

void DocumentWidget::SetYScroll(int value) {
  if (yScroll == value) {
    return;
  }
  yScroll = value;
  update(rect());
  
  if (codeCompletionWidget) {
    codeCompletionWidget->SetInvocationPoint(GetTextRect(DocumentRange(codeCompletionInvocationLocation, codeCompletionInvocationLocation)).bottomLeft() + QPoint(0, 1));
    codeCompletionWidget->Relayout();
  }
  if (argumentHintWidget) {
    argumentHintWidget->SetInvocationPoint(GetTextRect(DocumentRange(argumentHintInvocationLocation, argumentHintInvocationLocation)).topLeft());
    argumentHintWidget->Relayout();
  }
  container->GetMinimap()->update(rect());
}

void DocumentWidget::ShowDocumentationInDock() {
  if (!tooltipHelpBrowser) {
    return;
  }
  
  mainWindow->ShowDocumentationDock(tooltipHelpBrowser->GetCurrentUrl());
}

void DocumentWidget::Moved() {
  if (codeCompletionWidget) {
    codeCompletionWidget->Relayout();
  }
  if (argumentHintWidget) {
    argumentHintWidget->Relayout();
  }
}

void DocumentWidget::FontChanged() {
  fontMetrics.reset(new QFontMetrics(Settings::Instance().GetDefaultFont()));
  lineHeight = fontMetrics->ascent() + fontMetrics->descent();
  charWidth = fontMetrics->/*horizontalAdvance*/ width(' ');
}

bool DocumentWidget::CheckRelayout() {
  if (haveLayout &&
      layoutVersion == document->version() /*&&
      (!wordWrap || width() == layoutWidth)*/) {
    return false;
  }
  haveLayout = true;
  layoutVersion = document->version();
  
  // Compute layoutLines and maximum x extent
  // TODO: It seems like a waste to re-compute these completely after every change
  //       to the document. Should those instead be kept as a part of Document and be
  //       adapted iteratively to changes?
  maxTextWidth = 0;
  layoutLines.clear();
  layoutLines.reserve(document->LineCount());
  Document::LineIterator it(document.get());
  while (it.IsValid()) {
    DocumentRange range = it.GetLineRange();
    maxTextWidth = std::max(maxTextWidth, GetTextWidth(document->TextForRange(range), 0, nullptr));  // TODO: Implement GetText() in LineIterator to get it faster (already knowing the start block)
    layoutLines.push_back(range);
    ++ it;
  }
  
  // Update the x-scroll range
  UpdateScrollbar();
  
  // Copy the document for both the scrollbar minimap update and a possible
  // backup
  std::shared_ptr<Document> documentCopy(new Document());
  documentCopy->AssignTextAndStyles(*document);
  
  container->GetMinimap()->UpdateMap(layoutLines, documentCopy);
  
  if (document->HasUnsavedChanges()) {
    CrashBackup::Instance().MakeBackup(document->path(), documentCopy);
  } else {
    CrashBackup::Instance().RemoveBackup(document->path());
  }
  
  return true;
}

QRect DocumentWidget::GetCursorRect() {
  constexpr int kCursorExtent = 1;
  
  DocumentRange lineRange = layoutLines[std::min(static_cast<int>(layoutLines.size()) - 1, cursorLine)];
  int actualCursorCol = std::min(lineRange.size(), cursorCol);
  
  int cursorMinY = cursorLine * lineHeight - yScroll;
  int cursorMaxY = cursorMinY + lineHeight - 1;
  DocumentRange leftLineRange(lineRange.start, lineRange.start + actualCursorCol);
  int leftTextWidth = GetTextWidth(document->TextForRange(leftLineRange), 0, nullptr);
  int cursorMinX = leftTextWidth - xScroll;
  int cursorMaxX = cursorMinX + kCursorExtent;
  return QRect(sidebarWidth + cursorMinX, cursorMinY, cursorMaxX - cursorMinX + 1, cursorMaxY - cursorMinY + 1);
}

void DocumentWidget::ResetCursorBlinkPhase() {
  cursorBlinkState = true;
  cursorBlinkTimer->start(cursorBlinkInterval);
}

QRect DocumentWidget::GetTextRect(const DocumentRange& range) {
  if (range.IsInvalid()) {
    return QRect();
  }
  
  // TODO: binary search
  int startLine = -1;
  int startCol = -1;
  int endLine = -1;
  int endCol = -1;
  int found = 0;
  for (int line = 0, size = layoutLines.size(); line < size; ++ line) {
    const DocumentRange& lineRange = layoutLines[line];
    if (range.start >= lineRange.start && range.start <= lineRange.end) {
      startLine = line;
      startCol = range.start.offset - lineRange.start.offset;
      ++ found;
    }
    if (range.end >= lineRange.start && range.end <= lineRange.end) {
      endLine = line;
      endCol = range.end.offset - lineRange.start.offset;
      ++ found;
      break;
    }
  }
  
  if (found < 2) {
    qDebug() << "Error: GetTextRect() did not find the layout lines for the given range (found:" << found << ", range.start:" << range.start.offset << ", range.end:" << range.end.offset << ", last layoutLine end: " << layoutLines.back().end.offset << ")";
    return QRect();
  }
  
  int top = startLine * lineHeight;
  int bottom = (endLine + 1) * lineHeight - 1;
  int left, right;
  if (startLine != endLine) {
    left = 0;
    right = 0;
    for (int line = startLine; line < endLine; ++ line) {
      right = std::max(right, GetTextWidth(document->TextForRange(layoutLines[line]), 0, nullptr));
    }
    right = std::max(right, GetTextWidth(document->TextForRange(layoutLines[endLine]).left(endCol), 0, nullptr));
  } else {
    left = GetTextWidth(document->TextForRange(layoutLines[startLine]).left(startCol), 0, nullptr);
    right = GetTextWidth(document->TextForRange(layoutLines[endLine]).left(endCol), 0, nullptr) - 1;
  }
  
  return QRect(sidebarWidth + left - xScroll, top - yScroll, right - left + 1, bottom - top + 1);
}

QRect DocumentWidget::GetLineRect(int line) {
  int top = line * lineHeight;
  int bottom = (line + 1) * lineHeight - 1;
  return QRect(sidebarWidth, top - yScroll, width(), bottom - top + 1);
}

int DocumentWidget::GetTextWidth(const QString& text, int startColumn, int* numColumns) {
  int width = 0;
  int column = startColumn;
  for (int i = 0, size = text.size(); i < size; ++ i) {
    int thisColumns;
    int thisCharWidth = GetTextWidth(text[i], column, &thisColumns);
    column += thisColumns;
    width += thisCharWidth;
  }
  if (numColumns) {
    *numColumns = column - startColumn;
  }
  return width;
}

int DocumentWidget::GetTextWidth(QChar text, int column, int* numColumns) {
  if (text == '\t') {
    int desiredCharacters = (column / spacesPerTab) * spacesPerTab + spacesPerTab;
    *numColumns = desiredCharacters - column;
    return (*numColumns) * charWidth;
  } else {
    // TODO: Support non-monospace fonts?
    *numColumns = 1;
    return charWidth;
  }
}

bool DocumentWidget::MapDocumentToLayout(const DocumentLocation& location, int* line, int* col) {
  // TODO: binary search
  for (int l = 0, size = layoutLines.size(); l < size; ++ l) {
    const DocumentRange& lineRange = layoutLines[l];
    if (location >= lineRange.start && location <= lineRange.end) {
      *line = l;
      *col = location.offset - lineRange.start.offset;
      return true;
    }
  }
  return false;
}

DocumentLocation DocumentWidget::MapLayoutToDocument(int line, int col) {
  line = std::min(static_cast<int>(layoutLines.size()) - 1, line);
  const DocumentRange& lineRange = layoutLines[line];
  return lineRange.start + std::min(col, lineRange.size());
}

DocumentLocation DocumentWidget::MapCursorToDocument() {
  CheckRelayout();
  return MapLayoutToDocument(cursorLine, cursorCol);
}

DocumentLocation DocumentWidget::MapLineColToDocumentLocation(int line, int col) {
  CheckRelayout();
  return MapLayoutToDocument(line, col);
}

void DocumentWidget::SetCursorTo(const DocumentLocation& location) {
  CheckRelayout();
  
  if (!MapDocumentToLayout(location, &cursorLine, &cursorCol)) {
    qDebug() << "Error: SetCursorTo() did not find the given location in the layout.";
  }
}

void DocumentWidget::StartMovingCursor() {
  if (movingCursor) {
    qFatal("Missing EndMovingCursor!");
  }
  movingCursor = true;
  
  movingCursorOldLocation = MapCursorToDocument();
  movingCursorOldRect = GetCursorRect();
}

void DocumentWidget::EndMovingCursor(bool addToSelection, bool preserveSelection) {
  if (!movingCursor) {
    qFatal("Missing StartMovingCursor!");
  }
  movingCursor = false;
  
  DocumentLocation cursorLoc = MapCursorToDocument();
  
  // Update code completion filter text / close code completion?
  if (codeCompletionInvocationLocation.IsValid()) {
    UpdateCodeCompletion(cursorLoc);
  }
  
  // Update argument hint widget?
  if (argumentHintWidget) {
    if (cursorLoc < argumentHintInvocationLocation) {
      UpdateArgumentHintWidget(DocumentRange(cursorLoc, argumentHintInvocationLocation), false);
    } else {
      UpdateArgumentHintWidget(DocumentRange(argumentHintInvocationLocation, cursorLoc), true);
    }
  }
  
  if (addToSelection || !preserveSelection) {
    RemoveHighlights();
  }
  
  // Handle text selection changes
  if (addToSelection) {
    if (selection.IsInvalid()) {
      preSelectionCursor = movingCursorOldLocation;
    }
    
    DocumentLocation movingCursorNewLocation(cursorLoc);
    QRect updateRect = GetTextRect(selection);
    selection = DocumentRange(
        preSelectionCursor.Min(movingCursorNewLocation),
        preSelectionCursor.Max(movingCursorNewLocation));
    CheckPhraseHighlight();
    updateRect = updateRect.united(GetTextRect(selection));
    updateRect.setRight(width());
    update(updateRect);
  } else if (!preserveSelection) {
    QRect updateRect = GetTextRect(selection);
    updateRect.setRight(width());
    update(updateRect);
    selection = DocumentRange::Invalid();
    preSelectionCursor = cursorLoc;
  }
  
  // Update bracket highlighting (if there is no selection)
  CheckBracketHighlight();
  
  // Determine area to update
  if (Settings::Instance().GetHighlightCurrentLine()) {
    int oldLine;
    int oldCol;
    if (MapDocumentToLayout(movingCursorOldLocation, &oldLine, &oldCol)) {
      movingCursorOldRect = movingCursorOldRect.united(GetLineRect(oldLine));
    }
    update(movingCursorOldRect.united(GetCursorRect()).united(
           GetLineRect(cursorLine)));
  } else {
    update(movingCursorOldRect.united(GetCursorRect()));
  }
  
  ResetCursorBlinkPhase();
  EnsureCursorIsInView();
  emit CursorMoved(cursorLine, std::min(document->TextForRange(layoutLines[cursorLine]).size(), cursorCol));
}

void DocumentWidget::UpdateScrollbar() {
  int availableWidth = std::max(0, width() - sidebarWidth);
  if (maxTextWidth > availableWidth) {
    container->GetScrollbar()->setRange(0, maxTextWidth - availableWidth);
    container->GetScrollbar()->show();
    // updateGeometry() seemed to be necessary to make this behave correctly when
    // double-clicking a window title bar of a maximized window that does not have the scrollbar
    // when maximized, but does get it when getting un-maximized with the double-click.
    // In case of normal resizing, updateGeometry() was not necessary.
    container->GetScrollbar()->updateGeometry();
  } else {
    container->GetScrollbar()->hide();
    SetXScroll(0);
  }
}

void DocumentWidget::ApplyFixIt(const std::shared_ptr<Problem>& problem, int fixitIndex, bool ignoreDocumentVersion) {
  if (!ignoreDocumentVersion &&
      fixitButtonsDocumentVersion != document->version()) {
    // TODO: Try to adapt the fix-it range to the current version of the document
    return;
  }
  Replace(
      problem->fixits()[fixitIndex].range,
      problem->fixits()[fixitIndex].newText);
  document->RemoveProblem(problem);
  update(rect());
}

std::vector<std::shared_ptr<Problem>> DocumentWidget::GetHoveredProblems() {
  const std::set<ProblemRange>& problemRanges = document->problemRanges();
  std::vector<std::shared_ptr<Problem>> hoveredProblems;
  QPoint cursorPos = mapFromGlobal(QCursor::pos());
  
  // NOTE: At the moment, we consider only one line here (the one which contains
  //       event->y()), but this implementation could extend to check problems
  //       from neighboring lines if that seems desirable
  int minLine = (yScroll + cursorPos.y()) / lineHeight;
  int maxLine = std::min<int>(static_cast<int>(layoutLines.size()) - 1, (yScroll + cursorPos.y()) / lineHeight);
  
  for (int line = minLine; line <= maxLine; ++ line) {
    int minCharacterOffset = layoutLines[line].start.offset;
    int maxCharacterOffset = layoutLines[line].start.offset + layoutLines[line].size() - 1;
    
    // Iterate over ranges within this line
    auto problemRangeIt = problemRanges.begin();  // TODO: Use the set to find the first relevant problem range faster
    while (problemRangeIt != problemRanges.end()) {
      if (problemRangeIt->range.start.offset <= maxCharacterOffset &&
          problemRangeIt->range.end.offset >= minCharacterOffset) {
        // This problem range starts before or in this line and has at least
        // some part within the line.
        QRect textRect = GetTextRect(problemRangeIt->range);
        if (textRect.contains(cursorPos)) {
          std::shared_ptr<Problem> hoveredProblem = document->problems()[problemRangeIt->problemIndex];
          bool alreadyThere = false;
          for (const std::shared_ptr<Problem>& problem : hoveredProblems) {
            if (problem.get() == hoveredProblem.get()) {
              alreadyThere = true;
              break;
            }
          }
          if (!alreadyThere) {
            hoveredProblems.push_back(hoveredProblem);
          }
        }
      } else if (problemRangeIt->range.start.offset > maxCharacterOffset) {
        break;
      }
      ++ problemRangeIt;
    }
  }
  
  // Ensure a unique ordering of the hovered problems
  std::sort(hoveredProblems.begin(), hoveredProblems.end(), [](const std::shared_ptr<Problem>& a, const std::shared_ptr<Problem>& b) {
    return a->items().front().offset < b->items().front().offset;
  });
  
  return hoveredProblems;
}

void DocumentWidget::UpdateTooltip(
    const QPoint& cursorPos,
    const std::vector<std::shared_ptr<Problem>>* hoveredProblems,
    const QString* codeHtml,
    const QUrl* helpUrl,
    const LineDiff* hoveredDiff) {
  // Additional offset applied to the mouse position for positioning the tooltip.
  // Should be at least as large as lineHeight to reduce the probability of the
  // tooltip covering important information.
  const int kTooltipOffset = static_cast<int>(1.5f * lineHeight + 0.5f);
  
  // Check whether the correct contents are already shown.
  bool ok = true;
  bool notOkDueToDataRemoval = true;
  
  // Check whether the correct problems are displayed
  if (hoveredProblems) {
    ok &= (tooltip && tooltipProblems.size() == hoveredProblems->size()) ||
           (!tooltip && hoveredProblems->empty());
    if (!ok && hoveredProblems && !hoveredProblems->empty()) {
      notOkDueToDataRemoval = false;
    }
  }
  if (ok && tooltip && hoveredProblems) {
    for (int i = 0; i < tooltipProblems.size(); ++ i) {
      if (tooltipProblems[i].get() != (*hoveredProblems)[i].get()) {
        ok = false;
        notOkDueToDataRemoval = false;
        break;
      }
    }
  }
  
  // Check whether the correct code HTML is displayed and the cursor is still
  // within the corresponding rect.
  QString emptyString;
  QUrl invalidUrl;
  if (!tooltipCodeRect.contains(cursorPos)) {
    codeHtml = &emptyString;
    helpUrl = &invalidUrl;
  }
  if (codeHtml && *codeHtml != tooltipCodeHtml) {
    ok = false;
    if (codeHtml && !codeHtml->isEmpty()) {
      notOkDueToDataRemoval = false;
    }
  }
  
  // Check whether the correct line diff is displayed.
  if (hoveredDiff != tooltipLineDiff) {
    ok = false;
    if (hoveredDiff != nullptr) {
      notOkDueToDataRemoval = false;
    }
  }
  
  // Make it easy to move the cursor over the tooltip by preventing it
  // from disappearing too easily (but only if the tooltip contents have only
  // been removed, but have not changed in other ways).
  if (!ok &&
      tooltip &&
      notOkDueToDataRemoval &&
      cursorPos.x() <= tooltipCreationPos.x() + kTooltipOffset + tooltip->width() &&
      cursorPos.y() <= tooltipCreationPos.y() + kTooltipOffset + 10 &&
      cursorPos.x() >= tooltipCreationPos.x() - 40 &&
      cursorPos.y() >= tooltipCreationPos.y() - 15) {
    ok = true;
  }
  
  // If the correct contents are already shown, there is nothing to do.
  if (ok) {
    return;
  }
  
  // Update tooltip information.
  if (hoveredProblems) {
    tooltipProblems = *hoveredProblems;
  }
  if (codeHtml) {
    tooltipCodeHtml = *codeHtml;
  }
  if (helpUrl) {
    tooltipHelpUrl = *helpUrl;
  }
  tooltipLineDiff = hoveredDiff;
  
  // Create the "Problems" part of the tooltip?
  tooltipProblemsFrame = nullptr;
  if (isVisible() && tooltipProblems.size() > 0) {
    int hoveredLine;
    GetDocumentLocationAt(cursorPos.x(), cursorPos.y(), nullptr, &hoveredLine, nullptr);
    
    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    
    for (const std::shared_ptr<Problem>& problem : tooltipProblems) {
      QFrame* labelFrame = new QFrame();
      labelFrame->setContentsMargins(1, 1, 1, 1);  // margin *outside* of label frame
      labelFrame->setFrameStyle(QFrame::Panel | QFrame::Plain);
      QVBoxLayout* labelFrameLayout = new QVBoxLayout();
      labelFrameLayout->setContentsMargins(0, 0, 0, 0);
      
      QLabel* label = new QLabel(problem->GetFormattedDescription(document->path(), hoveredLine));
      label->setTextInteractionFlags(Qt::TextBrowserInteraction);
      label->setContentsMargins(3, 3, 3, 3);  // margin *outside* of label
      label->setWordWrap(true);  // TODO: Maybe only activate this if necessary, as for the code info?
      connect(label, &QLabel::linkActivated, mainWindow, &MainWindow::GotoDocumentLocation);
      
      QPalette pal = label->palette();
      pal.setColor(QPalette::Background, (problem->type() == Problem::Type::Error) ? qRgb(255, 230, 230) : qRgb(230, 255, 230));
      label->setAutoFillBackground(true);
      label->setPalette(pal);
      
      labelFrameLayout->addWidget(label);
      labelFrame->setLayout(labelFrameLayout);
      layout->addWidget(labelFrame);
    }
    
    tooltipProblemsFrame = new QFrame();
    tooltipProblemsFrame->setFrameStyle(QFrame::Panel | QFrame::Plain);
    QPalette pal = tooltipProblemsFrame->palette();
    pal.setColor(QPalette::Background, qRgb(255, 255, 255));
    tooltipProblemsFrame->setAutoFillBackground(true);
    tooltipProblemsFrame->setPalette(pal);
    tooltipProblemsFrame->setLayout(layout);
  }
  
  // Create the "Code info" part of the tooltip?
  QFrame* codeFrame = nullptr;
  tooltipCodeHtmlLabel = nullptr;
  if (isVisible() && !tooltipCodeHtml.isEmpty()) {
    tooltipCodeHtmlLabel = new QLabel();
    tooltipCodeHtmlLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    tooltipCodeHtmlLabel->setTextFormat(Qt::RichText);
    tooltipCodeHtmlLabel->setText(tooltipCodeHtml);
    // tooltipCodeHtmlLabel->setWordWrap(true);
    
    connect(tooltipCodeHtmlLabel, &QLabel::linkActivated, [&](const QString& linkTarget) {
      if (linkTarget.startsWith(QStringLiteral("info://"))) {
        // This is a link that should show information about the given location
        // within the current tooltip. Request that information (which will be
        // obtained in a background thread).
        QString path;
        int pathLine;
        int pathCol;
        SplitPathAndLineAndColumn(linkTarget.mid(7), &path, &pathLine, &pathCol);
        path = QFileInfo(path).canonicalFilePath();
        
        showCodeInfoInExistingWidget = true;
        CodeInfo::Instance().RequestCodeInfo(this, path, pathLine, pathCol, document->path(), false);
      } else {
        // This is a link to a file location. Close the tooltip and go there.
        CloseTooltip();
        mainWindow->GotoDocumentLocation(linkTarget);
      }
    });
    
    tooltipCodeHtmlScrollArea = new QScrollArea();
    tooltipCodeHtmlScrollArea->setWidget(tooltipCodeHtmlLabel);
    
    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tooltipCodeHtmlScrollArea);
    
    codeFrame = new QFrame();
    codeFrame->setFrameStyle(QFrame::Panel | QFrame::Plain);
    QPalette pal = codeFrame->palette();
    pal.setColor(QPalette::Background, qRgb(255, 255, 255));
    codeFrame->setAutoFillBackground(true);
    codeFrame->setPalette(pal);
    codeFrame->setLayout(layout);
  }
  
  // Create the "Qt help" part of the tooltip?
  tooltipHelpFrame = nullptr;
  tooltipHelpBrowser = nullptr;
  if (isVisible() && (tooltipHelpUrl.isValid() || !tooltipCodeHtml.isEmpty())) {
    tooltipHelpBrowser = new HelpBrowser();
    tooltipHelpBrowser->setSource(tooltipHelpUrl);
    
    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tooltipHelpBrowser);
    
    tooltipHelpFrame = new QFrame();
    tooltipHelpFrame->setFrameStyle(QFrame::Panel | QFrame::Plain);
    QPalette pal = tooltipHelpFrame->palette();
    pal.setColor(QPalette::Background, qRgb(255, 255, 255));
    tooltipHelpFrame->setAutoFillBackground(true);
    tooltipHelpFrame->setPalette(pal);
    tooltipHelpFrame->setLayout(layout);
    
    if (!tooltipHelpUrl.isValid()) {
      // TODO: Do not create the frame in this case?
      tooltipHelpFrame->setVisible(false);
    }
  }
  
  // Create the "line diff" part of the tooltip?
  QFrame* diffFrame = nullptr;
  tooltipDiffLabel = nullptr;
  if (isVisible() && hoveredDiff && !hoveredDiff->oldText.isEmpty()) {
    tooltipDiffLabel = new QLabel();
    tooltipDiffLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    tooltipDiffLabel->setTextFormat(Qt::RichText);
    int numToChop = hoveredDiff->oldText.endsWith('\n') ? 1 : 0;
    tooltipDiffLabel->setText(tr("<b>Removed text:</b><br/>%1").arg(hoveredDiff->oldText.chopped(numToChop).toHtmlEscaped().replace('\n', "<br/>").replace(' ', "&nbsp;")));
    
    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tooltipDiffLabel);
    
    diffFrame = new QFrame();
    diffFrame->setFrameStyle(QFrame::Panel | QFrame::Plain);
    QPalette pal = diffFrame->palette();
    pal.setColor(QPalette::Background, qRgb(255, 255, 255));
    diffFrame->setAutoFillBackground(true);
    diffFrame->setPalette(pal);
    diffFrame->setLayout(layout);
  }
  
  // Delete the old tooltip (if any)
  if (tooltip) {
    tooltip->deleteLater();
  }
  tooltip = nullptr;
  
  // If no problems or code widget was created, there is nothing more to do.
  if (tooltipProblemsFrame == nullptr &&
      codeFrame == nullptr &&
      tooltipHelpFrame == nullptr &&
      diffFrame == nullptr) {
    CloseTooltip();
    return;
  }
  
  // Start creating the tooltip.
  tooltipCreationPos = cursorPos;
  
  tooltip = new QFrame(nullptr, GetCustomTooltipWindowFlags());
  tooltip->setFrameStyle(QFrame::Panel | QFrame::Plain);
  QPalette pal = tooltip->palette();
  pal.setColor(QPalette::Background, qRgb(255, 255, 255));
  tooltip->setAutoFillBackground(true);
  tooltip->setPalette(pal);
  
  tooltip->setMaximumSize(800, 400);
  
  // Insert the problems frame, code frame, or both into the tooltip.
  QVBoxLayout* layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  if (diffFrame) {
    layout->addWidget(diffFrame);
  }
  if (codeFrame) {
    layout->addWidget(codeFrame);
  }
  if (tooltipHelpFrame) {
    layout->addWidget(tooltipHelpFrame);
  }
  if (tooltipProblemsFrame) {
    layout->addWidget(tooltipProblemsFrame);
  }
  tooltip->setLayout(layout);
  
  // Finally, show the tooltip at the correct position.
  // Use an additional offset to the bottom-right that is at least of the size
  // of the line height to prevent the tooltip from covering important parts of
  // the code too easily.
  tooltip->move(mapToGlobal(QPoint(0, 0)) + cursorPos + QPoint(kTooltipOffset, kTooltipOffset));
  tooltip->show();
  
  ResizeTooltipToContents(true);
}

void DocumentWidget::ResizeTooltipToContents(bool allowShrinking) {
  // Try to prevent the tooltip from going beyond the screen width. After making
  // changes to the tooltip, this needs to be done with QTimer::singleShot() to
  // ensure that the tooltip width has been computed.
  QTimer::singleShot(0, [&, allowShrinking](){
    if (!tooltip) {
      return;
    }
    
    // The initial size of the tooltip seems to behave erratically. Try to
    // resize it to the label plus scroll bars.
    // TODO: Why do we need to use more than the scrollbar size here (random factor of 1.5 at the moment)?
    int scrollBarSize;
    if (qApp && qApp->style()) {
      scrollBarSize = 1.5 * qApp->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    } else {
      scrollBarSize = 32;  // random default guess
    }
    QSize tooltipSize =
        (tooltipDiffLabel ? tooltipDiffLabel->size() : QSize(0, 0)) +
        (tooltipCodeHtmlLabel ? tooltipCodeHtmlLabel->size() : QSize(0, 0)) +
        ((tooltipHelpBrowser && tooltipHelpBrowser->isVisible()) ? tooltipHelpBrowser->size() : QSize(0, 0)) +
        QSize(scrollBarSize, scrollBarSize) +
        (tooltipProblemsFrame ? tooltipProblemsFrame->size() : QSize(0, 0));
    if (tooltipSize.width() > tooltip->maximumWidth()) {
      tooltipSize.setWidth(tooltip->maximumWidth());
    }
    if (tooltipSize.height() > tooltip->maximumHeight()) {
      tooltipSize.setHeight(tooltip->maximumHeight());
    }
    if (!allowShrinking) {
      tooltipSize.setWidth(std::max(tooltipSize.width(), tooltip->width()));
      tooltipSize.setHeight(std::max(tooltipSize.height(), tooltip->height()));
    }
    tooltip->resize(tooltipSize);
    
    int tooltipRight = tooltip->mapToGlobal(tooltip->rect().topLeft()).x() + tooltipSize.width();
    int screenRight = QGuiApplication::screens()[QApplication::desktop()->screenNumber()]->availableGeometry().right();
    if (tooltipRight > screenRight) {
      tooltip->move(std::max(0, tooltip->x() - (tooltipRight - screenRight)), tooltip->y());
    }
  });
}

void DocumentWidget::CloseAllPopups() {
  CloseTooltip();
  CloseCodeCompletion();
  CloseArgumentHint();
}

void DocumentWidget::CloseTooltip() {
  if (tooltip) {
    tooltip->deleteLater();
  }
  tooltip = nullptr;
  
  tooltipCodeHtmlLabel = nullptr;
  tooltipHelpBrowser = nullptr;
  tooltipProblemsFrame = nullptr;
  tooltipDiffLabel = nullptr;
  
  codeInfoRequestRect = QRect();
  tooltipCodeRect = QRect();
}

void DocumentWidget::UpdateArgumentHintWidget(const DocumentRange& range, bool afterwards) {
  if (!argumentHintWidget) {
    return;
  }
  
  // TODO: Counting the commas is too simple. We need to parse strings "..."
  //       (including escape sequences within them) and chars '.' and inline
  //       comments (and perhaps more?).
  //       Alternatively, we could also try to use the cursor ranges from parsing.
  int numCommas = 0;
  
  // If there is any '(' or ')' or ';' within the range, close the argument hint widget.
  Document::CharacterAndStyleIterator charIt(document.get(), range.start.offset);
  while (charIt.GetCharacterOffset() < range.end.offset) {
    if (!charIt.GetStyleOfLayer(0).isNonCodeRange) {
      QChar character = charIt.GetChar();
      if (character == '(' || character == ')' || character == ';') {
        CloseArgumentHint();
        return;
      }
      if (character == ',') {
        ++ numCommas;
      }
    }
    ++ charIt;
  }
  
  int currentParameter = std::max(0, argumentInvocationCurrentParameter + (afterwards ? 1 : -1) * numCommas);
  if (currentParameter != argumentHintWidget->GetCurrentParameter()) {
    // Try to find the start point of the current parameter.
    // TODO: See above for better parsing.
    Document::CharacterIterator it(document.get(), afterwards ? range.end.offset : range.start.offset);
    -- it;
    bool found = false;
    while (it.IsValid()) {
      QChar character = it.GetChar();
      if (character == '(' || character == ',') {
        found = true;
        break;
      }
      -- it;
    }
    
    if (found) {
      argumentHintInvocationLocation = it.GetCharacterOffset() + 1;
      argumentHintWidget->SetInvocationPoint(GetTextRect(DocumentRange(argumentHintInvocationLocation, argumentHintInvocationLocation)).topLeft());
      argumentInvocationCurrentParameter = currentParameter;
      argumentHintWidget->SetCurrentParameter(currentParameter);
    } else {
      // There is some inconsistency. Close the argument hint.
      CloseArgumentHint();
    }
  }
}

void DocumentWidget::TabPressed(bool shiftHeld) {
  auto project = mainWindow->GetCurrentProject();
  
  if (selection.size() == 0 && project && !project->GetInsertSpacesOnTab() && !shiftHeld) {
    // Insert a tab character.
    InsertText(QStringLiteral("\t"));
  } else if (selection.size() == 0) {
    // (Un)indent the section of whitespace in which the cursor is.
    DocumentLocation originalCursorLoc = MapCursorToDocument();
    DocumentLocation cursorLoc = originalCursorLoc;
    Document::CharacterIterator it(document.get());
    
    bool keepCursorPos = false;
    if (shiftHeld) {
      // If Shift is held and there is no whitespace before the cursor,
      // move the cursor to the start of the line to un-indent there.
      it = Document::CharacterIterator(document.get(), std::max(0, cursorLoc.offset - 1));
      if (it.IsValid() && !IsWhitespace(it.GetChar())) {
        it = Document::CharacterIterator(document.get(), layoutLines[cursorLine].start.offset);
        keepCursorPos = true;
      } else if (it.IsValid() && it.GetChar() == '\t') {
        // Delete the tab character before the cursor.
        Replace(DocumentRange(cursorLoc.offset - 1, cursorLoc.offset), QStringLiteral(""));
        update(rect());  // TODO: limit update
        return;
      } else {
        ++ it;
      }
      cursorLoc = it.GetCharacterOffset();
    } else {
      it = Document::CharacterIterator(document.get(), cursorLoc.offset);
    }
    
    bool lastCharacterIsTab = false;
    int numSpaces = 0;
    while (it.IsValid()) {
      QChar c = it.GetChar();
      if (c != ' ' && c != '\t') {
        break;
      }
      lastCharacterIsTab = (c == '\t');
      ++ numSpaces;
      ++ it;
    }
    cursorLoc += numSpaces;
    
    int numCharacters = cursorLoc.offset - layoutLines[cursorLine].start.offset;
    int desiredCharacters;
    if (!shiftHeld) {
      desiredCharacters = (numCharacters / spacesPerTab) * spacesPerTab + spacesPerTab;
      int numAddedSpaces = desiredCharacters - numCharacters;
      document->Replace(DocumentRange(cursorLoc, cursorLoc), QStringLiteral(" ").repeated(numAddedSpaces));
      SetCursor(cursorLoc + numAddedSpaces, false);
    } else if (lastCharacterIsTab) {
      // Simply delete the last character.
      Replace(DocumentRange(layoutLines[cursorLine].start.offset + numCharacters - 1, layoutLines[cursorLine].start.offset + numCharacters), QStringLiteral(""));
      update(rect());  // TODO: limit update
      return;
    } else {
      desiredCharacters = std::max(0, ((numCharacters - 1) / spacesPerTab + 1) * spacesPerTab - spacesPerTab);
      if (desiredCharacters < numCharacters) {
        DocumentRange rangeToRemove(layoutLines[cursorLine].start.offset + desiredCharacters, layoutLines[cursorLine].start.offset + numCharacters);
        QString rangeText = document->TextForRange(rangeToRemove);
        int lastNonSpace = -1;
        for (int c = 0; c < rangeText.size(); ++ c) {
          if (rangeText[c] != ' ') {
            lastNonSpace = c;
          }
        }
        rangeToRemove.start += (1 + lastNonSpace);
        if (rangeToRemove.size() > 0) {
          document->Replace(rangeToRemove, QStringLiteral(""));
        }
        SetCursor(keepCursorPos ? (originalCursorLoc - rangeToRemove.size()) : rangeToRemove.start, false);
      }
    }
  } else {
    // (Un)indent all lines that are covered by the selection.
    std::vector<DocumentLocation> lineStarts;
    
    // Collect all the line starts for lines that intersect the selection range.
    DocumentLocation firstLineStart = DocumentLocation::Invalid();
    DocumentLocation lastLineEnd = DocumentLocation::Invalid();
    
    Document::LineIterator lineIt(document.get());
    while (lineIt.IsValid()) {
      DocumentRange lineRange = lineIt.GetLineRange();
      if (lineRange.end < selection.start) {
        ++ lineIt;
        continue;
      } else if (lineRange.start > selection.end) {
        break;
      }
      
      if (!firstLineStart.IsValid()) {
        firstLineStart = lineRange.start;
      }
      lastLineEnd = lineRange.end;
      
      lineStarts.emplace_back(lineRange.start);
      ++ lineIt;
    }
    if (firstLineStart.IsInvalid()) {
      qDebug() << "Error: While handling a Tab key press, could not find the start of the first line that intersects the selection.";
      return;
    }
    
    SetSelection(DocumentRange::Invalid());
    
    // Back to front (in order not to invalidate the remaining stored locations),
    // perform the changes.
    document->StartUndoStep();
    for (int i = static_cast<int>(lineStarts.size()) - 1; i >= 0; -- i) {
      // Find the whitespace range at the line start.
      int numSpaces = 0;
      Document::CharacterIterator it(document.get(), lineStarts[i].offset);
      while (it.IsValid()) {
        if (it.GetChar() != ' ') {
          break;
        }
        ++ numSpaces;
        ++ it;
      }
      
      int desiredSpaces;
      if (!shiftHeld) {
        desiredSpaces = (numSpaces / spacesPerTab) * spacesPerTab + spacesPerTab;
        int numAddedSpaces = desiredSpaces - numSpaces;
        DocumentLocation spaceEndLocation = lineStarts[i] + numSpaces;
        document->Replace(DocumentRange(spaceEndLocation, spaceEndLocation), QStringLiteral(" ").repeated(numAddedSpaces));
      } else {
        desiredSpaces = std::max(0, ((numSpaces - 1) / spacesPerTab + 1) * spacesPerTab - spacesPerTab);
        if (desiredSpaces < numSpaces) {
          document->Replace(DocumentRange(lineStarts[i] + desiredSpaces, lineStarts[i] + numSpaces), QStringLiteral(""));
        }
      }
      
      lastLineEnd += (desiredSpaces - numSpaces);
    }
    document->EndUndoStep();
    
    // Select the complete range of all involved lines afterwards.
    SetSelection(DocumentRange(firstLineStart, lastLineEnd));
  }
  
  update(rect());  // TODO: limit update
}

void DocumentWidget::BookmarksChanged() {
  container->GetMinimap()->UpdateMap(layoutLines, nullptr);
}

void DocumentWidget::CheckForWordCompletion() {
  DocumentLocation cursorLoc = MapCursorToDocument();
  Document::CharacterIterator charIt(document.get(), std::max(0, cursorLoc.offset - 2));
  
  auto doesNonWhitespaceFollow = [&]() {
    Document::CharacterIterator charIt(document.get(), cursorLoc.offset);
    while (charIt.IsValid()) {
      QChar c = charIt.GetChar();
      ++ charIt;
      if (c == '\n') {
        break;
      } else if (IsWhitespace(c)) {
        continue;
      }
      return true;
    }
    return false;
  };
  
  const std::vector<WordCompletion>& completions = Settings::Instance().GetWordCompletions();
  
  std::vector<bool> ok(completions.size(), true);
  int charIndex = 0;
  while (charIt.IsValid()) {
    // Check which word the current letter fits to
    QChar c = charIt.GetChar();
    -- charIt;
    ++ charIndex;
    
    for (int i = 0; i < completions.size(); ++ i) {
      if (!ok[i]) {
        continue;
      }
      
      const QString& word = completions[i].word;
      if (!word.isEmpty() && charIndex == word.size() + 1) {
        if (IsWhitespace(c)) {
          // Word fits completely
          if ((!completions[i].applyIfNonWhitespaceFollows && doesNonWhitespaceFollow()) ||
              (completions[i].applyWithinCodeOnly && !IsCursorLikelyInCodeSection())) {
            ok[i] = false;
          } else {
            ApplyWordCompletion(completions[i]);
            return;
          }
        } else {
          ok[i] = false;
        }
      } else if (!word.isEmpty() && word[word.size() - charIndex] == c) {
        // Character fits
      } else {
        ok[i] = false;
      }
    }
  }
  
  for (int i = 0; i < completions.size(); ++ i) {
    if (!ok[i]) {
      continue;
    }
    
    const QString& word = completions[i].word;
    if (charIndex == word.size()) {
      // Word fits completely at the start of the document
      if ((!completions[i].applyIfNonWhitespaceFollows && doesNonWhitespaceFollow()) ||
          (completions[i].applyWithinCodeOnly && !IsCursorLikelyInCodeSection())) {
        ok[i] = false;
      } else {
        ApplyWordCompletion(completions[i]);
        return;
      }
    }
  }
}

bool DocumentWidget::IsCursorLikelyInCodeSection() {
  if (!isCFile) {
    return false;
  }
  
  // Problem:
  // * Doing this properly (looking up libclang's tokenization) might
  //   introduce stuttering since we need to wait for the latest version
  //   of the document to be parsed; using the latest known parse result may be wrong
  // * Doing this heuristically might break easily. The possibility of multi-line
  //   comments means that we need to parse the whole document up to the cursor
  //   position in principle and need to parse everything correctly on the way
  //   (such as multi-line "raw" strings). This needs to be updated in case
  //   C++ introduces new ways of specifying strings.
  
  // We first check the latest parse result.
  DocumentLocation cursorLoc = MapCursorToDocument();
  Document::CharacterAndStyleIterator it(document.get(), cursorLoc.offset);
  if (it.IsValid()) {
    if (it.GetStyleOfLayer(0).isNonCodeRange) {
      return false;
    }
  }
  
  // We then use a heuristic to fix writing at the end of single-line comments
  // (where the adaptation of the comment highlight range to edits does not work
  // well currently when typing new words - it gets interrupted at whitespace).
  // This heuristic does not take into account that the // might be within a string.
  DocumentRange currentLineRange = layoutLines[cursorLine];
  QString lineText = document->TextForRange(currentLineRange);
  int commentStartIndex = lineText.indexOf("//");
  if (commentStartIndex >= 0 &&
      commentStartIndex < cursorLoc.offset - currentLineRange.start.offset) {
    return false;
  }
  
  return true;
}

void DocumentWidget::ApplyWordCompletion(const WordCompletion& item) {
  // Note that we cannot insert the completion text directly, since
  // we need to properly apply indentation, and place the cursor at the '$'.
  int wordLen = item.word.size();
  DocumentLocation cursorLoc = MapCursorToDocument();
  DocumentLocation wordStartLoc = cursorLoc - 1 - wordLen;
  
  QString indentation;
  Document::CharacterIterator indentationIt(document.get(), layoutLines[cursorLine].start.offset);
  while (indentationIt.IsValid()) {
    QChar c = indentationIt.GetChar();
    ++ indentationIt;
    if (IsWhitespace(c)) {
      indentation += c;
    } else {
      break;
    }
    if (indentationIt.GetCharacterOffset() >= wordStartLoc.offset) {
      break;
    }
  }
  
  DocumentRange wordAndSpaceRange(wordStartLoc, cursorLoc);
  
  QStringList lines = item.replacement.split('\n');
  DocumentRange insertionRange = wordAndSpaceRange;
  int charactersInserted = 0;
  bool cursorHasBeenSet = false;
  
  document->StartUndoStep();
  for (int line = 0; line < lines.size(); ++ line) {
    QString& text = lines[line];
    int dollarIndex = text.indexOf('$');
    if (dollarIndex >= 0) {
      text.remove(dollarIndex, 1);
    }
    
    document->Replace(insertionRange, ((line > 0) ? indentation : "") + text + ((line < lines.size() - 1) ? "\n" : ""));
    
    if (dollarIndex >= 0) {
      SetCursor(wordStartLoc + charactersInserted + ((line > 0) ? indentation.size() : 0) + dollarIndex, false);
      cursorHasBeenSet = true;
    }
    
    int newCharacters = ((line > 0) ? indentation.size() : 0) + text.size() + ((line < lines.size() - 1) ? 1 : 0);
    DocumentLocation newTextEndLocation = insertionRange.start + newCharacters;
    insertionRange = DocumentRange(newTextEndLocation, newTextEndLocation);
    charactersInserted += newCharacters;
  }
  document->EndUndoStep();
  
  if (!cursorHasBeenSet) {
    SetCursor(wordStartLoc + charactersInserted, false);
  }
  
  update(rect());
}

void DocumentWidget::resizeEvent(QResizeEvent* /*event*/) {
  UpdateScrollbar();
}

void DocumentWidget::paintEvent(QPaintEvent* event) {
  auto& settings = Settings::Instance();
  
  QRgb editorBackgroundColor = settings.GetConfiguredColor(Settings::Color::EditorBackground);
  QColor sidebarDefaultColor = palette().window().color();
  QRgb highlightTrailingSpaceColor = settings.GetConfiguredColor(Settings::Color::TrailingSpaceHighlight);
  QRgb outsideOfContextLineColor = settings.GetConfiguredColor(Settings::Color::OutsizeOfContextLine);
  QRgb highlightLineColor = settings.GetConfiguredColor(Settings::Color::CurrentLine);
  QRgb selectionColor = settings.GetConfiguredColor(Settings::Color::EditorSelection);
  QRgb bookmarkColor = settings.GetConfiguredColor(Settings::Color::BookmarkLine);
  QRgb errorUnderlineColor = settings.GetConfiguredColor(Settings::Color::ErrorUnderline);
  QRgb warningUnderlineColor = settings.GetConfiguredColor(Settings::Color::WarningUnderline);
  QRgb columnMarkerColor = settings.GetConfiguredColor(Settings::Color::ColumnMarker);
  QRgb gitDiffAddedColor = settings.GetConfiguredColor(Settings::Color::GitDiffAdded);
  QRgb gitDiffModifiedColor = settings.GetConfiguredColor(Settings::Color::GitDiffModified);
  QRgb gitDiffRemovedColor = settings.GetConfiguredColor(Settings::Color::GitDiffRemoved);
  const auto& defaultStyle = settings.GetConfiguredTextStyle(Settings::TextStyle::Default);
  const auto& inlineErrorStyle = settings.GetConfiguredTextStyle(Settings::TextStyle::ErrorInlineDisplay);
  const auto& inlineWarningStyle = settings.GetConfiguredTextStyle(Settings::TextStyle::WarningInlineDisplay);
  
  bool highlightCurrentLine = settings.GetHighlightCurrentLine();
  bool highlightTrailingSpaces = settings.GetHighlightTrailingSpaces();
  bool darkenNonContextRegions = settings.GetDarkenNonContextRegions();
  bool showColumnMarker = settings.GetShowColumnMarker();
  int columnMarkerX = -1;
  if (showColumnMarker) {
    columnMarkerX = -xScroll + sidebarWidth + charWidth * settings.GetColumnMarkerPosition();
  }
  
  // Re-layout?
  CheckRelayout();
  
  // Always "re-layout" the fix-it buttons
  // Note: Since we may erase elements here, we cannot cache the end() of the vector.
  fixitButtonsDocumentVersion = document->version();
  for (auto it = fixitButtons.begin(); it != fixitButtons.end(); ) {
    if (it->buttonRect.intersects(event->rect())) {
      it = fixitButtons.erase(it);
    } else {
      ++ it;
    }
  }
  
  // Check whether the currently selected frame (from the debugger) is in this
  // file. If so, we may want to display it.
  bool currentFrameInFile = document->path() == mainWindow->GetCurrentFrameCanonicalPath();
  
  // Get the contexts for context boundary display
  const std::set<Context>& contexts = document->GetContexts();
  auto contextIt = contexts.begin();
  
  // Get the diff lines for diff display
  const std::vector<LineDiff>& diffLines = document->diffLines();
  int currentDiffLine = 0;
  
  // Start painting
  QPainter painter(this);
  QRect rect = event->rect();
  painter.setClipRect(rect);
  
  // Start problem range handling
  const std::set<ProblemRange>& problemRanges = document->problemRanges();
  auto problemRangeIt = problemRanges.begin();
  int lastProblem = -1;
  int warningRangeEnd = -1;
  int errorRangeEnd = -1;
  
  // Draw lines
  int minLine = (yScroll + rect.top()) / lineHeight;
  int maxLine = std::min<int>(static_cast<int>(layoutLines.size()) - 1, (yScroll + rect.bottom()) / lineHeight);
  
  int currentY = minLine * lineHeight - yScroll;
  // NOTE: This uses a LineIterator for performance. However, if we would want to support word wrap, we would
  //       also need to account for the layoutLines instead.
  Document::LineIterator lineIt(document.get(), minLine);
  for (int line = minLine; line <= maxLine; ++ line) {
    while (currentDiffLine < diffLines.size() &&
           diffLines[currentDiffLine].line + diffLines[currentDiffLine].numLines <= line) {
      ++ currentDiffLine;
    }
    
    int xCoord = -xScroll + sidebarWidth;
    
    // Note that the iterator will be invalid if the last character in the document is a newline character
    Document::CharacterAndStyleIterator it = lineIt.GetCharacterAndStyleIterator();
    int lineAttributes = lineIt.GetAttributes();
    
    // Get the line text
    QString text;
    Document::CharacterIterator charIt = it.ToCharacterIterator();
    while (charIt.IsValid()) {
      QChar c = charIt.GetChar();
      if (c == '\n') {
        break;
      }
      text += c;
      ++ charIt;
    }
    
    // Check whether the line is empty or there is trailing whitespace at the end
    bool lineIsEmptyOrOnlyWhitespace = text.isEmpty();
    int highlightTrailingSpaceStart = std::numeric_limits<int>::max();
    if (!text.isEmpty()) {
      int c = text.size() - 1;
      for (; c > 0; -- c) {
        if (!IsWhitespace(text[c])) {
          break;
        }
      }
      if (c == 0 && IsWhitespace(text[c])) {
        lineIsEmptyOrOnlyWhitespace = true;
        // Do not highlight empty lines consisting of only whitespace
        // TODO: Could make an option to highlight those, or try to determine the
        //       indentation width and still highlight inconsistencies with the indentation
      } else {
        highlightTrailingSpaceStart = c + 1;
      }
    }
    if (!highlightTrailingSpaces) {
      highlightTrailingSpaceStart = std::numeric_limits<int>::max();
    }
    
    // Update the context iterator
    while (contextIt != contexts.end() &&
           contextIt->range.end < layoutLines[line].start) {
      // Go to the next context while jumping over inner contexts.
      DocumentLocation prevContextEnd = contextIt->range.end;
      do {
        ++ contextIt;
      } while (contextIt != contexts.end() &&
               contextIt->range.end <= prevContextEnd);
    }
    bool withinAnyContext =
        contextIt != contexts.end() &&
        contextIt->range.start <= layoutLines[line].end &&
        contextIt->range.end >= layoutLines[line].start;
    
    // Determine the line background color
    float red = 0;
    float green = 0;
    float blue = 0;
    int bgColorCount = 0;
    if (lineAttributes & static_cast<int>(LineAttribute::Bookmark)) {
      red += qRed(bookmarkColor);
      green += qGreen(bookmarkColor);
      blue += qBlue(bookmarkColor);
      ++ bgColorCount;
    }
    if (highlightCurrentLine && line == cursorLine) {
      red += qRed(highlightLineColor);
      green += qGreen(highlightLineColor);
      blue += qBlue(highlightLineColor);
      ++ bgColorCount;
    }
    if (darkenNonContextRegions && isCFile && !withinAnyContext && lineIsEmptyOrOnlyWhitespace) {
      red += qRed(outsideOfContextLineColor);
      green += qGreen(outsideOfContextLineColor);
      blue += qBlue(outsideOfContextLineColor);
      ++ bgColorCount;
    }
    
    QColor lineBackgroundColor;
    if (bgColorCount > 0) {
      lineBackgroundColor = qRgb(red / bgColorCount, green / bgColorCount, blue / bgColorCount);
    } else {
      lineBackgroundColor = editorBackgroundColor;
    }
    
    QColor styleBackgroundColor;
    bool haveStyleBackgroundColor = false;
    
    int lastProblemInLine = -1;
    
    int column = 0;
    for (int c = 0; c < text.size(); ++ c, ++ it) {
      int characterOffset = layoutLines[line].start.offset + c;
      
      // Handle underlining
      while (problemRangeIt != document->problemRanges().end()) {
        if (characterOffset >= problemRangeIt->range.start.offset) {
          if (document->problems()[problemRangeIt->problemIndex]->type() == Problem::Type::Warning) {
            warningRangeEnd = std::max(warningRangeEnd, problemRangeIt->range.end.offset);
          } else {  // if (document->problems[problemRangeIt->problemIndex].type == Problem::Type::Error) {
            errorRangeEnd = std::max(errorRangeEnd, problemRangeIt->range.end.offset);
          }
          lastProblem = problemRangeIt->problemIndex;
          ++ problemRangeIt;
        } else {
          break;
        }
      }
      
      // Handle style changes due to highlight range boundaries
      if (it.StyleChanged()) {
        const HighlightRange& style = it.GetStyle();
        if (style.bold) {
          painter.setFont(Settings::Instance().GetBoldFont());
        } else {
          painter.setFont(Settings::Instance().GetDefaultFont());
        }
        painter.setPen(QPen(style.textColor));
        
        haveStyleBackgroundColor = style.affectsBackground;
        styleBackgroundColor = style.backgroundColor;
      }
      
      // Draw the character
      int charColumns;
      int charWidth = GetTextWidth(text.at(c), column, &charColumns);
      column += charColumns;
      if (xCoord + charWidth - 1 >= 0 && xCoord < width()) {
        // Draw background color (based on line background color and highlighting)
        QColor backgroundColor;
        if (selection.ContainsCharacter(layoutLines[line].start.offset + c)) {
          backgroundColor = selectionColor;
        } else if (c >= highlightTrailingSpaceStart && (line != cursorLine || cursorCol <= c)) {
          backgroundColor = highlightTrailingSpaceColor;
        } else if (haveStyleBackgroundColor) {
          backgroundColor = styleBackgroundColor;
        } else {
          backgroundColor = lineBackgroundColor;
        }
        painter.fillRect(xCoord, currentY, charWidth, lineHeight, backgroundColor);
        if (showColumnMarker && columnMarkerX >= xCoord && columnMarkerX < xCoord + charWidth) {
          QColor oldColor = painter.pen().color();
          painter.setPen(columnMarkerColor);
          painter.drawLine(columnMarkerX, currentY, columnMarkerX, currentY + lineHeight - 1);
          painter.setPen(oldColor);
        }
        if (currentFrameInFile && mainWindow->GetCurrentFrameLine() == line) {
          QColor oldColor = painter.pen().color();
          painter.setPen(backgroundColor.darker(200));
          painter.drawLine(xCoord, currentY, xCoord + charWidth, currentY);
          painter.drawLine(xCoord, currentY + lineHeight - 1, xCoord + charWidth, currentY + lineHeight - 1);
          painter.setPen(oldColor);
        }
        
        // Draw text character
        if (text.at(c) != ' ') {
          painter.drawText(QRect(xCoord, currentY, charWidth, lineHeight), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, text.at(c));
        }
        
        // Draw underlining
        if (errorRangeEnd > characterOffset || warningRangeEnd > characterOffset) {
          lastProblemInLine = lastProblem;
          
          QColor color = (errorRangeEnd > characterOffset) ? errorUnderlineColor : warningUnderlineColor;
          
          QPen oldPen = painter.pen();
          painter.setPen(color);
          // TODO: Maybe use drawRect() for high-DPI screens?
          painter.drawLine(xCoord, currentY + fontMetrics->ascent(), xCoord + charWidth, currentY + fontMetrics->ascent());
          painter.setPen(oldPen);
        }
      }
      xCoord += charWidth;
    }
    
    // Draw line background color to the right of the text
    if (xCoord <= rect.right()) {
      QColor backgroundColor = selection.ContainsCharacter(layoutLines[line].end.offset) ? selectionColor : lineBackgroundColor;
      painter.fillRect(xCoord, currentY, rect.right() - xCoord + 1, lineHeight, backgroundColor);
      if (showColumnMarker && columnMarkerX >= xCoord && columnMarkerX < rect.right()) {
        QColor oldColor = painter.pen().color();
        painter.setPen(columnMarkerColor);
        painter.drawLine(columnMarkerX, currentY, columnMarkerX, currentY + lineHeight - 1);
        painter.setPen(oldColor);
      }
      if (currentFrameInFile && mainWindow->GetCurrentFrameLine() == line) {
        QColor oldColor = painter.pen().color();
        painter.setPen(backgroundColor.darker(200));
        painter.drawLine(xCoord, currentY, rect.right(), currentY);
        painter.drawLine(xCoord, currentY + lineHeight - 1, rect.right(), currentY + lineHeight - 1);
        painter.setPen(oldColor);
      }
    }
    
    // Draw inline problem description to the right of the text
    if (lastProblemInLine >= 0) {
      const std::shared_ptr<Problem>& problem = document->problems()[lastProblemInLine];
      
      auto& problemStyle = (problem->type() == Problem::Type::Warning) ? inlineWarningStyle : inlineErrorStyle;
      
      const int lineToDescriptionMargin = 3 * charWidth;
      const int descriptionToFixitMargin = 3 * charWidth;
      
      QString text = problem->items()[0].text;
      if (problem->items().size() > 1 || !problem->items()[0].children.empty()) {
        // Indicate the presence of further descriptive text that is not displayed inline.
        text += QStringLiteral(" ...");
      }
      int descriptionStartX = xCoord + lineToDescriptionMargin;
      int lineLen = text.indexOf(QChar('\n'));
      if (lineLen < 0) {
        lineLen = text.size();
      } else {
        -- lineLen;
      }
      QSize iconSize = warningIcon.size() * lineHeight / warningIcon.height();
      int iconAndSpaceWidth = iconSize.width() + charWidth;
      int descriptionEndX = descriptionStartX + iconAndSpaceWidth + lineLen * charWidth;
      
      // Draw problem icon
      // TODO: Avoid the overdraw of the colored background here with the general background above
      if (problemStyle.affectsBackground) {
        painter.fillRect(descriptionStartX + iconSize.width() / 2, currentY, iconSize.width() + charWidth - iconSize.width() / 2, lineHeight, problemStyle.backgroundColor);
      }
      painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
      painter.drawImage(QRect(descriptionStartX, currentY, iconSize.width(), iconSize.height()),
                        (problem->type() == Problem::Type::Warning) ? warningIcon : errorIcon);
      
      // Draw inline problem text
      int inlineTextStartX = descriptionStartX + iconAndSpaceWidth;
      painter.setPen(problemStyle.affectsText ? problemStyle.textColor : defaultStyle.textColor);
      painter.setFont(Settings::Instance().GetBoldFont());
      for (int i = 0;
           i < lineLen &&
               inlineTextStartX + i * charWidth < width();
           ++ i) {
        // TODO: Avoid the overdraw of the colored background here with the general background above
        if (problemStyle.affectsBackground) {
          painter.fillRect(inlineTextStartX + i * charWidth, currentY, charWidth, lineHeight, problemStyle.backgroundColor);
        }
        painter.drawText(QRect(inlineTextStartX + i * charWidth, currentY, charWidth, lineHeight), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, text.at(i));
      }
      
      // Draw fix-it button(s) to the right of the text
      auto formatFixitText = [](const Problem::FixIt& fixIt) {
        if (fixIt.oldText.isEmpty()) {
          return tr("Insert %1").arg(fixIt.newText);
        } else if (fixIt.newText.isEmpty()) {
          return tr("Remove %1").arg(fixIt.oldText);
        } else {
          return (fixIt.oldText % QStringLiteral(" --> ") % fixIt.newText).operator QString();
        }
      };
      
      int availableSpace = width() - descriptionStartX;
      int fixitsWidth = 0;
      int numFixitsToShow = problem->fixits().size();
      for (int fixitIndex = 0; fixitIndex < problem->fixits().size(); ++ fixitIndex) {
        const Problem::FixIt& fixIt = problem->fixits()[fixitIndex];
        int textWidth = formatFixitText(fixIt).size() * charWidth;
        if (fixitsWidth + textWidth > availableSpace) {
          numFixitsToShow = fixitIndex;
          break;
        }
        fixitsWidth += textWidth;
      }
      
      int fixitStartX = std::min(width() - fixitsWidth, descriptionEndX + descriptionToFixitMargin);
      for (int fixitIndex = 0; fixitIndex < numFixitsToShow; ++ fixitIndex) {
        const Problem::FixIt& fixIt = problem->fixits()[fixitIndex];
        
        QString buttonText = formatFixitText(fixIt);
        int textWidth = buttonText.size() * charWidth;
        
        QStyleOptionButton opt;
        opt.state = QStyle::State_Active | QStyle::State_Enabled;
        opt.rect = QRect(fixitStartX, currentY, textWidth, lineHeight);
        style()->drawControl(QStyle::CE_PushButton, &opt, &painter);
        
        painter.setPen(qRgb(0, 0, 0));
        for (int i = 0, size = buttonText.size(); i < size; ++ i) {
          QRect charRect(opt.rect.left() + i * charWidth, opt.rect.top(), charWidth, opt.rect.height());
          painter.drawText(charRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, buttonText.at(i));
        }
        
        fixitButtons.emplace_back();
        fixitButtons.back().buttonRect = opt.rect;
        fixitButtons.back().problem = problem;
        fixitButtons.back().fixitIndex = fixitIndex;
        
        fixitStartX += textWidth;
      }
    }
    
    // Draw the sidebar
    QColor sidebarColor = sidebarDefaultColor;
    bool drawRedDot = false;
    while (currentDiffLine < diffLines.size() &&
           diffLines[currentDiffLine].line <= line &&
           diffLines[currentDiffLine].line + diffLines[currentDiffLine].numLines > line) {
      const LineDiff& diff = diffLines[currentDiffLine];
      if (diff.type == LineDiff::Type::Added) {
        sidebarColor = gitDiffAddedColor;
        break;
      } else if (diff.type == LineDiff::Type::Modified) {
        sidebarColor = gitDiffModifiedColor;
        break;
      } else if (diff.type == LineDiff::Type::Removed) {
        drawRedDot = true;
        ++ currentDiffLine;
      } else {
        qDebug() << "Unhandled diff type:" << static_cast<int>(diff.type);
      }
    }
    painter.fillRect(0, currentY, sidebarWidth, lineHeight, sidebarColor);
    if (drawRedDot) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QBrush(gitDiffRemovedColor));
      int diameter = std::min(sidebarWidth, lineHeight);
      painter.drawEllipse(0 + (sidebarWidth - diameter) / 2, currentY - diameter / 2, diameter, diameter);
    }
    if (line == static_cast<int>(layoutLines.size()) - 1 &&
        currentDiffLine < diffLines.size() &&
        diffLines[currentDiffLine].line >= layoutLines.size() &&
        diffLines[currentDiffLine].type == LineDiff::Type::Removed) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QBrush(gitDiffRemovedColor));
      int diameter = std::min(sidebarWidth, lineHeight);
      painter.drawEllipse(0 + (sidebarWidth - diameter) / 2, currentY + lineHeight - diameter / 2, diameter, diameter);
    }
    
    currentY += lineHeight;
    ++ lineIt;
  }
  
  // Draw cursor
  if (cursorBlinkState && hasFocus()) {
    QRect cursorRect = GetCursorRect();
    painter.fillRect(cursorRect, Qt::black);
  }
  
  // Draw gray space below the last line
  painter.fillRect(rect.left(), currentY, rect.right() + 1, rect.bottom() + 1 - currentY, Qt::gray);
  
  painter.end();
}

void DocumentWidget::mousePressEvent(QMouseEvent* event) {
  CloseTooltip();
  mouseHoverTimer.stop();
  
  if (event->button() == Qt::LeftButton) {
    for (int buttonIndex = 0; buttonIndex < fixitButtons.size(); ++ buttonIndex) {
      const FixitButton& button = fixitButtons[buttonIndex];
      if (button.buttonRect.contains(event->pos())) {
        // Clicked this fix-it button.
        ApplyFixIt(button.problem, button.fixitIndex, false);
        return;
      }
    }
    
    SetCursor(event->x(), event->y(), event->modifiers() & Qt::ShiftModifier);
    selectionDoubleClickOffset = -1;
    
    if (event->modifiers() & Qt::ControlModifier) {
      GotoReferencedCursor(MapLayoutToDocument(cursorLine, cursorCol));
    }
  } else if (event->button() == Qt::RightButton) {
    lastRightClickPoint = event->pos();
    
    int line, character;
    if (!GetCharacterAt(event->x(), event->y(), false, &line, &character)) {
      return;
    }
    int offset = layoutLines[line].start.offset + character;
    renameRequested = false;
    CodeInfo::Instance().RequestRightClickInfo(this, DocumentLocation(offset));
  }
}

void DocumentWidget::mouseMoveEvent(QMouseEvent* event) {
  // Manually buffer the event. This is to improve performance, since we then
  // only react to the last event that is in the queue. By default, Qt would do this
  // itself, however, we explicitly disable it by disabling the Qt::AA_CompressHighFrequencyEvents
  // attribute, which was necessary to fix wheel events getting buffered over a far too long
  // time window in cases where the event loop was somewhat busy.
  if (!haveMouseMoveEvent) {
    // Queue handling the event at the back of the event loop queue.
    QMetaObject::invokeMethod(this, &DocumentWidget::HandleMouseMoveEvent, Qt::QueuedConnection);
    haveMouseMoveEvent = true;
  }
  lastMouseMoveEventPos = event->pos();
  lastMouseMoveEventGlobalPos = event->globalPos();
  lastMouseMoveEventButtons = event->buttons();
}

void DocumentWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->buttons() & Qt::LeftButton) {
    int line, character;
    if (GetCharacterAt(event->x(), event->y(), false, &line, &character)) {
      int offset = layoutLines[line].start.offset + character;
      
      Document::CharacterAndStyleIterator it(document.get(), offset);
      if (it.IsValid() && IsBracket(it.GetChar())) {
        // Double-clicked on a bracket. Jump to the matching bracket.
        int matchOffset = document->FindMatchingBracket(it);
        if (matchOffset != -1) {
          SetCursor(DocumentLocation(matchOffset), false);
        }
      } else {
        // Double-clicked on a generic location. Select the word at this location.
        DocumentRange wordRange = GetWordForCharacter(offset);
        SetSelection(wordRange);
        selectionDoubleClickOffset = offset;
      }
    }
  }
}

bool DocumentWidget::event(QEvent* event) {
  if (event->type() == QEvent::KeyPress) {
    QKeyEvent* keyEvent = dynamic_cast<QKeyEvent*>(event);
    if (keyEvent) {
      if (keyEvent->key() == Qt::Key_Tab) {
        // Handle Tab key press.
        if (codeCompletionWidget) {
          Settings::CodeCompletionConfirmationKeys confirmKeys = Settings::Instance().GetCodeCompletionConfirmationKeys();
          if (confirmKeys == Settings::CodeCompletionConfirmationKeys::Tab ||
              confirmKeys == Settings::CodeCompletionConfirmationKeys::TabAndReturn) {
            AcceptCodeCompletion();
          } else {
            TabPressed(false);
          }
        } else {
          TabPressed(false);
        }
        event->accept();
        return true;
      } else if (keyEvent->key() == Qt::Key_Backtab) {
        // Handle Shift+Tab key press.
        TabPressed(true);
        event->accept();
        return true;
      }
    }
  }
  return QWidget::event(event);
}

void DocumentWidget::keyPressEvent(QKeyEvent* event) {
  // Make any keypress close the mouse-over tooltip
  CloseTooltip();
  mouseHoverTimer.stop();
  
  // For some reason, Shift+Home / Shift+End is misinterpreted on my laptop, yielding Shift+7 / Shift+1 instead.
  // Correct for this.
  // TODO: Is there a generic solution that we could use instead of this hack? Why does that even happen? Note that some
  //       applications show the same issue (e.g., Firefox), but others don't (e.g., KDevelop, CLion).
  int keyCode = event->key();
  bool shiftHeld = event->modifiers() & Qt::ShiftModifier;
  if (keyCode == '7' && shiftHeld) {
    keyCode = Qt::Key_Home;
  }
  if (keyCode == '1' && shiftHeld) {
    keyCode = Qt::Key_End;
  }
  
  if (keyCode == Qt::Key_Escape) {
    bool widgetClosed = false;
    if (codeCompletionWidget) {
      CloseCodeCompletion();
      widgetClosed = true;
    }
    if (argumentHintWidget) {
      CloseArgumentHint();
      widgetClosed = true;
    }
    if (widgetClosed) {
      event->accept();
      return;
    }
    container->CloseFindReplaceBar();
    container->CloseGotoLineBar();
    event->accept();
    return;
  }
  
  // If there is a code completion widget, first check whether it wants to
  // process the key event
  if (codeCompletionWidget) {
    if (keyCode == Qt::Key_Return) {
      Settings::CodeCompletionConfirmationKeys confirmKeys = Settings::Instance().GetCodeCompletionConfirmationKeys();
      if (confirmKeys == Settings::CodeCompletionConfirmationKeys::Return ||
          confirmKeys == Settings::CodeCompletionConfirmationKeys::TabAndReturn) {
        AcceptCodeCompletion();
        event->accept();
        return;
      }
    }
    
    QApplication::sendEvent(codeCompletionWidget, event);
    if (event->isAccepted()) {
      return;
    }
  }
  
  if (event->modifiers() & Qt::AltModifier) {
    event->ignore();
    return;
  }
  
  
  if (event->modifiers() & Qt::ControlModifier &&
      keyCode == Qt::Key_A) {
    SelectAll();
  } else if (keyCode == Qt::Key_Escape) {
    event->ignore();
    return;
  } else if (keyCode == Qt::Key_Right) {
    MoveCursorRight(shiftHeld, event->modifiers() & Qt::ControlModifier);
  } else if (keyCode == Qt::Key_Left) {
    MoveCursorLeft(shiftHeld, event->modifiers() & Qt::ControlModifier);
  } else if (keyCode == Qt::Key_Up) {
    MoveCursorUpDown(-1, shiftHeld);
  } else if (keyCode == Qt::Key_Down) {
    MoveCursorUpDown(1, shiftHeld);
  } else if (keyCode == Qt::Key_Home) {
    StartMovingCursor();
    
    if (event->modifiers() & Qt::ControlModifier) {
      cursorLine = 0;
      cursorCol = 0;
    } else {
      if (intelligentHomeAndEnd) {
        QString text = document->TextForRange(layoutLines[cursorLine]);
        int c = 0;
        for (; c < text.size(); ++ c) {
          if (!IsWhitespace(text[c])) {
            break;
          }
        }
        if (c != text.size() && cursorCol != c) {
          cursorCol = c;
        } else {
          cursorCol = 0;
        }
      } else {
        cursorCol = 0;
      }
    }
    
    EndMovingCursor(shiftHeld);
  } else if (keyCode == Qt::Key_End) {
    StartMovingCursor();
    
    if (event->modifiers() & Qt::ControlModifier) {
      cursorLine = static_cast<int>(layoutLines.size()) - 1;
      cursorCol = layoutLines[cursorLine].size();
    } else {
      if (intelligentHomeAndEnd && selection.IsEmpty() && layoutLines[cursorLine].size() > 0 && cursorCol == layoutLines[cursorLine].size()) {
        QString text = document->TextForRange(layoutLines[cursorLine]);
        int c = text.size() - 1;
        for (; c > 0; -- c) {
          if (!IsWhitespace(text[c])) {
            break;
          }
        }
        if (!IsWhitespace(text[c])) {
          cursorCol = c + 1;
        }
      } else {
        cursorCol = layoutLines[cursorLine].size();
      }
    }
    
    EndMovingCursor(shiftHeld);
  } else if (keyCode == Qt::Key_Backspace) {
    DocumentLocation loc = MapCursorToDocument();
    if (!selection.IsEmpty()) {
      InsertText("");
    } else if (loc.offset > 0 && event->modifiers() & Qt::ControlModifier) {
      DocumentRange wordRange = GetWordForCharacter(loc.offset - 1);
      wordRange.end = wordRange.end.Min(loc);
      Document::CharacterIterator charIt(document.get(), wordRange.start.offset - 1);
      if (charIt.IsValid() && charIt.GetChar() == '\n') {
        // Special case: we delete a newline character if it is directly before
        // the word range. This makes deletion of whitespace lines behave better.
        -- wordRange.start;
      }
      SetSelection(wordRange);
      InsertText("");
    } else if (loc.offset > 0) {
      DocumentRange range(loc - 1, loc);
      SetSelection(range);
      InsertText("");
    }
  } else if (keyCode == Qt::Key_Delete) {
    DocumentLocation loc = MapCursorToDocument();
    bool locNotAtEnd = loc < document->FullDocumentRange().end;
    if (!selection.IsEmpty()) {
      InsertText("");
    } else if (locNotAtEnd && event->modifiers() & Qt::ControlModifier) {
      DocumentRange wordRange = GetWordForCharacter(loc.offset);
      wordRange.start = wordRange.start.Max(loc);
      SetSelection(wordRange);
      InsertText("");
    } else if (locNotAtEnd) {
      DocumentRange range(loc, loc + 1);
      SetSelection(range);
      InsertText("");
    }
  } else if (keyCode == Qt::Key_Return) {
    // Get the last character in the previous line
    DocumentLocation loc = MapCursorToDocument();
    Document::CharacterIterator it(document.get(), static_cast<int>(loc.offset) - 1);
    QChar lastCharacterInPrevLine = QChar(0);
    if (it.IsValid()) {
      lastCharacterInPrevLine = it.GetChar();
    }
    
    // Move the iterator to the first character in the line
    while (it.IsValid() && it.GetChar() != QChar('\n')) {
      -- it;
    }
    if (!it.IsValid()) {
      it = Document::CharacterIterator(document.get(), 0);
    } else {
      ++ it;
    }
    
    // Get the whitespace at the start of the line.
    // If shift is held, also include '/' and '*' characters
    int firstWhitespaceOffset = it.GetCharacterOffset();
    while (it.IsValid() &&
           it.GetCharacterOffset() < loc.offset) {
      QChar c = it.GetChar();
      if (!IsWhitespace(c) && (!shiftHeld || (c != '/' && c != '*'))) {
        break;
      }
      ++ it;
    }
    int endWhitespaceOffset = it.GetCharacterOffset();
    QString whitespace;
    if (endWhitespaceOffset > firstWhitespaceOffset) {
      whitespace = document->TextForRange(DocumentRange(firstWhitespaceOffset, endWhitespaceOffset));
    }
    
    // Apply auto-indent
    if (lastCharacterInPrevLine == '{') {
      whitespace += QString(" ").repeated(spacesPerTab);
    }
    
    // Inser the new line together with its initial whitespace determined above
    InsertText("\n" + whitespace);
  } else if (!(event->modifiers() & Qt::ControlModifier)) {
    QString text = event->text();
    if (!text.isEmpty()) {
      bool codeCompletionWasOpen = codeCompletionWidget || codeCompletionInvocationLocation.IsValid();
      
      InsertText(text);
      
      if (text == ' ') {
        CheckForWordCompletion();
      }
      
      // Consider invoking code completion.
      // qDebug() << "Considering code completion invocation. widget:" << codeCompletionWidget
      //          << ", inv-loc is valid:" << codeCompletionInvocationLocation.IsValid()
      //          << ", codeCompletionWasOpen:" << codeCompletionWasOpen;
      if (!(codeCompletionWidget || codeCompletionInvocationLocation.IsValid()) &&
          (!codeCompletionWasOpen || (text[0] == '>') || (text[0] == '(')  || (text[0] == '.')  || (text[0] == '['))) {
        bool invokeCodeCompletion =
            !text[0].isSpace() &&
            text[0] != '\n' &&
            text[0] != '{' &&
            text[0] != '}' &&
            text[0] != ')' &&
            text[0] != ';';
        if (text[0].isDigit()) {
          // Do not invoke completion for numbers
          DocumentLocation cursorLoc = MapCursorToDocument();
          DocumentRange wordRange = GetWordForCharacter(cursorLoc.offset - 1);
          int leftWordPartSize = cursorLoc.offset - wordRange.start.offset;
          QString wordText = document->TextForRange(wordRange);
          bool haveLetter = false;
          for (int c = 0; c < leftWordPartSize; ++ c) {
            if (wordText[c].isLetter()) {
              haveLetter = true;
              break;
            }
          }
          if (!haveLetter) {
            invokeCodeCompletion = false;
          }
        }
        if (invokeCodeCompletion) {
          InvokeCodeCompletion();
        }
      }
    }
  }
}

void DocumentWidget::wheelEvent(QWheelEvent* event) {
  mouseHoverTimer.stop();
  
  QPointF degrees = event->angleDelta() / 8.0;
  QPointF numSteps = degrees / 15.0;
  
  int newXScroll = xScroll - 3 * numSteps.x() * lineHeight;
  newXScroll = std::max(0, newXScroll);
  if (container->GetScrollbar()->isVisible()) {
    newXScroll = std::min(container->GetScrollbar()->maximum(), newXScroll);
  } else {
    newXScroll = 0;
  }
  
  int newYScroll = yScroll - 3 * numSteps.y() * lineHeight;
  newYScroll = std::max(0, newYScroll);
  newYScroll = std::min(GetMaxYScroll(), newYScroll);
  
  if (newXScroll != xScroll &&
      newYScroll != yScroll) {
    SetXYScroll(newXScroll, newYScroll);
  } else if (newXScroll != xScroll) {
    SetXScroll(newXScroll);
  } else if (newYScroll != yScroll) {
    SetYScroll(newYScroll);
  } else {
    return;
  }
  
  CloseTooltip();
}

void DocumentWidget::moveEvent(QMoveEvent* /*event*/) {
  Moved();
}

void DocumentWidget::focusOutEvent(QFocusEvent* /*event*/) {
  CloseAllPopups();
  
  // Hide the cursor
  update(GetCursorRect());
}

void DocumentWidget::showEvent(QShowEvent* /*event*/) {
  if (reparseOnNextActivation) {
    if (isCFile) {
      ParseThreadPool::Instance().RequestParse(document, this, mainWindow);
    }
    reparseOnNextActivation = false;
  }
}

void DocumentWidget::hideEvent(QHideEvent* /*event*/) {
  CloseAllPopups();
}
