// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

// The DefaultTBuiltInResource values were copied from a file with the
// following license:
//
// Copyright (C) 2016 Google, Inc.
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//    Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
//    Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
//    Neither the name of Google Inc. nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "cide/glsl_parser.h"

#include "glslang/Public/ShaderLang.h"

#include "cide/document.h"
#include "cide/glsl_highlighting.h"
#include "cide/main_window.h"
#include "cide/parse_thread_pool.h"
#include "cide/qt_thread.h"

// NOTE: Copied from third_party/glslang/StandAlone/ResourceLimits.cpp
const TBuiltInResource DefaultTBuiltInResource = {
    /* .MaxLights = */ 32,
    /* .MaxClipPlanes = */ 6,
    /* .MaxTextureUnits = */ 32,
    /* .MaxTextureCoords = */ 32,
    /* .MaxVertexAttribs = */ 64,
    /* .MaxVertexUniformComponents = */ 4096,
    /* .MaxVaryingFloats = */ 64,
    /* .MaxVertexTextureImageUnits = */ 32,
    /* .MaxCombinedTextureImageUnits = */ 80,
    /* .MaxTextureImageUnits = */ 32,
    /* .MaxFragmentUniformComponents = */ 4096,
    /* .MaxDrawBuffers = */ 32,
    /* .MaxVertexUniformVectors = */ 128,
    /* .MaxVaryingVectors = */ 8,
    /* .MaxFragmentUniformVectors = */ 16,
    /* .MaxVertexOutputVectors = */ 16,
    /* .MaxFragmentInputVectors = */ 15,
    /* .MinProgramTexelOffset = */ -8,
    /* .MaxProgramTexelOffset = */ 7,
    /* .MaxClipDistances = */ 8,
    /* .MaxComputeWorkGroupCountX = */ 65535,
    /* .MaxComputeWorkGroupCountY = */ 65535,
    /* .MaxComputeWorkGroupCountZ = */ 65535,
    /* .MaxComputeWorkGroupSizeX = */ 1024,
    /* .MaxComputeWorkGroupSizeY = */ 1024,
    /* .MaxComputeWorkGroupSizeZ = */ 64,
    /* .MaxComputeUniformComponents = */ 1024,
    /* .MaxComputeTextureImageUnits = */ 16,
    /* .MaxComputeImageUniforms = */ 8,
    /* .MaxComputeAtomicCounters = */ 8,
    /* .MaxComputeAtomicCounterBuffers = */ 1,
    /* .MaxVaryingComponents = */ 60,
    /* .MaxVertexOutputComponents = */ 64,
    /* .MaxGeometryInputComponents = */ 64,
    /* .MaxGeometryOutputComponents = */ 128,
    /* .MaxFragmentInputComponents = */ 128,
    /* .MaxImageUnits = */ 8,
    /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
    /* .MaxCombinedShaderOutputResources = */ 8,
    /* .MaxImageSamples = */ 0,
    /* .MaxVertexImageUniforms = */ 0,
    /* .MaxTessControlImageUniforms = */ 0,
    /* .MaxTessEvaluationImageUniforms = */ 0,
    /* .MaxGeometryImageUniforms = */ 0,
    /* .MaxFragmentImageUniforms = */ 8,
    /* .MaxCombinedImageUniforms = */ 8,
    /* .MaxGeometryTextureImageUnits = */ 16,
    /* .MaxGeometryOutputVertices = */ 256,
    /* .MaxGeometryTotalOutputComponents = */ 1024,
    /* .MaxGeometryUniformComponents = */ 1024,
    /* .MaxGeometryVaryingComponents = */ 64,
    /* .MaxTessControlInputComponents = */ 128,
    /* .MaxTessControlOutputComponents = */ 128,
    /* .MaxTessControlTextureImageUnits = */ 16,
    /* .MaxTessControlUniformComponents = */ 1024,
    /* .MaxTessControlTotalOutputComponents = */ 4096,
    /* .MaxTessEvaluationInputComponents = */ 128,
    /* .MaxTessEvaluationOutputComponents = */ 128,
    /* .MaxTessEvaluationTextureImageUnits = */ 16,
    /* .MaxTessEvaluationUniformComponents = */ 1024,
    /* .MaxTessPatchComponents = */ 120,
    /* .MaxPatchVertices = */ 32,
    /* .MaxTessGenLevel = */ 64,
    /* .MaxViewports = */ 16,
    /* .MaxVertexAtomicCounters = */ 0,
    /* .MaxTessControlAtomicCounters = */ 0,
    /* .MaxTessEvaluationAtomicCounters = */ 0,
    /* .MaxGeometryAtomicCounters = */ 0,
    /* .MaxFragmentAtomicCounters = */ 8,
    /* .MaxCombinedAtomicCounters = */ 8,
    /* .MaxAtomicCounterBindings = */ 1,
    /* .MaxVertexAtomicCounterBuffers = */ 0,
    /* .MaxTessControlAtomicCounterBuffers = */ 0,
    /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
    /* .MaxGeometryAtomicCounterBuffers = */ 0,
    /* .MaxFragmentAtomicCounterBuffers = */ 1,
    /* .MaxCombinedAtomicCounterBuffers = */ 1,
    /* .MaxAtomicCounterBufferSize = */ 16384,
    /* .MaxTransformFeedbackBuffers = */ 4,
    /* .MaxTransformFeedbackInterleavedComponents = */ 64,
    /* .MaxCullDistances = */ 8,
    /* .MaxCombinedClipAndCullDistances = */ 8,
    /* .MaxSamples = */ 4,
    /* .maxMeshOutputVerticesNV = */ 256,
    /* .maxMeshOutputPrimitivesNV = */ 512,
    /* .maxMeshWorkGroupSizeX_NV = */ 32,
    /* .maxMeshWorkGroupSizeY_NV = */ 1,
    /* .maxMeshWorkGroupSizeZ_NV = */ 1,
    /* .maxTaskWorkGroupSizeX_NV = */ 32,
    /* .maxTaskWorkGroupSizeY_NV = */ 1,
    /* .maxTaskWorkGroupSizeZ_NV = */ 1,
    /* .maxMeshViewCountNV = */ 4,
    /* .maxDualSourceDrawBuffersEXT = */ 1,

    /* .limits = */ {
        /* .nonInductiveForLoops = */ 1,
        /* .whileLoops = */ 1,
        /* .doWhileLoops = */ 1,
        /* .generalUniformIndexing = */ 1,
        /* .generalAttributeMatrixVectorIndexing = */ 1,
        /* .generalVaryingIndexing = */ 1,
        /* .generalSamplerIndexing = */ 1,
        /* .generalVariableIndexing = */ 1,
        /* .generalConstantMatrixVectorIndexing = */ 1,
    }};

