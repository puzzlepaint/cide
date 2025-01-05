// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/document_widget_container.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QValidator>

#include "cide/main_window.h"
#include "cide/scroll_bar_minimap.h"
#include "cide/settings.h"

class EscapeSignalingLineEdit : public QLineEdit {
 Q_OBJECT
 signals:
  void EscapePressed();
  void ReturnPressed(bool ctrlHeld);
  
 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Escape) {
      emit EscapePressed();
      event->accept();
      return;
    } else if (event->key() == Qt::Key_Return) {
      emit ReturnPressed(event->modifiers() & Qt::ControlModifier);
      event->accept();
      return;
    }
    
    QLineEdit::keyPressEvent(event);
  }
};


DocumentWidgetContainer::DocumentWidgetContainer(const std::shared_ptr<Document>& document, MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent) {
  connect(document.get(), &Document::FileChangedExternally, this, &DocumentWidgetContainer::FileChangedExternally);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  
  // Message labels
  static const QColor messageColors[static_cast<int>(MessageType::HighestMessageType) + 1] = {
    qRgb(255, 255, 80),
    qRgb(255, 80, 80),
    qRgb(150, 150, 255)
  };
  mMessageLabels.resize(static_cast<int>(MessageType::HighestMessageType) + 1);
  for (int i = 0; i < mMessageLabels.size(); ++ i) {
    mMessageLabels[i] = new QLabel();
    QPalette palette = mMessageLabels[i]->palette();
    palette.setColor(QPalette::ColorRole::Background, messageColors[i]);
    mMessageLabels[i]->setPalette(palette);
    mMessageLabels[i]->setAutoFillBackground(true);
    mMessageLabels[i]->setVisible(false);
    layout->addWidget(mMessageLabels[i], 0);
  }
  
  QHBoxLayout* documentAndMinimapLayout = new QHBoxLayout();
  documentAndMinimapLayout->setContentsMargins(0, 0, 0, 0);
  documentAndMinimapLayout->setSpacing(0);
  
  QVBoxLayout* documentAndScrollbarLayout = new QVBoxLayout();
  documentAndScrollbarLayout->setContentsMargins(0, 0, 0, 0);
  documentAndScrollbarLayout->setSpacing(0);
  
  // Document widget
  mDocumentWidget = new DocumentWidget(document, this, mainWindow, this);
  connect(mDocumentWidget, &DocumentWidget::CursorMoved, this, &DocumentWidgetContainer::DocumentCursorMoved);
  
  // Horizontal scrollbar
  scrollbar = new QScrollBar(Qt::Horizontal);
  connect(scrollbar, &QScrollBar::valueChanged, mDocumentWidget, &DocumentWidget::SetXScroll);
  
  documentAndScrollbarLayout->addWidget(mDocumentWidget, 1);
  documentAndScrollbarLayout->addWidget(scrollbar);
  
  // Scrollbar mini-map
  constexpr int scrollBarWidthInPixels = 80;  // TODO: Make configurable?
  minimap = new ScrollbarMinimap(document, mDocumentWidget, scrollBarWidthInPixels);
  
  documentAndMinimapLayout->addLayout(documentAndScrollbarLayout, 1);
  documentAndMinimapLayout->addWidget(minimap);
  layout->addLayout(documentAndMinimapLayout, 1);
  
  // "Go to line" bar
  gotoLineContainer = new QWidget();
  QHBoxLayout* gotoLineLayout = new QHBoxLayout();
  gotoLineLayout->setContentsMargins(0, 0, 0, 0);
  
  QPushButton* closeGotoLineButton = new QPushButton(tr("X"));
  MinimizeButtonSize(closeGotoLineButton, 2);
  connect(closeGotoLineButton, &QPushButton::clicked, this, &DocumentWidgetContainer::CloseGotoLineBar);
  gotoLineLayout->addWidget(closeGotoLineButton);
  
  QLabel* gotoLineLabel = new QLabel(tr("Go to line: "));
  gotoLineLayout->addWidget(gotoLineLabel);
  
  gotoLineEdit = new EscapeSignalingLineEdit();
  connect(gotoLineEdit, &EscapeSignalingLineEdit::EscapePressed, this, &DocumentWidgetContainer::CloseGotoLineBar);
  connect(gotoLineEdit, &EscapeSignalingLineEdit::ReturnPressed, this, &DocumentWidgetContainer::ApplyGotoLine);
  connect(gotoLineEdit, &QLineEdit::textChanged, this, &DocumentWidgetContainer::GotoLineChanged);
  gotoLineLayout->addWidget(gotoLineEdit);
  gotoLineEdit->setValidator(new QIntValidator(gotoLineEdit));
  
  gotoLineButton = new QPushButton(tr("Go"));
  connect(gotoLineButton, &QPushButton::clicked, this, &DocumentWidgetContainer::ApplyGotoLine);
  gotoLineLayout->addWidget(gotoLineButton);
  
  gotoLineContainer->setLayout(gotoLineLayout);
  gotoLineContainer->setVisible(false);
  layout->addWidget(gotoLineContainer, 0);
  
  // "Find" / "Replace" bar
  findReplaceContainer = new QWidget();
  QVBoxLayout* findReplaceLayout = new QVBoxLayout();
  findReplaceLayout->setContentsMargins(0, 0, 0, 0);
  
  QHBoxLayout* findLayout = new QHBoxLayout();
  findLayout->setContentsMargins(0, 0, 0, 0);
  
  QPushButton* closeFindReplaceButton = new QPushButton(tr("X"));
  MinimizeButtonSize(closeFindReplaceButton, 2);
  connect(closeFindReplaceButton, &QPushButton::clicked, this, &DocumentWidgetContainer::CloseFindReplaceBar);
  findLayout->addWidget(closeFindReplaceButton);
  
  QLabel* findLabel = new QLabel(tr("Find: "));
  findLayout->addWidget(findLabel);
  
  findEdit = new EscapeSignalingLineEdit();
  connect(findEdit, &EscapeSignalingLineEdit::EscapePressed, this, &DocumentWidgetContainer::CloseFindReplaceBar);
  connect(findEdit, &EscapeSignalingLineEdit::ReturnPressed, this, &DocumentWidgetContainer::FindNext);
  connect(findEdit, &QLineEdit::textChanged, this, &DocumentWidgetContainer::FindTextChanged);
  findLayout->addWidget(findEdit);
  
  findNextButton = new QPushButton(tr("\\/"));
  MinimizeButtonSize(findNextButton, 2);
  connect(findNextButton, &QPushButton::clicked, this, &DocumentWidgetContainer::FindNext);
  findLayout->addWidget(findNextButton);
  
  findPreviousButton = new QPushButton(tr("/\\"));
  MinimizeButtonSize(findPreviousButton, 2);
  connect(findPreviousButton, &QPushButton::clicked, this, &DocumentWidgetContainer::FindPrevious);
  findLayout->addWidget(findPreviousButton);
  
  findMatchCase = new QCheckBox(tr("Match case"));
  connect(findMatchCase, &QCheckBox::stateChanged, this, &DocumentWidgetContainer::FindTextChanged);
  findLayout->addWidget(findMatchCase);
  
  findReplaceLayout->addLayout(findLayout);
  
  replaceContainer = new QWidget();
  QHBoxLayout* replaceLayout = new QHBoxLayout();
  replaceLayout->setContentsMargins(0, 0, 0, 0);
  
  int spacing = (findLayout->spacing() != -1) ? findLayout->spacing() : qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing);
  replaceLayout->addSpacing(closeFindReplaceButton->width() + spacing);
  
  QLabel* replaceLabel = new QLabel(tr("Replace: "));
  replaceLayout->addWidget(replaceLabel);
  
  replaceEdit = new EscapeSignalingLineEdit();
  connect(replaceEdit, &EscapeSignalingLineEdit::EscapePressed, this, &DocumentWidgetContainer::CloseFindReplaceBar);
  connect(replaceEdit, &EscapeSignalingLineEdit::ReturnPressed, this, &DocumentWidgetContainer::ReplaceReturnPressed);
  replaceLayout->addWidget(replaceEdit);
  
  replaceButton = new QPushButton(tr("Replace"));
  connect(replaceButton, &QPushButton::clicked, this, &DocumentWidgetContainer::Replace);
  replaceLayout->addWidget(replaceButton);
  
  replaceAllButton = new QPushButton(tr("Replace All (Ctrl+Return)"));
  connect(replaceAllButton, &QPushButton::clicked, this, &DocumentWidgetContainer::ReplaceAll);
  replaceLayout->addWidget(replaceAllButton);
  
  replaceInSelectionOnly = new QCheckBox(tr("In selection only"));
  replaceLayout->addWidget(replaceInSelectionOnly);
  
  replaceContainer->setLayout(replaceLayout);
  findReplaceLayout->addWidget(replaceContainer);
  findReplaceContainer->setLayout(findReplaceLayout);
  findReplaceContainer->setVisible(false);
  layout->addWidget(findReplaceContainer, 0);
  
  // Keyboard shortcuts
  QAction* findNextAction = new ActionWithConfigurableShortcut(tr("Find next"), findNextShortcut, this);
  connect(findNextAction, &QAction::triggered, this, &DocumentWidgetContainer::FindNext);
  addAction(findNextAction);
  
  QAction* findPreviousAction = new ActionWithConfigurableShortcut(tr("Find previous"), findPreviousShortcut, this);
  connect(findPreviousAction, &QAction::triggered, this, &DocumentWidgetContainer::FindPrevious);
  addAction(findPreviousAction);
  
  setLayout(layout);
  
  setTabOrder(findEdit, replaceEdit);
  setTabOrder(replaceEdit, replaceButton);
  setTabOrder(replaceButton, replaceAllButton);
  setTabOrder(replaceAllButton, findMatchCase);
  setTabOrder(findMatchCase, replaceInSelectionOnly);
}

