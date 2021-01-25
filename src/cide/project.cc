// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/project.h"

#include <fstream>

#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QtDebug>
#include <yaml-cpp/yaml.h>

#include "cide/clang_index.h"
#include "cide/clang_utils.h"
#include "cide/clang_parser.h"
#include "cide/main_window.h"
#include "cide/parse_thread_pool.h"
#include "cide/qt_thread.h"
#include "cide/settings.h"


std::vector<QByteArray> CompileSettings::BuildCommandLineArgs(bool enableSpellCheck, const QString& filePath, const Project* project) const {
  std::vector<QByteArray> commandLineArgs;
  commandLineArgs.reserve(
      (enableSpellCheck ? 1 : 0) +
      defines.size() +
      2 * systemIncludes.size() +
      includes.size() +
      compileCommandFragments.size());
  
  if (enableSpellCheck) {
    // This option adds useful suggestions ("did you mean X?") to some errors
    // reported by libclang
    commandLineArgs.emplace_back("-fspell-checking");
  }
  
  for (int i = 0; i < defines.size(); ++ i) {
    commandLineArgs.emplace_back("-D" + defines[i].toLocal8Bit());
  }
  
  // Special-case support for Qt: define the QT_ANNOTATE_ACCESS_SPECIFIER() macro
  // (which is by default empty) such that it adds annotations which we can look
  // for in libclang's AST.
  commandLineArgs.emplace_back("-DQT_ANNOTATE_ACCESS_SPECIFIER(x)=__attribute__((annotate(#x)))");
  
  for (int i = 0; i < systemIncludes.size(); ++ i) {
    commandLineArgs.emplace_back("-isystem");
    commandLineArgs.emplace_back(systemIncludes[i].toLocal8Bit());
  }
  
  for (int i = 0; i < includes.size(); ++ i) {
    commandLineArgs.emplace_back("-I" + includes[i].toLocal8Bit());
  }
  
  for (int i = 0; i < compileCommandFragments.size(); ++ i) {
    // If we detect MSVC-style flags, convert them to GCC-style to make libclang understand them.
    // TODO: Is this possible in a more general way?
    const auto& fragment = compileCommandFragments[i];
    if (fragment == QStringLiteral("-std:c++11")) {
      commandLineArgs.emplace_back("-std=c++11");
    } else if (fragment == QStringLiteral("-std:c++14")) {
      commandLineArgs.emplace_back("-std=c++14");
    } else if (fragment == QStringLiteral("-std:c++17")) {
      commandLineArgs.emplace_back("-std=c++17");
    } else {
      commandLineArgs.emplace_back(compileCommandFragments[i].toLocal8Bit());
    }
  }
  
  // Try to detect cuda files. The CUDA option ("-x cu" if invoking clang
  // instead of libclang?) seems to be added by CMake at a level where it does
  // not get included with the compile options that we can read from the CMake
  // file API, so we have to add it ourselves instead.
  if (filePath.endsWith(".cu", Qt::CaseInsensitive) ||
      filePath.endsWith(".cuh", Qt::CaseInsensitive)) {
    // Make (lib)clang treat the file as CUDA code
    commandLineArgs.emplace_back("-xcuda");
    // Correct CUDA parsing seems to require the resource dir to be set
    if (project) {
      commandLineArgs.emplace_back("-resource-dir=" + project->GetClangResourceDir().toUtf8());
    }
    // CUDA compilation actually compiles each .cu file multiple times, once
    // it compiles the host part, and then it compiles the CUDA parts again for
    // each chosen CUDA architecture. We somehow would like to get all at once
    // and I guess (not sure) that defining this manually accomplishes this
    // to some degree in a hacky way.
    commandLineArgs.emplace_back("-D__CUDACC__");
    // Since we are not doing it properly (see above), we need to tolerate more
    // errors (in CUDA headers) than usual.
    commandLineArgs.emplace_back("-ferror-limit=1000");
    // NOTE: The correct approach might be to do two passes, one with
    //       --cuda-host-only and one with --cuda-device-only (and only one
    //       architecture specified).
  } else if (filePath.endsWith(".h") || filePath.endsWith(".inl")) {
    // On Windows, we need to force libclang into C++ mode for .h files.
    // In addition, we also need to do the same for .inl files on every platform,
    // since it returns an "AST read error" otherwise when attempting to parse them.
    if (language == Language::CXX) {
      commandLineArgs.emplace_back("-xc++");
    } else if (language == Language::C) {
      commandLineArgs.emplace_back("-xc");
    }
  }
  
  return commandLineArgs;
}

bool CompileSettings::operator== (const CompileSettings& other) {
  if (language != other.language) {
    return false;
  }
  if (compileCommandFragments != other.compileCommandFragments) {
    return false;
  }
  if (includes != other.includes) {
    return false;
  }
  if (systemIncludes != other.systemIncludes) {
    return false;
  }
  if (defines != other.defines) {
    return false;
  }
  
  return true;
}


bool Target::ContainsOrIncludesFile(const QString& canonicalPath) const {
  for (const SourceFile& source : sources) {
    if (source.path == canonicalPath ||
        source.includedPaths.count(canonicalPath) > 0) {
      return true;
    }
  }
  return false;
}

QStringList Target::FindAllFilesThatInclude(const QString& canonicalPath) const {
  QStringList result;
  for (const SourceFile& source : sources) {
    if (source.path == canonicalPath ||
        source.includedPaths.count(canonicalPath) > 0) {
      result << source.path;
    }
  }
  return result;
}


Project::Project() {
  mayRequireReconfiguration = false;
  connect(&cmakeFileWatcher, &QFileSystemWatcher::fileChanged, this, &Project::CMakeFileChanged);
}

Project::~Project() {
  // Clean up loaded targets.
  RunInQtThreadBlocking([&]() {
    USRStorage::Instance().Lock();
    for (Target& oldTarget : targets) {
      for (SourceFile& oldSource : oldTarget.sources) {
        for (const QString& path : oldSource.includedPaths) {
          USRStorage::Instance().RemoveUSRMapReference(path);
        }
      }
    }
    USRStorage::Instance().Unlock();
  });
}

