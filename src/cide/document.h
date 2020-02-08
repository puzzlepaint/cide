// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <functional>
#include <map>
#include <set>
#include <vector>

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTime>

#include "cide/clang_tu_pool.h"
#include "cide/document_range.h"
#include "cide/problem.h"
#include "cide/settings.h"
#include "cide/text_block.h"

enum class LineAttribute {
  Bookmark = 1 << 0,
  Warning = 1 << 1,
  Error = 1 << 2,
};


/// A highlight range stored in a Document. It represents a range of text that
/// a specific font style is applied to.
struct HighlightRange {
  HighlightRange() = default;
  
  HighlightRange(
      const DocumentRange& range,
      bool affectsText,
      const QColor& textColor,
      bool bold,
      bool affectsBackground,
      const QColor& backgroundColor,
      bool isNonCodeRange)
      : range(range),
        affectsText(affectsText),
        textColor(textColor),
        bold(bold),
        affectsBackground(affectsBackground),
        isNonCodeRange(isNonCodeRange),
        backgroundColor(backgroundColor) {}
  
  /// Range of this highlight. This may be invalid, in this case it applies to
  /// the whole text in the document.
  DocumentRange range;
  
  /// Whether the textColor and bold attributes should be used.
  bool affectsText;
  
  /// Text color of this highlight.
  QColor textColor;
  
  /// Whether text in this highlight is displayed in bold.
  bool bold;
  
  /// Whether the backgroundColor attribute should be used.
  bool affectsBackground;
  
  /// Whether this is a range that marks a "non-code" section.
  /// This could be either a string/character literal range, or a comment range.
  bool isNonCodeRange;
  
  /// Background color of this highlight.
  QColor backgroundColor;
};


/// A context within the source code with a name and a range. Used to display
/// the name of the current function/class/struct, and to jump to functions/
/// classes/structs in the current file via the search bar.
struct Context {
  inline Context(const QString& name, const QString& description, const DocumentRange& nameInDescriptionRange, const DocumentRange& range)
      : name(name), description(description), nameInDescriptionRange(nameInDescriptionRange), range(range) {}
  
  inline bool operator< (const Context& other) const {
    return range.start < other.range.start;
  }
  
  /// Name of the context, used for searching. For example: "main".
  QString name;
  
  /// Longer description of the context. For example: "int main(int argc, char** argv)".
  QString description;
  
  /// The range of the name within the description string.
  /// NOTE: This is currently computed based on heuristics and may thus be wrong.
  DocumentRange nameInDescriptionRange;
  
  /// Range of the context, used to determine which context to display as the
  /// current one
  DocumentRange range;
};


struct LineDiff {
  enum class Type {
    Added = 0,
    Modified,
    Removed
  };
  
  inline LineDiff(Type type, int line, int numLines, const QString& oldText)
      : type(type),
        line(line),
        numLines(numLines),
        oldText(oldText) {}
  
  /// Type of change performed to this line.
  Type type;
  
  /// Line at which to display this diff in the current version of
  /// the document. For type == Type::Removed, the removal should be
  /// displayed *on the top* (instead of bottom) of this line.
  int line;
  
  /// Number of lines that this diff spans over in the current version
  /// of the document (for Type::Added and Type::Modified).
  int numLines;
  
  /// Number of removed lines that this diff went over in the old version
  /// of the document (for Type::Removed and Type::Modified).
  int numRemovedLines = 0;
  
  /// The old text of the line. This is empty for new lines.
  QString oldText;
};


/// Stores information about a replacement operation. This is used to store
/// undo/redo steps.
struct Replacement {
  /// The range that shall be replaced when applying this step.
  DocumentRange range;
  
  /// The text that the range shall be replaced with when applying this step.
  QString text;
};


struct DocumentVersion;

/// Information used to reach one DocumentVersion from another.
struct DocumentVersionLink {
  inline DocumentVersionLink(DocumentVersion* version, const Replacement& replacement)
      : linkedVersion(version), replacements{replacement} {}
  
  inline DocumentVersionLink(DocumentVersion* version, const std::vector<Replacement>& replacements)
      : linkedVersion(version), replacements{replacements} {}
  
  /// The version that can be reached with this link.
  DocumentVersion* linkedVersion;
  
  /// The set of replacements that need to be applied (in the order in which
  /// they are stored) to reach the linked version.
  std::vector<Replacement> replacements;
};