void DocumentWidgetContainer::SetMessage(MessageType type, const QString& message) {
  QLabel* messageLabel = mMessageLabels[static_cast<int>(type)];
  if (message.isEmpty()) {
    messageLabel->hide();
  } else {
    messageLabel->show();
    messageLabel->setText(message);
  }
}

void DocumentWidgetContainer::ShowGotoLineBar() {
  CloseFindReplaceBar();
  
  gotoLineContainer->setVisible(true);
  gotoLineEdit->selectAll();
  gotoLineEdit->setFocus();
}

void DocumentWidgetContainer::CloseGotoLineBar() {
  if (gotoLineContainer->isVisible()) {
    gotoLineContainer->setVisible(false);
    mDocumentWidget->setFocus();
  }
}

void DocumentWidgetContainer::ApplyGotoLine() {
  bool ok;
  int line = gotoLineEdit->text().toInt(&ok) - 1;
  if (ok) {
    ok = line >= 0 && line < mDocumentWidget->GetDocument()->LineCount();
    if (ok) {
      DocumentRange lineRange = mDocumentWidget->GetDocument()->GetRangeForLine(line);
      if (lineRange.IsValid()) {
        CloseGotoLineBar();
        mDocumentWidget->SetCursor(lineRange.start, false);
      }
    }
  }
}

