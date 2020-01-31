// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/cpp_utils.h"

#include <QDir>
#include <QFileInfo>

#include "cide/project.h"

std::vector<QString> headerExtensions = {
    QStringLiteral("h"),
    QStringLiteral("hh"),
    QStringLiteral("h++"),
    QStringLiteral("hpp"),
    QStringLiteral("hxx"),
    QStringLiteral("cuh"),
};
std::vector<QString> sourceExtensions = {
    QStringLiteral("c"),
    QStringLiteral("cc"),
    QStringLiteral("c++"),
    QStringLiteral("cpp"),
    QStringLiteral("cxx"),
    QStringLiteral("cu"),
    QStringLiteral("inl"),
};

bool GuessIsCFile(const QString& path) {
  for (const QString& headerExt : headerExtensions) {
    if (path.endsWith(QStringLiteral(".") + headerExt, Qt::CaseInsensitive)) {
      return true;
    }
  }
  for (const QString& sourceExt : sourceExtensions) {
    if (path.endsWith(QStringLiteral(".") + sourceExt, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

bool GuessIsHeader(const QString& path, bool* certain) {
  // Heuristically try to determine whether the current file is a header or a
  // source file.
  // NOTE: We could also check for include guards within the document text itself.
  
  bool foundExactMatch = false;
  bool isHeader;
  
  for (const QString& headerExt : headerExtensions) {
    if (path.endsWith(QStringLiteral(".") + headerExt, Qt::CaseInsensitive)) {
      foundExactMatch = true;
      isHeader = true;
      break;
    }
  }
  
  if (!foundExactMatch) {
    for (const QString& sourceExt : sourceExtensions) {
      if (path.endsWith(QStringLiteral(".") + sourceExt, Qt::CaseInsensitive)) {
        foundExactMatch = true;
        isHeader = false;
        break;
      }
    }
  }
  
  if (!foundExactMatch) {
    isHeader = path.endsWith('h', Qt::CaseInsensitive);
  }
  
  if (certain) {
    *certain = foundExactMatch;
  }
  return isHeader;
}

QString FindCorrespondingHeaderOrSource(const QString& path, const std::vector<std::shared_ptr<Project>>& projects) {
  QFileInfo thisFileInfo = QFileInfo(path);
  QString canonicalPath = thisFileInfo.canonicalFilePath();
  QString baseName = thisFileInfo.completeBaseName();
  QString extension = thisFileInfo.suffix();
  
  bool isHeader = GuessIsHeader(path, nullptr);
  
  // Look for other files with the same base name in the same directory.
  QDir dir = QFileInfo(path).dir();
  QStringList fileList = dir.entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::System);
  QStringList candidates;
  for (const QString& fileName : fileList) {
    QFileInfo fileInfo = QFileInfo(fileName);
    if (fileInfo.baseName() == baseName && fileInfo.suffix() != extension) {
      candidates << dir.filePath(fileName);
    }
  }
  
  // If anything was found, decide for one of those files.
  QString bestCandidate;
  if (!candidates.isEmpty()) {
    for (const QString& candidate : candidates) {
      bestCandidate = candidate;
      if (GuessIsHeader(candidate, nullptr) != isHeader) {
        break;
      }
    }
    return bestCandidate;
  }
  
  // Look for other project files with the same base name (in any other
  // directory), and decide for one of those.
  for (const auto& project : projects) {
    if (project->ContainsFile(canonicalPath)) {
      int numTargets = project->GetNumTargets();
      for (int targetIndex = 0; targetIndex < numTargets; ++ targetIndex) {
        const Target& target = project->GetTarget(targetIndex);
        
        for (const SourceFile& source : target.sources) {
          QFileInfo fileInfo = QFileInfo(source.path);
          if (fileInfo.baseName() == baseName && fileInfo.suffix() != extension) {
            candidates << source.path;
          }
        }
      }
    }
  }
  
  // If anything was found, decide for one of those files.
  // TODO: Code duplicated from above
  if (!candidates.isEmpty()) {
    for (const QString& candidate : candidates) {
      bestCandidate = candidate;
      if (GuessIsHeader(candidate, nullptr) != isHeader) {
        break;
      }
    }
    return bestCandidate;
  }
  
  return QStringLiteral("");
}