/// Represents a version of a document (however without storing any text). This
/// is used as nodes in a directed graph of versions that are reachable from
/// each other by applying Replacement steps.
struct DocumentVersion {
  inline DocumentVersion(int version, DocumentVersion* towardsCurrentVersion)
      : version(version),
        towardsCurrentVersion(towardsCurrentVersion),
        creationTime(QTime::currentTime()) {}
  
  /// Returns the index of the link in the @a towardsCurrentVersion node that
  /// links to this node.
  inline int FindBackLink() {
    if (!towardsCurrentVersion) {
      qDebug() << "Error: Tried to find a back-link for the root node";
      return -1;
    }
    
    for (int i = 0, size = towardsCurrentVersion->links.size(); i < size; ++ i) {
      if (towardsCurrentVersion->links[i].linkedVersion == this) {
        return i;
      }
    }
    
    qDebug() << "Error: Back link missing for a DocumentVersion node.";
    return -1;
  }
  
  
  /// Version number. Corresponds to the version member of the Document class.
  int version;
  
  /// List of other versions that are reachable from this one by applying the
  /// given Replacement. Note that the linking is one-sided: Only the link that
  /// goes from the version that is closer to the current version to the version
  /// that is farther away from the current version is stored in this list, not
  /// the other way round.
  std::vector<DocumentVersionLink> links;
  
  /// (Not owned) pointer to the next version that is closer to the current
  /// version of the document. The DocumentVersion referenced here must have a
  /// link to this DocumentVersion in @a links. For the DocumentVersion that
  /// represents the current version of the document, towardsCurrentVersion is
  /// set to nullptr.
  DocumentVersion* towardsCurrentVersion;
  
  /// Timestamp of when this version was created.
  QTime creationTime;
};


/// A text document.
class Document : public QObject {
 Q_OBJECT
 public:
  class CharacterIterator;
  class CharacterAndStyleIterator;
  
  class LineIterator {
   public:
    LineIterator(Document* document);
    LineIterator(Document* document, int initialLine);
    LineIterator(const LineIterator& other);
    
    bool IsValid() const;
    
    DocumentLocation GetLineStart() const;
    DocumentRange GetLineRange() const;
    
    int GetAttributes();
    void SetAttributes(int attributes);
    void AddAttributes(int attributes);
    void RemoveAttributes(int attributes);
    
    /// Returns a CharacterIterator for the first character in the current line.
    CharacterIterator GetCharacterIterator() const;
    
    /// Returns a CharacterAndStyleIterator for the first character in the current line.
    CharacterAndStyleIterator GetCharacterAndStyleIterator() const;
    
    void operator++();
    
   private:
    Document* document;
    int blockIndex;
    int blockStartOffset;
    int lineInBlockIndex;
  };
  
  class CharacterIterator {
   friend class LineIterator;
   friend class CharacterAndStyleIterator;
   public:
    CharacterIterator(Document* document);
    CharacterIterator(Document* document, int characterOffset);
    CharacterIterator(const CharacterIterator& other);
    
    bool IsValid() const;
    
    QChar GetChar() const;
    
    inline int GetCharacterOffset() const {
      return blockStartOffset + charInBlockIndex;
    }
    
    void operator--();
    void operator++();
    
   private:
    inline CharacterIterator(
        Document* document,
        int blockIndex,
        int blockStartOffset,
        int charInBlockIndex)
        : document(document),
          blockIndex(blockIndex),
          blockStartOffset(blockStartOffset),
          charInBlockIndex(charInBlockIndex) {}
    
    Document* document;
    int blockIndex;
    int blockStartOffset;
    int charInBlockIndex;
  };
  
  class CharacterAndStyleIterator {
   friend class LineIterator;
   public:
    CharacterAndStyleIterator(Document* document);
    CharacterAndStyleIterator(Document* document, int characterOffset);
    CharacterAndStyleIterator(const CharacterAndStyleIterator& other);
    
    bool IsValid() const;
    /// Returns whether the current style has changed in the last increment or
    /// decrement of the iterator. Note: this may currently sometimes return
    /// true even if the current HighlightRange has not changed.
    inline bool StyleChanged() const { return styleChanged; }
    
    QChar GetChar() const;
    /// Returns the style at the iterator's location, considering all style layers.
    HighlightRange GetStyle() const;
    /// Returns the style at the iterator's location, considering only the given style layer.
    const HighlightRange& GetStyleOfLayer(int layer) const;
    
    inline int GetCharacterOffset() const {
      return blockStartOffset + charInBlockIndex;
    }
    
