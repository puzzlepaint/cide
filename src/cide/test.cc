// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include <atomic>

#include <git2.h>
#include <gtest/gtest.h>
#include <QApplication>
#include <QStandardPaths>

#include "cide/code_completion_widget.h"
#include "cide/code_info.h"
#include "cide/crash_backup.h"
#include "cide/document.h"
#include "cide/git_diff.h"
#include "cide/main_window.h"
#include "cide/parse_thread_pool.h"
#include "cide/project.h"
#include "cide/qt_thread.h"

int main(int argc, char** argv) {
  // Initialize libgit2
  git_libgit2_init();
  
  ::testing::InitGoogleTest(&argc, argv);
  QApplication qapp(argc, argv);
  
  // Run the tests in a second thread while the main thread runs a Qt event
  // loop. This allows RunInQtThreadBlocking() to operate correctly.
  int result;
  std::atomic<bool> finished;
  finished = false;
  
  std::thread testThread([&]() {
    result = RUN_ALL_TESTS();
    ParseThreadPool::Instance().ExitAllThreads();
    CodeInfo::Instance().Exit();
    CrashBackup::Instance().Exit();
    GitDiff::Instance().Exit();
    finished = true;
  });
  
  QEventLoop exitEventLoop;
  while (!finished) {
    exitEventLoop.processEvents();
  }
  testThread.join();
  
  return result;
}

// TODO: Wait for parse thread pool to be idle after each test?


TEST(TextBlock, Append1) {
  TextBlock blockA("a\n", true);
  TextBlock blockB("b\n", false);
  blockA.Append(blockB);
  EXPECT_TRUE(blockA.DebugCheckNewlineoffsets(true));
  EXPECT_EQ("a\nb\n", blockA.text());
}

TEST(TextBlock, Append2) {
  TextBlock blockA("\na", true);
  TextBlock blockB("\nb", false);
  blockA.Append(blockB);
  EXPECT_TRUE(blockA.DebugCheckNewlineoffsets(true));
  EXPECT_EQ("\na\nb", blockA.text());
}

TEST(TextBlock, Split) {
  TextBlock blockA("a\nb\nc\n", true);
  std::vector<std::shared_ptr<TextBlock>> parts = blockA.Split(2);
  ASSERT_EQ(2, parts.size());
  
  EXPECT_EQ("a\n", blockA.text());
  EXPECT_EQ("b\n", parts[0]->text());
  EXPECT_EQ("c\n", parts[1]->text());
  
  EXPECT_TRUE(blockA.DebugCheckNewlineoffsets(true));
  EXPECT_TRUE(parts[0]->DebugCheckNewlineoffsets(false));
  EXPECT_TRUE(parts[1]->DebugCheckNewlineoffsets(false));
}

TEST(TextBlock, HighlightRangeSplit) {
  TextBlock blockA("abcdef", true);
  blockA.InsertStyleRange(DocumentRange(0, 6), 42, 0);
  std::vector<std::shared_ptr<TextBlock>> parts = blockA.Split(2);
  
  EXPECT_EQ(1, blockA.styleRanges(0).size());
  EXPECT_EQ(42, blockA.styleRanges(0)[0].rangeIndex);
  
  for (const std::shared_ptr<TextBlock>& part : parts) {
    EXPECT_EQ(1, part->styleRanges(0).size());
    EXPECT_EQ(42, part->styleRanges(0)[0].rangeIndex);
  }
}

TEST(TextBlock, HighlightRangeAppend) {
  TextBlock blockA("abc", true);
  blockA.InsertStyleRange(DocumentRange(0, 3), 42, 0);
  TextBlock blockB("def", false);
  blockB.InsertStyleRange(DocumentRange(0, 3), 42, 0);
  
  blockA.Append(blockB);
  
  EXPECT_EQ(1, blockA.styleRanges(0).size());
  EXPECT_EQ(42, blockA.styleRanges(0)[0].rangeIndex);
}


