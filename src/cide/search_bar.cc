// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/search_bar.h"

#include <QApplication>
#include <QDebug>
#include <QKeyEvent>
#include <QPainter>

#include "cide/main_window.h"
#include "cide/clang_parser.h"
#include "cide/clang_utils.h"
#include "cide/search_list_widget.h"
#include "cide/settings.h"

SearchBar::SearchBar(MainWindow* mainWindow, QWidget *parent)
    : QLineEdit(parent),
      mode(Mode::LocalContexts),
      mainWindow(mainWindow) {
  setFont(Settings::Instance().GetDefaultFont());
  
  mListWidget = new SearchListWidget(this);
  
  connect(this, &SearchBar::textChanged, this, &SearchBar::SearchTextChanged);
}

SearchBar::~SearchBar() {
  delete mListWidget;
}

void SearchBar::SetMode(SearchBar::Mode mode) {
  this->mode = mode;
  if (hasFocus()) {
    ComputeItems();
  }
}

void SearchBar::ShowListWidget() {
  ComputeItems();
  if (mListWidget->HasItems()) {
    mListWidget->show();
    if (!text().isEmpty()) {
      mListWidget->SetFilterText(text());
    }
  }
}

void SearchBar::CloseListWidget() {
  mListWidget->hide();
}

void SearchBar::Moved() {
  if (mListWidget->isVisible()) {
    mListWidget->Relayout();
  }
}

void SearchBar::CurrentDocumentChanged() {
  GetCurrentContext();
}

void SearchBar::CursorMoved() {
  GetCurrentContext();
}

void SearchBar::SearchTextChanged() {
  const QString& text = this->text();
  
  mListWidget->SetFilterText(text);
  if (mListWidget->HasItems()) {
    mListWidget->show();
  }
}

void SearchBar::focusInEvent(QFocusEvent* event) {
  QLineEdit::focusInEvent(event);
  
  if (event->reason() == Qt::MouseFocusReason) {
    // Calling SetMode() here already computes the items, since hasFocus()
    // returns true here.
    SetMode(mainWindow->GetCurrentDocumentWidget() ? Mode::LocalContexts : Mode::Files);
  } else {
    ComputeItems();
  }
  if (mListWidget->HasItems()) {
    mListWidget->show();
  }
}

void SearchBar::focusOutEvent(QFocusEvent* event) {
  QLineEdit::focusOutEvent(event);
  
  clear();
  CloseListWidget();
}

void SearchBar::paintEvent(QPaintEvent *event) {
  QLineEdit::paintEvent(event);
  
  // Draw the current context information if the search bar does not have focus
  if (text().isEmpty() && !hasFocus()) {
    QPainter painter(this);
    painter.setPen(qRgb(50, 50, 50));
    painter.setFont(Settings::Instance().GetDefaultFont());
    bool usingBoldFont = false;
    int charWidth = painter.fontMetrics()./*horizontalAdvance*/ width(' ');
    
    constexpr int leftMargin = 7;  // TODO: This was read off from a screenshot. How to find this in a portable way?
    int xCoord = leftMargin;
    for (int c = 0; c < currentContexts.size(); ++ c) {
      if (xCoord >= width()) {
        break;
      }
      bool bold = false;
      for (const DocumentRange& range : currentContextBoldRanges) {
        bold = range.ContainsCharacter(c);
        if (bold) {
          break;
        }
      }
      if (bold != usingBoldFont) {
        painter.setFont(bold ? Settings::Instance().GetBoldFont() : Settings::Instance().GetDefaultFont());
        usingBoldFont = bold;
      }
      painter.drawText(QRect(xCoord, 0, charWidth, height()),
                       Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                       currentContexts.at(c));
      xCoord += charWidth;
    }
    painter.end();
  }
}

void SearchBar::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    mListWidget->hide();
    QWidget* currentDocumentWidget = GetMainWindow()->GetCurrentDocumentWidget();
    if (currentDocumentWidget) {
      currentDocumentWidget->setFocus();
    }
    event->accept();
    return;
  }
  
  if (mListWidget->isVisible() &&
      (event->key() == Qt::Key_Up ||
       event->key() == Qt::Key_Down ||
       event->key() == Qt::Key_Return)) {
    QApplication::sendEvent(mListWidget, event);
    if (event->isAccepted()) {
      return;
    }
  }
  
  QLineEdit::keyPressEvent(event);
}

void SearchBar::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton &&
      hasFocus() &&
      !mListWidget->isVisible()) {
    ShowListWidget();
  }
  
  QLineEdit::mousePressEvent(event);
}