bool Project::Load(const QString& path) {
  this->path = QFileInfo(path).canonicalFilePath();
  
  YAML::Node fileNode;
  try {
    fileNode = YAML::LoadFile(path.toStdString());
  } catch (const YAML::ParserException& ex) {
    qDebug() << "Project::Load() caught ParserException with message:" << QString::fromStdString(ex.msg);
    return false;
  } catch (const YAML::BadFile& ex) {
    qDebug() << "Project::Load() caught BadFile with message:" << QString::fromStdString(ex.msg);
    return false;
  }
  if (fileNode.IsNull()) {
    return false;
  }
  
  name = QString::fromStdString(fileNode["name"].as<std::string>());
  projectDir = QFileInfo(this->path).dir();
  projectCMakeDir = projectDir;
  projectCMakeDir.cd(QString::fromStdString(fileNode["projectCMakeDir"].as<std::string>()));
  
  buildDir = projectDir;
  buildDir.cd(QString::fromStdString(fileNode["buildDir"].as<std::string>()));
  
  if (fileNode["buildCmd"].IsDefined()) {
    buildCmd = QString::fromStdString(fileNode["buildCmd"].as<std::string>());
  }
  
  buildTargets.clear();
  if (fileNode["buildTarget"].IsDefined()) {
    buildTargets.append(QString::fromStdString(fileNode["buildTarget"].as<std::string>()));
  } else if (fileNode["buildTargets"].IsDefined()) {
    YAML::Node buildTargetsNode = fileNode["buildTargets"];
    buildTargets.reserve(buildTargetsNode.size());
    for (int i = 0; i < buildTargetsNode.size(); ++ i) {
      buildTargets.append(QString::fromStdString(buildTargetsNode[i].as<std::string>()));
    }
  }
  
  if (fileNode["buildThreads"].IsDefined()) {
    buildThreads = fileNode["buildThreads"].as<int>();
  } else {
    buildThreads = QThread::idealThreadCount();
    if (buildThreads == 1) {
      buildThreads = 0;
    }
  }
  
  if (fileNode["runDir"].IsDefined()) {
    runDir = projectDir;
    runDir.cd(QString::fromStdString(fileNode["runDir"].as<std::string>()));
  } else {
    runDir = buildDir;
  }
  if (fileNode["runCmd"].IsDefined()) {
    runCmd = QString::fromStdString(fileNode["runCmd"].as<std::string>());
  }
  
  if (fileNode["spacesPerTab"].IsDefined()) {
    spacesPerTab = fileNode["spacesPerTab"].as<int>();
  } else {
    spacesPerTab = -1;
  }
  
  if (fileNode["insertSpacesOnTab"].IsDefined()) {
    insertSpacesOnTab = fileNode["insertSpacesOnTab"].as<bool>();
  } else {
    insertSpacesOnTab = true;
  }
  
  if (fileNode["defaultNewlineFormat"].IsDefined()) {
    std::string format = fileNode["defaultNewlineFormat"].as<std::string>();
    if (format == "CrLf") {
      defaultNewlineFormat = NewlineFormat::CrLf;
    } else if (format == "Lf") {
      defaultNewlineFormat = NewlineFormat::Lf;
    } else if (format == "NotConfigured") {
      defaultNewlineFormat = NewlineFormat::NotConfigured;
    } else {
      defaultNewlineFormat = NewlineFormat::NotConfigured;
      qDebug() << "Error while loading" << path << ": 'defaultNewlineFormat' node has unexpected value" << QString::fromStdString(format);
    }
  } else {
    defaultNewlineFormat = NewlineFormat::NotConfigured;
  }
  
  if (fileNode["indexAllProjectFiles"].IsDefined()) {
    indexAllProjectFiles = fileNode["indexAllProjectFiles"].as<bool>();
  } else {
    indexAllProjectFiles = true;
  }
  
  YAML::Node fileTemplatesNode = fileNode["fileTemplates"];
  if (fileTemplatesNode.IsDefined()) {
    if (fileTemplatesNode.size() != static_cast<int>(FileTemplate::NumTemplates)) {
      qDebug() << "Error while loading" << path << ": 'fileTemplates' node has unexpected size" << fileTemplatesNode.size() << "(instead of" << static_cast<int>(FileTemplate::NumTemplates) << ")";
    } else {
      for (int i = 0; i < static_cast<int>(FileTemplate::NumTemplates); ++ i) {
        fileTemplates[i] = QString::fromStdString(fileTemplatesNode[i].as<std::string>());
      }
    }
  }
  
  YAML::Node filenameStyleNode = fileNode["filenameStyle"];
  if (filenameStyleNode.IsDefined()) {
    std::string value = filenameStyleNode.as<std::string>();
    if (value == "CamelCase") {
      filenameStyle = FilenameStyle::CamelCase;
    } else if (value == "LowercaseWithUnderscores") {
      filenameStyle = FilenameStyle::LowercaseWithUnderscores;
    } else {
      qDebug() << "Error: Unhandled value for filenameStyle while loading" << path << ":" << QString::fromStdString(value);
    }
  }
  
  if (fileNode["headerFileExtension"].IsDefined()) {
    headerFileExtension = QString::fromStdString(fileNode["headerFileExtension"].as<std::string>());
  }
  if (fileNode["sourceFileExtension"].IsDefined()) {
    sourceFileExtension = QString::fromStdString(fileNode["sourceFileExtension"].as<std::string>());
  }
  
  if (fileNode["useDefaultCompiler"].IsDefined()) {
    useDefaultCompiler = fileNode["useDefaultCompiler"].as<bool>();
  }
  
  return true;
}

bool Project::Save(const QString& path) {
  std::ofstream file(path.toStdString(), std::ios::out);
  if (!file) {
    return false;
  }
  
  YAML::Emitter out;
  out << YAML::BeginMap;
  
  out << YAML::Key << "name";
  out << YAML::Value << name.toStdString();
  
  out << YAML::Key << "projectCMakeDir";
  out << YAML::Value << projectDir.relativeFilePath(projectCMakeDir.path()).toStdString();
  
  out << YAML::Key << "buildDir";
  out << YAML::Value << projectDir.relativeFilePath(buildDir.path()).toStdString();
  
  out << YAML::Key << "buildCmd";
  out << YAML::Value << buildCmd.toStdString();
  
  out << YAML::Key << "buildTargets";
  out << YAML::Value << YAML::Flow << YAML::BeginSeq;
  for (const QString& targetName : buildTargets) {
    out << targetName.toStdString();
  }
  out << YAML::EndSeq;
  
  out << YAML::Key << "buildThreads";
  out << YAML::Value << buildThreads;
  
  if (!runCmd.isEmpty()) {
    out << YAML::Key << "runDir";
    out << YAML::Value << projectDir.relativeFilePath(runDir.path()).toStdString();
    
    out << YAML::Key << "runCmd";
    out << YAML::Value << runCmd.toStdString();
  }
  
  if (spacesPerTab != -1) {
    out << YAML::Key << "spacesPerTab";
    out << YAML::Value << spacesPerTab;
  }
  
  out << YAML::Key << "insertSpacesOnTab";
  out << YAML::Value << insertSpacesOnTab;
  
  out << YAML::Key << "defaultNewlineFormat";
  if (defaultNewlineFormat == NewlineFormat::CrLf) {
    out << YAML::Value << "CrLf";
  } else if (defaultNewlineFormat == NewlineFormat::Lf) {
    out << YAML::Value << "Lf";
  } else {  // if (defaultNewlineFormat == NewlineFormat::NotConfigured) {
    out << YAML::Value << "NotConfigured";
  }
  
  out << YAML::Key << "indexAllProjectFiles";
  out << YAML::Value << indexAllProjectFiles;
  
  out << YAML::Key << "fileTemplates";
  out << YAML::Value << YAML::Flow << YAML::BeginSeq;
  for (int i = 0; i < static_cast<int>(FileTemplate::NumTemplates); ++ i) {
    out << fileTemplates[i].toStdString();
  }
  out << YAML::EndSeq;
  
  if (filenameStyle != FilenameStyle::NotConfigured) {
    switch (filenameStyle) {
    case FilenameStyle::CamelCase:
      out << YAML::Key << "filenameStyle" << YAML::Value << "CamelCase";
      break;
    case FilenameStyle::LowercaseWithUnderscores:
      out << YAML::Key << "filenameStyle" << YAML::Value << "LowercaseWithUnderscores";
      break;
    case FilenameStyle::NotConfigured: break; // do nothing
    }
  }
  
  if (!headerFileExtension.isEmpty()) {
    out << YAML::Key << "headerFileExtension";
    out << YAML::Value << headerFileExtension.toStdString();
  }
  if (!sourceFileExtension.isEmpty()) {
    out << YAML::Key << "sourceFileExtension";
    out << YAML::Value << sourceFileExtension.toStdString();
  }
  
  out << YAML::Key << "useDefaultCompiler";
  out << YAML::Value << useDefaultCompiler;
  
  out << YAML::EndMap;
  file << out.c_str();
  
  return true;
}

