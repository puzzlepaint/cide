// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/clang_highlighting.h"

#include <QColor>

#include "cide/clang_utils.h"
#include "cide/document.h"
#include "cide/settings.h"
#include "cide/text_utils.h"

// // These colors are taken from: https://ethanschoonover.com/solarized/
// constexpr int kPerVariableColorPoolSize = 16;
// QColor perVariableColorPool[kPerVariableColorPoolSize] = {
//     qRgb(  0,  43,  54),
//     qRgb(  7,  54,  66),
//     qRgb( 88, 110, 117),
//     qRgb(101, 123, 131),
//     qRgb(131, 148, 150),
//     qRgb(147, 161, 161),
//     qRgb(238, 232, 213),
//     qRgb(253, 246, 227),
//     qRgb(181, 137,   0),
//     qRgb(203,  75,  22),
//     qRgb(220,  50,  47),
//     qRgb(211,  54, 130),
//     qRgb(108, 113, 196),
//     qRgb( 38, 139, 210),
//     qRgb( 42, 161, 152),
//     qRgb(133, 153,   0)};

// // These colors are taken from: http://colorbrewer2.org/#type=qualitative&scheme=Paired&n=12
// // This seems well-suited, but some of the colors are a bit bright for our purpose.
// constexpr int kPerVariableColorPoolSize = 10;
// QColor perVariableColorPool[kPerVariableColorPoolSize] = {
//     qRgb(51,160,44),  // medium green
//     qRgb(178,223,138),  // light green
//     qRgb(31,120,180),  // medium blue
//     qRgb(166,206,227),  // light blue
//     qRgb(227,26,28),  // dark red
//     qRgb(251,154,153),  // beige
//     qRgb(106,61,154),  // dark purple
//     qRgb(202,178,214),  // grayish purple
//     qRgb(177,89,40), // brown
//     qRgb(253,191,111),  // light orange
//     // qRgb(255,127,0),  // saturated orange, too similar to our "class" color
//     // qRgb(255,255,153),  // yellow, too bright
//     };

// // These colors are taken from: https://sashat.me/2017/01/11/list-of-20-simple-distinct-colors/
// constexpr int kPerVariableColorPoolSize = 14;
// QColor perVariableColorPool[kPerVariableColorPoolSize] = {
//     qRgb(60, 180, 75),  // green
//     qRgb(230, 25, 75),  // red
//     // qRgb(255, 225, 25),  // yellow, too bright
//     // qRgb(0, 130, 200),  // blue, similar to our "function" color
//     // qRgb(245, 130, 48),  // orange, similar to our "class" color
//     qRgb(145, 30, 180),  // purple
//     qRgb(70, 240, 240),  // cyan
//     qRgb(240, 50, 230),  // magenta
//     qRgb(210, 245, 60),  // lime
//     qRgb(250, 190, 190),  // pink
//     qRgb(0, 128, 128),  // teal
//     qRgb(230, 190, 255),  // lavender
//     qRgb(170, 110, 40),  // brown
//     // qRgb(255, 250, 200),  // beige, too bright
//     qRgb(128, 0, 0),  // maroon
//     qRgb(170, 255, 195),  // mint
//     qRgb(128, 128, 0),  // olive
//     // qRgb(255, 215, 180),  // apricot, bright and similar to our "class" color
//     qRgb(0, 0, 128),  // navy
//     // qRgb(128, 128, 128),  // gray
//     // qRgb(255, 255, 255),  // white
//     // qRgb(0, 0, 0)  // black
// };

// These colors are taken from:
// P. Green-Armytage (2010): A Colour Alphabet and the Limits of Colour Coding. // Colour: Design & Creativity (5) (2010): 10, 1-23
constexpr int kPerVariableColorPoolSize = 15;
QColor perVariableColorPool[kPerVariableColorPoolSize] = {
    qRgb(255,0,16),  // red
    qRgb(0,117,220),  // medium blue
    qRgb(43,206,72),  // medium green
    qRgb(153,63,0),  // brown
    qRgb(0,92,49),  // dark green
    qRgb(143,124,0),  // green-brown
    qRgb(157,204,0),  // poison green
    qRgb(194,0,136),  // reddish purple
    qRgb(255,168,187),  // reddish beige
    qRgb(66,102,0),  // dark poison green
    qRgb(94,241,242),  // cyan
    qRgb(0,153,143),  // medium blueish greenish
    qRgb(116,10,255),  // dark purple
    qRgb(153,0,0),  // dark cinnober red
    qRgb(240,163,255),  // lavender
//     qRgb(255,255,128),  // too bright
//     qRgb(255,255,0),  // too bright
//     qRgb(255,80,5),  // too similar to our "class" color
//     qRgb(76,0,92),  // too dark
//     qRgb(25,25,25),  // too dark
//     qRgb(255,164,5),  // too similar to our "class" color
//     qRgb(0,51,128),  // blue, too similar to our "function" color
//     qRgb(128,128,128),  // gray, too similar to our "comment" color
//     qRgb(224,255,102),  // bright poison green, too bright
//     qRgb(255,204,153),  // beige, too bright
//     qRgb(148,255,181),  // light blueish green, too bright
};