TEST(Document, LineIterator) {
  std::vector<int> blockSizes = {3, 4, 5, 6, 7};
  
  for (int blockSize : blockSizes) {
    Document doc(blockSize);
    doc.Replace(doc.FullDocumentRange(), QStringLiteral("Line 1\nLine2\nLine3"));
    
    doc.DebugCheckNewlineoffsets();
    
    Document::LineIterator it(&doc);
    
    EXPECT_TRUE(it.IsValid()) << "blockSize: " << blockSize;
    DocumentRange range = it.GetLineRange();
    EXPECT_EQ(0, range.start.offset) << "blockSize: " << blockSize;
    EXPECT_EQ(6, range.end.offset) << "blockSize: " << blockSize;
    
    ++ it;
    
    EXPECT_TRUE(it.IsValid()) << "blockSize: " << blockSize;
    range = it.GetLineRange();
    EXPECT_EQ(7, range.start.offset) << "blockSize: " << blockSize;
    EXPECT_EQ(12, range.end.offset) << "blockSize: " << blockSize;
    
    ++ it;
    
    EXPECT_TRUE(it.IsValid()) << "blockSize: " << blockSize;
    range = it.GetLineRange();
    EXPECT_EQ(13, range.start.offset) << "blockSize: " << blockSize;
    EXPECT_EQ(18, range.end.offset) << "blockSize: " << blockSize;
    
    ++ it;
    
    EXPECT_FALSE(it.IsValid()) << "blockSize: " << blockSize;
  }
}


TEST(Document, CharacterIterator) {
  Document doc(2);
  doc.Replace(doc.FullDocumentRange(), QStringLiteral("abc"));
  
  Document::CharacterIterator it(&doc, 1);
  
  EXPECT_TRUE(it.IsValid());
  EXPECT_EQ(QChar('b'), it.GetChar());
  EXPECT_EQ(1, it.GetCharacterOffset());
  
  ++ it;
  
  EXPECT_TRUE(it.IsValid());
  EXPECT_EQ(QChar('c'), it.GetChar());
  EXPECT_EQ(2, it.GetCharacterOffset());
  
  ++ it;
  
  EXPECT_FALSE(it.IsValid());
  
  it = Document::CharacterIterator(&doc, 1);
  -- it;
  
  EXPECT_TRUE(it.IsValid());
  EXPECT_EQ(QChar('a'), it.GetChar());
  EXPECT_EQ(0, it.GetCharacterOffset());
  
  -- it;
  
  EXPECT_FALSE(it.IsValid());
  
  it = Document::CharacterIterator(&doc, 2);
  EXPECT_TRUE(it.IsValid());
  EXPECT_EQ(QChar('c'), it.GetChar());
  EXPECT_EQ(2, it.GetCharacterOffset());
  
  it = Document::CharacterIterator(&doc, 3);
  
  EXPECT_FALSE(it.IsValid());
}


TEST(Document, NewlineCountAndOffsets) {
  Document doc;
  
  EXPECT_EQ(1, doc.LineCount());
  EXPECT_TRUE(doc.DebugCheckNewlineoffsets());
  
  doc.Replace(doc.FullDocumentRange(), QStringLiteral("Line 1"));
  EXPECT_EQ(1, doc.LineCount());
  EXPECT_TRUE(doc.DebugCheckNewlineoffsets());
  
  doc.Replace(doc.FullDocumentRange(), QStringLiteral("Line 1\nLine2"));
  EXPECT_EQ(2, doc.LineCount());
  EXPECT_TRUE(doc.DebugCheckNewlineoffsets());
  
  doc.Replace(doc.FullDocumentRange(), QStringLiteral("Line 1\nLine2\n"));
  EXPECT_EQ(3, doc.LineCount());
  EXPECT_TRUE(doc.DebugCheckNewlineoffsets());
  
  doc.Replace(doc.FullDocumentRange(), QStringLiteral("Line 1\nLine2\nLine3"));
  EXPECT_EQ(3, doc.LineCount());
  EXPECT_TRUE(doc.DebugCheckNewlineoffsets());
  
  doc.Replace(doc.FullDocumentRange(), QStringLiteral(""));
  EXPECT_EQ(1, doc.LineCount());
  EXPECT_TRUE(doc.DebugCheckNewlineoffsets());
}

