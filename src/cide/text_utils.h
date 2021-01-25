// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QChar>

#include <vector>

inline bool IsWhitespace(QChar c) {
  return c.isSpace();
}

extern std::vector<bool> isSymbolArray;

inline bool IsSymbol(QChar c) {
  const ushort& unicode = c.unicode();
  if (unicode < isSymbolArray.size()) {
    return isSymbolArray[c.unicode()];
  } else {
    return false;
  }
}

inline void DefineAsSymbol(QChar c) {
  if (isSymbolArray.size() <= c.unicode()) {
    isSymbolArray.resize(static_cast<std::size_t>(c.unicode()) + 1, false);
  }
  isSymbolArray[c.unicode()] = true;
}

void InitializeSymbolArray();

enum class CharacterType {
  Whitespace = 0,
  Symbol = 2,
  Letter = 1
};

/// Returns which 'type' the given character is of:
///   0: whitespace,
///   1: characters,
///   2: symbols
/// Symbols will not be merged into 'words'.
inline int GetCharType(QChar c) {
  if (IsWhitespace(c)) {
    return static_cast<int>(CharacterType::Whitespace);
  } else if (IsSymbol(c)) {
    return static_cast<int>(CharacterType::Symbol);
  } else {
    return static_cast<int>(CharacterType::Letter);
  }
}

inline bool IsBracket(QChar c) {
  return c == '(' || c == '[' || c == '{' ||
         c == ')' || c == ']' || c == '}';
}

inline bool IsOpeningBracket(QChar c) {
  return c == '(' || c == '[' || c == '{';
}

inline bool IsClosingBracket(QChar c) {
  return c == ')' || c == ']' || c == '}';
}

inline QChar GetMatchingBracketCharacter(QChar bracket) {
  if (bracket == '(') {
    return ')';
  } else if (bracket == '[') {
    return ']';
  } else if (bracket == '{') {
    return '}';
  } else if (bracket == ')') {
    return '(';
  } else if (bracket == ']') {
    return '[';
  } else if (bracket == '}') {
    return '{';
  }
  return 0;
}

/// Returns whether the character is a valid character within an identifier in
/// C/C++, i.e., a letter, number, or underscore. The fact that numbers cannot
/// appear at the start of identifiers is not checked.
inline bool IsIdentifierChar(QChar c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         (c == '_');
}

struct FuzzyTextMatchScore {
  /// Constructor which leaves the struct uninitialized.
  inline FuzzyTextMatchScore() = default;
  
  inline FuzzyTextMatchScore(int matchedCharacters, int matchErrors, bool matchedCase, int matchedStartIndex)
      : matchedCharacters(matchedCharacters),
        matchErrors(matchErrors),
        matchedCase(matchedCase),
        matchedStartIndex(matchedStartIndex) {}
  
  /// Returns 1 if this score is better than the other score,
  ///         0 if this score is worse than the other score,
  ///         -1 if the scores are equal.
  /// Thus, if the result is not equal to -1, it can be directly used as a bool
  /// saying whether this score is better than the other.
  inline int Compare(const FuzzyTextMatchScore& other) const {
    if (matchedCharacters != other.matchedCharacters) { return matchedCharacters > other.matchedCharacters; }
    if (matchErrors != other.matchErrors) { return matchErrors < other.matchErrors; }
    if (matchedCase != other.matchedCase) { return matchedCase; }
    if (matchedStartIndex != other.matchedStartIndex) { return matchedStartIndex < other.matchedStartIndex; }
    return -1;
  }
  
  /// The number of characters matched in the best found match between the two
  /// strings.
  int matchedCharacters;
  
  /// The number of match errors included in the best found match (e.g., omitted
  /// characters, swapped characters, ...)
  int matchErrors;
  
  /// Whether the best found match is case-correct.
  bool matchedCase;
  
  /// The character index in the 'item' string at which the 'text' string
  /// starts to match, for the best found match.
  int matchedStartIndex;
};

/// Computes how well the 'text' matches the 'item' while accounting for some
/// possible spelling mistakes and being relatively quick to compute.
void ComputeFuzzyTextMatch(const QString& text, const QString& lowercaseText, const QString& item, const QString& lowercaseItem, FuzzyTextMatchScore* score);