bool Project::Configure(QString* errorReason, QString* warnings, bool* errorDisplayedAlready, QWidget* parent) {
  // This uses the CMake file API. See:
  // https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html
  
  *errorDisplayedAlready = false;
  
  // Clear watcher.
  if (!cmakeFileWatcher.files().isEmpty()) {
    cmakeFileWatcher.removePaths(cmakeFileWatcher.files());
  }
  
  // Create build directory?
  if (!projectCMakeDir.exists()) {
    projectCMakeDir.mkpath(".");
  }
  
  // Add the CMake query files into the build directory which make CMake output
  // the necessary info.
  if (!CreateCMakeQueryFilesIfNotExisting(errorReason)) {
    return false;
  }
  
  // If CMakeCache.txt exists, try to find the path to the CMake executable that
  // was used to generate it, such that we can use this one later.
  QString cmakeExecutable = "cmake";  // default if not found in CMakeCache.txt
  ExtractCMakeCommandFromCache(projectCMakeDir.filePath("CMakeCache.txt"), &cmakeExecutable);
  
  // Ensure that the CMake binary that we got is at least version 3.14.
  // If we got an older version, running the configuration will potentially work just fine,
  // but the API responses will (if they already exist) silently not get updated. Thus,
  // we explicitly warn here in this case to prevent confusion.
  std::shared_ptr<QProcess> cmakeVersionTestProcess(new QProcess());
  QStringList versionTestArguments;
  versionTestArguments << "--version";
  cmakeVersionTestProcess->start(cmakeExecutable, versionTestArguments);
  cmakeVersionTestProcess->waitForFinished(10000);
  
  // Example first line output from CMake: cmake version 2.8.12.2
  QString firstLine = cmakeVersionTestProcess->readLine();
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  QStringList versionWords = firstLine.trimmed().split(' ', Qt::SkipEmptyParts);
#else
  QStringList versionWords = firstLine.trimmed().split(' ', QString::SkipEmptyParts);
#endif
  if (versionWords.size() >= 3 &&
      versionWords[0] == "cmake" &&
      versionWords[1] == "version") {
    QString cmakeVersionString = versionWords[2];
    QStringList cmakeVersionNumberParts = cmakeVersionString.split('.');
    bool versionIsAtLeast3_14 = false;
    if (cmakeVersionNumberParts.size() == 1) {
      versionIsAtLeast3_14 = cmakeVersionNumberParts[0].toInt() >= 4;
    } else if (cmakeVersionNumberParts.size() >= 2) {
      versionIsAtLeast3_14 =
          cmakeVersionNumberParts[0].toInt() >= 4 ||
          (cmakeVersionNumberParts[0].toInt() == 3 && cmakeVersionNumberParts[1].toInt() >= 14);
    }
    if (!versionIsAtLeast3_14) {
      QMessageBox::warning(parent, tr("CMake version too old"), tr("The version of the CMake binary used for configuring (%1) is too old. At least version 3.14 is required for CIDE, since it uses the CMake file API.").arg(cmakeVersionString));
    }
  } else {
    QMessageBox::warning(parent, tr("Cannot determine CMake version"), tr("Failed to parse the CMake version, thus cannot determine whether it is supported by CIDE. Continuing, but be aware that building might not work. The first line in the output of cmake --version is: %1").arg(firstLine));
  }
  
  // Determine the arguments to pass to CMake
  QStringList arguments;
#ifdef WIN32
  // On Windows, we force the use of the Ninja generator for new build directories, since
  // it is the only supported one and the default is probably Visual Studio.
  // (On Linux, the default is probably make, which is fine since we support it.)
  // 
  // We also force the use of clang-cl in order to get a configuration that
  // CIDE is able to build.
  if (!QFile(projectCMakeDir.filePath("CMakeCache.txt")).exists()) {
    arguments << "-G";
    arguments << "Ninja";
    arguments << "-DCMAKE_C_COMPILER=clang-cl.exe";
    arguments << "-DCMAKE_CXX_COMPILER=clang-cl.exe";
  }
#endif
  arguments << projectDir.absolutePath();
  // qDebug() << "CMake call:" << cmakeExecutable << arguments;
  
  // Start the progress dialog. Not sure, but this might potentially interfere with the QMessageBoxes that might be shown
  // above, so only do it after this point.
  QDialog progress(parent);
  progress.setWindowModality(Qt::WindowModal);
  
  QString cmakeCall = cmakeExecutable + QStringLiteral(" ") + arguments.join(' ');
  QLabel* progressLabel = new QLabel(tr("Configuring the project. Running:<br/><b>%1</b>").arg(cmakeCall.toHtmlEscaped()));
  QProgressBar* progressBar = new QProgressBar();
  progressBar->setRange(0, 0);  // use the undetermined progress state display
  QPlainTextEdit* outputDisplay = new QPlainTextEdit();
  QLabel* progressStateLabel = new QLabel();
  QPushButton* abortButton = new QPushButton(tr("Abort"));
  
  QVBoxLayout* progressLayout = new QVBoxLayout();
  progressLayout->addWidget(progressLabel);
  progressLayout->addWidget(progressBar);
  progressLayout->addWidget(outputDisplay, 1);
  progressLayout->addWidget(progressStateLabel);
  progressLayout->addWidget(abortButton);
  progress.setLayout(progressLayout);
  
  bool operationWasCanceled = false;
  connect(abortButton, &QPushButton::clicked, [&]() {
    operationWasCanceled = true;
  });
  
  progress.resize(std::max(800, progress.width()), std::max(600, progress.height()));
  progress.show();
  QCoreApplication::processEvents();
  
  // Run CMake
  std::shared_ptr<QProcess> cmakeProcess(new QProcess());
  cmakeProcess->setWorkingDirectory(projectCMakeDir.path());
  
  // Set cmakeProcessFinished to true upon receiving the QProcess::finished signal
  std::atomic<bool> cmakeProcessFinished;
  cmakeProcessFinished = false;
  connect(cmakeProcess.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [&]() {
    cmakeProcessFinished = true;
  });
  
  // Append process output to outputDisplay
  // TODO: The user might want to read the configure output in general. Maybe it could be shown in a
  //       dock widget on the bottom (the compile output widget?) after the configuration was successful?
  //       Or keep the progress dialog open after configuration finishes, until it is closed manually?
  QString outputHtml;
  QString errorOutput;
  connect(cmakeProcess.get(),
          &QProcess::readyReadStandardOutput,
          [&]() {
    QString newOutput = cmakeProcess->readAllStandardOutput();
    outputHtml += newOutput.toHtmlEscaped().replace("\n", "<br/>");
    outputDisplay->clear();
    outputDisplay->appendHtml(outputHtml);
  });
  connect(cmakeProcess.get(),
          &QProcess::readyReadStandardError,
          [&]() {
    QString text = cmakeProcess->readAllStandardError();
    errorOutput += text;
    outputHtml += QStringLiteral("<span style='color:#ff2222'>") + text.toHtmlEscaped().replace("\n", "<br/>") + QStringLiteral("</span>");
    outputDisplay->clear();
    outputDisplay->appendHtml(outputHtml);
  });
  
  // Start the CMake process and wait for it to end or to be aborted
  cmakeProcess->start(cmakeExecutable, arguments);
  
  QEventLoop eventLoop;
  while (!cmakeProcessFinished) {
    eventLoop.processEvents();
    QThread::msleep(1);
    if (operationWasCanceled) {
      cmakeProcess->kill();
      *errorReason = QObject::tr("The process was canceled by the user.");
      return false;
    }
  }
  
  if (cmakeProcess->exitStatus() != QProcess::NormalExit ||
      cmakeProcess->exitCode() != 0) {
    QString errorString =
        (cmakeProcess->exitStatus() != QProcess::NormalExit) ?
        QObject::tr("The CMake process exited abnormally.") :
        QObject::tr("The CMake process exited with exit code: %1.").arg(cmakeProcess->exitCode());
    QString errorDetailsString =
        (cmakeProcess->exitStatus() != QProcess::NormalExit) ?
        "" :
        QObject::tr("Process error output:\n\n%1").arg(errorOutput);
    
    progressStateLabel->setText(QStringLiteral("<span style='color:#ff2222'><b>%1</b></span>").arg(errorString.toHtmlEscaped()));
    abortButton->setText(QObject::tr("Close"));
    progressBar->setRange(0, 1);
    progressBar->setValue(1);
    while (!operationWasCanceled) {
      eventLoop.processEvents();
      QThread::msleep(1);
    }
    
    *errorDisplayedAlready = true;
    *errorReason = errorString + (errorDetailsString.isEmpty() ? "" : (" " + errorDetailsString));
    return false;
  }
  cmakeProcess.reset();
  
  // Verify that the reply directory exists.
  QDir replyIndexDir = projectCMakeDir;
  replyIndexDir.cd(".cmake/api/v1/reply");
  if (!replyIndexDir.exists()) {
    *errorReason = QObject::tr("The directory for CMake file API replies does not exist: %1. CMake version 3.14 or later is required.").arg(replyIndexDir.path());
    return false;
  }
  
  // Find the latest reply index file.
  QStringList replyIndexFileList = replyIndexDir.entryList(
      QStringList{"index-*.json"},
      QDir::NoDotAndDotDot | QDir::Readable | QDir::Files,
      QDir::Name | QDir::Reversed);
  if (replyIndexFileList.isEmpty()) {
    *errorReason = QObject::tr("There is no reply index file in the reply directory for the CMake file API (%1). CMake version 3.14 or later is required.").arg(replyIndexDir.path());
    return false;
  }
  
  // Read the latest reply index file.
  QString replyIndexFilePath = replyIndexDir.filePath(replyIndexFileList[0]);
  YAML::Node fileNode = YAML::LoadFile(replyIndexFilePath.toStdString());
  if (fileNode.IsNull()) {
    *errorReason = QObject::tr("Cannot parse the reply index file: %1 (YAML parser failed).").arg(replyIndexFilePath);
    return false;
  }
  
  YAML::Node objectsNode = fileNode["objects"];
  if (!objectsNode.IsSequence()) {
    *errorReason = QObject::tr("Cannot parse the reply index file: %1 (objects node is not a sequence).").arg(replyIndexFilePath);
    return false;
  }
  
  auto findReplyFile = [&](QString kind, int majorVersion) {
    std::string stdKind = kind.toStdString();
    for (int i = 0; i < objectsNode.size(); ++ i) {
      YAML::Node node = objectsNode[i];
      if (node["kind"].as<std::string>() == stdKind &&
          node["version"]["major"].as<int>() == majorVersion) {
        return replyIndexDir.filePath(QString::fromStdString(node["jsonFile"].as<std::string>()));
      }
    }
    return QString();
  };
  
  QString codemodelReplyPath = findReplyFile("codemodel", 2);
  if (codemodelReplyPath.isEmpty()) {
    *errorReason = QObject::tr("Could not find the codemodel reply file for major version 2.");
    return false;
  }
  
  QString cacheReplyPath = findReplyFile("cache", 2);
  if (codemodelReplyPath.isEmpty()) {
    *errorReason = QObject::tr("Could not find the cache reply file for major version 2.");
    return false;
  }
  
  QString cmakeFilesReplyPath = findReplyFile("cmakeFiles", 1);
  if (codemodelReplyPath.isEmpty()) {
    *errorReason = QObject::tr("Could not find the cmakeFiles reply file for major version 1.");
    return false;
  }
  
  // Read the CMakeFiles reply to find the files to watch for changes.
  YAML::Node cmakefilesNode = YAML::LoadFile(cmakeFilesReplyPath.toStdString());
  if (cmakefilesNode.IsNull()) {
    *errorReason = QObject::tr("Cannot parse the cmakeFiles reply file: %1 (YAML parser failed).").arg(cmakeFilesReplyPath);
    return false;
  }
  
  YAML::Node inputsNode = cmakefilesNode["inputs"];
  if (!inputsNode.IsSequence()) {
    *errorReason = QObject::tr("Cannot parse the cmakeFiles reply file: %1 (inputs node is not a sequence).").arg(cmakeFilesReplyPath);
    return false;
  }
  
  for (int i = 0; i < inputsNode.size(); ++ i) {
    YAML::Node node = inputsNode[i];
    QString cmakeFilePath = QString::fromStdString(node["path"].as<std::string>());
    
    if (cmakeFilePath.isEmpty()) {
      continue;
    } else if (cmakeFilePath[0] != '/') {
      cmakeFilePath = projectDir.filePath(cmakeFilePath);
    }
    
    cmakeFileWatcher.addPath(cmakeFilePath);
  }
  
  // Read the cache reply to find the compiler paths.
  YAML::Node cacheNode = YAML::LoadFile(cacheReplyPath.toStdString());
  if (cacheNode.IsNull()) {
    *errorReason = QObject::tr("Cannot parse the cache reply file: %1 (YAML parser failed).").arg(cacheReplyPath);
    return false;
  }
  
  YAML::Node cacheEntriesNode = cacheNode["entries"];
  if (!cacheEntriesNode.IsSequence()) {
    *errorReason = QObject::tr("Cannot parse the cache reply file: %1 (entries node is not a sequence).").arg(cacheReplyPath);
    return false;
  }
  
  cxxCompiler = "";
  cxxDefaultIncludes.clear();
  QString cxxCompilerVersion;
  
  cCompiler = "";
  cDefaultIncludes.clear();
  QString cCompilerVersion;
  
  for (int i = 0; i < cacheEntriesNode.size(); ++ i) {
    YAML::Node node = cacheEntriesNode[i];
    
    if (node["name"].as<std::string>() == "CMAKE_CXX_COMPILER") {
      cxxCompiler = node["value"].as<std::string>();
      FindCompilerDefaults(cxxCompiler, &cxxDefaultIncludes, &cxxCompilerVersion);
      
      if (clangResourceDir.isEmpty()) {
        QueryClangResourceDir(cxxCompiler, &clangResourceDir);
      }
    } else if (node["name"].as<std::string>() == "CMAKE_C_COMPILER") {
      cCompiler = node["value"].as<std::string>();
      FindCompilerDefaults(cCompiler, &cDefaultIncludes, &cCompilerVersion);
      
      if (clangResourceDir.isEmpty()) {
        QueryClangResourceDir(cCompiler, &clangResourceDir);
      }
    }
  }
  
  if (cxxCompiler.empty() && cCompiler.empty()) {
    qDebug("Warning: Found neither CXX nor C compiler path");
  }
  
  // Check whether the used libclang version matches the versions of the compilers that were used
  // to get the default paths. Warn the user if there is a mismatch.
  QString libclangVersion = ClangString(clang_getClangVersion()).ToQString().trimmed();
  if (!cxxCompiler.empty() && cxxCompilerVersion != libclangVersion) {
    *warnings += tr("The libclang version used by CIDE (%1) differs from the C++ compiler version used"
                    " to get the default paths for the project (%2). This may cause parse issues due"
                    " to the use of incompatible versions of system headers. To avoid this, configure the"
                    " default compiler (in the CIDE program settings) to the path to a clang binary with"
                    " the same version as the used libclang version (%1), and enable using the default"
                    " compiler in the project settings.\n").arg(libclangVersion).arg(cxxCompilerVersion);
  }
  if (!cCompiler.empty() && cCompilerVersion != libclangVersion) {
    *warnings += tr("The libclang version used by CIDE (%1) differs from the C compiler version used"
                    " to get the default paths for the project (%2). This may cause parse issues due"
                    " to the use of incompatible versions of system headers. To avoid this, configure the"
                    " default compiler (in the CIDE program settings) to the path to a clang binary with"
                    " the same version as the used libclang version (%1), and enable using the default"
                    " compiler in the project settings.\n").arg(libclangVersion).arg(cCompilerVersion);
  }
  
  // Read the codemodel reply.
  YAML::Node codemodelNode = YAML::LoadFile(codemodelReplyPath.toStdString());
  if (codemodelNode.IsNull()) {
    *errorReason = QObject::tr("Cannot parse the codemodel reply file: %1 (YAML parser failed).").arg(codemodelReplyPath);
    return false;
  }
  
  YAML::Node configurationsNode = codemodelNode["configurations"];
  if (!configurationsNode.IsSequence()) {
    *errorReason = QObject::tr("Cannot parse the codemodel reply file: %1 (configurations node is not a sequence).").arg(codemodelReplyPath);
    return false;
  }
  
  // TODO: Any logic to choose the configuration / load all? Currently, we simply load the first one.
  YAML::Node configurationNode = configurationsNode[0];
  // if (configurationNode["name"].as<std::string>().empty()) {
  //   qDebug("Loading unnamed configuration");
  // } else {
  //   qDebug("Loading configuration: %s", configurationNode["name"].as<std::string>().c_str());
  // }
  
  // TODO: Ignoring the CMake projects for now. We could associate each target with a CMake project.
  
  // Load targets, sources, and compile settings.
  std::vector<Target> oldTargets;
  oldTargets.swap(targets);
  
  std::vector<std::vector<QString>> targetDependencies;
  YAML::Node targetsNode = configurationNode["targets"];
  std::unordered_map<QString, int> idToTargetIndex;
  
  std::unordered_multimap<QString, std::pair<int, int>> pathToTargetAndSourceIndex;
  
  for (int targetIndex = 0; targetIndex < targetsNode.size(); ++ targetIndex) {
    YAML::Node targetNode = targetsNode[targetIndex];
    
    targets.emplace_back();
    Target& newTarget = targets.back();
    newTarget.name = QString::fromStdString(targetNode["name"].as<std::string>());
    
    targetDependencies.emplace_back();
    std::vector<QString>& newTargetDependencies = targetDependencies.back();
    
    // qDebug("Loading target: %s", newTarget.name.toStdString().c_str());
    
    QString targetJsonFile = QFileInfo(codemodelReplyPath).dir().filePath(QString::fromStdString(targetNode["jsonFile"].as<std::string>()));
    
    // Load target JSON file
    YAML::Node targetFileNode = YAML::LoadFile(targetJsonFile.toStdString());
    if (targetFileNode.IsNull()) {
      *errorReason = QObject::tr("Cannot parse target reply file: %1 (YAML parser failed).").arg(targetJsonFile);
      return false;
    }
    
    std::string type = targetFileNode["type"].as<std::string>();
    // qDebug() << "- type:" << QString::fromStdString(type);
    if (type == "EXECUTABLE") {
      newTarget.type = Target::Type::Executable;
    } else if (type == "STATIC_LIBRARY") {
      newTarget.type = Target::Type::StaticLibrary;
    } else if (type == "SHARED_LIBRARY") {
      newTarget.type = Target::Type::SharedLibrary;
    } else if (type == "MODULE_LIBRARY") {
      newTarget.type = Target::Type::ModuleLibrary;
    } else if (type == "OBJECT_LIBRARY") {
      newTarget.type = Target::Type::ObjectLibrary;
    } else if (type == "UTILITY") {
      newTarget.type = Target::Type::Utility;
    } else {
      qDebug() << "Error: Encountered unknown value" << QString::fromStdString(type) << "for a CMake target type";
      newTarget.type = Target::Type::Unknown;
    }
    
    QString targetBuildFolder = QString::fromStdString(targetFileNode["paths"]["build"].as<std::string>());
    QDir targetBuildDir;
    if (!targetBuildFolder.isEmpty() && targetBuildFolder[0] == '.') {
      // Path is relative to build dir
      targetBuildDir = projectCMakeDir;
      targetBuildDir.cd(targetBuildFolder);
    } else {
      // Path is absolute.
      targetBuildDir = QDir(targetBuildFolder);
    }
    
    if (type != "UTILITY") {
      QString targetPath = targetBuildDir.filePath(QString::fromStdString(targetFileNode["nameOnDisk"].as<std::string>()));
      // qDebug() << "- path:" << targetPath;
      newTarget.path = targetPath;
      
      // Special case handling: If there is no run command set yet, we set it to
      // the path of the first executable target.
      if (runCmd.isEmpty() && newTarget.type == Target::Type::Executable) {
        runCmd = runDir.relativeFilePath(newTarget.path);
      }
    }
    
    newTarget.id = QString::fromStdString(targetFileNode["id"].as<std::string>());
    idToTargetIndex[newTarget.id] = targetIndex;
    
    YAML::Node dependenciesNode = targetFileNode["dependencies"];
    for (int i = 0; i < dependenciesNode.size(); ++ i) {
      newTargetDependencies.push_back(QString::fromStdString(dependenciesNode[i]["id"].as<std::string>()));
    }
    
    YAML::Node sourcesNode = targetFileNode["sources"];
    for (int i = 0; i < sourcesNode.size(); ++ i) {
      YAML::Node sourceNode = sourcesNode[i];
      newTarget.sources.emplace_back();
      SourceFile& newSource = newTarget.sources.back();
      newSource.path = QFileInfo(projectDir.filePath(QString::fromStdString(sourceNode["path"].as<std::string>()))).canonicalFilePath();
      if (newSource.path.isEmpty()) {
        // TODO: This probably happens for generated files which don't exist (yet)?
        //       Should we keep them?
        newTarget.sources.pop_back();
        continue;
      }
      // qDebug() << "- source file:" << newSource.path;
      YAML::Node compileGroupIndexNode = sourceNode["compileGroupIndex"];
      if (compileGroupIndexNode.IsScalar()) {
        newSource.compileSettingsIndex = compileGroupIndexNode.as<int>();
        // qDebug() << "  using compile settings:" << newSource.compileSettingsIndex;
      } else {
        newTarget.sources.pop_back();
        continue;
      }
      pathToTargetAndSourceIndex.insert(std::make_pair(newSource.path, std::make_pair(targets.size() - 1, newTarget.sources.size() - 1)));
    }
    
    YAML::Node compileGroupsNode = targetFileNode["compileGroups"];
    for (int settingsIdx = 0; settingsIdx < compileGroupsNode.size(); ++ settingsIdx) {
      YAML::Node settingsNode = compileGroupsNode[settingsIdx];
      // qDebug() << "Compile settings:" << settingsIdx;
      
      newTarget.compileSettings.emplace_back();
      CompileSettings& newSettings = newTarget.compileSettings.back();
      
      std::string language = settingsNode["language"].as<std::string>();
      if (language == "C") {
        newSettings.language = CompileSettings::Language::C;
        newSettings.systemIncludes.insert(newSettings.systemIncludes.end(), cDefaultIncludes.begin(), cDefaultIncludes.end());
      } else if (language == "CXX") {
        newSettings.language = CompileSettings::Language::CXX;
        newSettings.systemIncludes.insert(newSettings.systemIncludes.end(), cxxDefaultIncludes.begin(), cxxDefaultIncludes.end());
      } else {
        newSettings.language = CompileSettings::Language::Other;
      }
      // qDebug() << "- language:" << QString::fromStdString(language);
      
      YAML::Node compileCommandNode = settingsNode["compileCommandFragments"];
      if (compileCommandNode.IsSequence()) {
        for (int c = 0; c < compileCommandNode.size(); ++ c) {
          #if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            QStringList fragments = QString::fromStdString(compileCommandNode[c]["fragment"].as<std::string>()).split(QChar(' '), Qt::SkipEmptyParts);
          #else
            QStringList fragments = QString::fromStdString(compileCommandNode[c]["fragment"].as<std::string>()).split(QChar(' '), QString::SplitBehavior::SkipEmptyParts);
          #endif
          for (const QString& fragment : fragments) {
            newSettings.compileCommandFragments.emplace_back(fragment);
            // qDebug() << "- compile command fragment:" << newSettings.compileCommandFragments.back();
          }
        }
      }
      
      YAML::Node includesNode = settingsNode["includes"];
      if (includesNode.IsSequence()) {
        for (int c = 0; c < includesNode.size(); ++ c) {
          QString path = QString::fromStdString(includesNode[c]["path"].as<std::string>());
          YAML::Node systemIncludeNode = includesNode[c]["isSystem"];
          if (systemIncludeNode.IsDefined() && systemIncludeNode.as<bool>() == true) {
            newSettings.systemIncludes.emplace_back(path);
            // qDebug() << "- system include:" << newSettings.systemIncludes.back();
          } else {
            newSettings.includes.emplace_back(path);
            // qDebug() << "- include:" << newSettings.includes.back();
          }
        }
      }
      
      YAML::Node definesNode = settingsNode["defines"];
      if (definesNode.IsSequence()) {
        for (int c = 0; c < definesNode.size(); ++ c) {
          newSettings.defines.emplace_back(
              QString::fromStdString(definesNode[c]["define"].as<std::string>()));
          // qDebug() << "- define:" << newSettings.defines.back();
        }
      }
    }
  }
  
  // Resolve target dependencies.
  for (int targetIndex = 0; targetIndex < targets.size(); ++ targetIndex) {
    std::vector<Target*>& dependencies = targets[targetIndex].dependencies;
    
    for (const QString& dependsOnId : targetDependencies[targetIndex]) {
      auto it = idToTargetIndex.find(dependsOnId);
      if (it == idToTargetIndex.end()) {
        qDebug() << "ERROR: Cannot find target dependency with ID" << dependsOnId;
      } else {
        dependencies.push_back(&targets[it->second]);
      }
    }
  }
  
  // Clean up oldTargets, while taking over as much information as possible into
  // the new targets: SourceFile with the same path and compile settings are
  // retained.
  USRStorage::Instance().Lock();
  for (Target& oldTarget : oldTargets) {
    for (SourceFile& oldSource : oldTarget.sources) {
      // qDebug() << "Considering old source for info transfer:" << oldSource.path << " (pathToTargetAndSourceIndex.size(): " << pathToTargetAndSourceIndex.size() << ")";
      
      // Look for new source files to transfer the information over.
      int numFilesThatInfoWasTransferredTo = 0;
      
      auto range = pathToTargetAndSourceIndex.equal_range(oldSource.path);
      for (auto it = range.first; it != range.second; ++ it) {
        if (it->second.first < 0) {
          continue;  // this new source file has already received information
        }
        
        Target& newTarget = targets[it->second.first];
        SourceFile& newSource = newTarget.sources[it->second.second];
        
        if (oldTarget.compileSettings[oldSource.compileSettingsIndex] ==
            newTarget.compileSettings[newSource.compileSettingsIndex]) {
          // Transfer the information
          oldSource.TransferInformationTo(&newSource);
          // qDebug() << "Transferring info to new source file";
          
          ++ numFilesThatInfoWasTransferredTo;
          it->second.first = -1;  // mark as having received information
        }
      }
      
      // Adjust the reference count to the included files in USRStorage
      if (numFilesThatInfoWasTransferredTo == 0) {
        for (const QString& path : oldSource.includedPaths) {
          USRStorage::Instance().RemoveUSRMapReference(path);
        }
      } else if (numFilesThatInfoWasTransferredTo > 1) {
        for (int i = 1; i < numFilesThatInfoWasTransferredTo; ++ i) {
          for (const QString& path : oldSource.includedPaths) {
            USRStorage::Instance().AddUSRMapReference(path);
          }
        }
      }
    }
  }
  USRStorage::Instance().Unlock();
  
  mayRequireReconfiguration = false;
  emit ProjectConfigured();
  return true;
}

