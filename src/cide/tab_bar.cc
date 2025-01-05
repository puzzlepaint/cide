// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/tab_bar.h"

#include <QMouseEvent>

TabBar::TabBar(QWidget* parent)
    : QTabBar(parent) {
  contextMenu = new QMenu(this);
  
  copyFilePathAction = contextMenu->addAction(tr("Copy file path"));
  connect(copyFilePathAction, &QAction::triggered, this, &TabBar::CopyFilePathClicked);
  
  contextMenu->addSeparator();
  
  closeAction = contextMenu->addAction(tr("Close"));
  connect(closeAction, &QAction::triggered, this, &TabBar::CloseTabClicked);
  
  closeAllOthersAction = contextMenu->addAction(tr("Close all others"));
  connect(closeAllOthersAction, &QAction::triggered, this, &TabBar::CloseAllOtherTabsClicked);
  
  closeAllAction = contextMenu->addAction(tr("Close all"));
  connect(closeAllAction, &QAction::triggered, this, &TabBar::CloseAllTabsClicked);
}

void TabBar::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
    int tabIndex = tabAt(event->pos());
    if (tabIndex >= 0) {
      event->accept();
      if (event->button() == Qt::MiddleButton) {
        emit tabCloseRequested(tabIndex);
      } else {  // if (event->button() == Qt::RightButton)
        currentIndexForMenu = tabIndex;
        contextMenu->popup(mapToGlobal(event->pos()), closeAction);
      }
      return;
    }
  }
  
  QTabBar::mousePressEvent(event);
}

void TabBar::CopyFilePathClicked() {
  emit CopyFilePath(currentIndexForMenu);
}

void TabBar::CloseTabClicked() {
  emit tabCloseRequested(currentIndexForMenu);
}

void TabBar::CloseAllOtherTabsClicked() {
  emit CloseAllOtherTabs(currentIndexForMenu);
}

void TabBar::CloseAllTabsClicked() {
  emit CloseAllTabs();
}