bool IsWithinComment(int character, HighlightingASTVisitorData* visitorData) {
  for (const DocumentRange& range : visitorData->commentRanges) {
    if (range.ContainsCharacter(character)) {
      return true;
    }
  }
  return false;
}

void FindCommentMarkerRanges(CXToken* tokens, unsigned numTokens, HighlightingASTVisitorData* visitorData, std::vector<DocumentRange>* ranges) {
  QStringList commentMarkers = Settings::Instance().GetCommentMarkers();
  
  // Iterate over all tokens
  for (int t = 0; t < numTokens; ++ t) {
    CXTokenKind kind = clang_getTokenKind(tokens[t]);
    if (kind != CXToken_Comment) {
      continue;
    }
    
    QString commentString = ClangString(clang_getTokenSpelling(visitorData->TU, tokens[t])).ToQString();
    // To find the correct offsets of line markers in our representation of the text, we need to transform
    // libclang's representation to match ours. So, since \r\n gets converted to \n on file loading, we need to
    // do this here as well.
    commentString.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    DocumentRange commentRange = CXSourceRangeToDocumentRange(clang_getTokenExtent(visitorData->TU, tokens[t]), *visitorData->lineOffsets);
    
    for (const QString& commentMarker : commentMarkers) {
      if (commentMarker.isEmpty()) {
        continue;
      }
      
      int pos = 0;
      while (true) {
        pos = commentString.indexOf(commentMarker, pos, Qt::CaseSensitive);
        if (pos < 0) {
          break;
        }
        
        if (pos > 0 && GetCharType(commentString[pos]) == GetCharType(commentString[pos - 1])) {
          // Skip this occurrence.
        } else if (pos + commentMarker.size() < commentString.size() &&
                   GetCharType(commentString[pos + commentMarker.size() - 1]) == GetCharType(commentString[pos + commentMarker.size()])) {
          // Skip this occurrence.
        } else {
          ranges->emplace_back(commentRange.start + pos, commentRange.start + pos + commentMarker.size());
        }
        
        pos += commentMarker.size();
      }
    }
  }
}

void ApplyCommentMarkerRanges(Document* document, const std::vector<DocumentRange>& ranges) {
  const auto& commentMarkerStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::CommentMarker);
  for (const DocumentRange& range : ranges) {
    document->AddHighlightRange(range, true, commentMarkerStyle);
  }
}

