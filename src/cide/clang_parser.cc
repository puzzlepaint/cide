// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/clang_parser.h"

#include <iostream>

#include <clang-c/Index.h>
#include <QMessageBox>

#include "cide/clang_highlighting.h"
#include "cide/clang_index.h"
#include "cide/clang_utils.h"
#include "cide/document.h"
#include "cide/main_window.h"
#include "cide/parse_thread_pool.h"
#include "cide/problem.h"
#include "cide/project.h"
#include "cide/qt_thread.h"
#include "cide/settings.h"
#include "cide/text_utils.h"


void RetrieveDiagnostics(Document* document, CXFile file, const std::shared_ptr<ClangTU>& TU, const std::vector<unsigned>& lineOffsets) {
  document->ClearProblems();
  
  std::vector<Problem*> lastProblems;
  
  auto addProblem = [&](CXDiagnostic diagnostic, CXDiagnostic diagnosticForRanges, CXSourceLocation diagnosticLoc, int diagnosticLine, CXDiagnosticSeverity severity) {
    // Add warning/error line attribute to color the line in green/red
    document->AddLineAttributes(
        diagnosticLine,
        (severity == CXDiagnostic_Warning) ?
            static_cast<int>(LineAttribute::Warning) :
            static_cast<int>(LineAttribute::Error));
    
    // Create the problem and add it to the document
    std::shared_ptr<Problem> newProblem(new Problem(diagnostic, TU->TU(), lineOffsets));
    int problemIndex = document->AddProblem(newProblem);
    lastProblems.push_back(newProblem.get());
    
    // Add the problem ranges to the document to underline them
    unsigned numRanges = clang_getDiagnosticNumRanges(diagnosticForRanges);
    for (int rangeIndex = 0; rangeIndex < numRanges; ++ rangeIndex) {
      CXSourceRange range = clang_getDiagnosticRange(diagnosticForRanges, rangeIndex);
      document->AddProblemRange(problemIndex, CXSourceRangeToDocumentRange(range, lineOffsets));
    }
    
    // Since many types of problems do not have ranges associated with them,
    // determine the word that contains the given problem location and add it
    // as an additional range.
    // TODO: We would need to transform this location through all edits that
    //       happened between the parse and now if we apply parse results to
    //       updated document versions.
    DocumentLocation diagnosticDocLoc = CXSourceLocationToDocumentLocation(diagnosticLoc, lineOffsets);
    Document::CharacterIterator charIt(document, diagnosticDocLoc.offset);
    if (diagnosticDocLoc.offset > 0 &&
        charIt.IsValid() &&
        charIt.GetChar() == '\n') {
      -- diagnosticDocLoc;
    }
    
    if (charIt.IsValid()) {
      int wordType = GetCharType(charIt.GetChar());
      
      Document::CharacterIterator wordStartIt = charIt;
      -- wordStartIt;
      while (wordStartIt.IsValid() &&
             GetCharType(wordStartIt.GetChar()) == wordType) {
        -- wordStartIt;
      }
      ++ wordStartIt;
      
      Document::CharacterIterator wordEndIt = charIt;
      ++ wordEndIt;
      while (wordEndIt.IsValid() &&
             GetCharType(wordEndIt.GetChar()) == wordType) {
        ++ wordEndIt;
      }
      
      // NOTE: Could check for overlaps with existing ranges here and merge
      document->AddProblemRange(problemIndex, DocumentRange(wordStartIt.GetCharacterOffset(), wordEndIt.GetCharacterOffset()));
    }
    
    return newProblem;
  };
  
  // Clear warning/error line attributes
  Document::LineIterator lineIt(document);
  while (lineIt.IsValid()) {
    lineIt.SetAttributes(lineIt.GetAttributes() & ~(static_cast<int>(LineAttribute::Warning) | static_cast<int>(LineAttribute::Error)));
    ++ lineIt;
  }
  
  // Loop over all diagnostics from libclang
  unsigned numDiagnostics = clang_getNumDiagnostics(TU->TU());
  for (unsigned diagnosticIndex = 0; diagnosticIndex < numDiagnostics; ++ diagnosticIndex) {
    CXDiagnostic diagnostic = clang_getDiagnostic(TU->TU(), diagnosticIndex);
    
    CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diagnostic);
    if (severity == CXDiagnostic_Note) {
      // This note must be attached to the previous non-note diagnostic.
      for (Problem* lastProblem : lastProblems) {
        lastProblem->AddNote(diagnostic, TU->TU(), lineOffsets);
      }
      clang_disposeDiagnostic(diagnostic);
      continue;
    } else if (severity == CXDiagnostic_Ignored) {
      clang_disposeDiagnostic(diagnostic);
      lastProblems.clear();
      continue;
    }
    
    lastProblems.clear();
    
    // Iterate over child diagnostics and create an additional problem for each
    // 'requested here' line in the current file.
    CXDiagnosticSet children = clang_getChildDiagnostics(diagnostic);
    unsigned numChildren = clang_getNumDiagnosticsInSet(children);
    for (unsigned i = 0; i < numChildren; ++ i) {
      CXDiagnostic child = clang_getDiagnosticInSet(children, i);
      
      CXSourceLocation childLoc = clang_getDiagnosticLocation(child);
      CXFile childFile;
      unsigned childLine;
      clang_getFileLocation(
          childLoc,
          &childFile,
          &childLine,
          nullptr,
          nullptr);
      if (clang_File_isEqual(childFile, file)) {
        QString childSpelling = ClangString(clang_getDiagnosticSpelling(child)).ToQString();
        if (childSpelling.endsWith(QStringLiteral("requested here"))) {
          std::shared_ptr<Problem> newProblem = addProblem(diagnostic, child, childLoc, childLine - 1, severity);
          newProblem->SetIsRequestedHere();
        }
      }
      
      clang_disposeDiagnostic(child);
    }
    
    CXSourceLocation diagnosticLoc = clang_getDiagnosticLocation(diagnostic);
    CXFile diagnosticFile;
    unsigned diagnosticLine;
    clang_getFileLocation(
        diagnosticLoc,
        &diagnosticFile,
        &diagnosticLine,
        nullptr,
        nullptr);
    if (clang_File_isEqual(diagnosticFile, file)) {
      addProblem(diagnostic, diagnostic, diagnosticLoc, diagnosticLine - 1, severity);
    }
    
    clang_disposeDiagnostic(diagnostic);
  }
}