int Project::IndexAllNewFiles(MainWindow* mainWindow) {
  if (!indexAllProjectFiles) {
    return 0;
  }
  
  int numRequestsCreated = 0;
  for (Target& target : targets) {
    for (SourceFile& source : target.sources) {
      if (source.compileSettingsIndex < 0 ||
          source.hasBeenIndexed) {
        continue;
      }
      
      ParseThreadPool::Instance().RequestParseIfOpenElseIndex(source.path, mainWindow);
      ++ numRequestsCreated;
      source.hasBeenIndexed = true;
    }
  }
  return numRequestsCreated;
}

bool Project::ContainsFile(const QString& canonicalPath) {
  // TODO: Enter file paths into an unordered_map for faster lookup?
  
  for (Target& target : targets) {
    for (SourceFile& source : target.sources) {
      if (source.path == canonicalPath) {
        return true;
      }
    }
  }
  
  return false;
}

bool Project::ContainsFileOrInclude(const QString& canonicalPath, Target** target) {
  // TODO: Enter file paths into an unordered_map for faster lookup?
  
  for (Target& testTarget : targets) {
    if (testTarget.ContainsOrIncludesFile(canonicalPath)) {
      if (target) {
        *target = &testTarget;
      }
      return true;
    }
  }
  
  return false;
}

