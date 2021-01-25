// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>
#include <unordered_map>

#include <QDebug>
#include <QDialog>
#include <QFont>
#include <QListWidget>
#include <QSettings>
#include <QTableWidget>

#include "cide/util.h"

class ActionWithConfigurableShortcut;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedLayout;


enum class NewlineFormat {
  Lf = 0,
  CrLf = 1,
  NotConfigured = 2
};


struct WordCompletion {
  WordCompletion() = default;
  
  inline WordCompletion(const QString& word, const QString& replacement, bool applyIfNonWhitespaceFollows, bool applyWithinCodeOnly)
      : word(word),
        replacement(replacement),
        applyIfNonWhitespaceFollows(applyIfNonWhitespaceFollows),
        applyWithinCodeOnly(applyWithinCodeOnly) {}
  
  /// Word that will be watched out for. The completion will be triggered if a space
  /// is typed after the word, while there is also a whitespace character before it.
  QString word;
  
  /// Replacement for the word.
  QString replacement;
  
  /// Whether to apply this completion even if non-whitespace characters follow in the same line.
  /// This should be set to true for spelling corrections.
  bool applyIfNonWhitespaceFollows;
  
  /// Whether to apply this completion only if we think that the trigger word is written within
  /// a section of code, rather than within a string or a comment.
  bool applyWithinCodeOnly;
};


// List of configuration key names for configurable shortcuts
constexpr const char* buildCurrentTargetShortcut = "build_current_target";
constexpr const char* startDebuggingShortcut = "start_debugging";
constexpr const char* searchInFilesShortcut = "search_in_files";
constexpr const char* searchLocalContextsShortcut = "search_local_contexts";
constexpr const char* searchGlobalSymbolsShortcut = "search_global_symbols";
constexpr const char* switchHeaderSourceShortcut = "switch_header_source";
constexpr const char* goToRightTabShortcut = "go_to_right_tab";
constexpr const char* goToLeftTabShortcut = "go_to_left_tab";
constexpr const char* reloadFileShortcut = "reload_file";
constexpr const char* newFileShortcut = "new_file";
constexpr const char* openFileShortcut = "open_file";
constexpr const char* saveFileShortcut = "save_file";
constexpr const char* saveAsFileShortcut = "save_file_as";
constexpr const char* closeFileShortcut = "close_file";
constexpr const char* quitShortcut = "quit_program";
constexpr const char* findAndReplaceInFilesShortcut = "find_and_replace_in_files";
constexpr const char* showProjectFilesDockShortcut = "show_project_files_dock";
constexpr const char* showRunDockShortcut = "show_run_dock";
constexpr const char* runGitkShortcut = "run_gitk";
constexpr const char* undoShortcut = "undo";
constexpr const char* redoShortcut = "redo";
constexpr const char* cutShortcut = "cut";
constexpr const char* copyShortcut = "copy";
constexpr const char* pasteShortcut = "paste";
constexpr const char* findShortcut = "open_find_bar";
constexpr const char* replaceShortcut = "open_replace_bar";
constexpr const char* gotoLineShortcut = "open_goto_line_bar";
constexpr const char* toggleBookmarkShortcut = "toggle_bookmark";
constexpr const char* jumpToPreviousBookmarkShortcut = "jump_to_previous_bookmark";
constexpr const char* jumpToNextBookmarkShortcut = "jump_to_next_bookmark";
constexpr const char* removeAllBookmarksShortcut = "remove_all_bookmarks";
constexpr const char* commentOutShortcut = "comment_out";
constexpr const char* uncommentShortcut = "uncomment";
constexpr const char* invokeCodeCompletionShortcut = "invoke_code_completion";
constexpr const char* showDocumentationInDockShortcut = "show_documentation_in_dock";
constexpr const char* renameItemAtCursorShortcut = "rename_item_at_cursor";
constexpr const char* fixAllVisibleTrivialIssuesShortcut = "fix_all_visible_trivial_issues";
constexpr const char* findNextShortcut = "find_next";
constexpr const char* findPreviousShortcut = "find_previous";