void VisitInclusions_GetPathsAndLastModificationTimes(
    CXFile included_file,
    CXSourceLocation* /*inclusion_stack*/,
    unsigned /*include_len*/,
    CXClientData client_data) {
  std::vector<ClangTU::IncludeWithModificationTime>* result = reinterpret_cast<std::vector<ClangTU::IncludeWithModificationTime>*>(client_data);
  result->emplace_back(
      GetClangFilePathAsByteArray(included_file),
      clang_getFileTime(included_file));
}


/// @p document may be null. In this case, @p canonicalPath must be valid. If
/// @p document is valid, @p canonicalPath may be empty. @p canonicalPath must
/// be given if @p alwaysIndex is true.
void ParseAndOrIndexFileImpl(QString canonicalPath, Document* document, MainWindow* mainWindow, bool alwaysIndex) {
  std::vector<QByteArray> commandLineArgs;
  std::vector<const char*> commandLineArgPtrs;
  std::vector<CXUnsavedFile> unsavedFiles;
  std::vector<std::string> unsavedFileContents;
  std::vector<std::string> unsavedFilePaths;
  
  std::vector<unsigned> lineOffsets;
  unsigned utf8FileSize = 0;
  
  CompileSettings* settings = nullptr;
  std::shared_ptr<CompileSettings> settingsDeleter;
  std::shared_ptr<ClangTU> TU;
  QString parseSettingsAreGuessedNotification;
  QString parseNotification;
  int parsedDocumentVersion = -1;
  bool usePerVariableColoring;
  bool exit = false;
  
  RunInQtThreadBlocking([&]() {
    // If the document has been closed in the meantime, we must not access it or
    // its widget anymore.
    // TODO: In this case, should we always still update the indexing?
    if (document) {
      if (!ParseThreadPool::Instance().IsDocumentBeingParsed(document)) {
        if (alwaysIndex) {
          document = nullptr;
        } else {
          exit = true;
          return;
        }
      }
    }
    
    if (document) {
      canonicalPath = QFileInfo(document->path()).canonicalFilePath();
      parsedDocumentVersion = document->version();
    }
    
    // Find the parse settings for the source file
    bool settingsAreGuessed;
    std::shared_ptr<Project> usedProject;
    settings = FindParseSettingsForFile(canonicalPath, mainWindow->GetProjects(), &usedProject, &settingsAreGuessed);
    
    if (!settings) {
      parseNotification = QObject::tr("Could not find compile settings to parse the file (no project open)");
      
      settingsDeleter.reset(new CompileSettings());
      settings = settingsDeleter.get();
    } else if (settingsAreGuessed) {
      parseSettingsAreGuessedNotification = QObject::tr("Compile settings for this file are guessed (no #include of this file found in any project yet)");
    }
    
    
    // Create translation unit
    // TODO: If the file has not been saved yet, it seems that we have to make a temporary file for it to be able to use this function,
    //       and change the TU later once the file gets saved properly.
    if (canonicalPath.isEmpty()) {
      if (document) {
        DocumentWidget* widget = mainWindow->GetWidgetForDocument(document);
        if (widget) {
          widget->GetContainer()->SetMessage(
              DocumentWidgetContainer::MessageType::ParseSettingsAreGuessedNotification,
              parseSettingsAreGuessedNotification);
          widget->GetContainer()->SetMessage(
              DocumentWidgetContainer::MessageType::ParseNotification,
              QObject::tr("The file has not been saved yet, CIDE's parsing code currently does not handle this case."));
        }
      }
      exit = true;
      return;
    }
    
    commandLineArgs = settings->BuildCommandLineArgs(true, canonicalPath, usedProject.get());
    commandLineArgPtrs.resize(commandLineArgs.size());
    for (int i = 0; i < commandLineArgs.size(); ++ i) {
      commandLineArgPtrs[i] = commandLineArgs[i].data();
    }
    // qDebug() << "PARSE ARGS: ";
    // for (QByteArray& arg : commandLineArgs) {
    //   qDebug() << "  " << arg;
    // }
    
    
    usePerVariableColoring = Settings::Instance().GetUsePerVariableColoring();
    
    // Get all unsaved files that are opened
    if (document) {
      std::string documentStringUtf8 = document->GetDocumentText().toStdString();
      utf8FileSize = documentStringUtf8.size();
    }
    
    GetAllUnsavedFiles(mainWindow, &unsavedFiles, &unsavedFileContents, &unsavedFilePaths);
    
    if (document) {
      // Get the newline positions of the main file to be able to map the "line, column"
      // positions given by libclang to offsets in our UTF-16 (QString) version of
      // the document.
      // NOTE: We could also get this after parsing finished, since currently we
      //       anyway only use the parsing result if the document has not changed.
      lineOffsets.resize(document->LineCount());
      Document::LineIterator lineIt(document);
      int lineIndex = 0;
      while (lineIt.IsValid()) {
        lineOffsets[lineIndex] = lineIt.GetLineStart().offset;
        
        ++ lineIndex;
        ++ lineIt;
      }
      if (lineIndex != lineOffsets.size()) {
        qDebug() << "Error: Line iterator returned a different line count than Document::LineCount().";
      }
      
      
      TU = document->GetTUPool()->TakeLeastUpToDateTU();
      if (!TU) {
        DocumentWidget* widget = mainWindow->GetWidgetForDocument(document);
        if (widget) {
          widget->GetContainer()->SetMessage(
              DocumentWidgetContainer::MessageType::ParseSettingsAreGuessedNotification,
              parseSettingsAreGuessedNotification);
          widget->GetContainer()->SetMessage(
              DocumentWidgetContainer::MessageType::ParseNotification,
              QObject::tr("Failed to obtain a libclang TU for parsing."));
        }
        
        exit = true;
        return;
      }
    }
  });
  if (exit) {
    return;
  }
  
  if (!TU) {
    // Create a temporary TU for indexing.
    TU.reset(new ClangTU());
  }
  
  
  // Parse translation unit.
  /// preambleIsLikelyUnchanged is set to true if the list of includes and their
  /// modification times, as well as the global compile flags have not changed.
  /// In principle, it should also check whether the #defines in front of any
  /// #include have changed, but this is currently not done as it is unclear how
  /// much effort that would be. Therefore it is only "likely unchanged".
  bool preambleIsLikelyUnchanged = true;
  unsigned parseOptions;
  if (document) {
    parseOptions =
        CXTranslationUnit_DetailedPreprocessingRecord |
        CXTranslationUnit_PrecompiledPreamble |
        CXTranslationUnit_CacheCompletionResults |
        CXTranslationUnit_CreatePreambleOnFirstParse |
        CXTranslationUnit_KeepGoing;
    // Note: The following two flags should not be used, otherwise
    //       errors of the type "[...] requested from here" may
    //       not be noticed.
    // CXTranslationUnit_SkipFunctionBodies |
    // CXTranslationUnit_LimitSkipFunctionBodiesToPreamble;
    #if CINDEX_VERSION_MINOR >= 59
      parseOptions |= CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles;
    #endif
  } else {
    parseOptions =
        CXTranslationUnit_Incomplete |
        // TODO: It seems that if we skip function bodies, we cannot determine
        //       whether something is a definition or a declaration. However, if
        //       we changed to index things in the parsed source file only, we
        //       could enable skipping function bodies in other files.
        // CXTranslationUnit_SkipFunctionBodies |
        CXTranslationUnit_KeepGoing;
  }
  
  CXErrorCode parseResult = CXError_Failure;
  if (TU->CanBeReparsed(canonicalPath, commandLineArgs)) {
    parseResult = static_cast<CXErrorCode>(clang_reparseTranslationUnit(
        TU->TU(),
        unsavedFiles.size(),
        unsavedFiles.data(),
        clang_defaultReparseOptions(TU->TU())));
    if (parseResult != CXError_Success) {
      qDebug() << "Parse error: failed to reparse.";
    }
  }
  
  if (parseResult != CXError_Success) {
    preambleIsLikelyUnchanged = false;
    
    CXTranslationUnit clangTU;
    parseResult = clang_parseTranslationUnit2(
        TU->index(),
        canonicalPath.toLocal8Bit().data(),
        commandLineArgPtrs.data(),
        commandLineArgPtrs.size(),
        unsavedFiles.data(),
        unsavedFiles.size(),
        parseOptions,
        &clangTU);
    TU->Set(clangTU, commandLineArgs);
  }
  
  if (parseResult == CXError_Crashed) {
    if (document) {
      RunInQtThreadBlocking([&]() {
        DocumentWidget* widget = mainWindow->GetWidgetForDocument(document);
        if (widget) {
          widget->GetContainer()->SetMessage(
              DocumentWidgetContainer::MessageType::ParseSettingsAreGuessedNotification,
              parseSettingsAreGuessedNotification);
          widget->GetContainer()->SetMessage(
              DocumentWidgetContainer::MessageType::ParseNotification,
              parseNotification + QObject::tr("\nParse error: libclang crashed."));
        }
        document->GetTUPool()->PutTU(TU, false);
      });
    }
    return;
  } else if (parseResult == CXError_InvalidArguments) {
    if (document) {
      RunInQtThreadBlocking([&]() {
        DocumentWidget* widget = mainWindow->GetWidgetForDocument(document);
        if (widget) {
          widget->GetContainer()->SetMessage(
              DocumentWidgetContainer::MessageType::ParseSettingsAreGuessedNotification,
              parseSettingsAreGuessedNotification);
          widget->GetContainer()->SetMessage(
              DocumentWidgetContainer::MessageType::ParseNotification,
              parseNotification + QObject::tr("\nParse error: invalid arguments given."));
        }
        document->GetTUPool()->PutTU(TU, false);
      });
    }
    return;
  } else if (parseResult == CXError_Failure || parseResult != CXError_Success) {
    if (document) {
      RunInQtThreadBlocking([&]() {
        DocumentWidget* widget = mainWindow->GetWidgetForDocument(document);
        if (widget) {
          widget->GetContainer()->SetMessage(
              DocumentWidgetContainer::MessageType::ParseSettingsAreGuessedNotification,
              parseSettingsAreGuessedNotification);
          widget->GetContainer()->SetMessage(
              DocumentWidgetContainer::MessageType::ParseNotification,
              parseNotification + QObject::tr("\nAn unspecified parse error has occurred."));
        }
        document->GetTUPool()->PutTU(TU, false);
      });
    }
    return;
  }
  
  // (Approximately) determine whether the preamble changed.
  // TODO: It would be great if clang_reparseTranslationUnit() would simply
  //       return this piece of information.
  std::vector<ClangTU::IncludeWithModificationTime> newIncludes;
  newIncludes.reserve(512);
  clang_getInclusions(TU->TU(), &VisitInclusions_GetPathsAndLastModificationTimes, &newIncludes);
  
  std::vector<ClangTU::IncludeWithModificationTime>& fileIncludes = TU->GetIncludes();
  if (preambleIsLikelyUnchanged) {
    preambleIsLikelyUnchanged = fileIncludes.size() == newIncludes.size();
    if (preambleIsLikelyUnchanged) {
      for (int i = 0, size = fileIncludes.size(); i < size; ++ i) {
        if (fileIncludes[i].lastModificationTime != newIncludes[i].lastModificationTime ||
            fileIncludes[i].path != newIncludes[i].path) {
          preambleIsLikelyUnchanged = false;
          break;
        }
      }
    }
  }
  fileIncludes.swap(newIncludes);
  
  // (Re-)index the file. Perform the include update in the main thread, but
  // the USR update in the parsing thread. The include file update is only
  // necessary if !preambleIsLikelyUnchanged.
  // TODO: For the likely-unchanged-preamble check, we already collect the
  //       list of included files. Use that in IndexFile_GetInclusions() instead
  //       of iterating over the inclusions via libclang again?
  if (!preambleIsLikelyUnchanged) {
    RunInQtThreadBlocking([&]() {
      // Note: We do not exit here if the document has been closed in the
      // meantime, as we always want to update the file's indexing information,
      // even if it got closed.
      
      // First, check whether the file acts as a source file in a project.
      SourceFile* sourceFile = nullptr;
      std::shared_ptr<Project> usedProject = nullptr;
      for (auto& project : mainWindow->GetProjects()) {
        sourceFile = project->GetSourceFile(canonicalPath);
        if (sourceFile) {
          usedProject = project;
          break;
        }
      }
      // If the file is a project source file, update its list of included files.
      USRStorage::Instance().Lock();
      if (sourceFile) {
        IndexFile_GetInclusions(TU->TU(), sourceFile, usedProject.get(), mainWindow);
      }
      USRStorage::Instance().Unlock();
      // Note: We cannot leave the USRStorage locked here, since the code below
      // will be executed in another thread.
    });
  }
  
  // Update USRs with the TU.
  // TODO: For headers, the USR update may be only partial. This is because
  //       headers might be included by different files with different pre-
  //       processor defines set, yielding different USRs. Updating with a
  //       single TU corresponds to using only one set of pre-processor
  //       defines. For a correct update, we would need to parse the header
  //       with all used configurations after it is edited. But that seems
  //       infeasible.
  USRStorage::Instance().Lock();
  IndexFile_StoreUSRs(TU->TU(), preambleIsLikelyUnchanged);
  // USRStorage::Instance().DebugPrintInfo();
  USRStorage::Instance().Unlock();
  
  // Indexing finished, so we can return if we do not have a document.
  if (!document) {
    return;
  }
  
  // Prepare AST visitor data
  HighlightingASTVisitorData visitorData;
  visitorData.document = document;
  visitorData.TU = TU->TU();
  visitorData.file = clang_getFile(TU->TU(), canonicalPath.toUtf8().data());
  visitorData.lineOffsets = &lineOffsets;
  visitorData.prevCursor = clang_getNullCursor();
  visitorData.perVariableColoring = usePerVariableColoring;
  
  // Build a CXSourceRange for the whole document
  CXSourceLocation startLocation = clang_getLocationForOffset(
      visitorData.TU, visitorData.file, 0);
  CXSourceLocation endLocation = clang_getLocationForOffset(
      visitorData.TU, visitorData.file, utf8FileSize);
  CXSourceRange clangRange = clang_getRange(startLocation, endLocation);
  
  // Tokenize the document
  CXToken* tokens;
  unsigned numTokens;
  clang_tokenize(visitorData.TU, clangRange, &tokens, &numTokens);
  
  // Find comment marker ranges while in the background thread
  std::vector<DocumentRange> commentMarkerRanges;
  FindCommentMarkerRanges(tokens, numTokens, &visitorData, &commentMarkerRanges);
  
  RunInQtThreadBlocking([&]() {
    // If the document has been closed in the meantime, we must not access it or
    // its widget anymore.
    if (!ParseThreadPool::Instance().IsDocumentBeingParsed(document)) {
      clang_disposeTokens(visitorData.TU, tokens, numTokens);
      exit = true;
      return;
    }
    if (parsedDocumentVersion != document->version()) {
      // Do the next reparse instead. It should already have been triggered.
      // TODO: We could instead try to adjust the ranges to the changes in the
      //       document here
      clang_disposeTokens(visitorData.TU, tokens, numTokens);
      document->GetTUPool()->PutTU(TU, true);
      exit = true;
      return;
    }
    
    DocumentWidget* widget = mainWindow->GetWidgetForDocument(document);
    
    // Re-check the parse settings for this file after the parse in case they were
    // guessed before the parse. If we know them for sure now, compare the settings.
    // If they are equal, we can drop the "parse settings were guessed" warning. If
    // they differ, we schedule a reparse.
    if (!parseSettingsAreGuessedNotification.isEmpty()) {
      std::vector<std::shared_ptr<Project>>& projects = mainWindow->GetProjects();
      std::shared_ptr<Project> usedProject = nullptr;
      for (auto& project : projects) {
        bool isGuess;
        int guessQuality;
        CompileSettings* fileSettings = project->FindSettingsForFile(canonicalPath, &isGuess, &guessQuality);
        
        if (fileSettings && !isGuess) {
          std::vector<QByteArray> commandLineArgs;
          commandLineArgs = fileSettings->BuildCommandLineArgs(true, canonicalPath, project.get());
          if (TU->CanBeReparsed(canonicalPath, commandLineArgs)) {
            // The compile settings are equal. Remove the warning about guessed compile settings.
            parseSettingsAreGuessedNotification = QStringLiteral("");
          } else {
            // The compile settings changed. Schedule a reparse for the document.
            if (widget) {
              widget->ParseFile();
            }
          }
          
          break;
        }
      }
    }
    
    if (widget) {
      widget->GetContainer()->SetMessage(
          DocumentWidgetContainer::MessageType::ParseSettingsAreGuessedNotification, parseSettingsAreGuessedNotification);
      widget->GetContainer()->SetMessage(
          DocumentWidgetContainer::MessageType::ParseNotification, parseNotification);
    }
    
    document->ClearHighlightRanges(/*layer*/ 0);
    document->ClearContexts();
    
    // Tokenize the whole document range in order to get keywords and comments
    // (which are not reported by clang_visitChildren() unfortunately)
    // TODO: Check the performance of this approach. If this takes too long,
    //       it would be possible to restrict the tokenization to smaller ranges
    //       (or even do it manually).
    AddTokenHighlighting(document, tokens, numTokens, &visitorData);
    clang_disposeTokens(visitorData.TU, tokens, numTokens);
    ApplyCommentMarkerRanges(document, commentMarkerRanges);
    
    // Visit the resulting AST, extract information for highlighting, and add it
    // to the document
    clang_visitChildren(clang_getTranslationUnitCursor(TU->TU()),
                        &VisitClangAST_AddHighlightingAndContexts, &visitorData);
    
    // Retrieve the problems and fix-its
    RetrieveDiagnostics(document, visitorData.file, TU, lineOffsets);
    
    // Return the TU back to the pool, signaling that it has been reparsed.
    document->GetTUPool()->PutTU(TU, true);
    
    // Notify the main window about the parse.
    mainWindow->DocumentParsed(document);
    
    document->FinishedHighlightingChanges();
  });
  if (exit) {
    return;
  }
}