void DocumentWidgetContainer::ShowFindBar() {
  CloseGotoLineBar();
  
  // TODO: How to handle findReplaceStartRange on edits to the document? Transform through these edits? Make an interface in the document to register such external ranges that need to be transformed?
  // TODO: Similarly, do we need to update findReplaceStartRange when the user changes the document selection?
  findReplaceStartRange = mDocumentWidget->GetSelection();
  replaceContainer->setVisible(false);
  findReplaceContainer->setVisible(true);
  findEdit->setText(mDocumentWidget->GetSelectedText());
  findEdit->selectAll();
  findEdit->setFocus();
  findMatchCase->setChecked(false);
}

void DocumentWidgetContainer::CloseFindReplaceBar() {
  if (findReplaceContainer->isVisible()) {
    findReplaceContainer->setVisible(false);
    mDocumentWidget->setFocus();
  }
}

void DocumentWidgetContainer::FindPrevious() {
  // Find the previous occurrence starting from the document selection, select it,
  // and move findReplaceStartRange to it.
  FindImpl(true, false, true);
}

void DocumentWidgetContainer::FindNext() {
  // Find the next occurrence starting from the document selection, select it,
  // and move findReplaceStartRange to it.
  FindImpl(true, true, true);
}

void DocumentWidgetContainer::FindTextChanged() {
  bool haveFindText = !findEdit->text().isEmpty();
  findNextButton->setEnabled(haveFindText);
  findPreviousButton->setEnabled(haveFindText);
  replaceButton->setEnabled(haveFindText);
  replaceAllButton->setEnabled(haveFindText);
  
  if (!doNotSearchOnTextChange && !replaceContainer->isVisible()) {
    // Find the next occurrence starting from findReplaceStartRange, select it,
    // but do not move findReplaceStartRange to it.
    FindImpl(false, true, false);
  }
}

