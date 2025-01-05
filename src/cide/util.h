// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <functional>

#include <QAction>
#include <QByteArray>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QTreeWidget>

class QPushButton;


#if (QT_VERSION < QT_VERSION_CHECK(5, 14, 0))
// Make it possible to use QString and QByteArray as key in e.g. std::unordered_map.
namespace std {
  template<> struct hash<QString> {
    std::size_t operator()(const QString& s) const noexcept {
      return (size_t) qHash(s);
    }
  };
  
  template<> struct hash<QByteArray> {
    std::size_t operator()(const QByteArray& s) const noexcept {
      return (size_t) qHash(s);
    }
  };
}
#endif


/// Splits paths of the form "filepath:line:column", where line and column are
/// optional. Returns the filepath component in @p path, and the line and column
/// components in @p line and @p column, respectively. If line or column are not
/// given, -1 is returned for them. The path is always returned.
void SplitPathAndLineAndColumn(const QString& fullPath, QString* path, int* line, int* column);


/// Sets the button's size to its text width. This allows buttons to shrink more
/// than they could by default. The @p factor is applied to the text width, allowing
/// to leave some free space at the sides.
void MinimizeButtonSize(QPushButton* button, float factor);


/// Searches the directories in the PATH environment variable for a "clang" binary.
QString FindDefaultClangBinaryPath();


/// Returns a set of Qt::WindowFlags that allow for making custom tooltip-style widgets.
/// Using Qt::ToolTip worked on Linux but failed on Windows, since those tooltips
/// automatically close under a variety of conditions there, such as any mouse clicks.
/// Qt::X11BypassWindowManagerHint is required to be able to move the widget partially off-screen.
inline Qt::WindowFlags GetCustomTooltipWindowFlags() {
  return Qt::Widget | Qt::Tool | Qt::CustomizeWindowHint | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint | Qt::X11BypassWindowManagerHint
#if __APPLE__
      // Required to prevent the code info tooltip from disappearing the first time it is clicked
      | Qt::ToolTip
#endif
    ;
}


/// Parses the given text as an HTML-like color. For example, "ff0000" would be parsed as qRgb(255, 0, 0).
QRgb ParseHexColor(const QString& text);

/// Converts the given color to an HTML-like color string. For example, qRgb(255, 0, 0) would be converted to "ff0000".
QString ToHexColorString(const QRgb& color);


class ActionWithConfigurableShortcut : public QAction {
 Q_OBJECT
 public:
  /// Note: Since the ownership of QActions is not transferred in addAction(), we do not use nullptr as default value for @p parent here.
  ActionWithConfigurableShortcut(const QString& name, const char* configurationKeyName, QObject* parent);
  ~ActionWithConfigurableShortcut();
  
 private:
  QString configurationKeyName;
};


class DockWidgetWithClosedSignal : public QDockWidget {
 Q_OBJECT
 public:
  inline DockWidgetWithClosedSignal(const QString& title, QWidget* parent = nullptr)
      : QDockWidget(title, parent) {}
  
 signals:
  void closed();
  
 protected:
  inline void closeEvent(QCloseEvent* event) override {
    QDockWidget::closeEvent(event);
    
    if (event->isAccepted()) {
      emit closed();
    }
  }
};


class WidgetWithRightClickSignal : public QWidget {
 Q_OBJECT
 public:
  inline WidgetWithRightClickSignal(QWidget* parent = nullptr)
      : QWidget(parent) {}
  
 signals:
  void rightClicked(QPoint pos, QPoint globalPos);
  
 protected:
  inline void mousePressEvent(QMouseEvent* event) override {
    QWidget::mousePressEvent(event);
    
    // TODO: This does not prevent that right-clicking a label in addition shows
    //       a second pop-up menu with options to copy the text, link, etc.
    //       How to prevent showing our menu if the label already shows its?
    if (!event->isAccepted() && event->button() == Qt::RightButton) {
      emit rightClicked(event->pos(), event->globalPos());
    }
  }
};


class TreeWidgetWithRightClickSignal : public QTreeWidget {
 Q_OBJECT
 public:
  inline TreeWidgetWithRightClickSignal(QWidget* parent = nullptr)
      : QTreeWidget(parent) {}
  
 signals:
  void itemRightClicked(QTreeWidgetItem* item, QPoint pos);
  void rightClicked(QPoint pos);
  
 protected:
  inline void mousePressEvent(QMouseEvent* event) override {
    QTreeWidget::mousePressEvent(event);
    
    if (event->button() == Qt::RightButton) {
      QTreeWidgetItem* item = itemAt(event->pos());
      if (item) {
        emit itemRightClicked(item, event->pos());
      } else {
        emit rightClicked(event->pos());
      }
    }
  }
};