CompileSettings* FindParseSettingsForFile(const QString& canonicalPath, const std::vector<std::shared_ptr<Project>>& projects, std::shared_ptr<Project>* usedProject, bool* settingsAreGuessed) {
  CompileSettings* settings = nullptr;
  bool localSettingsAreGuessed;
  if (!settingsAreGuessed) {
    settingsAreGuessed = &localSettingsAreGuessed;
  }
  *settingsAreGuessed = true;
  int bestGuessQuality = -1;
  
  *usedProject = nullptr;
  for (auto& project : projects) {
    bool isGuess;
    int guessQuality;
    CompileSettings* fileSettings = project->FindSettingsForFile(canonicalPath, &isGuess, &guessQuality);
    
    // TODO: Any preference in cases where multiple projects have a result which is not a guess?
    if (!settings || !isGuess || guessQuality > bestGuessQuality) {
      settings = fileSettings;
      *usedProject = project;
      *settingsAreGuessed = isGuess;
      if (settingsAreGuessed) {
        bestGuessQuality = guessQuality;
      }
    }
  }
  
  return settings;
}

void ParseFile(Document* document, MainWindow* mainWindow) {
  ParseAndOrIndexFileImpl("", document, mainWindow, false);
}

void ParseFileIfOpenElseIndex(const QString& canonicalPath, Document* document, MainWindow* mainWindow) {
  ParseAndOrIndexFileImpl(canonicalPath, document, mainWindow, true);
}


