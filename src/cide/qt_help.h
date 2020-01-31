// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <mutex>

#include <QTextBrowser>

class QHelpEngineCore;

/// Keeps a QHelpEngineCore object which indexes all loaded .qch documentation files in a .qhc file.
class QtHelp {
 public:
  ~QtHelp();
  
  static QtHelp& Instance();
  
  inline bool IsReady() { return helpEngine != nullptr; }
  
  /// Registers the given .qch file in the QtHelp's .qhc file.
  /// This change is directly saved to disk. Returns true if successful,
  /// false if an error occurs. In the latter case, the reason of the
  /// error is returned in @p errorReason.
  bool RegisterQCHFile(const QString& path, QString* errorReason);
  
  bool UnregisterNamespace(const QString& namespaceName, QString* errorReason);
  
  QStringList GetRegisteredNamespaces();
  
  /// Queries for documentation for the given identifier
  /// (for example, "std::string::push_back" or "QString").
  QUrl QueryIdentifier(const QString& identifier);
  
  QByteArray GetFileData(const QUrl& url);
  
 private:
  QtHelp();
  
  std::mutex engineMutex;
  QHelpEngineCore* helpEngine;
};

/// Widget to display a help page loaded from a .qch documentation file via QtHelp.
class HelpBrowser : public QTextBrowser {
 Q_OBJECT
 public:
  HelpBrowser(QWidget* parent = nullptr);
  
  void setSource(const QUrl& name) override;
  QVariant loadResource(int type, const QUrl& name) override;
  
  inline const QUrl& GetCurrentUrl() const { return currentUrl; }
  
 public slots:
  void FixFontSize();
  void ReopenCurrentUrl();
  
 private:
  QUrl currentUrl;
  
  int desiredFontSize = 9;  // TODO: Make configurable
};
