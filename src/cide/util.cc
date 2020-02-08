// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/util.h"

#include <QDir>
#include <QPushButton>
#include <QProcessEnvironment>

#include "cide/settings.h"

void SplitPathAndLineAndColumn(const QString& fullPath, QString* path, int* line, int* column) {
  path->clear();
  *line = -1;
  *column = -1;
  
  QStringList parts = fullPath.split(':', QString::SkipEmptyParts);
  int partsParsedAsNumbers = 0;
  if (parts.size() >= 2) {
    bool ok;
    int lastPartNumber = parts.back().toInt(&ok);
    if (ok && parts.size() >= 3) {
      int secondLastPartNumber = parts[parts.size() - 2].toInt(&ok);
      if (ok) {
        // Parsed two numbers ok.
        *line = secondLastPartNumber;
        *column = lastPartNumber;
        partsParsedAsNumbers = 2;
      } else {
        // Parsed the last number ok, but not the second last.
        *line = lastPartNumber;
        partsParsedAsNumbers = 1;
      }
    } else if (ok) {
      // Parsed last number ok, but there are only 2 parts.
      *line = lastPartNumber;
      partsParsedAsNumbers = 1;
    }
  }
  
  for (int i = 0; i < parts.size() - partsParsedAsNumbers; ++ i) {
    if (i > 0) {
      *path += ":";
    }
    *path += parts[i];
  }
}

void MinimizeButtonSize(QPushButton* button, float factor) {
  // Implementation adapted from:
  // https://stackoverflow.com/a/19502467
  auto textSize = button->fontMetrics().size(Qt::TextShowMnemonic, button->text());
  QStyleOptionButton opt;
  opt.initFrom(button);
  opt.rect.setSize(textSize);
  button->setMaximumWidth(
      factor * button->style()->sizeFromContents(
          QStyle::CT_PushButton, &opt, textSize, button).width());
}

QString FindDefaultClangBinaryPath() {
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  QString paths = env.value("PATH");
  for (const QString& path : paths.split(':')) {
    QString clangPath = QDir(path).filePath("clang");
    if (QFileInfo(clangPath).exists()) {
      return clangPath;
    }
  }
  return "";
}


QRgb ParseHexColor(const QString& text) {
  if (text.size() != 6) {
    qDebug() << "Warning: Failed to parse hex color string (size != 6):" << text;
    return qRgb(0, 0, 0);
  }
  
  bool ok;
  int r = text.mid(0, 2).toInt(&ok, 16);
  if (!ok) {
    qDebug() << "Warning: Failed to parse hex color string (cannot parse red part):" << text;
    return qRgb(0, 0, 0);
  }
  
  int g = text.mid(2, 2).toInt(&ok, 16);
  if (!ok) {
    qDebug() << "Warning: Failed to parse hex color string (cannot parse green part):" << text;
    return qRgb(0, 0, 0);
  }
  
  int b = text.mid(4, 2).toInt(&ok, 16);
  if (!ok) {
    qDebug() << "Warning: Failed to parse hex color string (cannot parse blue part):" << text;
    return qRgb(0, 0, 0);
  }
  
  return qRgb(r, g, b);
}

QString ToHexColorString(const QRgb& color) {
  QString::number(qRed(color), 16);
  
  return QStringLiteral("%1%2%3")
      .arg(static_cast<uint>(qRed(color)), 2, 16, QLatin1Char('0'))
      .arg(static_cast<uint>(qGreen(color)), 2, 16, QLatin1Char('0'))
      .arg(static_cast<uint>(qBlue(color)), 2, 16, QLatin1Char('0'));
}


ActionWithConfigurableShortcut::ActionWithConfigurableShortcut(const QString& name, const char* configurationKeyName, QObject* parent)
    : QAction(name, parent),
      configurationKeyName(configurationKeyName) {
  setShortcut(Settings::Instance().GetConfiguredShortcut(configurationKeyName).sequence);
  Settings::Instance().RegisterConfigurableAction(this, configurationKeyName);
}

ActionWithConfigurableShortcut::~ActionWithConfigurableShortcut() {
  Settings::Instance().DeregisterConfigurableAction(this, configurationKeyName.toStdString().c_str());
}