    inline CharacterIterator ToCharacterIterator() const {
      return CharacterIterator(document, blockIndex, blockStartOffset, charInBlockIndex);
    }
    
    void operator--();
    void operator++();
    
   private:
    inline CharacterAndStyleIterator(
        Document* document,
        int blockIndex,
        int blockStartOffset,
        int charInBlockIndex,
        int styleInBlockIndex[TextBlock::kLayerCount],
        bool styleChanged)
        : document(document),
          blockIndex(blockIndex),
          blockStartOffset(blockStartOffset),
          charInBlockIndex(charInBlockIndex),
          styleChanged(styleChanged) {
      for (int i = 0; i < TextBlock::kLayerCount; ++ i) {
        this->styleInBlockIndex[i] = styleInBlockIndex[i];
      }
    }
    
    Document* document;
    int blockIndex;
    int blockStartOffset;
    int charInBlockIndex;
    int styleInBlockIndex[TextBlock::kLayerCount];
    bool styleChanged;
  };
  
  /// Creates an empty document.
  Document(int desiredBlockSize = 128);
  
  /// Destructor. Frees the document version graph.
  ~Document();
  
  // Assigns the text and styles from the other document to this document.
  void AssignTextAndStyles(const Document& other);
  
  /// Attempts to open the file at the given path.
  bool Open(const QString& path);
  
  /// Attempts to save the file to the given path.
  bool Save(const QString& path);
  
  /// Attempts to open the backup file at @p backupPath. Returns the original
  /// path of the file in @p originalPath. Returns true if successful, false
  /// otherwise.
  /// NOTE: Does not set the path of this document.
  bool OpenBackup(const QString& backupPath, QString* originalPath);
  
  /// Attempts to save a backup file to the given path. The backup file contains
  /// the original file path in its first line.
  /// NOTE: Does not set the path of this document.
  bool SaveBackup(const QString& backupPath, const QString& originalPath);
  
  /// The basic editing operation that all edits are implemented with: text replacement.
  /// This replaces the given @p range in the document with @p newText.
  void Replace(const DocumentRange& range, const QString& newText, bool createUndoStep = true, Replacement* undoReplacement = nullptr);
  
  /// This may be called before a series of calls to Replace() to mark the start
  /// of a single undo step that encompasses multiple replacements. For example,
  /// the "replace all" functionality would group all individual replacements
  /// together in this way. After performing the replacements, EndUndoStep()
  /// must be called to mark the end of the successive replacements.
  void StartUndoStep();
  
  /// Counterpart to StartUndoStep().
  void EndUndoStep();
  
  /// Returns the document text for the given range.
  QString TextForRange(const DocumentRange& range);
  
  /// Checks if the block with the given index is too small or too large, and
  /// considers splitting or merging the block to get it closer to the desired
  /// size.
  void CheckBlockSplitOrMerge(int index);
  
  /// Undoes the last replacement (if there is any). Returns true if a step has
  /// been undone, false if there was nothing to undo. If @p newTextRange is
  /// given, it will be set to the range of the new text (if any) inserted as
  /// part of the undo step.
  bool Undo(DocumentRange* newTextRange = nullptr);
  
  /// Redoes the last replacement (if there is any). Returns true if a step has
  /// been redone, false if there was nothing to redo. If @p newTextRange is
  /// given, it will be set to the range of the new text (if any) inserted as
  /// part of the redo step.
  bool Redo(DocumentRange* newTextRange = nullptr);
  
  /// Returns whether this document contains changes that have not been saved
  /// to file.
  inline bool HasUnsavedChanges() const { return mVersion != mSavedVersion; }
  
  /// Returns the location directly before the found occurrence on success, or
  /// an invalid location on failure. Does not perform wrap-around.
  DocumentLocation Find(const QString& searchString, const DocumentLocation& searchStart, bool forwards, bool matchCase);
  
  inline const QString& path() const { return mPath; }
  inline const QString& fileName() const { return mFileName; }
  void setPath(const QString& path);
  
  /// Returns a DocumentRange that encompasses the complete document.
  DocumentRange FullDocumentRange() const;
  
  /// Returns a range for the "word" that contains the given @p characterOffset.
  /// A word is defined as a continuous range of characters that are classified
  /// to be of the same type by @p charClassifier. This function must return the
  /// type (an arbitrary integer) for the QChar parameter passed to it.
  /// The type noWordType is treated as an exception: Characters of this type
  /// are never merged together into words.
  DocumentRange RangeForWordAt(int characterOffset, const std::function<int(QChar)>& charClassifier, int noWordType) const;
  
