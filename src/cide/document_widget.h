// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>

#include <QFrame>
#include <QTimer>
#include <QWidget>

#include "cide/argument_hint_widget.h"
#include "cide/code_completion_widget.h"
#include "cide/document.h"
#include "cide/document_location.h"
#include "cide/document_range.h"
#include "cide/qt_help.h"

class DocumentWidgetContainer;
class MainWindow;
class Problem;
class QLabel;
class QScrollArea;
struct WordCompletion;

// TODO: Move this to a more appropriate place
constexpr int kHighlightLayer = 1;

class DocumentWidget : public QWidget {
 Q_OBJECT
 friend struct FontStyle;
 public:
  DocumentWidget(const std::shared_ptr<Document>& document, DocumentWidgetContainer* container, MainWindow* mainWindow, QWidget* parent = nullptr);
  
  ~DocumentWidget();
  
  void EnsureCursorIsInView(int marginInPixels = 0);
  
  /// Scrolls the view such that the given location is shown in the first line.
  void ScrollTo(const DocumentLocation& location);
  
  /// Sets the cursor to a certain position in the widget (as if clicking this
  /// position).
  void SetCursor(int x, int y, bool addToSelection, bool ensureCursorIsVisible = true);
  void SetCursor(const DocumentLocation& location, bool addToSelection, bool ensureCursorIsVisible = true);
  
  /// Returns the current cursor position, clamped to the line length.
  /// This is where the cursor is displayed.
  /// (The actual cursor position might be at a virtual position beyond the line's end.)
  void GetCursor(int* line, int* column);
  
  /// Wraps Document::Replace(), while additionally adapting the cursor position
  /// to the inserted text.
  void Replace(const DocumentRange& range, const QString& newText, bool createUndoStep = true, Replacement* undoReplacement = nullptr);
  
  /// Replaces all occurrences of @p find with @p replacement.
  void ReplaceAll(const QString& find, const QString& replacement, bool matchCase, bool inSelectionOnly);
  
  /// Inserts the given text into the document (as if typed). This will replace
  /// the current selection (if any) with the text, respectively insert it at
  /// the current cursor position.
  void InsertText(const QString& text, bool forceNewUndoStep = false);
  
  /// Returns the selected text as a QString (i.e., as a single string including
  /// potential newlines).
  QString GetSelectedText() const;
  
  /// Returns the document location of the character at the given widget
  /// position. If there is no character at this position, returns false.
  /// If clamp is true, always returns the last character of the given line,
  /// respectively the last line, if the position is out of bounds. In this
  /// case, the function always returns true. Note that the character may be
  /// invalid then: For empty lines, character 0 is returned. If wasClamped
  /// is non-null, it is set to true in case the position was out-of-bounds
  /// and got clamped.
  bool GetCharacterAt(int x, int y, bool clamp, int* line, int* character, bool* wasClamped = nullptr);
  
  /// Returns the DocumentLocation at which the cursor would be placed when
  /// clicking pixel (x, y). Any of loc, line, and col may be nullptr.
  void GetDocumentLocationAt(int x, int y, DocumentLocation* loc, int* line, int* col);
  
  /// Maps the given document location to a line and column in the current
  /// layout. Returns false in case the location could not be found within the
  /// layout; in this case, nothing is returned.
  bool MapDocumentToLayout(const DocumentLocation& location, int* line, int* col);
  
  /// Returns a valid DocumentLocation for the current cursor position. Note
  /// that this does not modify the cursor position.
  DocumentLocation MapCursorToDocument();
  
  /// Maps the given line and column to the corresponding DocumentLocation.
  DocumentLocation MapLineColToDocumentLocation(int line, int col);
  
  /// Determines the word which the given character is part of and returns its
  /// range.
  DocumentRange GetWordForCharacter(int characterOffset);
  
  /// Sets the current selection to the given range. The cursor will be placed
  /// at the end of the range by default (if placeCursorAtEnd is true). Otherwise,
  /// the cursor will be placed at the start of the range.
  void SetSelection(const DocumentRange& range, bool placeCursorAtEnd = true, bool ensureCursorIsVisible = true);
  