TEST(Document, Replace) {
  // Create the document with a very small desired block size such that the test
  // will likely use (and thus text) many blocks
  constexpr int desiredBlockSize = 8;
  Document doc(desiredBlockSize);
  QString groundTruth;
  
  // Do a number of random edits
  for (int i = 0; i < 100; ++ i) {
    DocumentRange fullRange = doc.FullDocumentRange();
    
    // Determine a random range to replace
    int pos1 = rand() % (fullRange.end.offset + 1);
    int pos2 = rand() % (fullRange.end.offset + 1);
    DocumentRange replacedRange(std::min(pos1, pos2), std::max(pos1, pos2));
    
    // Create a replacement text made of random characters
    QString newText;
    int newTextLength = rand() % 16;
    for (int k = 0; k < newTextLength; ++ k) {
      int random = rand() % 5;
      if (random == 0) {
        newText += '\n';
      } else if (random == 1) {
        newText += 'a';
      } else if (random == 2) {
        newText += 'b';
      } else if (random == 3) {
        newText += 'c';
      } else {
        newText += 'd';
      }
    }
    
    // Replace in the document and the ground truth
//     qDebug() << "previous text:" << groundTruth;
    doc.Replace(replacedRange, newText);
    groundTruth = groundTruth.left(replacedRange.start.offset) + newText + groundTruth.right(groundTruth.size() - replacedRange.end.offset);
//     qDebug() << "new text:     " << groundTruth;
    
    // Block statistics check
    int blockCount;
    float avgBlockSize;
    int maxBlockSize;
  float avgStyleRanges;
    doc.DebugGetBlockStatistics(&blockCount, &avgBlockSize, &maxBlockSize, &avgStyleRanges);
    // qDebug() << "blockCount:" << blockCount << ", avgBlockSize:" << avgBlockSize << ", maxBlockSize:" << maxBlockSize << ", avgStyleRanges:" << avgStyleRanges;
    if (maxBlockSize > 2 * desiredBlockSize) {
      qDebug() << "Warning: large block (maxBlockSize =" << maxBlockSize << "). This may happen if a small block can only be merged with blocks that almost violate the max block size. However, it should happen rarely.";
    }
    // qDebug() << "avgStyleRanges:" << avgStyleRanges;
    
    // Ensure that the document contains the same text as the ground truth
    ASSERT_EQ(groundTruth.toStdString(), doc.GetDocumentText().toStdString());
    
    // Additional sanity checks
    ASSERT_TRUE(doc.DebugCheckNewlineoffsets());
    ASSERT_EQ(1 + groundTruth.count('\n'), doc.LineCount());
  }
}

TEST(Document, ReplaceOnBlockBoundary) {
  Document doc(2);
  doc.Replace(doc.FullDocumentRange(), QStringLiteral("AACC"));
  
  int blockCount;
  float avgBlockSize;
  int maxBlockSize;
  float avgStyleRanges;
  doc.DebugGetBlockStatistics(&blockCount, &avgBlockSize, &maxBlockSize, &avgStyleRanges);
  // qDebug() << "blockCount:" << blockCount << ", avgBlockSize:" << avgBlockSize << ", maxBlockSize:" << maxBlockSize << ", avgStyleRanges:" << avgStyleRanges;
  ASSERT_EQ(2, blockCount);
  ASSERT_EQ(2, maxBlockSize);
  // qDebug() << "avgStyleRanges:" << avgStyleRanges;
  
  doc.Replace(DocumentRange(2, 2), QStringLiteral("BB"));
  EXPECT_EQ("AABBCC", doc.GetDocumentText().toStdString());
}

TEST(Document, LineAttributes) {
  Document doc;
  
  doc.Replace(doc.FullDocumentRange(), QStringLiteral("Line0\nLine1\nLine2"));
  EXPECT_EQ(0, doc.lineAttributes(0));
  EXPECT_EQ(0, doc.lineAttributes(1));
  EXPECT_EQ(0, doc.lineAttributes(2));
  doc.SetLineAttributes(2, 1);
  EXPECT_EQ(1, doc.lineAttributes(2));
  
  doc.Replace(DocumentRange(0, 0), QStringLiteral("Additional text in line 0"));
  EXPECT_EQ(0, doc.lineAttributes(0));
  EXPECT_EQ(0, doc.lineAttributes(1));
  EXPECT_EQ(1, doc.lineAttributes(2));
}

TEST(Document, HighlightRanges1) {
  std::vector<int> blockSizes = {1, 2, 3};
  for (int blockSize : blockSizes) {
    Document doc(blockSize);
    doc.Replace(doc.FullDocumentRange(), QStringLiteral("ABC"));
    doc.AddHighlightRange(DocumentRange(2, 3), false, qRgb(255, 0, 0), true);  // Make 'C' red and bold
    Document::CharacterAndStyleIterator it(&doc, 0);
    
    EXPECT_EQ(QChar('A'), it.GetChar());
    EXPECT_FALSE(it.GetStyle().bold);
    
    ++ it;
    
    EXPECT_EQ(QChar('B'), it.GetChar());
    EXPECT_FALSE(it.GetStyle().bold);
    
    ++ it;
    
    EXPECT_EQ(QChar('C'), it.GetChar());
    EXPECT_TRUE(it.GetStyle().bold);
    
    -- it;
    
    EXPECT_EQ(QChar('B'), it.GetChar());
    EXPECT_FALSE(it.GetStyle().bold);
  }
}

