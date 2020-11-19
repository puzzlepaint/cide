// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/build_target_selector.h"

#include <QApplication>
#include <QPainter>

#include "cide/build_target_list_widget.h"
#include "cide/main_window.h"
#include "cide/settings.h"

BuildTargetSelector::BuildTargetSelector(MainWindow* mainWindow, QWidget* parent)
    : QLineEdit(parent),
      mainWindow(mainWindow) {
  setFont(Settings::Instance().GetDefaultFont());
  setSizePolicy(QSizePolicy::Minimum, sizePolicy().verticalPolicy());
  
  mTargetListWidget = new BuildTargetListWidget(this);
  connect(mTargetListWidget, &QListWidget::itemChanged, this, &BuildTargetSelector::ListSelectionChanged);
  
  connect(this, &BuildTargetSelector::textChanged, this, &BuildTargetSelector::TextChanged);
}

BuildTargetSelector::~BuildTargetSelector() {
  delete mTargetListWidget;
}

void BuildTargetSelector::ClearTargets() {
  mTargetListWidget->clear();
}

void BuildTargetSelector::AddTarget(const QString& targetName, bool selected) {
  mTargetListWidget->AddBuildTarget(targetName, selected);
}

QStringList BuildTargetSelector::GetSelectedTargets() {
  QStringList result;
  for (int i = 0, size = mTargetListWidget->count(); i < size; ++ i) {
    auto* it = mTargetListWidget->item(i);
    if (it->checkState() == Qt::Checked) {
      result.append(it->text());
    }
  }
  return result;
}

void BuildTargetSelector::ShowListWidget() {
  if (mTargetListWidget->count() > 0) {
    if (!mTargetListWidget->isVisible()) {
      mTargetListWidget->Relayout();
      mTargetListWidget->show();
    }
    
    mTargetListWidget->SetFilterText(text());
  }
}

void BuildTargetSelector::CloseListWidget() {
  mTargetListWidget->hide();
}

void BuildTargetSelector::Moved() {
  if (mTargetListWidget->isVisible()) {
    mTargetListWidget->Relayout();
  }
}

void BuildTargetSelector::TextChanged() {
  ShowListWidget();
}

void BuildTargetSelector::ListSelectionChanged() {
  update(rect());
  
  emit TargetSelectionChanged();
}

void BuildTargetSelector::focusInEvent(QFocusEvent* event) {
  QLineEdit::focusInEvent(event);
  
  ShowListWidget();
}

void BuildTargetSelector::focusOutEvent(QFocusEvent* event) {
  QLineEdit::focusOutEvent(event);
  
  clear();
  CloseListWidget();
}

void BuildTargetSelector::paintEvent(QPaintEvent* event) {
  QLineEdit::paintEvent(event);
  
  // Write the current build targets if the line edit does not have focus
  if (text().isEmpty() && !hasFocus()) {
    QPainter painter(this);
    painter.setPen(qRgb(50, 50, 50));
    painter.setFont(Settings::Instance().GetDefaultFont());
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
  int charWidth = painter.fontMetrics().horizontalAdvance(' ');
#else
  int charWidth = painter.fontMetrics().width(' ');
#endif
    
    constexpr int leftMargin = 7;  // TODO: This was read off from a screenshot. How to find this in a portable way?
    int xCoord = leftMargin;
    
    for (int i = 0, size = mTargetListWidget->count(); i < size; ++ i) {
      auto* it = mTargetListWidget->item(i);
      if (it->checkState() == Qt::Checked) {
        for (int c = 0; c < it->text().size(); ++ c) {
          painter.drawText(QRect(xCoord, 0, charWidth, height()),
                           Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                           it->text().at(c));
          xCoord += charWidth;
        }
        
        xCoord += charWidth;
      }
    }
    painter.end();
  }
}

bool BuildTargetSelector::event(QEvent* event) {
  if (event->type() == QEvent::KeyPress) {
    QKeyEvent* keyEvent = dynamic_cast<QKeyEvent*>(event);
    if (keyEvent) {
      if (keyEvent->key() == Qt::Key_Tab) {
        // Handle Tab key press.
        mTargetListWidget->ToggleCurrentTarget();
        if (event->isAccepted()) {
          return true;
        }
      }
    }
  }
  return QWidget::event(event);
}

void BuildTargetSelector::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    CloseListWidget();
    QWidget* currentDocumentWidget = mainWindow->GetCurrentDocumentWidget();
    if (currentDocumentWidget) {
      currentDocumentWidget->setFocus();
    }
    event->accept();
    return;
  }
  
  if (mTargetListWidget->isVisible() &&
      (event->key() == Qt::Key_Up ||
       event->key() == Qt::Key_Down ||
       event->key() == Qt::Key_Return)) {
    QApplication::sendEvent(mTargetListWidget, event);
    if (event->isAccepted()) {
      return;
    }
  }
  
  QLineEdit::keyPressEvent(event);
}

void BuildTargetSelector::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton &&
      hasFocus() &&
      !mTargetListWidget->isVisible()) {
    ShowListWidget();
  }
  
  QLineEdit::mousePressEvent(event);
}