  /// Returns the currently selected range, or an empty range at the cursor
  /// location if no text is selected. This is not const because it may trigger
  /// a re-layout.
  inline DocumentRange GetSelection() {
    if (selection.size() > 0) {
      return selection;
    } else {
      DocumentLocation cursorLoc = MapCursorToDocument();
      return DocumentRange(cursorLoc, cursorLoc);
    }
  }
  
  /// Closes the code completion widget (in case it is open).
  void CloseCodeCompletion();
  
  /// Shows the code completion widget. To be called after the code completion
  /// thread finishes.
  void ShowCodeCompletion(DocumentLocation invocationLocation, std::vector<CompletionItem>&& items, CXCodeCompleteResults* libclangResults);
  
  /// This is called by the CodeInfo class if it discards a code completion request
  /// after it was initially made successfully. This happens when a higher-priority
  /// request is made afterwards, before the code completion request was handled.
  void CodeCompletionRequestWasDiscarded();
  
  /// Updates the code completion widget about a changed cursor location.
  void UpdateCodeCompletion(const DocumentLocation& cursorLoc);
  
  /// Closes the argument hint widget (in case it is open).
  void CloseArgumentHint();
  
  /// Shows the argument hint widget.
  void ShowArgumentHint(DocumentLocation invocationLocation, std::vector<ArgumentHintItem>&& items, int currentParameter);
  
  /// Requests to jump to the libclang cursor referenced at the given document
  /// location, performed in a background thread.
  void GotoReferencedCursor(DocumentLocation invocationLocation);
  
  /// Called by invocations of CompleteRequest::Type::Info requests to report
  /// their results, showing them as a tooltip.
  void SetCodeTooltip(const DocumentRange& tooltipRange, const QString& codeHtml, const QUrl& helpUrl, const std::vector<DocumentRange>& referenceRanges);
  
  inline int GetMaxYScroll() const { return (static_cast<int>(layoutLines.size()) - 1) * lineHeight; }
  
  inline int GetCodeCompletionInvocationCounter() const { return codeCompletionInvocationCounter; }
  
  inline const std::shared_ptr<Document> GetDocument() const { return document; }
  inline DocumentWidgetContainer* GetContainer() const { return container; }
  inline MainWindow* GetMainWindow() const { return mainWindow; }
  
 public slots:
  void HandleMouseMoveEvent();
  
  void ShowRightClickMenu(const QString& clickedCursorUSR, const QString& clickedCursorSpelling, bool cursorHasLocalDefinition, const QString& clickedTokenSpelling, const DocumentRange& clickedTokenRange);
  void RenameClickedItem();
  void RenameItemAtCursor();
  
  void SelectAll();
  
  void Undo();
  void Redo();
  
  void Cut();
  void Copy();
  void Paste();
  
  void ToggleBookmark();
  void JumpToPreviousBookmark();
  void JumpToNextBookmark();
  void RemoveAllBookmarks();
  
  void Comment();
  void Uncomment();
  void UncommentLine(int line);
  
  void FixAll();
  
  void CheckFileType();
  void StartParseTimer();
  void ParseFile();
  void SetReparseOnNextActivation();
  void InvokeCodeCompletion();
  void AcceptCodeCompletion();
  
  /// Checks whether the current selection is on a word or phrase, and if so,
  /// highlights all occurrences of this word/phrase in the document.
  void CheckPhraseHighlight();
  /// Requests to highlight all occurrences of phrase. Might be performed in a
  /// background thread and added asynchronously.
  void RequestPhraseHighlight(const QString& phrase);
  
  void CheckBracketHighlight();
  
  void RemoveHighlights();
  
  void HighlightingChanged();
  
  void MoveCursorLeft(bool shiftHeld, bool controlHeld);  // called when the left arrow key is pressed
  void MoveCursorRight(bool shiftHeld, bool controlHeld);  // called when the right arrow key is pressed
  void MoveCursorUpDown(int direction, bool shiftHeld);  // called when the up / down arrow keys are pressed
  
  void BlinkCursor();
  