SourceFile * Project::GetSourceFile(const QString& canonicalPath) {
  // TODO: Enter file paths into an unordered_map for faster lookup?
  
  for (Target& target : targets) {
    for (SourceFile& source : target.sources) {
      if (source.path == canonicalPath) {
        return &source;
      }
    }
  }
  
  return nullptr;
}

CompileSettings* Project::FindSettingsForFile(const QString& canonicalPath, bool* isGuess, int* guessQuality) {
  // TODO: Enter file paths into an unordered_map for faster lookup?
  
  constexpr bool kDebug = false;
  
  // First, test for an exact match as a source file.
  for (Target& target : targets) {
    for (SourceFile& source : target.sources) {
      if (source.path == canonicalPath) {
        *isGuess = false;
        if (kDebug) {
          qDebug() << "FindSettingsForFile: Found source file match.";
        }
        return &target.compileSettings[source.compileSettingsIndex];
      }
    }
  }
  
  // Second, test for being included by a source file.
  for (Target& target : targets) {
    for (SourceFile& source : target.sources) {
      if (source.includedPaths.count(canonicalPath) > 0) {
        *isGuess = false;
        if (kDebug) {
          qDebug() << "FindSettingsForFile: Found header file match (source file:" << source.path << ")";
        }
        return &target.compileSettings[source.compileSettingsIndex];
      }
    }
  }
  
  // We have to guess. Iterate over all source files in the targets and return
  // the compile settings of the source file whose path shares the most initial
  // characters with the given canonical path.
  if (kDebug) {
    qDebug() << "Guessing the settings. Using canonicalPath:" << canonicalPath;
  }
  const int canonicalPathSize = canonicalPath.size();
  CompileSettings* anyCompileSettings = nullptr;
  int bestMatchSize = 0;
  
  for (Target& target : targets) {
    for (SourceFile& source : target.sources) {
      int commonSize = std::min(source.path.size(), canonicalPathSize);
      int matchSize = 0;
      for (; matchSize < commonSize; ++ matchSize) {
        if (source.path[matchSize] != canonicalPath[matchSize]) {
          break;
        }
      }
      
      if (matchSize >= bestMatchSize) {
        if (kDebug) {
          qDebug() << "FindSettingsForFile: New best guess from file:" << source.path;
        }
        bestMatchSize = matchSize;
        anyCompileSettings = &target.compileSettings[source.compileSettingsIndex];
      }
    }
  }
  
  *isGuess = true;
  *guessQuality = bestMatchSize;
  if (kDebug) {
    qDebug() << "FindSettingsForFile: Used guess (quality:" << *guessQuality << ")";
  }
  return anyCompileSettings;
}