TEST(Document, HighlightRanges2) {
  std::vector<int> blockSizes = {1, 2, 3};
  for (int blockSize : blockSizes) {
    Document doc(blockSize);
    doc.Replace(doc.FullDocumentRange(), QStringLiteral("ABC"));
    doc.AddHighlightRange(DocumentRange(1, 2), false, qRgb(255, 0, 0), true);  // Make 'B' red and bold
    Document::CharacterAndStyleIterator it(&doc, 0);
    
    EXPECT_EQ(QChar('A'), it.GetChar());
    EXPECT_FALSE(it.GetStyle().bold);
    
    ++ it;
    
    EXPECT_EQ(QChar('B'), it.GetChar());
    EXPECT_TRUE(it.GetStyle().bold);
    
    ++ it;
    
    EXPECT_EQ(QChar('C'), it.GetChar());
    EXPECT_FALSE(it.GetStyle().bold);
    
    -- it;
    
    EXPECT_EQ(QChar('B'), it.GetChar());
    EXPECT_TRUE(it.GetStyle().bold);
  }
}

TEST(Document, HighlightRanges3) {
  std::vector<int> blockSizes = {1, 2, 3};
  for (int blockSize : blockSizes) {
    Document doc(blockSize);
    doc.Replace(doc.FullDocumentRange(), QStringLiteral("ABC"));
    doc.AddHighlightRange(DocumentRange(0, 1), false, qRgb(255, 0, 0), true);  // Make 'A' red and bold
    Document::CharacterAndStyleIterator it(&doc, 0);
    
    EXPECT_EQ(QChar('A'), it.GetChar());
    EXPECT_TRUE(it.GetStyle().bold);
    
    ++ it;
    
    EXPECT_EQ(QChar('B'), it.GetChar());
    EXPECT_FALSE(it.GetStyle().bold);
    
    ++ it;
    
    EXPECT_EQ(QChar('C'), it.GetChar());
    EXPECT_FALSE(it.GetStyle().bold);
    
    -- it;
    
    EXPECT_EQ(QChar('B'), it.GetChar());
    EXPECT_FALSE(it.GetStyle().bold);
  }
}