  /// Sets both scroll values.
  void SetXYScroll(int x, int y);
  
  /// Sets the horizontal scroll value (xScroll).
  void SetXScroll(int value);
  
  /// Returns the horizontal scroll value (xScroll).
  inline int GetXScroll() const { return xScroll; }
  
  /// Sets the vertical scroll value (yScroll).
  void SetYScroll(int value);
  
  /// Returns the vertical scroll value (yScroll).
  inline int GetYScroll() const { return yScroll; }
  
  inline int GetLineHeight() const { return lineHeight; }
  inline int GetCharWidth() const { return charWidth; }
  
  void ShowDocumentationInDock();
  
  /// Notifies the widget that it was moved. This allows dependent tooltip
  /// widets to move along with the document widget.
  void Moved();
  
  void FontChanged();
  
 signals:
  void CursorMoved(int line, int col);
  
 protected:
  struct FixitButton {
    std::shared_ptr<Problem> problem;
    int fixitIndex;
    QRect buttonRect;
  };
  
  /// Checks whether the layout needs to be re-computed and does that in this case.
  /// Returns true if the layout has been re-computed, false otherwise.
  bool CheckRelayout();
  
  /// Returns the area taken up by the insertion cursor in widget coordinates
  /// (i.e., accounting for the current scroll position).
  QRect GetCursorRect();
  void ResetCursorBlinkPhase();
  
  QRect GetTextRect(const DocumentRange& range);
  
  QRect GetLineRect(int line);
  
  /// Returns the text width as displayed in the widget (this allows to account
  /// for different tab size settings).
  int GetTextWidth(const QString& text, int startColumn, int* numColumns);
  int GetTextWidth(QChar text, int column, int* numColumns);
  
  /// Maps the given line and column in the current layout to a document location.
  DocumentLocation MapLayoutToDocument(int line, int col);
  
  /// Sets the cursor to the given document location.
  /// TODO: Rename to make the difference to the SetCursor() functions clear?
  void SetCursorTo(const DocumentLocation& location);
  
  void StartMovingCursor();
  void EndMovingCursor(bool addToSelection, bool preserveSelection = false, bool ensureCursorIsVisible = true);
  
  void UpdateScrollbar();
  
  void ApplyFixIt(const std::shared_ptr<Problem>& problem, int fixitIndex, bool ignoreDocumentVersion);
  
  std::vector<std::shared_ptr<Problem>> GetHoveredProblems();
  
  /// Updates the data for the tooltip:
  /// * If hoveredProblems is not nullptr, updates the list of hovered problems.
  /// * If codeHtml is not nullptr, updates the HTML string and help URL for the hovered code.
  /// * Updates the information about removed code. hoveredDiff == nullptr means that no line diff is hovered.
  void UpdateTooltip(const QPoint& cursorPos, const std::vector<std::shared_ptr<Problem>>* hoveredProblems, const QString* codeHtml, const QUrl* helpUrl, const LineDiff* hoveredDiff);
  
  /// Tries to resize the tooltip to its contents and keep it within the screen.
  void ResizeTooltipToContents(bool allowShrinking);
  
  /// Closes all kinds of popups that could possibly be shown for this DocumentWidget.
  void CloseAllPopups();
  
  /// Closes the tooltip (in case it is visible).
  void CloseTooltip();
  
  /// Given the range of text between the argument hint invocation location and
  /// the current cursor position, decides whether to update the current parameter
  /// that is highlighted in the argument hint widget, or to close the widget.
  void UpdateArgumentHintWidget(const DocumentRange& range, bool afterwards);
  
  void TabPressed(bool shiftHeld);
  
  void BookmarksChanged();
  