  /// Returns the offset of the matching bracket to the bracket at @p pos, or -1
  /// if no matching bracket was found.
  /// TODO: This function does not account for brackets in strings or macros.
  ///       It may thus return wrong results.
  int FindMatchingBracket(const CharacterAndStyleIterator& pos);
  
  /// Returns the number of lines in the document.
  int LineCount() const;
  
  /// For debugging, verifies that the newline offsets (as stored in the lineAttributes
  /// elements of the TextBlocks) are at the correct places.
  bool DebugCheckNewlineoffsets() const;
  
  /// For debugging, verifies that the version graph is an acyclic graph.
  bool DebugCheckVersionGraph() const;
  
  /// Returns some statistics about the blocks in this document for debugging.
  void DebugGetBlockStatistics(int* blockCount, float* avgBlockSize, int* maxBlockSize, float* avgStyleRanges);
  
  /// Returns the complete document as a QString.
  QString GetDocumentText() const;
  
  /// Returns the range of the characters in the given line.
  DocumentRange GetRangeForLine(int l);
  
  /// Line diffs
  inline const std::vector<LineDiff>& diffLines() const { return mDiffLines; }
  inline void SwapDiffLines(std::vector<LineDiff>* lineDiff) { lineDiff->swap(mDiffLines); }
  
  /// Line attributes, see the LineAttribute enum.
  int lineAttributes(int l);
  void SetLineAttributes(int l, int attributes);
  void AddLineAttributes(int l, int attributes);
  void RemoveLineAttributes(int l, int attributes);
  
  /// Returns a number that is always increased if any text change is made to
  /// the document. This can be used to determine whether the text has changed
  /// from when the document was last accessed.
  inline int version() const { return mVersion; }
  
  /// Highlight ranges.
  void AddHighlightRange(const DocumentRange& range, bool isNonCodeRange, const QColor& textColor, bool bold, bool affectsText = true, bool affectsBackground = false, const QColor& backgroundColor = qRgb(255, 255, 255), int layer = 0);
  inline void AddHighlightRange(const DocumentRange& range, bool isNonCodeRange, const Settings::ConfigurableTextStyle& style, int layer = 0) {
    AddHighlightRange(range, isNonCodeRange, style.textColor, style.bold, style.affectsText, style.affectsBackground, style.backgroundColor, layer);
  }
  void ClearHighlightRanges(int layer);
  inline std::vector<HighlightRange>& GetHighlightRanges(int layer) { return mRanges[layer]; }
  /// To be called after adding/clearing highlight ranges.
  void FinishedHighlightingChanges();
  
  // Problems.
  /// Adds a problem to the document. Returns the problem index. Note that the
  /// indices become invalid when using RemoveProblem().
  int AddProblem(const std::shared_ptr<Problem>& problem);
  void AddProblemRange(int problemIndex, const DocumentRange& range);
  void RemoveProblem(const std::shared_ptr<Problem>& problem);
  void ClearProblems();
  
  const std::vector<std::shared_ptr<Problem>>& problems() const { return mProblems; }
  const std::set<ProblemRange>& problemRanges() const { return mProblemRanges; }
  
  // Contexts.
  void ClearContexts();
  void AddContext(const QString& name, const QString& description, const DocumentRange& nameInDescriptionRange, const DocumentRange& range);
  /// Returns the stack of context at the given document location. Items at
  /// earlier indices are supposed to enclose the items at later indices in the
  /// returned vector.
  std::vector<Context> GetContextsAt(const DocumentLocation& location);
  const std::set<Context>& GetContexts() const { return mContexts; }
  
  /// Returns the libclang TU pool for this document. Allocates the pool if it
  /// does not exist yet.
  ClangTUPool* GetTUPool();
  
 signals:
  void Changed();
  void HighlightingChanged();
  void FileChangedExternally();
  
 private slots:
  void FileWatcherNotification();
  
 private:
  /// Returns the block index for the block containing the given location.
  /// For locations that are at the border between two blocks, @p forwards is
  /// used to disambiguate: if true, the following block is returned,
  /// otherwise the previous block is returned. If the location is not in any
  /// block, -1 is returned. Also returns the start offset of the returned block
  /// in blockStartOffset.
  int BlockForLocation(const DocumentLocation& loc, bool forwards, int* blockStartOffset) const;
  