TEST(Document, HighlightRangeUpdatingOnEdits) {
  auto expectStyle = [](Document& doc, const QString& bold, const QString& testName) {
    ASSERT_EQ(bold.size(), doc.FullDocumentRange().size());
    QString actualBoldString;
    Document::CharacterAndStyleIterator it(&doc, 0);
    while (it.IsValid()) {
      bool docIsBold = it.GetStyle().bold;
      actualBoldString += docIsBold ? "X" : " ";
      ++ it;
    }
    EXPECT_EQ(bold.toStdString(), actualBoldString.toStdString()) << "Test: " << testName.toStdString() << " with doc text: " << doc.GetDocumentText().toStdString() << ", mismatch at: " << it.GetCharacterOffset();
  };
  
  std::vector<int> blockSizes = {1, 2, 3, 4, 5, 6, 100};
  for (int blockSize : blockSizes) {
//     qDebug() << "blockSize:" << blockSize;
    
    // Test: Style on word gets extended when typing characters on the right
    {
      Document doc(blockSize);
      doc.Replace(doc.FullDocumentRange(), QStringLiteral("Word   "));
      doc.AddHighlightRange(DocumentRange(0, 4), false, qRgb(255, 0, 0), true);
      doc.Replace(DocumentRange(4, 4), QStringLiteral("AAA"));
      expectStyle(doc, QStringLiteral("XXXXXXX   "), QStringLiteral("WordTypingRight"));
    }
    
    // Test: Style on word does not get extended when typing spaces on the right
    {
      Document doc(blockSize);
      doc.Replace(doc.FullDocumentRange(), QStringLiteral("Word   "));
      doc.AddHighlightRange(DocumentRange(0, 4), false, qRgb(255, 0, 0), true);
      doc.Replace(DocumentRange(4, 4), QStringLiteral("   "));
      expectStyle(doc, QStringLiteral("XXXX      "), QStringLiteral("WordSpacesRight"));
    }
    
    // Test: Style on word gets extended when typing characters on the left
    {
      Document doc(blockSize);
      doc.Replace(doc.FullDocumentRange(), QStringLiteral("   Word"));
      doc.AddHighlightRange(DocumentRange(3, 7), false, qRgb(255, 0, 0), true);
      doc.Replace(DocumentRange(3, 3), QStringLiteral("AAA"));
      expectStyle(doc, QStringLiteral("   XXXXXXX"), QStringLiteral("WordTypingLeft"));
    }
    
    // Test: Style on word does not get extended when typing spaces on the left
    {
      Document doc(blockSize);
      doc.Replace(doc.FullDocumentRange(), QStringLiteral("   Word"));
      doc.AddHighlightRange(DocumentRange(3, 7), false, qRgb(255, 0, 0), true);
      doc.Replace(DocumentRange(3, 3), QStringLiteral("   "));
      expectStyle(doc, QStringLiteral("      XXXX"), QStringLiteral("WordSpacesLeft"));
    }
    
    // Test: Style on word gets moved when typing left of it
    {
      Document doc(blockSize);
      doc.Replace(doc.FullDocumentRange(), QStringLiteral("   Word"));
      doc.AddHighlightRange(DocumentRange(3, 7), false, qRgb(255, 0, 0), true);
      doc.Replace(DocumentRange(0, 0), QStringLiteral("..."));
      expectStyle(doc, QStringLiteral("      XXXX"), QStringLiteral("StyleMovement"));
    }
    
    // Test: Style on word gets extended when typing inside of it
    {
      Document doc(blockSize);
      doc.Replace(doc.FullDocumentRange(), QStringLiteral("  Word  "));
      doc.AddHighlightRange(DocumentRange(2, 6), false, qRgb(255, 0, 0), true);
      doc.Replace(DocumentRange(4, 4), QStringLiteral("AAA"));
      expectStyle(doc, QStringLiteral("  XXXXXXX  "), QStringLiteral("StyleExtension"));
    }
    
    // Test: Style gets deleted when replacing a larger range
    {
      Document doc(blockSize);
      doc.Replace(doc.FullDocumentRange(), QStringLiteral("  Word  "));
      doc.AddHighlightRange(DocumentRange(2, 6), false, qRgb(255, 0, 0), true);
      doc.Replace(DocumentRange(1, 7), QStringLiteral("AAA"));
      expectStyle(doc, QStringLiteral("     "), QStringLiteral("StyleDeletion"));
    }
    
    // Test: Styles get cut when replacing parts of them with spaces
    {
      Document doc(blockSize);
      doc.Replace(doc.FullDocumentRange(), QStringLiteral("Word  Word"));
      doc.AddHighlightRange(DocumentRange(0, 4), false, qRgb(255, 0, 0), true);
      doc.AddHighlightRange(DocumentRange(6, 10), false, qRgb(255, 0, 0), true);
      doc.Replace(DocumentRange(2, 8), QStringLiteral("  "));
      expectStyle(doc, QStringLiteral("XX  XX"), QStringLiteral("StyleCut"));
    }
    
    // Test: Styles get extended partially when cutting them, up to whitespace
    {
      Document doc(blockSize);
      doc.Replace(doc.FullDocumentRange(), QStringLiteral("Word  Word"));
      doc.AddHighlightRange(DocumentRange(0, 4), false, qRgb(255, 0, 0), true);
      doc.AddHighlightRange(DocumentRange(6, 10), false, qRgb(255, 0, 0), true);
      doc.Replace(DocumentRange(2, 8), QStringLiteral("A A"));
      expectStyle(doc, QStringLiteral("XXX XXX"), QStringLiteral("StylePartialExtension"));
    }
    
    // Test: Complex example (with more ranges)
    {
      Document doc(blockSize);
      doc.Replace(doc.FullDocumentRange(), QStringLiteral("Word  Word  Word"));
      doc.AddHighlightRange(DocumentRange(0, 4), false, qRgb(255, 0, 0), true);
      doc.AddHighlightRange(DocumentRange(6, 10), false, qRgb(255, 0, 0), true);
      doc.AddHighlightRange(DocumentRange(12, 16), false, qRgb(255, 0, 0), true);
      doc.Replace(DocumentRange(2, 14), QStringLiteral("A A A"));
      expectStyle(doc, QStringLiteral("XXX   XXX"), QStringLiteral("ComplexExample"));
    }
  }
}

