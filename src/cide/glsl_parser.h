// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QString>

class Document;
class MainWindow;

/// Singleton class used to do global resource initialization of glslang
class ParserGlobal {
 public:
  ~ParserGlobal();
  
  static inline ParserGlobal& Instance() {
    static ParserGlobal instance;
    return instance;
  }
  
 private:
  ParserGlobal();
};

void ParseGLSLFile(const QString& canonicalPath, Document* document, MainWindow* mainWindow);