void AddTokenHighlighting(Document* document, CXToken* tokens, unsigned numTokens, HighlightingASTVisitorData* visitorData) {
  constexpr bool kDebug = false;
  
  const auto& languageKeywordStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::LanguageKeyword);
  const auto& commentStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::Comment);
  const auto& extraPunctuationStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ExtraPunctuation);
  const auto& preprocessorDirectiveStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::PreprocessorDirective);
  
  // Note: "#pragma once" is not reported via cursors at all. So we highlight it here via tokens.
  // The token sequence we need to watch out for is:
  // Punctuation token: #
  // Identifier token:  pragma
  // Identifier token:  once
  // The current detection state is stored in HighlightingASTVisitorData::pragmaOnceState, saying how many
  // matching tokens we detected.
  
  // Iterate over all tokens
  for (int t = 0; t < numTokens; ++ t) {
    CXTokenKind kind = clang_getTokenKind(tokens[t]);
    bool pragmaOnceStateUpdated = false;
    
    if (kind == CXToken_Keyword) {
      DocumentRange tokenRange = CXSourceRangeToDocumentRange(clang_getTokenExtent(visitorData->TU, tokens[t]), *visitorData->lineOffsets);
      document->AddHighlightRange(tokenRange, false, languageKeywordStyle);
      
      if (kDebug) {
        qDebug() << "Keyword token: " << ClangString(clang_getTokenSpelling(visitorData->TU, tokens[t])).ToQString();
      }
    } else if (kind == CXToken_Comment) {
      DocumentRange tokenRange = CXSourceRangeToDocumentRange(clang_getTokenExtent(visitorData->TU, tokens[t]), *visitorData->lineOffsets);
      document->AddHighlightRange(tokenRange, true, commentStyle);
      
      visitorData->commentRanges.push_back(tokenRange);
      
      if (kDebug) {
        qDebug() << "Comment token: " << ClangString(clang_getTokenSpelling(visitorData->TU, tokens[t])).ToQString();
      }
    } else if (kind == CXToken_Punctuation) {
      DocumentRange tokenRange = CXSourceRangeToDocumentRange(clang_getTokenExtent(visitorData->TU, tokens[t]), *visitorData->lineOffsets);
      CXString tokenSpelling = clang_getTokenSpelling(visitorData->TU, tokens[t]);
      
      char token = clang_getCString(tokenSpelling)[0];
      if (token == ';' || token == '{' || token == '}') {
        document->AddHighlightRange(tokenRange, false, extraPunctuationStyle);
      } else if (token == '#') {
        visitorData->pragmaOnceState = 1;
        pragmaOnceStateUpdated = true;
      }
      
      if (kDebug) {
        qDebug() << "Punctuation token: " << clang_getCString(tokenSpelling);
      }
      clang_disposeString(tokenSpelling);
    } else if (kind == CXToken_Identifier) {
      // TODO: libclang reports the "override" keyword as an identifier, this seems like a bug to me.
      CXString tokenSpelling = clang_getTokenSpelling(visitorData->TU, tokens[t]);
      const char* cSpelling = clang_getCString(tokenSpelling);
      if (cSpelling[0] == 'o' &&
          cSpelling[1] == 'v' &&
          cSpelling[2] == 'e' &&
          cSpelling[3] == 'r' &&
          cSpelling[4] == 'r' &&
          cSpelling[5] == 'i' &&
          cSpelling[6] == 'd' &&
          cSpelling[7] == 'e' &&
          cSpelling[8] == 0) {
        // TODO: "override" only acts as a keyword in the correct context. So we should also only highlight it in this case, instead of highlighting it always.
        DocumentRange tokenRange = CXSourceRangeToDocumentRange(clang_getTokenExtent(visitorData->TU, tokens[t]), *visitorData->lineOffsets);
        document->AddHighlightRange(tokenRange, false, languageKeywordStyle);
      } else if (visitorData->pragmaOnceState == 1 &&
                 cSpelling[0] == 'p' &&
                 cSpelling[1] == 'r' &&
                 cSpelling[2] == 'a' &&
                 cSpelling[3] == 'g' &&
                 cSpelling[4] == 'm' &&
                 cSpelling[5] == 'a' &&
                 cSpelling[6] == 0) {
        visitorData->pragmaOnceState = 2;
        pragmaOnceStateUpdated = true;
      } else if (visitorData->pragmaOnceState == 2 &&
                 cSpelling[0] == 'o' &&
                 cSpelling[1] == 'n' &&
                 cSpelling[2] == 'c' &&
                 cSpelling[3] == 'e' &&
                 cSpelling[4] == 0) {
        visitorData->pragmaOnceState = 0;
        
        // We detected all three tokens forming "#pragma once" successively.
        // Highlight the ranges of the last three tokens which are not comment tokens.
        int tokensToHighlight = 3;
        int currentToken = t;
        while (tokensToHighlight > 0 && currentToken >= 0) {
          CXTokenKind kind = clang_getTokenKind(tokens[currentToken]);
          if (kind != CXToken_Comment) {
            DocumentRange tokenRange = CXSourceRangeToDocumentRange(clang_getTokenExtent(visitorData->TU, tokens[currentToken]), *visitorData->lineOffsets);
            document->AddHighlightRange(tokenRange, false, preprocessorDirectiveStyle);
            
            -- tokensToHighlight;
          }
          -- currentToken;
        }
      }
      // TODO: We probably need to apply the same as for "override" also for "final"?
      
      if (kDebug) {
        qDebug() << "Identifier token: " << clang_getCString(tokenSpelling);
      }
      clang_disposeString(tokenSpelling);
    } else if (kDebug) {  // kind == CXToken_Literal
      qDebug() << "Literal token: " << ClangString(clang_getTokenSpelling(visitorData->TU, tokens[t])).ToQString();
    }
    
    if (kind != CXToken_Comment &&
        !pragmaOnceStateUpdated) {
      visitorData->pragmaOnceState = 0;
    }
  }
}