/// Singleton class which stores program-level settings.
class Settings : public QObject {
 Q_OBJECT
 public:
  enum class CodeCompletionConfirmationKeys {
    Tab = 1,  // bits: 01
    Return = 2,  // bits: 10
    TabAndReturn = 3  // bits: 11
  };
  
  /// List of configurable colors
  enum class Color {
    EditorBackground = 0,
    TrailingSpaceHighlight,
    OutsizeOfContextLine,
    CurrentLine,
    EditorSelection,
    BookmarkLine,
    ErrorUnderline,
    WarningUnderline,
    ColumnMarker,
    GitDiffAdded,
    GitDiffModified,
    GitDiffRemoved,
    NumColors
  };
  
  /// List of configurable text styles
  enum class TextStyle {
    Default = 0,
    JustReplaced,
    ReferenceHighlight,
    CopyHighlight,
    LeftBracketHighlight,
    RightBracketHighlight,
    ErrorInlineDisplay,
    WarningInlineDisplay,
    CommentMarker,
    LanguageKeyword,
    Comment,
    ExtraPunctuation,
    PreprocessorDirective,
    MacroDefinition,
    MacroInvocation,
    TemplateParameterDefinition,
    TemplateParameterUse,
    VariableDefinition,
    VariableUse,
    MemberVariableUse,
    TypedefDefinition,
    TypedefUse,
    EnumConstantDefinition,
    EnumConstantUse,
    ConstructorOrDestructorDefinition,
    ConstructorOrDestructorUse,
    FunctionDefinition,
    FunctionUse,
    UnionDefinition,
    EnumDefinition,
    ClassOrStructDefinition,
    ClassOrStructUse,
    LabelStatement,
    LabelReference,
    IntegerLiteral,
    FloatingLiteral,
    ImaginaryLiteral,
    StringLiteral,
    CharacterLiteral,
    IncludePath,
    NamespaceDefinition,
    NamespaceUse,
    ProjectTreeViewDefault,
    ProjectTreeViewCurrentItem,
    ProjectTreeViewOpenedItem,
    ProjectTreeViewModifiedItem,
    ProjectTreeViewUntrackedItem,
    NumTextStyles
  };
  
  struct ConfigurableShortcut {
    inline ConfigurableShortcut(const QString& name, const QKeySequence& sequence)
        : name(name),
          sequence(sequence) {}
    
    QString name;
    QKeySequence sequence;
    std::vector<ActionWithConfigurableShortcut*> registeredActions;
  };
  
  struct ConfigurableColor {
    ConfigurableColor() = default;
    
    inline ConfigurableColor(const QString& name, const QString& keyName, const QRgb& value)
        : name(name),
          keyName(keyName),
          value(value) {}
    
    QString name;
    QString keyName;
    QRgb value;
  };
  
  struct ConfigurableTextStyle {
    ConfigurableTextStyle() = default;
    
    inline ConfigurableTextStyle(const QString& name, const QString& keyName, bool affectsText, const QRgb& textColor, bool bold, bool affectsBackground, const QRgb& backgroundColor)
        : name(name),
          keyName(keyName),
          affectsText(affectsText),
          textColor(textColor),
          bold(bold),
          affectsBackground(affectsBackground),
          backgroundColor(backgroundColor) {}
    
    QString name;
    QString keyName;
    
    /// Whether the textColor and bold attributes should be used.
    bool affectsText;
    QRgb textColor;
    bool bold;
    
    /// Whether the backgroundColor attribute should be used.
    bool affectsBackground;
    QRgb backgroundColor;
  };
  
  
  static Settings& Instance();
  