  void CheckForWordCompletion();
  /// Tries to determine whether the cursor is likely in a 'code' section rather
  /// than within a comment or a string. This is checked heuristically and may be wrong.
  bool IsCursorLikelyInCodeSection();
  void ApplyWordCompletion(const WordCompletion& item);
  
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  bool event(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void moveEvent(QMoveEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent* event) override;
  
  
  std::unique_ptr<QFontMetrics> fontMetrics;
  int lineHeight = 0;
  int charWidth = 0;
  
  int xScroll = 0;
  int yScroll = 0;
  
  // Cursor. Note that this may be in "invalid" positions, for example past the
  // end of lines. It is reset to the next valid position on edits with
  // MapCursorToDocument().
  /// Cursor line.
  int cursorLine = 0;
  /// Cursor column.
  int cursorCol = 0;
  
  // Document layout.
  bool haveLayout = false;
  int layoutVersion;
  std::vector<DocumentRange> layoutLines;
  int maxTextWidth = 0;
  
  // Icons for inline problem display.
  QImage warningIcon;
  QImage errorIcon;
  
  // Fix-it buttons.
  /// The document version for which the fix-it buttons were generated.
  int fixitButtonsDocumentVersion;
  /// List of fix-it buttons. Those are generated on-the-fly during paintEvent().
  std::vector<FixitButton> fixitButtons;
  
  // Code / Problem tooltip.
  std::vector<std::shared_ptr<Problem>> tooltipProblems;
  QFrame* tooltipProblemsFrame;
  
  QRect codeInfoRequestRect;
  QString tooltipCodeHtml;
  QLabel* tooltipCodeHtmlLabel;
  QScrollArea* tooltipCodeHtmlScrollArea;
  QRect tooltipCodeRect;
  
  QFrame* tooltipHelpFrame = nullptr;
  QUrl tooltipHelpUrl;
  HelpBrowser* tooltipHelpBrowser = nullptr;
  
  const LineDiff* tooltipLineDiff = nullptr;
  QLabel* tooltipDiffLabel = nullptr;
  
  QFrame* tooltip = nullptr;
  QPoint tooltipCreationPos;
  bool showCodeInfoInExistingWidget = false;
  
  // Right-click menu.
  QPoint lastRightClickPoint;
  QString rightClickedCursorUSR;
  QString rightClickedCursorSpelling;
  bool rightClickedCursorHasLocalDefinition;
  QString rightClickedTokenSpelling;
  DocumentRange rightClickedTokenRange;
  QMenu* rightClickMenu;
  QAction* renameClickedItemAction;
  bool renameRequested = false;
  
  // Code completion.
  CodeCompletionWidget* codeCompletionWidget = nullptr;
  DocumentLocation codeCompletionInvocationLocation = DocumentLocation::Invalid();
  /// Counter that can be used by the completion thread to identify cases where
  /// its completion results are outdated.
  int codeCompletionInvocationCounter = 0;
  
  // Argument hint.
  ArgumentHintWidget* argumentHintWidget = nullptr;
  DocumentLocation argumentHintInvocationLocation = DocumentLocation::Invalid();
  /// The current parameter within the widget at the time of invocation (i.e.,
  /// matching @a argumentHintInvocationLocation).
  int argumentInvocationCurrentParameter;
  
  // Mouse move event buffering.
  bool haveMouseMoveEvent = false;
  QPoint lastMouseMoveEventPos;
  QPoint lastMouseMoveEventGlobalPos;
  Qt::MouseButtons lastMouseMoveEventButtons;
  
  // Settings. TODO: Make configurable.
  bool intelligentHomeAndEnd = true;
  const int sidebarWidth = 5;
  
  bool movingCursor = false;
  DocumentLocation movingCursorOldLocation;
  QRect movingCursorOldRect;
  
  DocumentRange selection = DocumentRange::Invalid();
  DocumentLocation preSelectionCursor;
  int selectionDoubleClickOffset;
  
  bool cursorBlinkState = true;
  QTimer* cursorBlinkTimer;
  int cursorBlinkInterval = 500;
  
  int spacesPerTab = 2;
  
  bool isCFile = false;
  bool isGLSLFile = false;
  QTimer* parseTimer;
  bool reparseOnNextActivation = false;
  
  QTimer mouseHoverTimer;
  QPoint mouseHoverPosGlobal;
  QPoint mouseHoverPosLocal;
  
  std::shared_ptr<Document> document;
  
  DocumentWidgetContainer* container;
  MainWindow* mainWindow;
};