void DocumentWidgetContainer::ShowReplaceBar() {
  doNotSearchOnTextChange = true;
  ShowFindBar();
  replaceContainer->setVisible(true);
  findMatchCase->setChecked(true);
  doNotSearchOnTextChange = false;
  
  // Check replaceInSelectionOnly if there is a multi-line selection
  if (mDocumentWidget->GetSelection().IsEmpty()) {
    replaceInSelectionOnly->setChecked(false);
  } else {
    int startLine, startCol;
    int endLine, endCol;
    if (mDocumentWidget->MapDocumentToLayout(mDocumentWidget->GetSelection().start, &startLine, &startCol) &&
        mDocumentWidget->MapDocumentToLayout(mDocumentWidget->GetSelection().end, &endLine, &endCol)) {
      replaceInSelectionOnly->setChecked(startLine != endLine);
    } else {
      replaceInSelectionOnly->setChecked(false);
    }
  }
}

void DocumentWidgetContainer::ReplaceReturnPressed(bool ctrlHeld) {
  if (ctrlHeld) {
    ReplaceAll();
  } else {
    Replace();
  }
}

void DocumentWidgetContainer::Replace() {
  Qt::CaseSensitivity caseSensitivity = findMatchCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
  if (mDocumentWidget->GetSelectedText().compare(findEdit->text(), caseSensitivity) == 0) {
    // The current selection contains the search string, replace it
    mDocumentWidget->InsertText(replaceEdit->text());
    findReplaceStartRange = mDocumentWidget->GetSelection();
  }
  
  FindNext();
}

void DocumentWidgetContainer::ReplaceAll() {
  mDocumentWidget->ReplaceAll(findEdit->text(), replaceEdit->text(), findMatchCase->isChecked(), replaceInSelectionOnly->isChecked());
}

void DocumentWidgetContainer::DocumentCursorMoved() {
  if (replaceContainer->isVisible()) {
    replaceInSelectionOnly->setChecked(!mDocumentWidget->GetSelection().IsEmpty());
  }
}

