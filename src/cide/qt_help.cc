// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/qt_help.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QHelpEngineCore>
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  #include <QHelpLink>
#endif
#include <QTimer>

QtHelp::QtHelp() {
  // Start the help engine with a collection file where all external help files
  // will be registered. The collection file will be created if it does not exist
  // yet.
  QString collectionFile = QDir(qApp->applicationDirPath()).filePath(QStringLiteral("cide-external-help-collection.qhc"));
  helpEngine = new QHelpEngineCore(collectionFile);
  if (!helpEngine->setupData()) {
    delete helpEngine;
    helpEngine = nullptr;
  }
}

QtHelp::~QtHelp() {
  delete helpEngine;
}

QtHelp& QtHelp::Instance() {
  static QtHelp instance;
  return instance;
}

bool QtHelp::RegisterQCHFile(const QString& path, QString* errorReason) {
  std::unique_lock<std::mutex> lock(engineMutex);
  
  if (!IsReady()) {
    *errorReason = QObject::tr("The QtHelp instance is not ready.");
    return false;
  }
  
  bool result = helpEngine->registerDocumentation(path);
  if (!result) {
    *errorReason = helpEngine->error();
  }
  return result;
}

bool QtHelp::UnregisterNamespace(const QString& namespaceName, QString* errorReason) {
  std::unique_lock<std::mutex> lock(engineMutex);
  
  if (!IsReady()) {
    *errorReason = QObject::tr("The QtHelp instance is not ready.");
    return false;
  }
  
  bool result = helpEngine->unregisterDocumentation(namespaceName);
  if (!result) {
    *errorReason = helpEngine->error();
  }
  return result;
}

QStringList QtHelp::GetRegisteredNamespaces() {
  std::unique_lock<std::mutex> lock(engineMutex);
  
  if (!IsReady()) {
    return QStringList();
  }
  
  return helpEngine->registeredDocumentations();
  
  // QStringList namespaces = helpEngine->registeredDocumentations();
  // QStringList files;
  // for (const QString& namespaceName : namespaces) {
  //   QString path = helpEngine->documentationFileName(namespaceName);
  //   if (path.isEmpty()) {
  //     qDebug() << "Error: Failed to get the qch path for namespace " << namespaceName << " obtained from helpEngine->registeredDocumentations()";
  //   } else {
  //     files << path;
  //   }
  // }
  // 
  // return files;
}

QUrl QtHelp::QueryIdentifier(const QString& identifier) {
  if (!IsReady()) {
    return QUrl();
  }
  
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  QList<QHelpLink> links = helpEngine->documentsForIdentifier(identifier);
  if (links.count()) {
    return links.constBegin()->url;
  } else {
    return QUrl();
  }
#else
  QMap<QString, QUrl> links = helpEngine->linksForIdentifier(identifier);
  if (links.count()) {
    return links.constBegin().value();
  } else {
    return QUrl();
  }
#endif
}

QByteArray QtHelp::GetFileData(const QUrl& url) {
  if (!IsReady()) {
    return QByteArray();
  } else {
    return helpEngine->fileData(url);
  }
}


HelpBrowser::HelpBrowser(QWidget* parent)
    : QTextBrowser(parent) {
  connect(this, &HelpBrowser::textChanged, this, &HelpBrowser::FixFontSize);
}

void HelpBrowser::setSource(const QUrl& name) {
  currentUrl = name;
  
  QTextBrowser::setSource(name);
}

QVariant HelpBrowser::loadResource(int type, const QUrl& name) {
  QByteArray ba;
  if (type == QTextDocument::HtmlResource ||
      type == QTextDocument::ImageResource ||
      type == QTextDocument::StyleSheetResource) {
    QUrl url(name);
    if (name.isRelative()) {
      url = source().resolved(url);
    }
    ba = QtHelp::Instance().GetFileData(url);
  }
  return ba;
}

void HelpBrowser::FixFontSize() {
  disconnect(this, &HelpBrowser::textChanged, this, &HelpBrowser::FixFontSize);
  
  // Change text size
  QTextCursor cursor = textCursor();
  selectAll();
  setFontPointSize(desiredFontSize);
  setTextCursor(cursor);
  
  // For some reason, links with anchors do not seem to open at the proper location.
  // (This seemed unrelated to the possible font size change above.)
  // So, if the link has an anchor, load it again to correct the view.
  // TODO: This is a dirty hack. QTextBrowser::scrollToAnchor() did not work (it
  //       scrolled to the document beginning instead), and a QTimer delay of 0
  //       also did not work (nothing happened). Find a better solution that does
  //       not rely on an arbitrary delay.
  if (currentUrl.hasFragment()) {
    QTimer::singleShot(250, this, &HelpBrowser::ReopenCurrentUrl);
  }
  
  connect(this, &HelpBrowser::textChanged, this, &HelpBrowser::FixFontSize);
}

void HelpBrowser::ReopenCurrentUrl() {
  setSource(currentUrl);
}
