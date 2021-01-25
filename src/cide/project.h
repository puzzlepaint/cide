// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <clang-c/Index.h>
#include <QDir>
#include <QFileSystemWatcher>
#include <QString>

#include "cide/settings.h"
#include "cide/util.h"


class MainWindow;
class Project;


struct CompileSettings {
  enum class Language {
    C = 0,
    CXX,
    Other
  };
  
  
  std::vector<QByteArray> BuildCommandLineArgs(bool enableSpellCheck, const QString& filePath, const Project* project) const;
  
  bool operator== (const CompileSettings& other);
  
  static inline QString LanguageToString(Language language) {
    if (language == Language::C) {
      return QObject::tr("C");
    } else if (language == Language::CXX) {
      return QObject::tr("C++");
    } else if (language == Language::Other) {
      return QObject::tr("other");
    }
    return QObject::tr("<unknown>");
  }
  
  
  Language language;
  std::vector<QString> compileCommandFragments;
  std::vector<QString> includes;
  std::vector<QString> systemIncludes;
  
  // Format: DEFINE, or DEFINE=VALUE
  std::vector<QString> defines;
};


struct SourceFile {
  /// Transfers derived information from an old SourceFile instance to a new
  /// one resembling the same file. This is used on matching files after
  /// reloading the configuration.
  inline void TransferInformationTo(SourceFile* dest) {
    dest->hasBeenIndexed = hasBeenIndexed;
    dest->includedPaths = includedPaths;
  }
  
  
  /// Canonical path to the source file
  QString path;
  
  /// Index into the compileSettings array for this file's compile settings,
  /// or -1 if the source is not compiled.
  int compileSettingsIndex;
  
  // --- Attributes below need to be determined by indexing the file ---
  
  /// This is set to true once an indexing request for this source file has
  /// been made.
  bool hasBeenIndexed = false;
  
  /// List of canonical paths to all files included by this source file, either
  /// directly or transitively, as determined by the indexing procedure. Note
  /// that this is only known to be correct if no changes to the file were made
  /// after indexing.
  std::unordered_set<QString> includedPaths;
};


struct Target {
  bool ContainsOrIncludesFile(const QString& canonicalPath) const;
  QStringList FindAllFilesThatInclude(const QString& canonicalPath) const;
  
  
  enum class Type {
    Executable = 0,
    StaticLibrary,
    SharedLibrary,
    ModuleLibrary,
    ObjectLibrary,
    Utility,
    Unknown
  };
  
  /// Name of the target
  QString name;
  
  /// An ID that uniquely identifies the target.
  QString id;
  
  /// Type of the target
  Type type;
  
  /// Path of the target. For executable targets, this is the path of the
  /// created executable.
  QString path;
  
  /// List of source files of this target (may exclude headers)
  std::vector<SourceFile> sources;
  
  /// List of compile setting groups for this target
  std::vector<CompileSettings> compileSettings;
  
  /// List of other targets that this target depends on.
  std::vector<Target*> dependencies;
};


class Project : public QObject {
 Q_OBJECT
 public:
   enum class FileTemplate {
    LicenseHeader = 0,
    HeaderFile = 1,
    SourceFile = 2,
    NumTemplates = 3
  };
  
  enum class FilenameStyle {
    CamelCase = 0,
    LowercaseWithUnderscores = 1,
    NotConfigured = 2
  };
  
  
  Project();
  
  ~Project();
  
  /// Loads a CIDE project file.
  bool Load(const QString& path);
  
  /// Saves a CIDE project file.
  bool Save(const QString& path);
  
  /// Attempts to get the required information for correct code parsing from the
  /// build system. Returns true if successful, false otherwise.
  bool Configure(QString* errorReason, QString* warnings, bool* errorDisplayedAlready, QWidget* parent);
  
  /// Requests indexing for all source files (which will happen in the
  /// background parse threads). Returns the number of requests created.
  int IndexAllNewFiles(MainWindow* mainWindow);
  
  /// Returns whether the project contains the file with the given path.
  bool ContainsFile(const QString& canonicalPath);
  
  /// Returns whether the project contains the file with the given path, or any
  /// file within the project includes that file (possibly transitively).
  /// If @p target is non-null, it will be set to the target containing the
  /// path if the function returns true.
  bool ContainsFileOrInclude(const QString& canonicalPath, Target** target = nullptr);
  
  /// Returns the SourceFile for the given path, or null if the path does not
  /// correspond to a source file of this project.
  SourceFile* GetSourceFile(const QString& canonicalPath);
  
  /// Attempts to find the compile settings for the given file. If no concrete
  /// information is available, tries to guess and sets isGuess to true. In this
  /// case, guessQuality is set to a quality measure for the guess (larger is
  /// better).
  CompileSettings* FindSettingsForFile(const QString& canonicalPath, bool* isGuess, int* guessQuality);
  
  /// Returns the project name.
  inline const QString& GetName() const { return name; }
  
  /// Returns the path to the directory containing the project YAML file.
  inline QString GetDir() const { return QFileInfo(path).dir().path(); }
  
  /// Returns the path to the project YAML file.
  inline const QString& GetYAMLFilePath() const { return path; }
  
  inline const QDir& GetBuildDir() const { return buildDir; }
  inline const QString& GetBuildCmd() const { return buildCmd; }
  
  inline const QStringList& GetBuildTargets() const { return buildTargets; }
  
