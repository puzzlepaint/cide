// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QDialog>
#include <QTextBrowser>

class AboutDialog;


class LicenseBrowser : public QTextBrowser {
 public:
  LicenseBrowser(QWidget* parent = nullptr);
  
  QVariant loadResource(int type, const QUrl& name) override;
};


class AboutDialog : public QDialog {
 public:
  AboutDialog(QWidget* parent = nullptr);
  
 private:
  LicenseBrowser* textBrowser;
  QPushButton* backButton;
};