void Project::SetRunConfiguration(const QDir& runDir, const QString& runCmd) {
  this->runDir = runDir;
  this->runCmd = runCmd;
}

QString Project::GetFileTemplate(int templateIndex) {
  if (!fileTemplates[templateIndex].isEmpty()) {
    return fileTemplates[templateIndex];
  }
  
  // Return the default.
  if (templateIndex == static_cast<int>(FileTemplate::LicenseHeader)) {
    return tr("// License header. This can be configured in the project settings.");
  } else if (templateIndex == static_cast<int>(FileTemplate::HeaderFile)) {
    return tr("${LicenseHeader}\n\n#pragma once\n\n// TODO: Document the class. Note that this file template can be configured in the project settings.\nclass ${ClassName} {\n public:\n  \n private:\n  \n};\n");
  } else if (templateIndex == static_cast<int>(FileTemplate::SourceFile)) {
    return tr("${LicenseHeader}\n\n#include \"${HeaderFilename}\"\n\n");
  } else {
    return tr("<Error: Unknown file template>");
  }
}

void Project::CMakeFileChanged() {
  mayRequireReconfiguration = true;
  emit ProjectMayRequireReconfiguration();
}

bool Project::FindCompilerDefaults(
    const std::string& compilerPath,
    std::vector<QString>* includes,
    QString* compilerVersion) {
  std::shared_ptr<QProcess> compilerProcess(new QProcess());
  QStringList arguments;
  arguments << "-x" << "c++" << "-v" << "-E" << "-";
  
  compilerProcess->setStandardInputFile(QProcess::nullDevice());  // send end of input
  compilerProcess->start(QString::fromStdString(GetCompilerPathForDirectoryQueries(compilerPath)), arguments);
  
  if (!compilerProcess->waitForFinished(10000)) {
    // The process did not finish.
    qDebug() << "FindCompilerDefaults(): The compiler process timed out (timeout is set to 10 seconds) (call: " << compilerProcess->program() << " " << compilerProcess->arguments() << ").";
    compilerProcess->kill();
    return false;
  }
  if (compilerProcess->exitStatus() != QProcess::NormalExit) {
    qDebug() << "FindCompilerDefaults(): The compiler process exited abnormally (call: " << compilerProcess->program() << " " << compilerProcess->arguments() << ").";
    return false;
  }
  if (compilerProcess->exitCode() != 0) {
    qDebug() << "FindCompilerDefaults(): The compiler process exited with non-zero exit code (call: " << compilerProcess->program() << " " << compilerProcess->arguments() << ").";
    return false;
  }
  
  compilerProcess->setReadChannel(QProcess::StandardError);
  QList<QByteArray> err = compilerProcess->readAll().split('\n');
  
  // Relevant lines in err:
  // "#include \"...\" search starts here:",
  // "#include <...> search starts here:",
  // " /usr/lib/gcc/x86_64-linux-gnu/8/../../../../include/c++/8",
  // ...
  // "End of search list."
  
  bool includesListStarted = false;
  for (int i = 0; i < err.size(); ++ i) {
    QByteArray& line = err[i];
    if (line.startsWith("#include ")) {
      includesListStarted = true;
      continue;
    } else if (line.startsWith("clang version") || line.startsWith("gcc version")) {
      *compilerVersion = line.trimmed();
    } else if (line == "End of search list.") {
      break;
    } else if (includesListStarted) {
      QString path = line.trimmed();
      if (QDir(path).exists()) {
        includes->push_back(path);
      }
    }
  }
  
  return true;
}