  /// Returns the number of threads that should be used for building.
  /// If this returns 0, no thread count should be specified for the build process.
  inline int GetBuildThreads() const { return buildThreads; }
  
  void SetRunConfiguration(const QDir& runDir, const QString& runCmd);
  
  inline const QDir& GetRunDir() const { return runDir; }
  inline const QString& GetRunCmd() const { return runCmd; }
  
  /// Returns the spacesPerTab setting for this project or -1 if no value has
  /// been defined for this.
  inline int GetSpacesPerTab() const { return spacesPerTab; }
  
  /// Returns whether spaces should be inserted when pressing Tab.
  /// If false, a tab character should be inserted instead.
  inline bool GetInsertSpacesOnTab() const { return insertSpacesOnTab; }
  
  /// Returns the default newline format configured for this project.
  /// May be NewlineFormat::NotConfigured. In this case, the program-level setting should be used.
  inline NewlineFormat GetDefaultNewlineFormat() const { return defaultNewlineFormat; }
  
  /// Returns whether all project files should be indexed. If not,
  /// only open files will be indexed.
  inline bool GetIndexAllProjectFiles() const { return indexAllProjectFiles; }
  
  /// Returns the file template with the given index from the @a FileTemplate enum.
  /// If no project-specific setting is present, returns the default template.
  QString GetFileTemplate(int templateIndex);
  
  inline void SetFileTemplate(int templateIndex, const QString& text) { fileTemplates[templateIndex] = text; }
  
  inline FilenameStyle GetFilenameStyle() const { return filenameStyle; }
  inline void SetFilenameStyle(FilenameStyle style) { filenameStyle = style; }
  
  inline const QString& GetHeaderFileExtension() const { return headerFileExtension; }
  inline void SetHeaderFileExtension(const QString& extension) { headerFileExtension = extension; }
  
  inline const QString& GetSourceFileExtension() const { return sourceFileExtension; }
  inline void SetSourceFileExtension(const QString& extension) { sourceFileExtension = extension; }
  
  inline bool GetUseDefaultCompiler() const { return useDefaultCompiler; }
  inline void SetUseDefaultCompiler(bool enable) { useDefaultCompiler = enable; }
  
  inline int GetNumTargets() const { return targets.size(); }
  inline const Target& GetTarget(int i) const { return targets[i]; }
  
  /// Returns true if the project thinks that its configuration files may have
  /// changed and it thus may require to be reconfigured to be up-to-date.
  inline bool MayRequireReconfiguration() const { return mayRequireReconfiguration; }
  
  inline QString GetClangResourceDir() const { return clangResourceDir; }
  
 public slots:
  inline void SetName(const QString& name) { this->name = name; }
  inline void SetBuildDir(const QDir& dir) { buildDir = dir; }
  inline void SetBuildTargets(const QStringList& targets) { buildTargets = targets; }
  inline void SetBuildThreads(int numThreads) { buildThreads = numThreads; }
  inline void SetSpacesPerTab(int value) { spacesPerTab = value; }
  inline void SetInsertSpacesOnTab(bool enable) { insertSpacesOnTab = enable; }
  inline void SetDefaultNewlineFormat(NewlineFormat format) { defaultNewlineFormat = format; }
  inline void SetIndexAllProjectFiles(bool enable) { indexAllProjectFiles = enable; }
  
 signals:
  void ProjectMayRequireReconfiguration();
  void ProjectConfigured();
  
 private slots:
  void CMakeFileChanged();
  
 private:
  bool FindCompilerDefaults(
      const std::string& compilerPath,
      std::vector<QString>* includes,
      QString* compilerVersion);
  bool QueryClangResourceDir(
      const std::string& compilerPath,
      QString* resourceDir);
  
  bool CreateCMakeQueryFilesIfNotExisting(QString* errorReason);
  bool ExtractCMakeCommandFromCache(const QString& CMakeCachePath, QString* cmakeExecutable);
  
  std::string GetCompilerPathForDirectoryQueries(const std::string& projectCompiler);
  
  
  /// Path to the project YAML file.
  QString path;
  
  // --- Project settings ---
  QString name;
  QDir projectDir;
  QDir projectCMakeDir;
  
  QDir buildDir;
  QString buildCmd;  // TODO: Deprecated. Remove?
  
  QStringList buildTargets;
  
  /// Number of threads that should be used for building.
  /// If set to 0, no thread count should be specified for the build process.
  int buildThreads;
  
  // TODO: Store a list/history of run configurations, not only one
  QDir runDir;
  QString runCmd;
  
  bool insertSpacesOnTab;
  int spacesPerTab;
  
  NewlineFormat defaultNewlineFormat;
  
  bool indexAllProjectFiles;
  
  /// If empty, the template is unset and the default should be used instead.
  QString fileTemplates[static_cast<int>(FileTemplate::NumTemplates)];
  FilenameStyle filenameStyle = FilenameStyle::NotConfigured;
  QString headerFileExtension = QStringLiteral("");
  QString sourceFileExtension = QStringLiteral("");
  
  bool useDefaultCompiler = true;
  
  // --- Information retrieved from the build system ---
  std::vector<Target> targets;
  
  std::string cxxCompiler;
  std::vector<QString> cxxDefaultIncludes;
  
  std::string cCompiler;
  std::vector<QString> cDefaultIncludes;
  
  QString clangResourceDir;
  
  // --- State / watcher ---
  QFileSystemWatcher cmakeFileWatcher;
  bool mayRequireReconfiguration;
};