TEST(Document, UndoRedo) {
  std::vector<int> blockSizes = {2, 4, 5};
  for (int blockSize : blockSizes) {
    Document doc(blockSize);
    doc.Replace(doc.FullDocumentRange(), QStringLiteral("Cartoon"));
    doc.Replace(DocumentRange(0, 4), QStringLiteral("Typh"));
    
    EXPECT_EQ("Typhoon", doc.GetDocumentText().toStdString());
    
    ASSERT_TRUE(doc.DebugCheckVersionGraph());
    ASSERT_TRUE(doc.Undo());
    
    EXPECT_EQ("Cartoon", doc.GetDocumentText().toStdString());
    
    ASSERT_TRUE(doc.DebugCheckVersionGraph());
    ASSERT_TRUE(doc.Redo());
    ASSERT_FALSE(doc.Redo());
    
    EXPECT_EQ("Typhoon", doc.GetDocumentText().toStdString());
    
    ASSERT_TRUE(doc.DebugCheckVersionGraph());
    ASSERT_TRUE(doc.Undo());
    ASSERT_TRUE(doc.DebugCheckVersionGraph());
    ASSERT_TRUE(doc.Undo());
    ASSERT_TRUE(doc.DebugCheckVersionGraph());
    ASSERT_FALSE(doc.Undo());
    
    EXPECT_EQ("", doc.GetDocumentText().toStdString());
  }
}


TEST(CodeCompletion, Sorting) {
  for (int testIndex = 0; testIndex < 2; ++ testIndex) {
    std::vector<CompletionItem> items;
    
    items.emplace_back();
    items.back().filterText = (testIndex == 0) ? "Test" : "Something";
    
    items.emplace_back();
    items.back().filterText = (testIndex == 0) ? "Something" : "Test";
    
    CodeCompletionWidget* widget = new CodeCompletionWidget(std::move(items), nullptr, QPoint(0, 0), nullptr);
    widget->SetFilterText(QStringLiteral("TeAst"));
    std::vector<CompletionItem> sortedItems = widget->GetSortedItems();
    
    EXPECT_EQ(2, sortedItems.size());
    
    // "Te[A]st" matched with "Test"
    EXPECT_EQ(4, sortedItems[0].matchScore.matchedCharacters);
    EXPECT_EQ(1, sortedItems[0].matchScore.matchErrors);
    EXPECT_TRUE(sortedItems[0].matchScore.matchedCase);
    EXPECT_EQ(0, sortedItems[0].matchScore.matchedStartIndex);
    
    // "Te" matched with "et"
    EXPECT_EQ(2, sortedItems[1].matchScore.matchedCharacters);
    EXPECT_EQ(1, sortedItems[1].matchScore.matchErrors);
    EXPECT_FALSE(sortedItems[1].matchScore.matchedCase);
    EXPECT_EQ(3, sortedItems[1].matchScore.matchedStartIndex);
    
    widget->SetFilterText(QStringLiteral("Tet"));
    sortedItems = widget->GetSortedItems();
    
    EXPECT_EQ(2, sortedItems.size());
    
    // "Tet" matched with "Te[s]t"
    EXPECT_EQ(3, sortedItems[0].matchScore.matchedCharacters);
    EXPECT_EQ(1, sortedItems[0].matchScore.matchErrors);
    EXPECT_TRUE(sortedItems[0].matchScore.matchedCase);
    EXPECT_EQ(0, sortedItems[0].matchScore.matchedStartIndex);
    
    // "et" matched with "et"
    EXPECT_EQ(2, sortedItems[1].matchScore.matchedCharacters);
    EXPECT_EQ(1, sortedItems[1].matchScore.matchErrors);
    EXPECT_TRUE(sortedItems[1].matchScore.matchedCase);
    EXPECT_EQ(2, sortedItems[1].matchScore.matchedStartIndex);
    
    widget->SetFilterText(QStringLiteral("TeFt"));
    sortedItems = widget->GetSortedItems();
    
    EXPECT_EQ(2, sortedItems.size());
    
    // "TeFt" matched with "Test"
    EXPECT_EQ(3, sortedItems[0].matchScore.matchedCharacters);
    EXPECT_EQ(1, sortedItems[0].matchScore.matchErrors);
    EXPECT_TRUE(sortedItems[0].matchScore.matchedCase);
    EXPECT_EQ(0, sortedItems[0].matchScore.matchedStartIndex);
    
    // "Te" matched with "et"
    EXPECT_EQ(2, sortedItems[1].matchScore.matchedCharacters);
    EXPECT_EQ(1, sortedItems[1].matchScore.matchErrors);
    EXPECT_FALSE(sortedItems[1].matchScore.matchedCase);
    EXPECT_EQ(3, sortedItems[1].matchScore.matchedStartIndex);
    
    delete widget;
  }
}


