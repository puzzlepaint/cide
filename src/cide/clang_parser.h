// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <clang-c/Index.h>
#include <QByteArray>
#include <QDebug>
#include <QString>

#include "cide/util.h"

struct CompileSettings;
class Document;
class MainWindow;
class Project;
struct SourceFile;


CompileSettings* FindParseSettingsForFile(const QString& canonicalPath, const std::vector<std::shared_ptr<Project>>& projects, std::shared_ptr<Project>* usedProject, bool* settingsAreGuessed = nullptr);

/// Perform full parsing of the file corresponding to @p document.
/// TODO: Not used anymore since it seems better to always use ParseFileIfOpenElseIndex() (in order to index all changes).
void ParseFile(Document* document, MainWindow* mainWindow);

/// If @a document is non-null and remains valid during parsing, parses the file
/// normally. Otherwise, indexes it only.
void ParseFileIfOpenElseIndex(const QString& canonicalPath, Document* document, MainWindow* mainWindow);

/// Given a parsed TU, extracts indexing information (part 1: inclusions) into @p sourceFile.
/// This function must be called from the main (Qt) thread.
void IndexFile_GetInclusions(CXTranslationUnit clangTU, SourceFile* sourceFile, Project* project, MainWindow* mainWindow);

/// Given a parsed TU, extracts indexing information (part 2: USRs).
/// This function can be called from any thread.
void IndexFile_StoreUSRs(CXTranslationUnit clangTU, bool onlyForTUFile);


/// Stores the location of a definition or declaration together with the "USR"
/// (a string that uniquely determines the entity and can be used to
/// cross-reference definitions / declarations across separate translation units.
struct USRDecl {
  inline USRDecl(const QString& spelling, int line, int column, bool isDefinition, CXCursorKind kind, int namePos = -1, int nameSize = -1)
      : spelling(spelling),
        line(line),
        column(column),
        isDefinition(isDefinition),
        kind(kind),
        namePos(namePos),
        nameSize(nameSize) {}
  
  
  /// Spelling of the referenced definition / declaration.
  QString spelling;
  
  /// Line of the definition / declaration (1-based)
  int line;
  
  /// Column of the definition / declaration (1-based)
  int column;
  
  /// True if this represents a definition, false if it represents a declaration
  bool isDefinition;
  
  /// Cursor kind of the USR.
  CXCursorKind kind;
  
  /// If the "name" of this entity is a sub-string within the spelling string,
  /// this gives the first character of this sub-string. Otherwise, it is set to
  /// -1.
  int namePos;
  
  /// The length of the "name" sub-string, see @a namePos.
  int nameSize;
};


/// Stores USRs for one file. The file path is given by the corresponding key in
/// the map in which the USRMap is stored, so it is not stored redundantly in
/// this struct again.
struct USRMap {
  /// Reference count for how many project source files are equal to, or include
  /// this file. If this reaches zero, the USRMap can be removed.
  int referenceCount;
  
  /// Maps USR string -> USRDecl
  std::unordered_multimap<QByteArray, USRDecl> map;
};


/// Singleton class which stores "USR"s in a global map. These are used for
/// cross-referencing declarations/definitions between different libclang
/// translation units.
/// TODO: Currently, for each file, a single map of USRs is stored. The correct
///       thing to do would be to store a map for each combination of file and
///       parsing environment (set of preprocessor defines active when starting
///       to parse the file). Unfortunately, that seems infeasible. We probably
///       cannot even easily query the parsing environment from libclang.
class USRStorage {
 public:
   static USRStorage& Instance();
  
  /// Locks the USRStorage mutex.
  /// NOTE: If combining this with locking the main (Qt) thread, always lock the
  ///       thread first and then the USRStorage to avoid deadlocks.
  inline void Lock() { lock.lock(); }
  
  /// Unlocks the USRStorage mutex.
  inline void Unlock() { lock.unlock(); }
  
  void ClearUSRsForFile(const QString& canonicalPath);
  
  /// Returns true if a new USR map has been created, false if a reference to an
  /// existing map has been added.
  bool AddUSRMapReference(const QString& canonicalPath);
  void RemoveUSRMapReference(const QString& canonicalPath);
  
  // NOTE: The complete process to look up USRs looks like this:
  // 
  // std::unordered_set<QString> relevantFiles;
  // RunInQtThreadBlocking([&]() {
  //   USRStorage::Instance().GetFilesForUSRLookup(canonicalFilePath, request.widget->GetMainWindow(), &relevantFiles);
  // });
  // std::vector<std::pair<QString, USRDecl>> foundDecls;  // pair of file path and USR
  // USRStorage::Instance().LookupUSRs(
  //     ClangString(clang_getCursorUSR(definition)).ToQByteArray(),
  //     relevantFiles,
  //     &foundDecls);
  
  /// First stage of USR lookup. Determines the files that are relevant. This must be done in the main (Qt) thread.
  /// The USRStorage does *not* need to be locked during this operation.
  void GetFilesForUSRLookup(const QString& canonicalPath, MainWindow* mainWindow, std::unordered_set<QString>* relevantFiles);
  /// Second stage of USR lookup. Returns pairs of file path and USR in @p foundDecls. Does not need to be done in the main thread.
  /// This function internally locks the USRStorage during the operation. It must not be locked already when the function is called.
  void LookupUSRs(const QByteArray& USR, std::unordered_set<QString> relevantFiles, std::vector<std::pair<QString, USRDecl>>* foundDecls);
  
  inline USRMap* GetUSRMapForFile(const QString& canonicalPath) {
    auto it = USRs.find(canonicalPath);
    if (it == USRs.end()) {
      return nullptr;
    } else {
      return it->second.get();
    }
  }
  
  inline const std::unordered_map<QString, std::shared_ptr<USRMap>>& GetAllUSRs() const { return USRs; }
  
  inline void DebugPrintInfo() {
    qDebug() << "USRStorage: Storing USRMaps for" << USRs.size() << "files";
  }
  
 private:
  USRStorage() = default;
  
  
  /// Maps file name --> USR multimap shared_ptr.
  /// The USR multimap maps USR string --> USRDecl.
  /// shared_ptr are used to avoid copying the second-level multimaps when the
  /// first-level map needs to be resized.
  std::unordered_map<QString, std::shared_ptr<USRMap>> USRs;
  
  std::mutex lock;
};
