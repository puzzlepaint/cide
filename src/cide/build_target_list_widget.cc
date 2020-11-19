// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/build_target_list_widget.h"

#include <QGuiApplication>
#include <QScreen>

#include "cide/build_target_selector.h"
#include "cide/settings.h"

BuildTargetListWidget::BuildTargetListWidget(BuildTargetSelector* buildTargetSelector, QWidget* parent)
    : QListWidget(parent),
      buildTargetSelector(buildTargetSelector) {
  setWindowFlags(GetCustomTooltipWindowFlags());
  setFocusPolicy(Qt::NoFocus);
  
  connect(this, &QListWidget::itemActivated, this, &BuildTargetListWidget::ListItemActivated);
  connect(this, &QListWidget::itemClicked, this, &BuildTargetListWidget::ListItemActivated);
}

void BuildTargetListWidget::AddBuildTarget(const QString& name, bool selected) {
  QListWidgetItem* newItem = new QListWidgetItem(name);
  newItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
  newItem->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
  addItem(newItem);
}

void BuildTargetListWidget::ToggleCurrentTarget() {
  auto* item = currentItem();
  if (item) {
    item->setCheckState((item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked);
  }
}

void BuildTargetListWidget::SetFilterText(const QString& filter) {
  for (int i = 0; i < count(); ++ i) {
    auto* it = item(i);
    it->setHidden(!filter.isEmpty() && !it->text().contains(filter));
  }
}

void BuildTargetListWidget::Relayout() {
  if (count() == 0) {
    return;
  }
  
  bool item0Hidden = item(0)->isHidden();
  item(0)->setHidden(false);
  int lineHeight = rectForIndex(indexFromItem(item(0))).height();
  item(0)->setHidden(item0Hidden);
  
  int goodHeight = 4;
  
  int visibleItems = std::min<int>(count(), maxNumVisibleItems);
  goodHeight += visibleItems * lineHeight;
  
  QPoint leftPoint = buildTargetSelector->mapToGlobal(buildTargetSelector->rect().bottomLeft());
  QPoint rightPoint = buildTargetSelector->mapToGlobal(buildTargetSelector->rect().bottomRight());
  int goodWidth = rightPoint.x() - leftPoint.x();
  
  QScreen* widgetScreen;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  widgetScreen = buildTargetSelector->screen();
#else
  widgetScreen = QGuiApplication::screenAt(buildTargetSelector->mapToGlobal({0, 0}));
#endif
  goodHeight = std::min(goodHeight, widgetScreen->geometry().bottom() - leftPoint.y());
  
  if (leftPoint != pos() || width() != goodWidth || height() != goodHeight) {
    setGeometry(leftPoint.x(), leftPoint.y(), goodWidth, goodHeight);
  }
  update(rect());
}

void BuildTargetListWidget::ListItemActivated(QListWidgetItem* item) {
  item->setCheckState((item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked);
}