bool Project::QueryClangResourceDir(
    const std::string& compilerPath,
    QString* resourceDir) {
  QProcess compilerProcess;
  QStringList arguments;
  arguments << "-print-resource-dir";
  compilerProcess.start(QString::fromStdString(GetCompilerPathForDirectoryQueries(compilerPath)), arguments);
  
  if (!compilerProcess.waitForFinished(10000)) {
    qDebug() << "QueryClangResourceDir(): The compiler process timed out (timeout is set to 10 seconds) (call: " << compilerProcess.program() << " " << compilerProcess.arguments() << ").";
    compilerProcess.kill();
    return false;
  }
  if (compilerProcess.exitStatus() != QProcess::NormalExit ||
      compilerProcess.exitCode() != 0) {
    qDebug() << "QueryClangResourceDir(): The compiler process exited abnormally (call: " << compilerProcess.program() << " " << compilerProcess.arguments() << "). Trying to fall back to the default clang binary.";
  }
  
  compilerProcess.setReadChannel(QProcess::StandardOutput);
  *resourceDir = compilerProcess.readAll().trimmed();
  // qDebug() << "Got resource dir: " << *resourceDir;
  
  return true;
}

bool Project::CreateCMakeQueryFilesIfNotExisting(QString* errorReason) {
  QFileInfo codemodelQueryFile(projectCMakeDir.filePath(".cmake/api/v1/query/codemodel-v2"));
  if (!codemodelQueryFile.exists()) {
    codemodelQueryFile.dir().mkpath(".");
    QFile file(codemodelQueryFile.filePath());
    if (!file.open(QIODevice::WriteOnly)) {
      *errorReason = QObject::tr("Failed to write codemodel query file (path: %1)").arg(codemodelQueryFile.filePath());
      return false;
    }
  }
  
  QFileInfo cacheQueryFile(projectCMakeDir.filePath(".cmake/api/v1/query/cache-v2"));
  if (!cacheQueryFile.exists()) {
    cacheQueryFile.dir().mkpath(".");
    QFile file(cacheQueryFile.filePath());
    if (!file.open(QIODevice::WriteOnly)) {
      *errorReason = QObject::tr("Failed to write codemodel query file (path: %1)").arg(cacheQueryFile.filePath());
      return false;
    }
  }
  
  QFileInfo cmakeFilesQueryFile(projectCMakeDir.filePath(".cmake/api/v1/query/cmakeFiles-v1"));
  if (!cmakeFilesQueryFile.exists()) {
    cmakeFilesQueryFile.dir().mkpath(".");
    QFile file(cmakeFilesQueryFile.filePath());
    if (!file.open(QIODevice::WriteOnly)) {
      *errorReason = QObject::tr("Failed to write codemodel query file (path: %1)").arg(cmakeFilesQueryFile.filePath());
      return false;
    }
  }
  
  return true;
}

bool Project::ExtractCMakeCommandFromCache(const QString& CMakeCachePath, QString* cmakeExecutable) {
  QFile cmakeCacheFile(CMakeCachePath);
  if (cmakeCacheFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QString prefix = "CMAKE_COMMAND:INTERNAL=";
    
    while (!cmakeCacheFile.atEnd()) {
      QByteArray lineBytes = cmakeCacheFile.readLine();
      QString line = QString::fromUtf8(lineBytes.data(), lineBytes.size());
      
      if (line.startsWith(prefix)) {
        *cmakeExecutable = line.right(line.size() - prefix.size()).trimmed();
        qDebug("Using CMake executable path from CMakeCache.txt: %s", cmakeExecutable->toStdString().c_str());
        return true;
      }
    }
    
    qWarning("Read through CMakeCache.txt, but did not find the path to the CMake executable.");
  }
  return false;
}

std::string Project::GetCompilerPathForDirectoryQueries(const std::string& projectCompiler) {
  if (useDefaultCompiler) {
    QString defaultCompiler = Settings::Instance().GetDefaultCompiler();
    if (!defaultCompiler.isEmpty()) {
      return defaultCompiler.toStdString();
    }
  }
  return projectCompiler;
}