struct StoreDefinitionsVisitorData {
  bool updateTUFileOnly;
  CXFile TUFile;
  
  /// The file of the last visited cursor. The file of a new cursor can be
  /// compared to this. If equal, the cached lastFileUSRMap can be used.
  QString lastFile;
  
  /// Cached pointer to the USRMap of lastFile.
  USRMap* lastFileUSRMap;
};

CXChildVisitResult VisitClangAST_StoreUSRs(CXCursor cursor, CXCursor /*parent*/, CXClientData client_data) {
  StoreDefinitionsVisitorData* data = reinterpret_cast<StoreDefinitionsVisitorData*>(client_data);
  
  // If we need to update the TU file only, skip over everything outside of that
  // file.
  if (data->updateTUFileOnly) {
    CXSourceLocation location = clang_getCursorLocation(cursor);
    CXFile locationFile;
    clang_getFileLocation(location, &locationFile, nullptr, nullptr, nullptr);
    if (!clang_File_isEqual(locationFile, data->TUFile)) {
      return CXChildVisit_Continue;
    }
  }
  
  // Try to reduce the effort / memory use by only storing USRs for certain
  // cursor kinds.
  CXCursorKind kind = clang_getCursorKind(cursor);
  if (IsClassDeclLikeCursorKind(kind) ||
      kind == CXCursor_FunctionDecl ||
      kind == CXCursor_FunctionTemplate ||
      kind == CXCursor_CXXMethod ||
      kind == CXCursor_Constructor ||
      kind == CXCursor_Destructor ||
      kind == CXCursor_ConversionFunction ||
      kind == CXCursor_FieldDecl ||
      kind == CXCursor_VarDecl) {
    // Get the cursor's location.
    CXSourceLocation location = clang_getCursorLocation(cursor);
    CXFile locationFile;
    unsigned line;
    unsigned column;
    clang_getFileLocation(
        location,
        &locationFile,
        &line,
        &column,
        /*unsigned* offset*/ nullptr);
    
    // Get the current file's USRMap.
    USRMap* usrMap;
    QString filePath = GetClangFilePath(locationFile);
    if (filePath == data->lastFile) {
      usrMap = data->lastFileUSRMap;
    } else {
      data->lastFile = filePath;
      filePath = QFileInfo(filePath).canonicalFilePath();
      
      // Get or create the USR map for the file.
      usrMap = USRStorage::Instance().GetUSRMapForFile(filePath);
      data->lastFileUSRMap = usrMap;
      
      if (usrMap == nullptr) {
        // NOTE: This can happen if a header (that is not listed as a source
        //       file) is parsed before any source file is parsed that created
        //       the USRMaps seen by the header. So, this is not an error.
        //       (But maybe still a situation that we might want to improve:
        //        perhaps it could be useful to let each open file add their
        //        own references on USRMaps, not only project source files?)
        // qDebug() << "ERROR: While indexing, found no USRMap for file " << filePath << " of a cursor -> the USR cannot be stored. The existence of the USRMap should have been ensured in the included file list update.";
      }
    }
    
    if (usrMap) {
      // Build the USR.
      bool isDefinition =
          clang_isCursorDefinition(cursor) ||
          clang_Cursor_isFunctionInlined(cursor);
      
      // Store the USR if it does not exist already.
      bool existsAlready = false;
      QByteArray USR = ClangString(clang_getCursorUSR(cursor)).ToQByteArray();
      if (!USR.isEmpty()) {
        auto range = usrMap->map.equal_range(USR);
        for (auto it = range.first; it != range.second; ++ it) {
          if (it->second.line == line &&
              it->second.column == column) {
            existsAlready = true;
            break;
          }
        }
        if (!existsAlready) {
          // TODO: This is somewhat duplicated from the context creation code.
          // Use clang_getCursorPrettyPrinted() to get a "nice" version of the function spelling.
          // TODO: It would be preferable to get this as a semantic string, the same
          //       way that code completion results are delivered, such that we can
          //       easily semantically color the different parts. Not sure how easy
          //       this is with the current libclang interface though.
          CXPrintingPolicy printingPolicy = clang_getCursorPrintingPolicy(cursor);
          clang_PrintingPolicy_setProperty(printingPolicy, CXPrintingPolicy_TerseOutput, 1);  // print declaration only, skip body
          CXString cursorDisplayName = clang_getCursorPrettyPrinted(cursor, printingPolicy);
          clang_PrintingPolicy_dispose(printingPolicy);
          QString displayName = QString::fromUtf8(clang_getCString(cursorDisplayName));
          clang_disposeString(cursorDisplayName);
          if (displayName.endsWith(QStringLiteral(" {}"))) {
            displayName.chop(3);
          } else if (displayName.endsWith(QStringLiteral(" {\n}"))) {
            displayName.chop(4);
          }
          
          // Try to find the name within the displayName (TODO: Is there any way to do this without heuristics?).
          QString name = ClangString(clang_getCursorSpelling(cursor)).ToQString();
          int namePos = -1;
          if (!name.isEmpty()) {
            int from = 0;
            while (from + name.size() <= displayName.size()) {
              int pos = displayName.indexOf(name, from, Qt::CaseSensitive);
              if (pos < 0) {
                break;
              }
              
              namePos = pos;
              // If the match seems to be good, stop looking for other matches
              if (pos > 0 && (displayName[pos - 1] == ' ' || displayName[pos - 1] == ':')) {
                break;
              }
              from = pos + name.size();
            }
          }
          
          usrMap->map.insert(std::make_pair(
              USR,
              USRDecl(displayName, line, column, isDefinition, kind, namePos, name.size())));
        }
      }
    }
  }
  
  //   qDebug() << "Type: " << ClangString(clang_getCursorKindSpelling(clang_getCursorKind(cursor))).ToQString()
  //            << ", Spelling: " << ClangString(clang_getCursorSpelling(cursor)).ToQString()
  //            << ", USR: " << ClangString(clang_getCursorUSR(cursor)).ToQString();
  
  if (kind == CXCursor_Namespace ||
      kind == CXCursor_UnexposedDecl ||  // this is required to recurse into: extern "C" { ... }
      IsClassDeclLikeCursorKind(kind)) {
    return CXChildVisit_Recurse;
  } else {
    return CXChildVisit_Continue;
  }
}