TEST(Project, Reconfigure) {
  // Create a project in a temporary directory
  QString tmpPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QDir tmpDir(tmpPath);
  ASSERT_TRUE(tmpDir.mkpath("."));
  QDir projectDir = tmpDir.filePath("cide_test_dir");
  if (projectDir.exists()) {
    projectDir.removeRecursively();
  }
  ASSERT_TRUE(projectDir.mkpath("."));
  
  QString projectFileText =
      "name: \"CIDE Unit Test Project\"\n"
      "projectCMakeDir: \"build\"\n"
      "buildDir: \"build\"\n"
      "buildCmd: \"/usr/bin/ninja\"\n"
      "runDir: \"build\"\n"
      "runCmd: \"./CIDEUnitTest\"\n";
  QFile projectFile(projectDir.filePath("project.cide"));
  ASSERT_TRUE(projectFile.open(QIODevice::WriteOnly | QIODevice::Text));
  projectFile.write(projectFileText.toUtf8());
  projectFile.close();
  
  QString cmakeFileText =
      "project(CIDEUnitTest)\n"
      "set(CMAKE_CXX_STANDARD 11)\n"
      "add_executable(CIDEUnitTest main.cc)\n";
  QFile cmakeFile(projectDir.filePath("CMakeLists.txt"));
  ASSERT_TRUE(cmakeFile.open(QIODevice::WriteOnly | QIODevice::Text));
  cmakeFile.write(cmakeFileText.toUtf8());
  cmakeFile.close();
  
  QString sourceFileText =
      "int main(int /*argc*/, char** /*argv*/) {return 42;}\n";
  QFile sourceFile(projectDir.filePath("main.cc"));
  ASSERT_TRUE(sourceFile.open(QIODevice::WriteOnly | QIODevice::Text));
  sourceFile.write(sourceFileText.toUtf8());
  sourceFile.close();
  
  QDir(projectDir.filePath("build")).mkpath(".");
  
  // Create a MainWindow in the Qt thread which will also get destructed in the Qt thread again
  std::shared_ptr<MainWindow> mainWindow;
  RunInQtThreadBlocking([&]() {
    mainWindow.reset(new MainWindow(), [&](MainWindow* ptr) {
      RunInQtThreadBlocking([&]() {
        delete ptr;
      });
    });
  });
  
  // Load the project
  Project* project;
  RunInQtThreadBlocking([&]() {
    ASSERT_TRUE(mainWindow->LoadProject(projectFile.fileName(), nullptr));
    project = mainWindow->GetProjects().front().get();
  });
  
  // Change the CMakeLists.txt file to include another source file
  cmakeFileText =
      "project(CIDEUnitTest)\n"
      "set(CMAKE_CXX_STANDARD 11)\n"
      "add_executable(CIDEUnitTest main.cc newfile.cc)\n";
  ASSERT_TRUE(cmakeFile.open(QIODevice::WriteOnly | QIODevice::Text));
  cmakeFile.write(cmakeFileText.toUtf8());
  cmakeFile.close();
  
  QString newSourceFileText =
      "int something() {return 33;}\n";
  QFile newSourceFile(projectDir.filePath("newfile.cc"));
  ASSERT_TRUE(newSourceFile.open(QIODevice::WriteOnly | QIODevice::Text));
  newSourceFile.write(newSourceFileText.toUtf8());
  newSourceFile.close();
  
  // Reconfigure the project
  QString errorReason;
  QString warnings;
  bool result;
  RunInQtThreadBlocking([&]() {
    bool errorDisplayedAlready;
    result = project->Configure(&errorReason, &warnings, &errorDisplayedAlready, nullptr);
  });
  if (!result) {
    qDebug() << "Reconfiguring failed, error reason given is:" << errorReason;
  }
  ASSERT_TRUE(result) << "Reconfiguring failed.";
  
  // Verify that the initial source file is still marked as indexed (taken over
  // from the initial configuration), and the new file is not indexed yet.
  for (int targetIdx = 0; targetIdx < project->GetNumTargets(); ++ targetIdx) {
    for (const SourceFile& source : project->GetTarget(targetIdx).sources) {
      if (source.path == sourceFile.fileName()) {
        EXPECT_TRUE(source.hasBeenIndexed) << "The old source file's hasBeenIndexed attribute was not transferred correctly";
      } else if (source.path == newSourceFile.fileName()) {
        EXPECT_FALSE(source.hasBeenIndexed) << "The new source file's hasBeenIndexed attribute is incorrectly set to true";
      } else {
        EXPECT_TRUE(false) << "Encountered an unexpected source file: " << source.path.toStdString();
      }
    }
  }
}


