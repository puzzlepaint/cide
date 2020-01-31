// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QAction>
#include <QMenu>
#include <QTabBar>

class TabBar : public QTabBar {
 Q_OBJECT
 public:
  TabBar(QWidget* parent = nullptr);
  
 signals:
  void CloseAllOtherTabs(int index);
  void CloseAllTabs();
  
 protected:
  void mousePressEvent(QMouseEvent* event) override;
  
 private slots:
  void CloseTabClicked();
  void CloseAllOtherTabsClicked();
  void CloseAllTabsClicked();
  
 private:
  int currentIndexForMenu;
  
  QMenu* contextMenu;
  QAction* closeAction;
  QAction* closeAllOthersAction;
  QAction* closeAllAction;
};