  void AddConfigurableShortcut(const QString& name, const char* configurationKeyName, const QKeySequence& defaultValue);
  QStringList GetAllConfigurableShortcutKeys() const;
  const ConfigurableShortcut& GetConfiguredShortcut(const char* configurationKeyName) const;
  void SetConfiguredShortcut(const char* configurationKeyName, const QKeySequence& value);
  
  void RegisterConfigurableAction(ActionWithConfigurableShortcut* action, const char* configurationKeyName);
  void DeregisterConfigurableAction(ActionWithConfigurableShortcut* action, const char* configurationKeyName);
  
  void AddConfigurableColor(Color id, const QString& name, const char* configurationKeyName, const QRgb& defaultValue);
  inline int GetNumConfigurableColors() const { return static_cast<int>(Color::NumColors); }
  inline QRgb GetConfiguredColor(Color id) const { return configuredColors[static_cast<int>(id)].value; }
  inline const ConfigurableColor& GetConfigurableColor(Color id) { return configuredColors[static_cast<int>(id)]; }
  void SetConfigurableColor(Color id, QRgb value);
  
  void AddConfigurableTextStyle(TextStyle id, const QString& name, const char* configurationKeyName, bool affectsText, const QRgb& textColor, bool bold, bool affectsBackground, const QRgb& backgroundColor);
  inline int GetNumConfigurableTextStyles() const { return static_cast<int>(TextStyle::NumTextStyles); }
  inline const ConfigurableTextStyle& GetConfiguredTextStyle(TextStyle id) const { return configuredTextStyles[static_cast<int>(id)]; }
  void SetConfigurableTextStyle(TextStyle id, bool affectsText, const QRgb& textColor, bool bold, bool affectsBackground, const QRgb& backgroundColor);
  
  void LoadLocalVariableColorPool();
  void SaveLocalVariableColorPool();
  inline int GetLocalVariableColorPoolSize() const { return localVariableColorPool.size(); }
  inline QRgb GetLocalVariableColor(int index) const { return localVariableColorPool[index]; }
  /// Make sure to call SaveLocalVariableColorPool() after calling this function.
  inline void SetLocalVariableColor(int index, const QRgb& color) { localVariableColorPool[index] = color; }
  inline void SetLocalVariableColors(const std::vector<QRgb>& colors) { localVariableColorPool = colors; SaveLocalVariableColorPool(); }
  
  /// (Re-)loads the fonts. This must be done after changing the font size.
  void ReloadFonts();
  
  inline float GetFontSize() const {
    float defaultFontSize = 10.5f;
    #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
      defaultFontSize = 11;
    #elif __APPLE__
      defaultFontSize = 16;
    #endif
    return settings.value("font_size", defaultFontSize).toFloat();
  }
  
  inline const QFont& GetDefaultFont() const {
    return defaultFont;
  }
  
  inline const QFont& GetBoldFont() const {
    return boldFont;
  }
  
  inline QString GetDefaultCompiler() const {
    return settings.value("default_compiler").toString();
  }
  
  inline QString GetGDBPath() const {
    return settings.value("gdb_path", "gdb").toString();
  }
  
  inline bool GetUsePerVariableColoring() const {
    return settings.value("per_variable_coloring", true).toBool();
  }
  
  inline bool GetHighlightCurrentLine() const {
    return settings.value("highlight_current_line", true).toBool();
  }
  
  inline bool GetHighlightTrailingSpaces() const {
    return settings.value("highlight_trailing_spaces", true).toBool();
  }
  
  inline bool GetDarkenNonContextRegions() const {
    return settings.value("darken_non_context_regions", true).toBool();
  }
  
  inline bool GetSourceLeftOfHeaderOrdering() const {
    return settings.value("source_left_of_header", true).toBool();
  }
  
  inline bool GetShowColumnMarker() const {
    return settings.value("show_column_marker", false).toBool();
  }
  
  inline int GetColumnMarkerPosition() const {
    return settings.value("column_marker_position", 80).toInt();
  }
  