void IndexFile_StoreUSRs(CXTranslationUnit clangTU, bool onlyForTUFile) {
  // Clear USRs of this file.
  QString TUFilePath = QFileInfo(ClangString(clang_getTranslationUnitSpelling(clangTU)).ToQString()).canonicalFilePath();
  USRStorage::Instance().ClearUSRsForFile(TUFilePath);
  
  // Visit the AST to collect definitions / declarations for cross-referencing
  // with the corresponding definitions / declarations seen in other
  // translations units.
  StoreDefinitionsVisitorData visitorData;
  visitorData.updateTUFileOnly = onlyForTUFile;
  visitorData.TUFile = clang_getFile(clangTU, TUFilePath.toUtf8().data());
  clang_visitChildren(
      clang_getTranslationUnitCursor(clangTU),
      &VisitClangAST_StoreUSRs,
      &visitorData);
}

void VisitInclusionsForIndexing(
    CXFile included_file,
    CXSourceLocation* /*inclusion_stack*/,
    unsigned /*include_len*/,
    CXClientData client_data) {
  std::unordered_set<QString>* includedPaths = reinterpret_cast<std::unordered_set<QString>*>(client_data);
  includedPaths->insert(QFileInfo(GetClangFilePath(included_file)).canonicalFilePath());
}