ParserGlobal::~ParserGlobal() {
  glslang::FinalizeProcess();
}

ParserGlobal::ParserGlobal() {
  glslang::InitializeProcess();
}

static void RetrieveDiagnostics(Document* document, const char* infoLog, const std::vector<unsigned>& lineOffsets) {
  document->ClearProblems();
  
  // Clear warning/error line attributes
  Document::LineIterator lineIt(document);
  while (lineIt.IsValid()) {
    lineIt.SetAttributes(lineIt.GetAttributes() & ~(static_cast<int>(LineAttribute::Warning) | static_cast<int>(LineAttribute::Error)));
    ++ lineIt;
  }
  
  // Parse possible issue output from the info log.
  // In case of compile errors, the content of the into log looks like:
  //
  //  ERROR: /home/thomas/.../shaderfile.comp:25: 'f' : undeclared identifier 
  // ERROR: /home/thomas/.../shaderfile.comp:27: '' : compilation terminated 
  // ERROR: 2 compilation errors.  No code generated.
  
  // TODO: Parse the filename that the errors come from. Properly show errors that come from included files after the corresponding #include directive.
  
  QString infoString(infoLog);
  
  int lineStart = 0;
  
  auto parseLine = [&](const QStringRef& line) {
    if (line.isEmpty() || line.endsWith(QStringLiteral("compilation errors.  No code generated."))) {
      return;
    }
    
    if (!line.startsWith("ERROR: ")) {
      qDebug() << "GLSL parser: Failed to parse info log line:\n" << line;
      return;
    }
    
    // Find line number
    bool parsed = false;
    for (int i = 7, size = line.size(); i < size; ++ i) {
      if (line[i] == ':') {
        // Check if the line number starts here
        QString lineNumberText;
        bool foundTerminatingColon = false;
        int errorStringStart = 0;
        for (int k = i + 1; k < size - 1; ++ k) {
          QChar ch = line[k];
          if (ch >= '0' && ch <= '9') {
            lineNumberText += ch;
            continue;
          } else if (ch == ':' && line[k + 1] == ' ') {
            foundTerminatingColon = true;
            errorStringStart = k + 2;
          }
          break;
        }
        
        if (foundTerminatingColon) {
          // Add the problem
          int lineNumber = lineNumberText.toInt() - 1;
          unsigned lineStartOffset = 0;
          unsigned lineEndOffset = 0;
          if (lineNumber < lineOffsets.size()) {
            lineStartOffset = lineOffsets[lineNumber];
          }
          if (lineNumber + 1 < lineOffsets.size()) {
            lineEndOffset = lineOffsets[lineNumber + 1] - 1;
          } else {
            lineEndOffset = document->FullDocumentRange().end.offset;
          }
          QString errorString = line.mid(errorStringStart).trimmed().toString();
          
          if (errorString != QStringLiteral("compilation terminated") &&
              errorString != QStringLiteral("'' : compilation terminated")) {
            document->AddLineAttributes(lineNumber, static_cast<int>(LineAttribute::Error));
            
            std::shared_ptr<Problem> newProblem(new Problem(Problem::Type::Error, lineNumber + 1, /*col*/ 0, lineStartOffset, errorString, document->path()));
            int problemIndex = document->AddProblem(newProblem);
            
            // Add a problem range such that the problem is displayed.
            // Since we do not get exact range information here, use the whole line as problem range.
            document->AddProblemRange(problemIndex, DocumentRange(lineStartOffset, lineEndOffset));
          }
          
          parsed = true;
        }
      }
      
      if (parsed) {
        break;
      }
    }
    
    if (!parsed) {
      qDebug() << "GLSL parser: Failed to parse info log line:\n" << line;
      return;
    }
  };
  
  for (int c = 0, size = infoString.size(); c < size; ++ c) {
    QChar ch = infoString[c];
    if (ch == '\r' && c < size - 1 && infoString[c + 1] == '\n') {
      parseLine(QStringRef(&infoString, lineStart, c - lineStart));
      lineStart = c + 2;
      ++ c;
    } else if (ch == '\n') {
      parseLine(QStringRef(&infoString, lineStart, c - lineStart));
      lineStart = c + 1;
    }
  }
  
  if (lineStart < infoString.size()) {
    parseLine(QStringRef(&infoString, lineStart, infoString.size() - lineStart));
  }
}