  inline NewlineFormat GetDefaultNewlineFormat() const {
    return static_cast<NewlineFormat>(settings.value("default_newline_format", static_cast<int>(NewlineFormat::Lf)).toInt());
  }
  
  inline QStringList GetCommentMarkers() const {
    static QStringList defaultCommentMarkers = {"TODO", "FIXME", "TEST", "HACK", "END"};
    return settings.value("comment_markers", defaultCommentMarkers).toStringList();
  }
  
  inline const std::vector<WordCompletion>& GetWordCompletions() {
    if (wordCompletionsLookedUp) {
      return wordCompletions;
    }
    
    if (!settings.value("word_completions_written").toBool()) {
      // Return default word completions.
      static std::vector<WordCompletion> defaultCompletions = {
          // Keyword phrase completions
          WordCompletion(QStringLiteral("for"), QStringLiteral("for ($) {\n  \n}"), false, true),
          WordCompletion(QStringLiteral("if"), QStringLiteral("if ($) {\n  \n}"), false, true),
          WordCompletion(QStringLiteral("if"), QStringLiteral("if ($)"), true, true),
          WordCompletion(QStringLiteral("else"), QStringLiteral("else ${\n  \n}"), false, true),
          WordCompletion(QStringLiteral("while"), QStringLiteral("while ($) {\n  \n}"), false, true),
          WordCompletion(QStringLiteral("switch"), QStringLiteral("switch ($) {\ncase TODO:\n  \n}"), false, true),
          WordCompletion(QStringLiteral("do"), QStringLiteral("do {\n  $\n} while (TODO);"), false, true),
          // NOTE: This completion triggered too often when writing forward declarations. Also, there is
          //       the "create class" functionality which generates this as well. So, it is probably better
          //       to leave this out (unless we improve it such that it does not trigger for forward declarations).
          // WordCompletion(QStringLiteral("class"), QStringLiteral("class $ {\n public:\n  \n private:\n  \n};"), false),
          WordCompletion(QStringLiteral("struct"), QStringLiteral("struct $ {\n  \n};"), false, true),
          WordCompletion(QStringLiteral("enum"), QStringLiteral("enum $ {\n  \n};"), false, true),
          WordCompletion(QStringLiteral("union"), QStringLiteral("union $ {\n  \n};"), false, true),
          WordCompletion(QStringLiteral("return"), QStringLiteral("return $;"), false, true),
          
          // Spelling corrections
          WordCompletion(QStringLiteral("vool"), QStringLiteral("bool "), true, false),
          WordCompletion(QStringLiteral("e;se"), QStringLiteral("else "), true, false),
          WordCompletion(QStringLiteral("#inlcude"), QStringLiteral("#include "), true, false),
          WordCompletion(QStringLiteral("#incldue"), QStringLiteral("#include "), true, false),
      };
      return defaultCompletions;
    }
    
    int size = settings.beginReadArray("word_completions");
    wordCompletions.resize(size);
    for (int i = 0; i < size; ++ i) {
      settings.setArrayIndex(i);
      wordCompletions[i] = WordCompletion(
          settings.value("word").toString(),
          settings.value("replacement").toString(),
          settings.value("applyIfNonWhitespaceFollows").toBool(),
          settings.value("applyWithinCodeOnly").toBool());
    }
    settings.endArray();
    
    wordCompletionsLookedUp = true;
    return wordCompletions;
  }
  
  inline CodeCompletionConfirmationKeys GetCodeCompletionConfirmationKeys() const {
    return static_cast<CodeCompletionConfirmationKeys>(
        settings.value("code_completion_confirmation_keys", static_cast<int>(CodeCompletionConfirmationKeys::TabAndReturn)).toInt());
  }
  
  static void ShowSettingsWindow(QWidget* parent = nullptr);
  
 public slots:
  inline void SetFontSize(float size) {
    settings.setValue("font_size", size);
  }
  