void IndexFile_GetInclusions(CXTranslationUnit clangTU, SourceFile* sourceFile, Project* project, MainWindow* mainWindow) {
  // Clear included paths
  std::unordered_set<QString> oldIncludedPaths;
  oldIncludedPaths.swap(sourceFile->includedPaths);
  
  // Iterate over all file inclusions to collect the list of included files.
  clang_getInclusions(clangTU, &VisitInclusionsForIndexing, &sourceFile->includedPaths);
  
  // Add references to newly included files
  for (const QString& newPath : sourceFile->includedPaths) {
    if (oldIncludedPaths.count(newPath) == 0) {
      // Add reference.
      bool newUSRMapCreated = USRStorage::Instance().AddUSRMapReference(newPath);
      
      // If this include file is open in the editor, and its compile settings
      // were guessed before, then this has changed now. We compare the old
      // compile settings to the new ones and schedule a reparse if they differ,
      // respectively remove the warning about guessed compile settings if they
      // are the same.
      if (newUSRMapCreated) {
        Document* document;
        DocumentWidget* widget;
        if (mainWindow->GetDocumentAndWidgetForPath(newPath, &document, &widget)) {
          std::shared_ptr<ClangTU> includeTU = document->GetTUPool()->TakeMostUpToDateTU();
          if (includeTU) {
            if (includeTU->isInitialized()) {
              bool isGuess;
              int guessQuality;
              CompileSettings* fileSettings = project->FindSettingsForFile(newPath, &isGuess, &guessQuality);
              if (!fileSettings || isGuess) {
                qDebug() << "Error: After adding an include to the list of includes of a SourceFile, querying project->FindSettingsForFile() for this include returned none or guessed compile settings. fileSettings:" << fileSettings << ", isGuess:" << isGuess;
              } else {
                std::vector<QByteArray> commandLineArgs;
                commandLineArgs = fileSettings->BuildCommandLineArgs(true, newPath, project);
                if (includeTU->CanBeReparsed(newPath, commandLineArgs)) {
                  // The compile settings are equal. Remove the warning about guessed compile settings.
                  widget->GetContainer()->SetMessage(
                      DocumentWidgetContainer::MessageType::ParseSettingsAreGuessedNotification,
                      QStringLiteral(""));
                } else {
                  // The compile settings changed. Schedule a reparse for the document.
                  widget->ParseFile();
                }
              }
            }
            document->GetTUPool()->PutTU(includeTU, false);
          } else {
            // TODO: Currently we do nothing if both TUs of the document are in use here. It would be good to
            //       decide properly in this case as well. Maybe store the most recent parse settings in the
            //       TU pool in order to keep them accessible even when all TUs are taken? However, this is a
            //       minor issue, since both TUs being in use means that the file is being reparsed at the
            //       moment anyways, so it will arrive at a good state soon.
          }
        }
      }
    }
  }
  
  // Remove references to files that had been included, but are not included anymore now
  for (const QString& oldPath : oldIncludedPaths) {
    if (sourceFile->includedPaths.count(oldPath) == 0) {
      // Remove reference.
      USRStorage::Instance().RemoveUSRMapReference(oldPath);
    }
  }
}


