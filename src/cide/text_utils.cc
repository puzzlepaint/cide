// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/text_utils.h"

#include <mutex>

#include <QString>

std::vector<bool> isSymbolArray;

void InitializeSymbolArray() {
  static std::mutex mutex;
  mutex.lock();
  
  DefineAsSymbol('/');
  DefineAsSymbol('\\');
  DefineAsSymbol('&');
  DefineAsSymbol('|');
  DefineAsSymbol('(');
  DefineAsSymbol(')');
  DefineAsSymbol('[');
  DefineAsSymbol(']');
  DefineAsSymbol('{');
  DefineAsSymbol('}');
  DefineAsSymbol('>');
  DefineAsSymbol('<');
  DefineAsSymbol('-');
  DefineAsSymbol('+');
  DefineAsSymbol('*');
  DefineAsSymbol('%');
  DefineAsSymbol('"');
  DefineAsSymbol('\'');
  DefineAsSymbol('`');
  DefineAsSymbol(';');
  DefineAsSymbol(':');
  DefineAsSymbol(',');
  DefineAsSymbol('.');
  DefineAsSymbol('~');
  DefineAsSymbol('!');
  DefineAsSymbol('?');
  DefineAsSymbol('#');
  DefineAsSymbol('$');
  DefineAsSymbol('^');
  DefineAsSymbol('=');
  
  mutex.unlock();
}

void ComputeFuzzyTextMatch(const QString& text, const QString& lowercaseText, const QString& item, const QString& lowercaseItem, FuzzyTextMatchScore* score) {
  int textSize = text.size();
  
  score->matchedCharacters = 0;
  score->matchErrors = std::numeric_limits<int>::max();
  score->matchedCase = false;
  score->matchedStartIndex = std::numeric_limits<int>::max();
  for (int start = 0, size = item.size(); start < size; ++ start) {
    // Count the number of matching characters for comparing "text" and "item" from position "start" in "item".
    int matchedCharacters = 0;
    int matchErrors = 0;
    bool matchedCase = true;
    
    int pos = start;
    for (int c = 0; c < textSize; ++ c) {
      const QChar& filterTextChar = item[pos];
      const QChar& filterTextCharLowercase = lowercaseItem[pos];
      const QChar& textChar = text[c];
      const QChar& textCharLowercase = lowercaseText[c];
      
      // Case-sensitive match?
      if (filterTextChar == textChar) {
        ++ matchedCharacters;
      }
      // Case-insensitive match?
      else if (filterTextCharLowercase == textCharLowercase) {
        ++ matchedCharacters;
        matchedCase = false;
      }
      // Order of characters swapped?
      else if (c < textSize - 1 &&
                pos < size - 1 &&
                lowercaseItem[pos + 1] == lowercaseText[c] &&
                lowercaseItem[pos] == lowercaseText[c + 1]) {
        matchedCharacters += 2;
        ++ matchErrors;
        if (item[pos + 1] != text[c] ||
            item[pos] != text[c + 1]) {
          matchedCase = false;
        }
        ++ c;
        ++ pos;
      }
      // Extra character in text?
      else if (c < textSize - 1 &&
                filterTextCharLowercase == lowercaseText[c + 1]) {
        ++ matchErrors;
        ++ matchedCharacters;
        if (filterTextChar != text[c + 1]) {
          matchedCase = false;
        }
        ++ c;
      }
      // Wrong character in text?
      else if (c < textSize - 1 &&
                pos < size - 1 &&
                lowercaseItem[pos + 1] == lowercaseText[c + 1]) {
        ++ matchErrors;
        ++ matchedCharacters;
        if (item[pos + 1] != text[c + 1]) {
          matchedCase = false;
        }
        ++ c;
        ++ pos;
      }
      // Missing character in text?
      else if (pos < size - 1 &&
                lowercaseItem[pos + 1] == textCharLowercase) {
        ++ matchErrors;
        ++ matchedCharacters;
        if (item[pos + 1] != textChar) {
          matchedCase = false;
        }
        ++ pos;
      } else {
        break;
      }
      
      ++ pos;
      if (pos >= size) {
        break;
      }
    }
    
    if (matchedCharacters > score->matchedCharacters ||
            (matchedCharacters == score->matchedCharacters && (matchErrors < score->matchErrors ||
                (matchErrors == score->matchErrors && matchedCase && !score->matchedCase)))) {
      score->matchedCharacters = matchedCharacters;
      score->matchErrors = matchErrors;
      score->matchedCase = matchedCase;
      score->matchedStartIndex = start;
    }
    if ((score->matchedCase && score->matchedCharacters >= size - start) ||
        (!score->matchedCase && score->matchedCharacters > size - start)) {
      // No better match possible, exit early.
      break;
    }
  }
}