  inline void SetDefaultCompiler(const QString& path) {
    settings.setValue("default_compiler", path);
  }
  
  inline void SetGDBPath(const QString& path) {
    settings.setValue("gdb_path", path);
  }
  
  inline void SetUsePerVariableColoring(bool enable) {
    settings.setValue("per_variable_coloring", enable);
  }
  
  inline void SetHighlightCurrentLine(bool enable) {
    settings.setValue("highlight_current_line", enable);
  }
  
  inline void SetHighlightTrailingSpaces(bool enable) {
    settings.setValue("highlight_trailing_spaces", enable);
  }
  
  inline void SetDarkenNonContextRegions(bool enable) {
    settings.setValue("darken_non_context_regions", enable);
  }
  
  inline void SetSourceLeftOfHeaderOrdering(bool enable) {
    settings.setValue("source_left_of_header", enable);
  }
  
  inline void SetShowColumnMarker(bool enable) {
    settings.setValue("show_column_marker", enable);
  }
  
  inline void SetColumnMarkerPosition(int position) {
    settings.setValue("column_marker_position", position);
  }
  
  inline void SetDefaultNewlineFormat(NewlineFormat format) {
    settings.setValue("default_newline_format", static_cast<int>(format));
  }
  
  inline void SetCommentMarkers(const QStringList& markers) {
    settings.setValue("comment_markers", markers);
  }
  
  inline void SetWordCompletions(const std::vector<WordCompletion>& completions) {
    wordCompletionsLookedUp = true;
    wordCompletions = completions;
    
    settings.beginWriteArray("word_completions", wordCompletions.size());
    for (int i = 0; i < wordCompletions.size(); ++ i) {
      settings.setArrayIndex(i);
      settings.setValue("word", wordCompletions[i].word);
      settings.setValue("replacement", wordCompletions[i].replacement);
      settings.setValue("applyIfNonWhitespaceFollows", wordCompletions[i].applyIfNonWhitespaceFollows);
      settings.setValue("applyWithinCodeOnly", wordCompletions[i].applyWithinCodeOnly);
    }
    settings.endArray();
    
    settings.setValue("word_completions_written", true);
  }
  
  inline void SetCodeCompletionConfirmationKeys(CodeCompletionConfirmationKeys keys) {
    settings.setValue("code_completion_confirmation_keys", static_cast<int>(keys));
  }
  
 signals:
  void FontChanged();
  
 private:
  Settings();
  
  QSettings settings;
  
  QFont defaultFont;
  QFont boldFont;
  
  bool wordCompletionsLookedUp = false;
  std::vector<WordCompletion> wordCompletions;
  
  /// Maps: configuration key name --> shortcut data.
  std::unordered_map<QString, std::shared_ptr<ConfigurableShortcut>> shortcuts;
  
  /// Indexed by static_cast<int>(color_id) with color_id of type Color.
  std::vector<ConfigurableColor> configuredColors;
  
  /// Indexed by static_cast<int>(textstyle_id) with textstyle_id of type TextStyle.
  std::vector<ConfigurableTextStyle> configuredTextStyles;
  
  std::vector<QRgb> localVariableColorPool;
};


class SettingsDialog : public QDialog {
 Q_OBJECT
 public:
  SettingsDialog(QWidget* parent = nullptr);
  
 private:
  enum class Categories {
    General = 0,
    AutoCompletionsCorrections,
    CodeParsing,
    CodeHighlighting,
    Colors,
    Debugging,
    DocumentationFiles,
    KeyboardShortcuts
  };
  
  QStackedLayout* categoriesLayout;
  
  // "General" category
  QWidget* CreateGeneralCategory();

  QLineEdit* fontSizeEdit;
  QComboBox* headerSourceOrderingCombo;
  QComboBox* codeCompletionConfirmationCombo;
  QCheckBox* showColumnMarkerCheck;
  QLineEdit* columnMarkerEdit;
  QComboBox* defaultNewlineFormatCombo;
  
