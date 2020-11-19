// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QListWidget>

class BuildTargetSelector;

class BuildTargetListWidget : public QListWidget {
 public:
  BuildTargetListWidget(BuildTargetSelector* buildTargetSelector, QWidget* parent = nullptr);
  
  void AddBuildTarget(const QString& name, bool selected);
  
  void ToggleCurrentTarget();
  
  void SetFilterText(const QString& filter);
  
  void Relayout();
  
 private slots:
  void ListItemActivated(QListWidgetItem* item);
  
 private:
  BuildTargetSelector* buildTargetSelector;
  
  /// Maximum number of visible items in this widget.
  int maxNumVisibleItems = 50;
};