CXChildVisitResult VisitClangAST_AddHighlightingAndContexts(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  HighlightingASTVisitorData* data = reinterpret_cast<HighlightingASTVisitorData*>(client_data);
  Document* document = data->document;
  
  const auto& macroDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::MacroDefinition);
  const auto& macroInvocationStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::MacroInvocation);
  const auto& templateParameterDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::TemplateParameterDefinition);
  const auto& templateParameterUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::TemplateParameterUse);
  const auto& variableDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::VariableDefinition);
  const auto& variableUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::VariableUse);
  const auto& memberVariableUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::MemberVariableUse);
  const auto& typedefDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::TypedefDefinition);
  const auto& typedefUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::TypedefUse);
  const auto& enumConstantDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::EnumConstantDefinition);
  const auto& enumConstantUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::EnumConstantUse);
  const auto& constructorOrDestructorDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ConstructorOrDestructorDefinition);
  const auto& constructorOrDestructorUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ConstructorOrDestructorUse);
  const auto& functionDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::FunctionDefinition);
  const auto& functionUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::FunctionUse);
  const auto& unionDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::UnionDefinition);
  // TODO: Union use?
  const auto& enumDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::EnumDefinition);
  // TODO: Enum use?
  const auto& classOrStructDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ClassOrStructDefinition);
  const auto& classOrStructUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ClassOrStructUse);
  const auto& labelStatementStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::LabelStatement);
  const auto& labelReferenceStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::LabelReference);
  const auto& integerLiteralStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::IntegerLiteral);
  const auto& floatingLiteralStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::FloatingLiteral);
  const auto& imaginaryLiteralStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::ImaginaryLiteral);
  const auto& stringLiteralStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::StringLiteral);
  const auto& characterLiteralStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::CharacterLiteral);
  const auto& preprocessorDirectiveStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::PreprocessorDirective);
  const auto& includePathStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::IncludePath);
  const auto& namespaceDefinitionStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::NamespaceDefinition);
  const auto& namespaceUseStyle = Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::NamespaceUse);
  
  // Skip over cursors which are in included files
  CXSourceRange clangExtent = clang_getCursorExtent(cursor);
  CXSourceLocation extentStart = clang_getRangeStart(clangExtent);
  CXFile rangeFile;
  clang_getFileLocation(
      extentStart,
      /*CXFile *file*/ &rangeFile,
      /*unsigned startLine*/ nullptr,
      /*unsigned startColumn*/ nullptr,
      /*unsigned* offset*/ nullptr);
  if (!clang_File_isEqual(data->file, rangeFile)) {
    return CXChildVisit_Continue;
  }
  
  // qDebug() << "Cursor kind:" << ClangString(clang_getCursorKindSpelling(clang_getCursorKind(cursor))).ToQString()
  //          << "Spelling:" << ClangString(clang_getCursorSpelling(cursor)).ToQString();
  
  if (clang_getCursorKind(cursor) == CXCursor_MacroExpansion) {
    unsigned startOffset, endOffset;
    clang_getFileLocation(extentStart, nullptr, nullptr, nullptr, &startOffset);
    clang_getFileLocation(clang_getRangeEnd(clangExtent), nullptr, nullptr, nullptr, &endOffset);
    
    data->macroExpansionRanges.push_back(std::make_pair(startOffset, endOffset));
    
    document->AddHighlightRange(CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets), false, macroInvocationStyle);
    return CXChildVisit_Continue;
  }
  
  // Skip over cursors which refer to things within macro expansions.
  // NOTE: Without this, we would get a lot of arbitrary cursors having the
  //       complete macro expansion as a range. I did not find a different way
  //       to detect them. The libclang documentation makes it sound like one
  //       could check for a difference in the results of clang_getSpellingLocation(),
  //       clang_getFileLocation(), and clang_getExpansionLocation(), but these
  //       always seemed to return the same results. Also, it did not work to
  //       compare the ranges with clang_equalRanges(): it always returned false.
  if (!data->macroExpansionRanges.empty()) {
    unsigned startOffset, endOffset;
    clang_getFileLocation(extentStart, nullptr, nullptr, nullptr, &startOffset);
    clang_getFileLocation(clang_getRangeEnd(clangExtent), nullptr, nullptr, nullptr, &endOffset);
    
    for (const std::pair<unsigned, unsigned>& macroRange : data->macroExpansionRanges) {
      if (startOffset == macroRange.first &&
          endOffset == macroRange.second) {
        return CXChildVisit_Continue;
      }
    }
  }
  
  // Handle indent
  if (clang_equalCursors(parent, data->prevCursor)) {
    data->indent += "- ";
    data->parentCursors.push_back(parent);
  } else {
    while (!data->parentCursors.empty() &&
           data->indent.size() >= 2 &&
           !clang_equalCursors(parent, data->parentCursors.back())) {
      data->indent.erase(data->indent.begin() + (data->indent.size() - 1));
      data->indent.erase(data->indent.begin() + (data->indent.size() - 1));
      data->parentCursors.erase(data->parentCursors.begin() + (data->parentCursors.size() - 1));
    }
  }
  data->prevCursor = cursor;
  
  // Print cursor for debugging