  // "Auto-completions/corrections" category
  QWidget* CreateAutoCompletionsCorrectionsCategory();
  
  void UpdateAutoCompletionListItemLabel(int index);
  
  template <typename Callable>
  void EditCurrentAutoCompletion(Callable editFunc) {
    if (disableCompletionEditing) {
      return;
    }
    
    int index = autoCompletionsList->currentRow();
    std::vector<WordCompletion> completions = Settings::Instance().GetWordCompletions();
    if (index < 0 || index >= completions.size()) {
      qDebug() << "Error: index < 0 || index >= completions.size()";
      return;
    }
    editFunc(&completions[index]);
    Settings::Instance().SetWordCompletions(completions);
    
    UpdateAutoCompletionListItemLabel(index);
  }
  
  QListWidget* autoCompletionsList;
  QPushButton* removeAutoCompletionButton;
  QLineEdit* autoCompletionWordEdit;
  QPlainTextEdit* autoCompletionReplacementEdit;
  QCheckBox* applyIfNonWhitespaceFollowsCheck;
  QCheckBox* applyWithinCodeOnlyCheck;
  bool disableCompletionEditing = false;
  
  // "Code parsing" category
  QWidget* CreateCodeParsingCategory();
  
  QLineEdit* defaultCompilerEdit;
  
  // "Code highlighting" category
  QWidget* CreateCodeHighlightingCategory();
  void UpdateCommentMarkers();
  
  QListWidget* commentMarkerList;
  QPushButton* removeCommentMarkerButton;
  
  // "Colors" category
  QWidget* CreateColorsCategory();
  void UpdateTextColorLabel(const QRgb& color);
  void UpdateBackgroundColorLabel(const QRgb& color);
  void UpdateTextStyleItem(QTableWidgetItem* item, const Settings::ConfigurableTextStyle& style);
  void UpdateLocalVariableColorSettings();
  void UpdateLocalColorsTable();
  
  template <typename Callable>
  void EditCurrentTextStyle(Callable editFunc) {
    QTableWidgetItem* item = textStylesTable->currentItem();
    if (listenToColorUpdates && item) {
      Settings::TextStyle styleId = static_cast<Settings::TextStyle>(item->data(Qt::UserRole).toInt());
      Settings::ConfigurableTextStyle style = Settings::Instance().GetConfiguredTextStyle(styleId);
      editFunc(&style);
      Settings::Instance().SetConfigurableTextStyle(styleId, style.affectsText, style.textColor, style.bold, style.affectsBackground, style.backgroundColor);
      UpdateTextStyleItem(textStylesTable->item(item->row(), 1), style);
    }
  }
  
  bool listenToColorUpdates;
  
  QTableWidget* colorsTable;
  
  QTableWidget* textStylesTable;
  QCheckBox* affectsTextCheck;
  QLabel* textColorLabel;
  QPushButton* textColorButton;
  QCheckBox* boldCheck;
  QCheckBox* affectsBackgroundCheck;
  QLabel* backgroundColorLabel;
  QPushButton* backgroundColorButton;
  
  QTableWidget* localColorsTable;
  QPushButton* localColorMoveUpButton;
  QPushButton* localColorMoveDownButton;
  QPushButton* localColorChangeButton;
  QPushButton* localColorDeleteButton;
  QPushButton* localColorInsertButton;
  
  // "Debugging" category
  QWidget* CreateDebuggingCategory();
  
  // "Documentation files" category
  QWidget* CreateDocumentationFilesCategory();
  
  QListWidget* documentationFilesList;
  QPushButton* removeDocumentationFileButton;
  
  // "Keyboard shortcuts" category
  QWidget* CreateKeyboardShortcutsCategory();
  
  QTableWidget* shortcutsTable;
  bool disableShortcutChangeSlot;
};