/// Tests the abort mechanism of RunInQtThreadBlocking().
TEST(RunInQtThreadBlocking, Abort) {
  std::atomic<bool> testFailed;
  testFailed = false;
  
  // First, get into the Qt thread.
  RunInQtThreadBlocking([&]() {
    RunInQtThreadAbortData abortData;
    
    // From the Qt thread, we create another thread.
    std::thread anotherThread([&]() {
      // The other thread tries to execute something in the Qt thread.
      // This cannot work, since we block the Qt thread below.
      RunInQtThreadBlocking([&]() {
        // This must never be called
        testFailed = true;
      }, &abortData);
    });
    
    // Wait a bit to make it very likely that anotherThread starts blocking.
    QThread::msleep(50);
    
    // Invoke the abort mechanism on the RunInQtThreadBlocking() used by anotherThread.
    abortData.Abort();
    
    // Now we should be able to join anotherThread without getting a deadlock.
    anotherThread.join();
    
    // Verify that we can process events after aborting, which will process the timer
    // queued internally by RunInQtThreadBlocking(), without crashing.
    QEventLoop eventLoop;
    eventLoop.processEvents();
    
    // Verify that the function that blocked did not execute, even after processing events.
    EXPECT_FALSE(testFailed);
  });
}


/// Tests that a C++ file can be successfully parsed.
TEST(Parsing, ParseFile) {
  // Create a project in a temporary directory
  QString tmpPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QDir tmpDir(tmpPath);
  ASSERT_TRUE(tmpDir.mkpath("."));
  QDir projectDir = tmpDir.filePath("cide_test_dir");
  if (projectDir.exists()) {
    projectDir.removeRecursively();
  }
  ASSERT_TRUE(projectDir.mkpath("."));
  
  QString projectFileText =
      "name: \"CIDE Unit Test Project\"\n"
      "projectCMakeDir: \"build\"\n"
      "buildDir: \"build\"\n"
      "buildCmd: \"/usr/bin/ninja\"\n"
      "runDir: \"build\"\n"
      "runCmd: \"./CIDEUnitTest\"\n"
      "indexAllProjectFiles: false\n";
  QFile projectFile(projectDir.filePath("project.cide"));
  ASSERT_TRUE(projectFile.open(QIODevice::WriteOnly | QIODevice::Text));
  projectFile.write(projectFileText.toUtf8());
  projectFile.close();
  
  QString cmakeFileText =
      "project(CIDEUnitTest)\n"
      "set(CMAKE_CXX_STANDARD 11)\n"
      "add_executable(CIDEUnitTest main.cc)\n";
  QFile cmakeFile(projectDir.filePath("CMakeLists.txt"));
  ASSERT_TRUE(cmakeFile.open(QIODevice::WriteOnly | QIODevice::Text));
  cmakeFile.write(cmakeFileText.toUtf8());
  cmakeFile.close();
  
  QString sourceFileText =
      "int main(int /*argc*/, char** /*argv*/) {return 42;}\n";
  QString sourceFilePath = projectDir.filePath("main.cc");
  QFile sourceFile(sourceFilePath);
  ASSERT_TRUE(sourceFile.open(QIODevice::WriteOnly | QIODevice::Text));
  sourceFile.write(sourceFileText.toUtf8());
  sourceFile.close();
  
  QDir(projectDir.filePath("build")).mkpath(".");
  
  // Create a MainWindow in the Qt thread which will also get destructed in the Qt thread again
  std::shared_ptr<MainWindow> mainWindow;
  RunInQtThreadBlocking([&]() {
    mainWindow.reset(new MainWindow(), [&](MainWindow* ptr) {
      RunInQtThreadBlocking([&]() {
        delete ptr;
      });
    });
  });
  
  RunInQtThreadBlocking([&]() {
    // Load the project
    ASSERT_TRUE(mainWindow->LoadProject(projectFile.fileName(), nullptr));
    
    // Open the source file
    mainWindow->Open(sourceFilePath);
    Document* document;
    DocumentWidget* widget;
    ASSERT_TRUE(mainWindow->GetDocumentAndWidgetForPath(sourceFilePath, &document, &widget));
    
    // Verify that the source file gets parsed after a while
    QEventLoop eventLoop;
    while (ParseThreadPool::Instance().DoesAParseRequestExistForDocument(document) ||
           document->GetContexts().empty()) {
      eventLoop.processEvents();
    }
    
    // Verify that we got the main function context
    ASSERT_EQ(document->GetContexts().size(), 1);
    EXPECT_EQ(document->GetContexts().begin()->name, "main");
  });
}