USRStorage& USRStorage::Instance() {
  static USRStorage instance;
  return instance;
}

void USRStorage::ClearUSRsForFile(const QString& canonicalPath) {
  auto it = USRs.find(canonicalPath.toUtf8());
  if (it != USRs.end()) {
    it->second->map.clear();
    it->second->map.reserve(32);
  }
}

bool USRStorage::AddUSRMapReference(const QString& canonicalPath) {
  auto it = USRs.insert(std::make_pair(canonicalPath, nullptr)).first;
  if (it->second) {
    ++ it->second->referenceCount;
    return false;
  } else {
    it->second.reset(new USRMap());
    it->second->referenceCount = 1;
    it->second->map.reserve(32);
    return true;
  }
}

void USRStorage::RemoveUSRMapReference(const QString& canonicalPath) {
  auto it = USRs.find(canonicalPath);
  if (it != USRs.end()) {
    USRMap* usrMap = it->second.get();
    if (usrMap->referenceCount == 1) {
      // Delete the USRMap.
      USRs.erase(it);
    } else {
      -- usrMap->referenceCount;
    }
  } else {
    qDebug() << "ERROR: RemoveUSRMapReference() called for a path for which no USRMap exists";
  }
}

void USRStorage::GetFilesForUSRLookup(const QString& canonicalPath, MainWindow* mainWindow, std::unordered_set<QString>* relevantFiles) {
  // Get all targets that contain or include that file, as well as the
  // targets that these depend on
  std::set<const Target*> relevantTargets;
  for (auto& project : mainWindow->GetProjects()) {
    for (int targetIndex = 0; targetIndex < project->GetNumTargets(); ++ targetIndex) {
      const Target& target = project->GetTarget(targetIndex);
      if (target.ContainsOrIncludesFile(canonicalPath)) {
        relevantTargets.insert(&target);
        for (Target* dependency : target.dependencies) {
          relevantTargets.insert(dependency);
        }
      }
    }
  }
  
  // Assemble all files contained in or included by these targets
  relevantFiles->reserve(2048);
  for (const Target* target : relevantTargets) {
    for (const SourceFile& source : target->sources) {
      relevantFiles->insert(source.path);
      for (const QString& includedPath : source.includedPaths) {
        relevantFiles->insert(includedPath);
      }
    }
  }
}

void USRStorage::LookupUSRs(const QByteArray& USR, std::unordered_set<QString> relevantFiles, std::vector<std::pair<QString, USRDecl>>* foundDecls) {
  foundDecls->reserve(8);
  
  USRStorage::Instance().Lock();
  for (const QString& path : relevantFiles) {
    USRMap* usrMap = USRStorage::Instance().GetUSRMapForFile(path);
    if (!usrMap) {
      continue;
    }
    
    auto range = usrMap->map.equal_range(USR);
    for (auto it = range.first; it != range.second; ++ it) {
      // Found an occurrence of the USR.
      // TODO: Is this redundancy check still necessary? Each file's USRs should only be stored once now, so I think we can drop it.
      bool existsAlready = false;
      for (const std::pair<QString, USRDecl>& existingDecl : *foundDecls) {
        if (existingDecl.first == path &&
            existingDecl.second.line == it->second.line &&
            existingDecl.second.column == it->second.column) {
          existsAlready = true;
          break;
        }
      }
      
      if (!existsAlready) {
        // qDebug() << "Adding:" << path << ":" << it->second.line << ":" << it->second.column;
        foundDecls->push_back(std::make_pair(path, it->second));
      }
    }
  }
  USRStorage::Instance().Unlock();
}