//   CXString spelling = clang_getCursorDisplayName(cursor);
//   bool haveSpelling = clang_getCString(spelling)[0] != 0;
//   CXString kindSpelling = clang_getCursorKindSpelling(clang_getCursorKind(cursor));
//   DocumentRange extent = CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets);
//   std::cout << "- " << data->indent << clang_getCString(spelling) << (haveSpelling ? " (" : "(") << clang_getCString(kindSpelling) << ")";
  // std::cout << " | extent: " << (extent.start.line + 1) << ":" << (extent.start.col + 1) << " - " << (extent.end.line + 1) << ":" << (extent.end.col + 1);
//   int spellingIndex = 0;
//   while (true) {
//     DocumentRange spelling = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, spellingIndex, 0), *data->lineOffsets);
//     if (spelling.end.line < 0) {
//       break;
//     }
//     std::cout << " | spelling " << spellingIndex << ": " << (spelling.start.line + 1) << ":" << (spelling.start.col + 1) << " - " << (spelling.end.line + 1) << ":" << (spelling.end.col + 1);
//     ++ spellingIndex;
//   }
//   std::cout << std::endl;
//   clang_disposeString(spelling);
//   clang_disposeString(kindSpelling);
  
  bool addContext = false;
  
  CXCursorKind kind = clang_getCursorKind(cursor);
  if (kind == CXCursor_ParmDecl ||
      kind == CXCursor_VarDecl ||
      kind == CXCursor_FieldDecl ||
      kind == CXCursor_TemplateTypeParameter ||
      kind == CXCursor_NonTypeTemplateParameter ||
      kind == CXCursor_TemplateTemplateParameter) {  // TODO: Test CXCursor_TemplateTemplateParameter
    // The cursor range gives the complete definition here, for example:
    // "int test" for a parameter (ParmDecl),
    // "int test = 4" for a variable (VarDecl).
    // The spelling range gives the name of the variable only ("test" in the examples).
    // If the name is commented out (e.g., "int /*test*/") for function parameters,
    // then the extent only gives "int" and the spelling gives the punctuation after the
    // commented name.
    DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
    DocumentRange extentRange = CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets);
    if (extentRange.end > spellingRange.start) {
      const Settings::ConfigurableTextStyle& style = (kind == CXCursor_TemplateTypeParameter || kind == CXCursor_TemplateTemplateParameter) ? templateParameterDefinitionStyle : variableDefinitionStyle;
      
      // Determine whether to apply per-variable coloring to this definition.
      QColor overrideColor;
      if (data->perVariableColoring && kind != CXCursor_FieldDecl) {
        CXCursor parent = clang_getCursorSemanticParent(cursor);
        CXCursorKind parentKind = clang_getCursorKind(parent);
        if (IsFunctionDeclLikeCursorKind(parentKind)) {
          // Apply per-variable coloring.
          unsigned offset;
          clang_getFileLocation(clang_getCursorLocation(cursor), nullptr, nullptr, nullptr, &offset);
          overrideColor = perVariableColorPool[data->variableCounterPerFunction % kPerVariableColorPoolSize];
          data->perVariableColorMap[offset] = overrideColor;
          ++ data->variableCounterPerFunction;
        }
      }
      
      if (overrideColor.isValid()) {
        // We usually override the text color, but do override the background color instead if the style does not affect the text color.
        document->AddHighlightRange(spellingRange, false, overrideColor, style.bold, style.affectsText, style.affectsBackground, style.affectsText ? style.backgroundColor : overrideColor);
      } else {
        document->AddHighlightRange(spellingRange, false, style);
      }
    }
  } else if (kind == CXCursor_TypedefDecl) {
    DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
    document->AddHighlightRange(spellingRange, false, typedefDefinitionStyle);
  } else if (kind == CXCursor_EnumConstantDecl) {
    DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
    document->AddHighlightRange(spellingRange, false, enumConstantDefinitionStyle);
  } else if (IsFunctionDeclLikeCursorKind(kind)) {
    bool isConstructorOrDestructor = kind == CXCursor_Constructor || kind == CXCursor_Destructor;
    
    DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
    document->AddHighlightRange(spellingRange, false, isConstructorOrDestructor ? constructorOrDestructorDefinitionStyle : functionDefinitionStyle);
    
    data->variableCounterPerFunction = 0;
    data->perVariableColorMap.clear();
    
    addContext = clang_isCursorDefinition(cursor);
  } else if (kind == CXCursor_UnionDecl || kind == CXCursor_EnumDecl) {
    DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
    document->AddHighlightRange(spellingRange, false, (kind == CXCursor_UnionDecl) ? unionDefinitionStyle : enumDefinitionStyle);
    addContext = clang_isCursorDefinition(cursor);
  } else if (kind == CXCursor_ClassTemplate ||
             kind == CXCursor_ClassDecl ||
             kind == CXCursor_StructDecl ||
             kind == CXCursor_TypeRef) {
    DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
    const Settings::ConfigurableTextStyle* style;
    if (kind == CXCursor_TypeRef) {
      CXCursor referencedCursor = clang_getCursorReferenced(cursor);
      CXCursorKind referencedKind = clang_getCursorKind(referencedCursor);
      if (referencedKind == CXCursor_TypedefDecl) {
        style = &typedefUseStyle;
      } else {
        style = &classOrStructUseStyle;
      }
    } else {
      style = &classOrStructDefinitionStyle;
    }
    document->AddHighlightRange(spellingRange, false, *style);
  
    // Add a contexts for definitions (not for forward declarations).
    addContext = kind != CXCursor_TypeRef && clang_isCursorDefinition(cursor);
  } else if (kind == CXCursor_CallExpr) {
    CXCursor referencedCursor = clang_getCursorReferenced(cursor);
    CXCursorKind referencedKind = clang_getCursorKind(referencedCursor);
    if (referencedKind == CXCursor_Constructor) {
      DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
      document->AddHighlightRange(spellingRange, false, constructorOrDestructorUseStyle);
    }
  } else if (kind == CXCursor_MemberRefExpr) {
    // Find out whether the member is a function or an attribute
    CXCursor memberCursor = clang_getCursorReferenced(cursor);
    CXCursorKind memberKind = clang_getCursorKind(memberCursor);
    DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
    if (memberKind == CXCursor_FieldDecl) {
      document->AddHighlightRange(spellingRange, false, memberVariableUseStyle);
    } else if (memberKind == CXCursor_CXXMethod ||
               memberKind == CXCursor_ConversionFunction ||
               memberKind == CXCursor_OverloadedDeclRef) {
      document->AddHighlightRange(spellingRange, false, functionUseStyle);
    } else if (memberKind == CXCursor_Destructor){
      document->AddHighlightRange(spellingRange, false, constructorOrDestructorUseStyle);
    } else if (memberKind == CXCursor_InvalidFile) {
      // This happens for calling functions on template types, for example
      // for "SomeFunction" here:
      // template <class T>
      // void Func() {
      //   T.SomeFunction();
      // }
      // In this case, the spelling range is only "T", while the extent
      // is "T.SomeFunction".
      document->AddHighlightRange(CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets), false, functionUseStyle);
    } else {
      qDebug() << "Warning: MemberRefExpr cursor to unhandled member type" << memberKind;
    }
  } else if (kind == CXCursor_IfStmt) {
//     DocumentRange range = CXSourceRangeToDocumentRange(clangExtent);
    // Constrain the range to the word "if" instead of the whole statement.
//     range.end.line = range.start.line;
//     range.end.col = range.start.col + 2;
//     document->AddHighlightRange(range, false, qRgb(0, 0, 0), true);
  } else if (kind == CXCursor_WhileStmt) {
//     DocumentRange range = CXSourceRangeToDocumentRange(clangExtent);
//     // Constrain the range to the word "while" instead of the whole statement.
//     range.end.line = range.start.line;
//     range.end.col = range.start.col + 5;
//     document->AddHighlightRange(range, false, qRgb(0, 0, 0), true);
  } else if (kind == CXCursor_ReturnStmt) {
//     DocumentRange range = CXSourceRangeToDocumentRange(clangExtent);
//     // Constrain the range to the word "return" instead of the whole statement.
//     range.end.col = range.start.col + 6;
//     document->AddHighlightRange(range, false, qRgb(0, 0, 0), true);
  } else if (kind == CXCursor_DeclRefExpr) {
    // NOTE: These are references to non-members or enum constants.
    CXCursor referencedCursor = clang_getCursorReferenced(cursor);
    CXCursorKind referencedKind = clang_getCursorKind(referencedCursor);
    
    if (IsFunctionDeclLikeCursorKind(referencedKind)) {
      DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
      document->AddHighlightRange(spellingRange, false, functionUseStyle);
    } else if (referencedKind == CXCursor_EnumConstantDecl) {
      DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
      document->AddHighlightRange(spellingRange, false, enumConstantUseStyle);
    } else if (referencedKind == CXCursor_VarDecl || referencedKind == CXCursor_ParmDecl) {
      QColor color = variableUseStyle.textColor;
      
      if (data->perVariableColoring) {
        if (!clang_Cursor_isNull(referencedCursor)) {
          CXFile referencedFile;
          unsigned offset;
          clang_getFileLocation(clang_getCursorLocation(referencedCursor), &referencedFile, nullptr, nullptr, &offset);
          if (clang_File_isEqual(referencedFile, data->file)) {
            auto it = data->perVariableColorMap.find(offset);
            if (it != data->perVariableColorMap.end()) {
              color = it->second;
            }
          }
        }
      }
      
      // We usually override the text color, but do override the background color instead if the style does not affect the text color.
      DocumentRange range = CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets);
      document->AddHighlightRange(range, false, color, variableUseStyle.bold, variableUseStyle.affectsText, variableUseStyle.affectsBackground, variableUseStyle.affectsText ? variableUseStyle.backgroundColor : color);
    } else {
      qDebug() << "Clang highlighting: Encountered CXCursor_DeclRefExpr cursor which references an unhandled cursor kind: " << ClangString(clang_getCursorKindSpelling(referencedKind)).ToQString();
    }
  } else if (kind == CXCursor_TemplateRef) {
    CXCursor referencedCursor = clang_getCursorReferenced(cursor);
    CXCursorKind referencedKind = clang_getCursorKind(referencedCursor);
    const Settings::ConfigurableTextStyle* style = &templateParameterUseStyle;
    if (referencedKind == CXCursor_ClassTemplate) {
      style = &classOrStructUseStyle;
    } else if (referencedKind == CXCursor_ClassTemplatePartialSpecialization) {
      style = &classOrStructUseStyle;
    } else if (referencedKind == CXCursor_FunctionTemplate) {
      style = &functionUseStyle;
    } else if (referencedKind == CXCursor_TemplateTemplateParameter) {
      style = &templateParameterUseStyle;
    } else {
      qDebug() << "Clang highlighting: Encountered CXCursor_TemplateRef cursor that references an unhandled cursor type: " << ClangString(clang_getCursorKindSpelling(referencedKind)).ToQString();
    }
    DocumentRange range = CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets);
    document->AddHighlightRange(range, false, *style);
  } else if (kind == CXCursor_LabelStmt || kind == CXCursor_LabelRef) {
    DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
    document->AddHighlightRange(spellingRange, false, (kind == CXCursor_LabelStmt) ? labelStatementStyle : labelReferenceStyle);
  } else if (kind == CXCursor_CXXStaticCastExpr ||
             kind == CXCursor_CXXDynamicCastExpr ||
             kind == CXCursor_CXXReinterpretCastExpr ||
             kind == CXCursor_CXXConstCastExpr) {
//     DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0));
//     document->AddHighlightRange(spellingRange, false, qRgb(0, 0, 0), true);
  } else if (kind == CXCursor_CXXBoolLiteralExpr) {
//     DocumentRange range = CXSourceRangeToDocumentRange(clangExtent);
//     document->AddHighlightRange(range, false, qRgb(0, 0, 0), true);
  } else if (kind == CXCursor_IntegerLiteral) {
    DocumentRange range = CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets);
    document->AddHighlightRange(range, false, integerLiteralStyle);
  } else if (kind == CXCursor_FloatingLiteral ||
             kind == CXCursor_ImaginaryLiteral) {
    DocumentRange range = CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets);
    document->AddHighlightRange(range, false, (kind == CXCursor_FloatingLiteral) ? floatingLiteralStyle : imaginaryLiteralStyle);
  } else if (kind == CXCursor_StringLiteral ||
             kind == CXCursor_CharacterLiteral) {
    DocumentRange range = CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets);
    document->AddHighlightRange(range, true, (kind == CXCursor_StringLiteral) ? stringLiteralStyle : characterLiteralStyle);
  } else if (kind == CXCursor_MacroDefinition) {
//     DocumentRange range = CXSourceRangeToDocumentRange(clangExtent);
    
    // The macro definition range does not include the "#define" unfortunately.
    // We have to find it manually, going back from the start of the definition
    // range and skipping over comments, whitespace, and \ characters at the ends
    // of lines.
    // TODO: This must be done within the version that was used for parsing, not
    //       in the current version of the document!
//     DocumentLocation searchStart = range.start;
//     while (true) {
//       searchStart = document->FindInLine("#define", searchStart, /*forwards*/ false);
//       if (searchStart.IsInvalid()) {
//         break;
//       }
//       if (!IsWithinComment(searchStart.line, searchStart.col, visitorData)) {
//         DocumentRange defineRange;
//         defineRange.start = searchStart;
//         defineRange.end.line = searchStart.line;
//         defineRange.end.col = searchStart.col + 7;
//         document->AddHighlightRange(defineRange, false, qRgb(5, 113, 44), false);
//       }
//     }
    
    DocumentRange nameRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
    document->AddHighlightRange(nameRange, false, macroDefinitionStyle);
  } else if (kind == CXCursor_InclusionDirective) {
    DocumentRange range = CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets);
    
    DocumentRange includeRange;
    includeRange.start = range.start;
    includeRange.end.offset = range.start.offset + 8;
    document->AddHighlightRange(includeRange, false, preprocessorDirectiveStyle);
    
    // Highlight the path range. Unfortunately, it seems that we cannot retrieve
    // it directly. Include statements can go over multiple lines (with the \ separator)
    // and even contain /* */ comments between the #include and the path.
    // Thus, it seems easiest to find the path range by searching from the back.
    QString rangeText = GetClangText(clangExtent, data->TU);
    if (!rangeText.isEmpty()) {
      QChar separator = rangeText.back();
      if (separator == '>' || separator == '"') {
        if (separator == '>') {
          separator = '<';
        }
        int c = rangeText.size() - 2;
        for (; c >= 0; -- c) {
          if (rangeText.at(c) == separator) {
            break;
          }
        }
        if (c >= 0) {
          DocumentRange pathRange;
          pathRange.end = range.end;
          pathRange.start = range.end - (rangeText.size() - c);
          document->AddHighlightRange(pathRange, true, includePathStyle);
        }
      }
    }
  } else if (kind == CXCursor_Namespace || kind == CXCursor_NamespaceRef) {
    DocumentRange spellingRange = CXSourceRangeToDocumentRange(clang_Cursor_getSpellingNameRange(cursor, 0, 0), *data->lineOffsets);
    document->AddHighlightRange(spellingRange, false, (kind == CXCursor_Namespace) ? namespaceDefinitionStyle : namespaceUseStyle);
  }
  
  if (addContext) {
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
    QString name = GetClangText(clang_Cursor_getSpellingNameRange(cursor, 0, 0), data->TU);
    
    // Try to find the name within the displayName (TODO: Is there any way to do this without heuristics?).
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
    
    DocumentRange range = CXSourceRangeToDocumentRange(clangExtent, *data->lineOffsets);
    CXSourceRange clangCommentRange = clang_Cursor_getCommentRange(cursor);
    if (!clang_Range_isNull(clangCommentRange)) {
      CXFile commentFile;
      clang_getFileLocation(clang_getRangeStart(clangCommentRange), &commentFile, nullptr, nullptr, nullptr);
      if (clang_File_isEqual(commentFile, rangeFile)) {
        DocumentRange commentRange = CXSourceRangeToDocumentRange(clangCommentRange, *data->lineOffsets);
        range.start.offset = std::min(range.start.offset, commentRange.start.offset);
        range.end.offset = std::max(range.end.offset, commentRange.end.offset);
      }
    }
    
    document->AddContext(
        name,
        displayName,
        (namePos >= 0) ? DocumentRange(namePos, namePos + name.size()) : DocumentRange::Invalid(),
        range);
  }
  
  return (kind == CXCursor_InclusionDirective) ? CXChildVisit_Continue : CXChildVisit_Recurse;
}