void DocumentWidgetContainer::FileChangedExternally() {
  auto& document = mDocumentWidget->GetDocument();
  QFile file(document->path());
  
  if (!file.open(QIODevice::ReadOnly)) {
    // TODO: Should we watch the document's containing directory in this case, and act if a new file gets created at the document's path in the future?
    //       In particular, if the new file's content ends up being the same as the document's content, we could drop the file-deleted notification again,
    //       analogous to the case below where we avoid showing the file-modified notification.
    SetMessage(MessageType::ExternalModificationNotification, tr("This file has been deleted externally."));
  } else {
    // Sometimes, external applications trigger the file-changed signal without actually changing the file's contents.
    // In case the user does not have unsaved changes, detect these cases automatically to prevent showing
    // the file-modified notification then.
    // TODO: If the user does have unsaved changes, we should still do this, but compare the file contents from disk to the last saved version from the document.
    if (!document->HasUnsavedChanges()) {
      QString text = document->TextForRange(document->FullDocumentRange());
      if (document->newlineFormat() == NewlineFormat::CrLf) {
        text.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
      }
      QByteArray utf8Data = text.toUtf8();  // TODO: Support other encodings than UTF-8 only
      
      QByteArray readUtf8Data(utf8Data.size() + 1, 0);
      if (file.read(readUtf8Data.data(), readUtf8Data.size()) == utf8Data.size() &&
          memcmp(readUtf8Data.data(), utf8Data.data(), utf8Data.size()) == 0) {
        // The new file content is equal to the old file content.
        // qDebug() << "Ignoring file change since the contents are equal to the document contents";
        return;
      }
    }
    
    SetMessage(MessageType::ExternalModificationNotification, tr("This file has been modified externally."));
  }
}

void DocumentWidgetContainer::GotoLineChanged(const QString& text) {
  bool ok = false;
  if (!text.isEmpty()) {
    int line = text.toInt(&ok) - 1;
    if (ok) {
      ok = line >= 0 && line < mDocumentWidget->GetDocument()->LineCount();
    }
  }
  
  gotoLineButton->setEnabled(ok);
  
  QPalette palette = gotoLineEdit->palette();
  palette.setColor(QPalette::Base, (ok || text.isEmpty()) ? Qt::white : Qt::red);
  gotoLineEdit->setPalette(palette);
}

void DocumentWidgetContainer::FindImpl(bool startFromSelection, bool forwards, bool updateSearchStart) {
  const QString& findText = findEdit->text();
  if (findText.isEmpty() && findReplaceContainer->isVisible()) {
    mDocumentWidget->SetSelection(findReplaceStartRange);
    return;
  }
  
  DocumentRange startRange = startFromSelection ? mDocumentWidget->GetSelection() : findReplaceStartRange;
  
  DocumentLocation result;
  if (forwards) {
    result = mDocumentWidget->GetDocument()->Find(findText, startRange.end, true, findMatchCase->isChecked());
    if (result.IsInvalid()) {
      // Try again from the start of the document for wrap-around.
      // TODO: Speed this up by only searching up to startRange.end.
      result = mDocumentWidget->GetDocument()->Find(findText, 0, true, findMatchCase->isChecked());
      if (result.IsValid() && result >= startRange.end) {
        result = DocumentLocation::Invalid();
      }
    }
  } else {
    result = mDocumentWidget->GetDocument()->Find(findText, startRange.start, false, findMatchCase->isChecked());
    if (result.IsInvalid()) {
      // Try again from the end of the document for wrap-around.
      // TODO: Speed this up by only searching up to startRange.start.
      result = mDocumentWidget->GetDocument()->Find(findText, mDocumentWidget->GetDocument()->FullDocumentRange().end, false, findMatchCase->isChecked());
      if (result.IsValid() && result <= startRange.start) {
        result = DocumentLocation::Invalid();
      }
    }
  }
  
  if (result.IsInvalid()) {
    // Text not found.
    QPalette palette = findEdit->palette();
    palette.setColor(QPalette::Base, Qt::red);
    findEdit->setPalette(palette);
    
    DocumentLocation cursorLoc = mDocumentWidget->MapCursorToDocument();
    mDocumentWidget->SetSelection(DocumentRange(cursorLoc, cursorLoc));
    return;
  }
  
  // Text found.
  QPalette palette = findEdit->palette();
  palette.setColor(QPalette::Base, Qt::white);
  findEdit->setPalette(palette);
  
  DocumentRange foundRange = DocumentRange(result, result + findText.size());
  mDocumentWidget->SetSelection(foundRange);
  
  if (updateSearchStart) {
    findReplaceStartRange = foundRange;
  }
}

#include "document_widget_container.moc"
