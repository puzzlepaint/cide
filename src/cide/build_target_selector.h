// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QLineEdit>

class BuildTargetListWidget;
class MainWindow;

class BuildTargetSelector : public QLineEdit {
 Q_OBJECT
 public:
  explicit BuildTargetSelector(MainWindow* mainWindow, QWidget* parent = nullptr);
  
  ~BuildTargetSelector();
  
  void Clear();
  void AddTarget(const QString& targetName, bool selected);
  QStringList GetSelectedTargets();
  
  void ShowListWidget();
  void CloseListWidget();
  
  /// Must be called to notify the widget that its containing window moved.
  void Moved();
  
 signals:
  void TargetSelectionChanged();
  
 private slots:
  void TextChanged();
  void ListSelectionChanged();

 protected:
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  bool event(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  
 private:
  BuildTargetListWidget* mTargetListWidget;
  MainWindow* mainWindow;
};