class GLSLIncluder : public glslang::TShader::Includer {
 public:
  GLSLIncluder(MainWindow* mainWindow)
      : mainWindow(mainWindow) {}
  
  IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t inclusionDepth) override {
    // Prevent infinite recursion
    if (inclusionDepth > 20) {
      std::string* errorMsg = new std::string("Inclusion depth is larger than 20. Failing GLSL inclusion to prevent possible infinite recursion.");
      // Leaving the headerName field of the IncludeResult empty is interpreted as returning an error message.
      return new IncludeResult(/*headerName*/ "", errorMsg->c_str(), errorMsg->size(), /*userData*/ errorMsg);
    }
    
    // Build the included path
    QString headerString = headerName;
    
    QString includedPath;
    if (!headerString.isEmpty() && headerString[0] == '/') {
      // Absolute path
      includedPath = headerString;
    } else {
      // Relative path. We always interpret it as being relative to the includer-file's directory.
      // TODO: Should we allow to specify a list of include directories for GLSL files?
      includedPath = QFileInfo(includerName).dir().absoluteFilePath(headerString);
    }
    includedPath = QFileInfo(includedPath).canonicalFilePath();
    
    // qDebug() << "GLSL includer headerName:" << headerName;
    // qDebug() << "GLSL includer includerName:" << includerName;
    // qDebug() << "GLSL includer includedPath:" << includedPath;
    
    // Try to find the included file among the open documents
    std::string* includedText;
    bool includeResolved = false;
    
    RunInQtThreadBlocking([&]() {
      Document* includedDocument = nullptr;
      DocumentWidget* includedDocumentWidget = nullptr;
      if (mainWindow->GetDocumentAndWidgetForPath(includedPath, &includedDocument, &includedDocumentWidget)) {
        includedText = new std::string(includedDocument->GetDocumentText().toStdString());
        includeResolved = true;
      }
    });
    
    // If the file was not among the open documents, try to load it from disk
    if (!includeResolved) {
      QFile includedFile(includedPath);
      if (includedFile.open(QFile::ReadOnly | QFile::Text)) {
        includedText = new std::string(QString::fromUtf8(includedFile.readAll()).toStdString());  // TODO: Support other encodings than UTF-8
        includeResolved = true;
      }
    }
    
    if (includeResolved) {
      return new IncludeResult(includedPath.toStdString(), includedText->data(), includedText->size(), /*userData*/ includedText);
    } else {
      std::string* errorMsg = new std::string("Could not open file: ");
      *errorMsg += includedPath.toStdString();
      // Leaving the headerName field of the IncludeResult empty is interpreted as returning an error message.
      return new IncludeResult(/*headerName*/ "", errorMsg->c_str(), errorMsg->size(), /*userData*/ errorMsg);
    }
  }
  
  void releaseInclude(IncludeResult* result) override {
    if (result) {
      delete reinterpret_cast<std::string*>(result->userData);
      delete result;
    }
  }
  
 private:
  MainWindow* mainWindow;
};