void SearchBar::GetCurrentContext() {
  QString oldContexts = currentContexts;
  currentContexts = QStringLiteral("");
  currentContextBoldRanges.clear();
  
  DocumentWidget* widget = mainWindow->GetCurrentDocumentWidget();
  if (widget) {
    std::shared_ptr<Document> document = widget->GetDocument();
    std::vector<Context> contextStack = document->GetContextsAt(widget->MapCursorToDocument());
    currentContexts = QStringLiteral("");
    for (const auto& context : contextStack) {
      if (!currentContexts.isEmpty()) {
        currentContexts += QStringLiteral(", ");
      }
      if (context.nameInDescriptionRange.IsValid()) {
        currentContextBoldRanges.push_back(DocumentRange(
            context.nameInDescriptionRange.start + currentContexts.size(),
            context.nameInDescriptionRange.end + currentContexts.size()));
      }
      currentContexts += context.description;
    }
  }
  
  if (currentContexts.isEmpty()) {
    currentContexts = tr("(no context)");
  }
  
  if (currentContexts != oldContexts) {
    update(rect());
  }
}

void SearchBar::ComputeItems() {
  std::vector<SearchListItem> items;
  
  DocumentWidget* widget = mainWindow->GetCurrentDocumentWidget();
  if (widget) {
    std::shared_ptr<Document> document = widget->GetDocument();
    
    // List the contexts in the current file?
    if (mode == Mode::LocalContexts) {
      const auto& contexts = document->GetContexts();
      for (const auto& context : contexts) {
        items.emplace_back(SearchListItem::Type::LocalContext, context.description, context.name);
        items.back().displayTextBoldRange = context.nameInDescriptionRange;
        items.back().jumpLocation = context.range.start;
      }
    }
  }
  
  // List the project files?
  if (mode == Mode::Files) {
    std::unordered_set<QString> addedPaths;
    auto addPath = [&](const QString& path) {
      if (addedPaths.count(path) == 0) {
        addedPaths.insert(path);
        items.emplace_back(SearchListItem::Type::ProjectFile, path, path);
        items.back().displayTextBoldRange = DocumentRange::Invalid();
      }
    };
    
    for (const auto& project : mainWindow->GetProjects()) {
      QDir projectDir = QFileInfo(project->GetYAMLFilePath()).dir();
      
      int numTargets = project->GetNumTargets();
      for (int targetIndex = 0; targetIndex < numTargets; ++ targetIndex) {
        const Target& target = project->GetTarget(targetIndex);
        for (const SourceFile& source : target.sources) {
          addPath(source.path);
          
          for (const QString& includedPath : source.includedPaths) {
            // Do not include external headers.
            // TODO: Maybe these could be included as well as an option.
            if (!includedPath.startsWith(projectDir.path())) {
              continue;
            }
            
            addPath(includedPath);
          }
        }
      }
    }
    
    // TODO: Include an item to open non-project files or create new files
  }
  
  // List global symbols?
  if (mode == Mode::GlobalSymbols) {
    USRStorage::Instance().Lock();
    
    const auto& USRs = USRStorage::Instance().GetAllUSRs();
    for (auto fileIt = USRs.begin(), endFileIt = USRs.end(); fileIt != endFileIt; ++ fileIt) {
      for (auto usrIt = fileIt->second->map.begin(), endUsrIt = fileIt->second->map.end(); usrIt != endUsrIt; ++ usrIt) {
        if (usrIt->second.namePos < 0) {
          continue;
        }
        bool isClassDeclLike = IsClassDeclLikeCursorKind(usrIt->second.kind);
        if (!isClassDeclLike && !IsFunctionDeclLikeCursorKind(usrIt->second.kind)) {
          continue;
        }
        if (isClassDeclLike && !usrIt->second.isDefinition) {
          continue;
        }
        
        items.emplace_back(
            SearchListItem::Type::GlobalSymbol,
            QObject::tr("%1 at %2:%3:%4").arg(usrIt->second.spelling).arg(fileIt->first).arg(usrIt->second.line).arg(usrIt->second.column),
            usrIt->second.spelling.mid(usrIt->second.namePos, usrIt->second.nameSize));
        // if (usrIt->second.namePos >= 0) {
          items.back().displayTextBoldRange = DocumentRange(usrIt->second.namePos, usrIt->second.namePos + usrIt->second.nameSize);
        // } else {
        //   items.back().displayTextBoldRange = DocumentRange::Invalid();
        // }
      }
    }
    
    USRStorage::Instance().Unlock();
  }
  
  mListWidget->SetItems(std::move(items));
  mListWidget->Relayout();
}