  /// Analogous to BlockForLocation(), but takes a character offset as input
  /// instead of a DocumentLocation (which corresponds to a location *between*
  /// characters).
  int BlockForCharacter(int characterOffset, int* blockStartOffset) const;
  
  /// Deletes all (non-default) style ranges in the TextBlocks and re-applies
  /// the current stack of mRanges.
  void ReapplyHighlightRanges(int layer);
  
  /// Updates the style ranges in blocks with the highlight range.
  void ApplyHighlightRange(const DocumentRange& range, int highlightRangeIndex, int layer);
  
  /// Implementation for undo and redo. Returns true if a step was (un/re)done,
  /// false if there was no step to do.
  bool UndoRedoImpl(bool redo, DocumentRange* newTextRange);
  
  /// Deletes all non-root nodes in the version graph.
  void ClearVersionGraph();
  
  /// Reads the document text from the given open file and converts it to blocks.
  void ReadTextFromFile(QFile* file);
  
  /// If mVersion != mOffsetCacheVersion, updates the offset cache.
  inline void EnsureOffsetCacheIsUpToDate() const {
    if (mVersion == mOffsetCacheVersion) {
      return;
    }
    mOffsetCacheVersion = mVersion;
    
    UpdateOffsetCache();
  }
  
  /// Updates the offset cache. Note that usually, EnsureOffsetCacheIsUpToDate()
  /// should be used instead.
  inline void UpdateOffsetCache() const {
    unsigned int blockStartLine = 0;
    unsigned int blockStartOffset = 0;
    
    int numBlocks = mBlocks.size();
    for (int blockIndex = 0; blockIndex < numBlocks; ++ blockIndex) {
      TextBlock& block = *mBlocks[blockIndex];
      
      block.SetCachedOffsets(blockStartOffset, blockStartLine);
      
      blockStartLine += mBlocks[blockIndex]->lineAttributes().size();
      blockStartOffset += mBlocks[blockIndex]->text().size();
    }
  }
  
  
  /// Path of this document, or an empty string if it is a new document which
  /// has not been saved to a file yet.
  QString mPath;
  
  /// Caches the file name portion of @a mPath to avoid having to repeatedly
  /// call QFileInfo(path).fileName().
  QString mFileName;
  
  /// Watches the file at @a mPath for external modifications.
  std::unique_ptr<QFileSystemWatcher> watcher;
  
  /// Number that is always increased if any text change is made to the document.
  /// TODO: Replace that by accessing versionGraphRoot->version instead?
  int mVersion;
  
  /// The version which is stored on disk. If mVersion == mSavedVersion, the
  /// document can be closed without losing information.
  int mSavedVersion;
  
  /// The version for which the initial character and line offsets have been
  /// cached in the TextBlocks. If mVersion == mOffsetCacheVersion, the cached
  /// data is valid and may be used.
  mutable int mOffsetCacheVersion;
  
  /// Pointer to the root of the directed graph that contains undo/redo steps.
  /// The whole graph is owned by this Document instance. The root is never null.
  DocumentVersion* versionGraphRoot;
  
  /// This is true after StartUndoStep() has been called to create a combined
  /// undo step. It is reset to false by EndUndoStep().
  bool creatingCombinedUndoStep;
  
  /// Used for accumulating undo steps if creatingCombinedUndoStep is true.
  std::vector<Replacement> combinedUndoReplacements;
  
  /// Translation unit pool for parsing with libclang.
  std::unique_ptr<ClangTUPool> mTUPool;
  
  /// Stores the current git diff state of the document. Lines are stored
  /// in increasing order.
  /// TODO: These are currently not adapted on Replace().
  std::vector<LineDiff> mDiffLines;
  
  /// Stores all added highlight ranges, as well as the default style at index
  /// 0.
  /// NOTE: The ranges stored here are currently NOT updated on edits to the
  ///       document. Only the derived StyleRanges in the TextBlocks are.
  std::vector<HighlightRange> mRanges[TextBlock::kLayerCount];
  
  /// Stores all problems that have been added to this document.
  std::vector<std::shared_ptr<Problem>> mProblems;
  
  /// Stores all problem ranges, ordered by the start of the range.
  std::set<ProblemRange> mProblemRanges;
  
  /// Stores all contexts, ordered by the start of the context range.
  std::set<Context> mContexts;
  
  /// Small text blocks that make up the document text.
  std::vector<std::shared_ptr<TextBlock>> mBlocks;
  
  /// The desired text length within a single TextBlock.
  int desiredBlockSize;
};