void ParseGLSLFile(const QString& /*canonicalPath*/, Document* document, MainWindow* mainWindow) {
  if (!document) {
    qDebug() << "Null document passed to ParseGLSLFile()";
    return;
  }
  
  int parsedDocumentVersion = -1;
  QString documentContentQString;
  std::string documentFilePath;
  std::vector<unsigned> lineOffsets;
  bool exit = false;
  
  // Get the current document version and document text
  RunInQtThreadBlocking([&]() {
    // If the document has been closed in the meantime, we must not access it or
    // its widget anymore.
    if (!ParseThreadPool::Instance().IsDocumentBeingParsed(document)) {
      exit = true;
      return;
    }
    
    parsedDocumentVersion = document->version();
    documentContentQString = document->GetDocumentText();
    documentFilePath = QFileInfo(document->path()).canonicalFilePath().toStdString();
    
    // Get the newline positions of the main file to be able to map the "line, column"
    // positions to offsets in our UTF-16 (QString) version of the document.
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
  });
  if (exit) {
    return;
  }
  
  // Determine the shader stage from the filename
  QString shaderStageString = QString::fromStdString(documentFilePath);
  if (shaderStageString.endsWith(".glsl", Qt::CaseInsensitive)) {
    shaderStageString.chop(5);
  }
  
  EShLanguage shaderStage = EShLangCount;
  if (shaderStageString.endsWith(QStringLiteral(".vert"), Qt::CaseInsensitive)) {
    shaderStage = EShLangVertex;
  } else if (shaderStageString.endsWith(QStringLiteral(".tesc"), Qt::CaseInsensitive)) {
    shaderStage = EShLangTessControl;
  } else if (shaderStageString.endsWith(QStringLiteral(".tese"), Qt::CaseInsensitive)) {
    shaderStage = EShLangTessEvaluation;
  } else if (shaderStageString.endsWith(QStringLiteral(".geom"), Qt::CaseInsensitive)) {
    shaderStage = EShLangGeometry;
  } else if (shaderStageString.endsWith(QStringLiteral(".frag"), Qt::CaseInsensitive)) {
    shaderStage = EShLangFragment;
  } else if (shaderStageString.endsWith(QStringLiteral(".comp"), Qt::CaseInsensitive)) {
    shaderStage = EShLangCompute;
  } else if (shaderStageString.endsWith(QStringLiteral(".rgen"), Qt::CaseInsensitive)) {
    shaderStage = EShLangRayGen;
  } else if (shaderStageString.endsWith(QStringLiteral(".rint"), Qt::CaseInsensitive)) {
    shaderStage = EShLangIntersect;
  } else if (shaderStageString.endsWith(QStringLiteral(".rahit"), Qt::CaseInsensitive)) {
    shaderStage = EShLangAnyHit;
  } else if (shaderStageString.endsWith(QStringLiteral(".rchit"), Qt::CaseInsensitive)) {
    shaderStage = EShLangClosestHit;
  } else if (shaderStageString.endsWith(QStringLiteral(".rmiss"), Qt::CaseInsensitive)) {
    shaderStage = EShLangMiss;
  } else if (shaderStageString.endsWith(QStringLiteral(".rcall"), Qt::CaseInsensitive)) {
    shaderStage = EShLangCallable;
  } else if (shaderStageString.endsWith(QStringLiteral(".mesh"), Qt::CaseInsensitive)) {
    shaderStage = EShLangMeshNV;
  } else if (shaderStageString.endsWith(QStringLiteral(".task"), Qt::CaseInsensitive)) {
    shaderStage = EShLangTaskNV;
  } else {
    qDebug() << "Failed to determine the shader stage based on the file extension of the file: " << QString::fromStdString(documentFilePath);
    return;
  }
  
  // Ensure that glslang is initialized
  ParserGlobal::Instance();
  
  // Parse the file
  glslang::TShader shader(shaderStage);
  
  std::string documentContent = documentContentQString.toStdString();
  const char* sourceStrings = documentContent.c_str();
  int sourceLengths = documentContent.size();
  const char* sourceNames = documentFilePath.c_str();
  shader.setStringsWithLengthsAndNames(
      &sourceStrings,
      &sourceLengths,
      &sourceNames,
      /*n*/ 1);
  
  // Note: It seems that the version should be always 100 currently, judging from the glslang StandAlone code.
  // TODO: Allow the user to configure the client type (Vulkan vs. GLSL)
  shader.setEnvInput(glslang::EShSourceGlsl, shaderStage, glslang::EShClientVulkan, /*version*/ 100);
  
  // TODO: Allow the user to configure the target version type (Vulkan 1.0, 1.1, or 1.2)
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
  
  // TODO: Allow the user to configure the target SPIR-V type
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
  
  // TODO: Allow the user to configure a config file to load the resource limits from
  TBuiltInResource resources = DefaultTBuiltInResource;
  
  GLSLIncluder includer(mainWindow);
  
  /*bool parseSuccess =*/ shader.parse(
      &resources,
      /*defaultVersion*/ 110,  // default shader version used when there is no "#version" in the shader itself
      /*forwardCompatible*/ false,  // if true, use of deprecated features results in errors
      /*messages*/ static_cast<EShMessages>(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules | EShMsgKeepUncalled | EShMsgCascadingErrors),  // TODO: What are the best flags to use here? EShMsgRelaxedErrors?
      includer);
  
  // For debugging:
  // qDebug() << "GLSL parse success: " << parseSuccess;
  // qDebug() << "GLSL info log:\n" << shader.getInfoLog();
  
  RunInQtThreadBlocking([&]() {
    // If the document has been closed in the meantime, we must not access it or
    // its widget anymore.
    if (!ParseThreadPool::Instance().IsDocumentBeingParsed(document)) {
      exit = true;
      return;
    }
    if (parsedDocumentVersion != document->version()) {
      // Do the next reparse instead. It should already have been triggered.
      // TODO: We could instead try to adjust the ranges to the changes in the
      //       document here
      exit = true;
      return;
    }
    
    // DocumentWidget* widget = mainWindow->GetWidgetForDocument(document);
    
    RetrieveDiagnostics(document, shader.getInfoLog(), lineOffsets);
    
    document->ClearHighlightRanges(/*layer*/ 0);
    document->ClearContexts();
    
    AddGLSLHighlighting(document, documentContentQString, shader.getIntermediate(), lineOffsets);
  });
  if (exit) {
    return;
  }
}
