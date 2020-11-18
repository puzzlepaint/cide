// Copyright 2020 Thomas Schöps
// This file is part of CIDE, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "cide/settings.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedLayout>
#include <QTableWidget>

#include "cide/text_utils.h"
#include "cide/util.h"
#include "cide/qt_help.h"


Settings& Settings::Instance() {
  static Settings instance;
  return instance;
}

void Settings::AddConfigurableShortcut(const QString& name, const char* configurationKeyName, const QKeySequence& defaultValue) {
  QString fullKeyName = QStringLiteral("shortcut/") + configurationKeyName;
  
  QKeySequence sequence;
  QSettings settings;
  if (settings.contains(fullKeyName)) {
    sequence = QKeySequence(settings.value(fullKeyName).toString(), QKeySequence::PortableText);
  } else {
    sequence = defaultValue;
  }
  
  shortcuts.insert(std::make_pair(QString::fromUtf8(configurationKeyName), std::shared_ptr<ConfigurableShortcut>(new ConfigurableShortcut(name, sequence))));
}

QStringList Settings::GetAllConfigurableShortcutKeys() const {
  QStringList result;
  for (const auto& item : shortcuts) {
    result.push_back(item.first);
  }
  return result;
}

const Settings::ConfigurableShortcut& Settings::GetConfiguredShortcut(const char* configurationKeyName) const {
  auto it = shortcuts.find(QString::fromUtf8(configurationKeyName));
  if (it != shortcuts.end()) {
    return *it->second;
  }
  qDebug() << "Error: Unknown shortcut key passed to Settings::GetConfiguredShortcut():" << configurationKeyName;
  return *shortcuts.begin()->second;  // dummy return value
}

void Settings::SetConfiguredShortcut(const char* configurationKeyName, const QKeySequence& value) {
  auto it = shortcuts.find(QString::fromUtf8(configurationKeyName));
  if (it != shortcuts.end()) {
    it->second->sequence = value;
    
    for (ActionWithConfigurableShortcut* action : it->second->registeredActions) {
      action->setShortcut(value);
    }
    
    QString fullKeyName = QStringLiteral("shortcut/") + configurationKeyName;
    QSettings().setValue(fullKeyName, value.toString(QKeySequence::PortableText));
  } else {
    qDebug() << "Error: Unknown shortcut key passed to Settings::SetConfiguredShortcut():" << configurationKeyName;
  }
}

void Settings::RegisterConfigurableAction(ActionWithConfigurableShortcut* action, const char* configurationKeyName) {
  auto it = shortcuts.find(QString::fromUtf8(configurationKeyName));
  if (it != shortcuts.end()) {
    it->second->registeredActions.push_back(action);
  } else {
    qDebug() << "Error: Unknown shortcut key passed to Settings::RegisterConfigurableAction():" << configurationKeyName;
  }
}

void Settings::DeregisterConfigurableAction(ActionWithConfigurableShortcut* action, const char* configurationKeyName) {
  auto it = shortcuts.find(QString::fromUtf8(configurationKeyName));
  if (it != shortcuts.end()) {
    auto& registeredActions = it->second->registeredActions;
    
    for (int i = 0, size = registeredActions.size(); i < size; ++ i) {
      if (registeredActions[i] == action) {
        registeredActions.erase(registeredActions.begin() + i);
        return;
      }
    }
    qDebug() << "Error: Action passed to Settings::DeregisterConfigurableAction() was not registered";
  } else {
    qDebug() << "Error: Unknown shortcut key passed to Settings::DeregisterConfigurableAction():" << configurationKeyName;
  }
}

void Settings::AddConfigurableColor(Color id, const QString& name, const char* configurationKeyName, const QRgb& defaultValue) {
  QString fullKeyName = QStringLiteral("color/") + configurationKeyName;
  
  QRgb color;
  QSettings settings;
  if (settings.contains(fullKeyName)) {
    color = ParseHexColor(settings.value(fullKeyName).toString());
  } else {
    color = defaultValue;
  }
  
  configuredColors[static_cast<int>(id)] = ConfigurableColor(name, fullKeyName, color);
}

void Settings::SetConfigurableColor(Color id, QRgb value) {
  ConfigurableColor& color = configuredColors[static_cast<int>(id)];
  
  color.value = value;
  QSettings().setValue(color.keyName, ToHexColorString(value));
}

void Settings::AddConfigurableTextStyle(TextStyle id, const QString& name, const char* configurationKeyName, bool affectsText, const QRgb& textColor, bool bold, bool affectsBackground, const QRgb& backgroundColor) {
  QString fullKeyName = QStringLiteral("text_style/") + configurationKeyName;
  
  ConfigurableTextStyle style;
  
  QSettings settings;
  if (settings.contains(fullKeyName + QStringLiteral("/affects_text"))) {
    style.affectsText = settings.value(fullKeyName + QStringLiteral("/affects_text")).toBool();
    style.textColor = ParseHexColor(settings.value(fullKeyName + QStringLiteral("/text_color")).toString());
    style.bold = settings.value(fullKeyName + QStringLiteral("/bold")).toBool();
    style.affectsBackground = settings.value(fullKeyName + QStringLiteral("/affects_background")).toBool();
    style.backgroundColor = ParseHexColor(settings.value(fullKeyName + QStringLiteral("/background_color")).toString());
  } else {
    style.affectsText = affectsText;
    style.textColor = textColor;
    style.bold = bold;
    style.affectsBackground = affectsBackground;
    style.backgroundColor = backgroundColor;
  }
  
  style.name = name;
  style.keyName = fullKeyName;
  
  configuredTextStyles[static_cast<int>(id)] = style;
}

void Settings::SetConfigurableTextStyle(TextStyle id, bool affectsText, const QRgb& textColor, bool bold, bool affectsBackground, const QRgb& backgroundColor) {
  ConfigurableTextStyle& style = configuredTextStyles[static_cast<int>(id)];
  QSettings settings;
  
  style.affectsText = affectsText;
  settings.setValue(style.keyName + QStringLiteral("/affects_text"), affectsText);
  
  style.textColor = textColor;
  settings.setValue(style.keyName + QStringLiteral("/text_color"), ToHexColorString(textColor));
  
  style.bold = bold;
  settings.setValue(style.keyName + QStringLiteral("/bold"), bold);
  
  style.affectsBackground = affectsBackground;
  settings.setValue(style.keyName + QStringLiteral("/affects_background"), affectsBackground);
  
  style.backgroundColor = backgroundColor;
  settings.setValue(style.keyName + QStringLiteral("/background_color"), ToHexColorString(backgroundColor));
}

void Settings::LoadLocalVariableColorPool() {
  QSettings settings;
  int size = settings.beginReadArray("local_variable_color_pool");
  if (size == 0) {
    // Set the defaults.
    // These colors are taken from:
    // P. Green-Armytage (2010): A Colour Alphabet and the Limits of Colour Coding. // Colour: Design & Creativity (5) (2010): 10, 1-23
    localVariableColorPool.clear();
    localVariableColorPool.reserve(15);
    localVariableColorPool.emplace_back(qRgb(255,0,16));     // red
    localVariableColorPool.emplace_back(qRgb(0,117,220));    // medium blue
    localVariableColorPool.emplace_back(qRgb(43,206,72));    // medium green
    localVariableColorPool.emplace_back(qRgb(153,63,0));     // brown
    localVariableColorPool.emplace_back(qRgb(0,92,49));      // dark green
    localVariableColorPool.emplace_back(qRgb(143,124,0));    // green-brown
    localVariableColorPool.emplace_back(qRgb(157,204,0));    // poison green
    localVariableColorPool.emplace_back(qRgb(194,0,136));    // reddish purple
    localVariableColorPool.emplace_back(qRgb(255,168,187));  // reddish beige
    localVariableColorPool.emplace_back(qRgb(66,102,0));     // dark poison green
    localVariableColorPool.emplace_back(qRgb(94,241,242));   // cyan
    localVariableColorPool.emplace_back(qRgb(0,153,143));    // medium blueish greenish
    localVariableColorPool.emplace_back(qRgb(116,10,255));   // dark purple
    localVariableColorPool.emplace_back(qRgb(153,0,0));      // dark cinnober red
    localVariableColorPool.emplace_back(qRgb(240,163,255));  // lavender
    //     qRgb(255,255,128),  // too bright
    //     qRgb(255,255,0),    // too bright
    //     qRgb(255,80,5),     // too similar to our "class" color
    //     qRgb(76,0,92),      // too dark
    //     qRgb(25,25,25),     // too dark
    //     qRgb(255,164,5),    // too similar to our "class" color
    //     qRgb(0,51,128),     // blue, too similar to our "function" color
    //     qRgb(128,128,128),  // gray, too similar to our "comment" color
    //     qRgb(224,255,102),  // bright poison green, too bright
    //     qRgb(255,204,153),  // beige, too bright
    //     qRgb(148,255,181),  // light blueish green, too bright
  } else {
    localVariableColorPool.resize(size);
    for (int i = 0; i < size; ++ i) {
      settings.setArrayIndex(i);
      localVariableColorPool[i] = ParseHexColor(settings.value("color").toString());
    }
  }
  settings.endArray();
}

void Settings::SaveLocalVariableColorPool() {
  QSettings settings;
  settings.beginWriteArray("local_variable_color_pool");
  settings.remove("");  // remove previous entries in this group
  for (int i = 0; i < localVariableColorPool.size(); ++ i) {
    settings.setArrayIndex(i);
    settings.setValue("color", ToHexColorString(localVariableColorPool[i]));
  }
  settings.endArray();
}

void Settings::ReloadFonts() {
  int regularFontID = QFontDatabase::addApplicationFont(QDir(qApp->applicationDirPath()).filePath("resources/Inconsolata/Inconsolata-Regular.ttf"));
  int boldFontID = QFontDatabase::addApplicationFont(QDir(qApp->applicationDirPath()).filePath("resources/Inconsolata/Inconsolata-Bold.ttf"));
  
  if (regularFontID == -1 || boldFontID == -1) {
    qDebug() << "Failed to load at least one of the Inconsolata font files.";
    
    // Use default font.
    defaultFont = QFont("Monospace");
    boldFont = QFont("Monospace");
  } else {
    defaultFont = QFont(QFontDatabase::applicationFontFamilies(regularFontID)[0]);
    boldFont = QFont(QFontDatabase::applicationFontFamilies(boldFontID)[0]);
  }
  
  defaultFont.setPointSizeF(Settings::GetFontSize());
  boldFont.setPointSizeF(Settings::GetFontSize());
  
  boldFont.setBold(true);
  
  emit FontChanged();
}

Settings::Settings() {
  // Initial the symbol array
  InitializeSymbolArray();
  
  // Load fonts
  ReloadFonts();
  
  // Load the local-variable color pool
  LoadLocalVariableColorPool();
  
  // Set up the list of actions for which custom shortcuts can be configured
  AddConfigurableShortcut(tr("Build current target"), buildCurrentTargetShortcut, QKeySequence(Qt::Key_F7));
  AddConfigurableShortcut(tr("Start debugging"), startDebuggingShortcut, QKeySequence(Qt::Key_F9));
  AddConfigurableShortcut(tr("Search bar: Search in files"), searchInFilesShortcut, QKeySequence(Qt::Key_F4));
  AddConfigurableShortcut(tr("Search bar: Search local contexts"), searchLocalContextsShortcut, QKeySequence(Qt::Key_F5));
  AddConfigurableShortcut(tr("Search bar: Global symbol search"), searchGlobalSymbolsShortcut, QKeySequence(Qt::Key_F6));
  AddConfigurableShortcut(tr("Switch header/source"), switchHeaderSourceShortcut, QKeySequence(Qt::CTRL + Qt::Key_Tab));
  AddConfigurableShortcut(tr("Go to right tab"), goToRightTabShortcut, QKeySequence(Qt::ALT + Qt::SHIFT + Qt::Key_Right));
  AddConfigurableShortcut(tr("Go to left tab"), goToLeftTabShortcut, QKeySequence(Qt::ALT + Qt::SHIFT + Qt::Key_Left));
  AddConfigurableShortcut(tr("Reload file"), reloadFileShortcut, QKeySequence());
  AddConfigurableShortcut(tr("New file"), newFileShortcut, QKeySequence::New);
  AddConfigurableShortcut(tr("Open file"), openFileShortcut, QKeySequence::Open);
  AddConfigurableShortcut(tr("Save file"), saveFileShortcut, QKeySequence::Save);
  AddConfigurableShortcut(tr("Save file as..."), saveAsFileShortcut, QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_S));
  AddConfigurableShortcut(tr("Close file"), closeFileShortcut, QKeySequence::Close);
  AddConfigurableShortcut(tr("Quit program"), quitShortcut, QKeySequence());
  AddConfigurableShortcut(tr("Find and replace in files"), findAndReplaceInFilesShortcut, QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_F));
  AddConfigurableShortcut(tr("Show project files dock"), showProjectFilesDockShortcut, QKeySequence());
  AddConfigurableShortcut(tr("Show run dock"), showRunDockShortcut, QKeySequence());
  AddConfigurableShortcut(tr("Run gitk"), runGitkShortcut, QKeySequence(Qt::Key_F12));
  AddConfigurableShortcut(tr("Undo"), undoShortcut, QKeySequence::Undo);
  AddConfigurableShortcut(tr("Redo"), redoShortcut, QKeySequence::Redo);
  AddConfigurableShortcut(tr("Cut"), cutShortcut, QKeySequence::Cut);
  AddConfigurableShortcut(tr("Copy"), copyShortcut, QKeySequence::Copy);
  AddConfigurableShortcut(tr("Paste"), pasteShortcut, QKeySequence::Paste);
  AddConfigurableShortcut(tr("Open find bar"), findShortcut, QKeySequence(Qt::CTRL + Qt::Key_F));
  AddConfigurableShortcut(tr("Open replace bar"), replaceShortcut, QKeySequence(Qt::CTRL + Qt::Key_R));
  AddConfigurableShortcut(tr("Open goto line bar"), gotoLineShortcut, QKeySequence(Qt::CTRL + Qt::Key_G));
  AddConfigurableShortcut(tr("Toggle bookmark"), toggleBookmarkShortcut, QKeySequence(Qt::CTRL + Qt::Key_B));
  AddConfigurableShortcut(tr("Jump to previous bookmark"), jumpToPreviousBookmarkShortcut, QKeySequence(Qt::ALT + Qt::Key_PageUp));
  AddConfigurableShortcut(tr("Jump to next bookmark"), jumpToNextBookmarkShortcut, QKeySequence(Qt::ALT + Qt::Key_PageDown));
  AddConfigurableShortcut(tr("Remove all bookmarks"), removeAllBookmarksShortcut, QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_B));
  AddConfigurableShortcut(tr("Comment out"), commentOutShortcut, QKeySequence(Qt::CTRL + Qt::Key_D));
  AddConfigurableShortcut(tr("Uncomment"), uncommentShortcut, QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_D));
  AddConfigurableShortcut(tr("Invoke code completion"), invokeCodeCompletionShortcut, QKeySequence(Qt::CTRL + Qt::Key_Space));
  AddConfigurableShortcut(tr("Show documentation in dock"), showDocumentationInDockShortcut, QKeySequence(Qt::Key_F1));
  AddConfigurableShortcut(tr("Rename item at cursor"), renameItemAtCursorShortcut, QKeySequence(Qt::Key_F2));
  AddConfigurableShortcut(tr("Fix all visible trivial issues"), fixAllVisibleTrivialIssuesShortcut, QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_A));
  AddConfigurableShortcut(tr("Find next"), findNextShortcut, QKeySequence(Qt::Key_F3));
  AddConfigurableShortcut(tr("Find previous"), findPreviousShortcut, QKeySequence(Qt::SHIFT + Qt::Key_F3));
  
  // Set up the list of colors which can be configured
  configuredColors.resize(static_cast<int>(Color::NumColors));
  AddConfigurableColor(Color::EditorBackground, tr("Editor background"), "editor_background", qRgb(255, 255, 255));
  AddConfigurableColor(Color::TrailingSpaceHighlight, tr("Trailing space highlight"), "trailing_space_highlight", qRgb(255, 0, 0));
  AddConfigurableColor(Color::OutsizeOfContextLine, tr("Outside-of-context line background"), "outsize_of_context_line", qRgb(240, 240, 240));
  AddConfigurableColor(Color::CurrentLine, tr("Current line background"), "current_line_background", qRgb(248, 247, 246));
  AddConfigurableColor(Color::EditorSelection, tr("Selection background"), "editor_selection", qRgb(148, 202, 239));
  AddConfigurableColor(Color::BookmarkLine, tr("Bookmarked line background"), "bookmark_line", qRgb(229, 229, 255));
  AddConfigurableColor(Color::ErrorUnderline, tr("Underlining for errors"), "error_underline", qRgb(255, 0, 0));
  AddConfigurableColor(Color::WarningUnderline, tr("Underlining for warnings"), "warning_underline", qRgb(0, 255, 0));
  AddConfigurableColor(Color::ColumnMarker, tr("Column marker line color"), "column_marker", qRgb(230, 230, 230));
  AddConfigurableColor(Color::GitDiffAdded, tr("Git diff: Added lines marker"), "git_diff_add", qRgb(0, 255, 0));
  AddConfigurableColor(Color::GitDiffModified, tr("Git diff: Modified lines marker"), "git_diff_modified", qRgb(255, 255, 0));
  AddConfigurableColor(Color::GitDiffRemoved, tr("Git diff: Removed lines marker"), "git_diff_removed", qRgb(255, 0, 0));
  
  // Set up the list of text styles which can be configured
  configuredTextStyles.resize(static_cast<int>(TextStyle::NumTextStyles));
  AddConfigurableTextStyle(TextStyle::Default, tr("Default"), "default", true, qRgb(0, 0, 0), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::JustReplaced, tr("Range just replaced by \"Replace all\""), "just_replaced", false, qRgb(0, 0, 0), false, true, qRgb(236, 189, 237));
  AddConfigurableTextStyle(TextStyle::ReferenceHighlight, tr("Highlighted reference to the hovered item"), "reference_highlight", false, qRgb(0, 0, 0), false, true, qRgb(127, 255, 0));
  AddConfigurableTextStyle(TextStyle::CopyHighlight, tr("Highlighted occurrence of the same text as the selection"), "copy_highlight", false, qRgb(0, 0, 0), false, true, qRgb(255, 255, 0));
  AddConfigurableTextStyle(TextStyle::LeftBracketHighlight, tr("Highlight for bracket left of cursor and its matching bracket"), "left_bracket_highlight", false, qRgb(0, 0, 0), false, true, qRgb(255, 255, 0));
  AddConfigurableTextStyle(TextStyle::RightBracketHighlight, tr("Highlight for bracket right of cursor and its matching bracket"), "right_bracket_highlight", false, qRgb(0, 0, 0), false, true, qRgb(255, 144, 0));
  AddConfigurableTextStyle(TextStyle::ErrorInlineDisplay, tr("Inline error display"), "inline_error_display", true, qRgb(150, 127, 127), true, true, qRgb(255, 229, 229));
  AddConfigurableTextStyle(TextStyle::WarningInlineDisplay, tr("Inline warning display"), "inline_warning_display", true, qRgb(127, 150, 127), true, true, qRgb(229, 255, 229));
  AddConfigurableTextStyle(TextStyle::CommentMarker, tr("Marker word in a comment (such as \"TODO\"; can be configured)"), "comment_marker", true, qRgb(202, 146, 25), true, true, qRgb(69, 30, 26));
  AddConfigurableTextStyle(TextStyle::LanguageKeyword, tr("C/C++ keyword"), "language_keyword", true, qRgb(0, 0, 0), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::Comment, tr("Comment"), "comment", true, qRgb(80, 80, 80), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::ExtraPunctuation, tr("Punctuation that is usually redundant with the indentation (semicolon and curly braces)"), "extra_punctuation", true, qRgb(127, 127, 127), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::PreprocessorDirective, tr("Preprocessor directive"), "preprocessor_directive", true, qRgb(5, 113, 44), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::MacroDefinition, tr("Macro definition"), "macro_definition", true, qRgb(164, 18, 57), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::MacroInvocation, tr("Macro invocation"), "macro_invocation", false, qRgb(0, 0, 0), false, true, qRgb(235, 235, 235));
  AddConfigurableTextStyle(TextStyle::TemplateParameterDefinition, tr("Template parameter definition"), "template_parameter_definition", true, qRgb(175, 126, 2), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::TemplateParameterUse, tr("Template parameter use"), "template_parameter_use", true, qRgb(175, 126, 2), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::VariableDefinition, tr("Variable definition"), "variable_definition", true, qRgb(0, 127, 0), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::VariableUse, tr("Variable use"), "variable_use", true, qRgb(0, 127, 0), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::MemberVariableUse, tr("Member variable (attribute) use"), "member_variable_use", true, qRgb(179, 134, 12), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::TypedefDefinition, tr("Typedef definition"), "typedef_definition", true, qRgb(200, 0, 180), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::TypedefUse, tr("Typedef use"), "typedef_use", true, qRgb(200, 0, 180), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::EnumConstantDefinition, tr("Enum constant definition"), "enum_constant_definition", true, qRgb(0, 127, 0), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::EnumConstantUse, tr("Enum constant use"), "enum_constant_use", true, qRgb(0, 127, 0), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::ConstructorOrDestructorDefinition, tr("Constructor or destructor definition"), "constructor_or_destructor_definition", true, qRgb(175, 126, 2), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::ConstructorOrDestructorUse, tr("Constructor or destructor use"), "constructor_or_destructor_use", true, qRgb(175, 126, 2), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::FunctionDefinition, tr("Function definition"), "function_definition", true, qRgb(0, 0, 127), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::FunctionUse, tr("Function use"), "function_use", true, qRgb(0, 0, 127), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::UnionDefinition, tr("Union definition"), "union_definition", true, qRgb(140, 100, 2), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::EnumDefinition, tr("Enum definition"), "enum_definition", true, qRgb(140, 100, 2), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::ClassOrStructDefinition, tr("Class / struct definition"), "class_or_struct_definition", true, qRgb(220, 80, 2), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::ClassOrStructUse, tr("Class / struct use"), "class_or_struct_use", true, qRgb(220, 80, 2), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::LabelStatement, tr("Label statement (e.g., \"label:\")"), "label_statement", true, qRgb(200, 0, 42), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::LabelReference, tr("Label use (e.g., \"goto label\")"), "label_use", true, qRgb(200, 0, 42), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::IntegerLiteral, tr("Integer literal"), "integer_literal", true, qRgb(185, 143, 35), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::FloatingLiteral, tr("Floating-point literal"), "floating_literal", true, qRgb(185, 85, 35), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::ImaginaryLiteral, tr("Imaginary literal"), "imaginary_literal", true, qRgb(185, 85, 35), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::StringLiteral, tr("String literal"), "string_literal", true, qRgb(192, 8, 8), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::CharacterLiteral, tr("Character literal"), "character_literal", true, qRgb(192, 8, 8), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::IncludePath, tr("Include path"), "include_path", true, qRgb(255, 85, 0), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::NamespaceDefinition, tr("Namespace definition"), "namespace_definition", true, qRgb(127, 127, 127), true, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::NamespaceUse, tr("Namespace use"), "namespace_use", true, qRgb(127, 127, 127), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::ProjectTreeViewDefault, tr("Project tree view: Default style"), "project_tree_view_default", true, qRgb(0, 0, 0), false, true, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::ProjectTreeViewCurrentItem, tr("Project tree view: Current item"), "project_tree_view_current_item", false, qRgb(0, 0, 0), false, true, qRgb(220, 220, 255));
  AddConfigurableTextStyle(TextStyle::ProjectTreeViewOpenedItem, tr("Project tree view: Opened item"), "project_tree_view_opened_item", false, qRgb(0, 0, 0), false, true, qRgb(237, 233, 215));
  AddConfigurableTextStyle(TextStyle::ProjectTreeViewModifiedItem, tr("Project tree view: Modified item"), "project_tree_view_modified_item", true, qRgb(255, 100, 0), false, false, qRgb(255, 255, 255));
  AddConfigurableTextStyle(TextStyle::ProjectTreeViewUntrackedItem, tr("Project tree view: Untracked item"), "project_tree_view_untracked_item", true, qRgb(100, 100, 255), false, false, qRgb(255, 255, 255));
}

void Settings::ShowSettingsWindow(QWidget* parent) {
  SettingsDialog settingsDialog(parent);
  settingsDialog.exec();
}


SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(tr("Program settings"));
  setWindowIcon(QIcon(":/cide/cide.png"));
  
  QHBoxLayout* layout = new QHBoxLayout();
  
  
  // Category list
  QListWidget* categoryList = new QListWidget();
  
  QListWidgetItem* newItem = new QListWidgetItem(tr("General"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::General));
  categoryList->addItem(newItem);
  
  newItem = new QListWidgetItem(tr("Auto-completions/corrections"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::AutoCompletionsCorrections));
  categoryList->addItem(newItem);
  
  newItem = new QListWidgetItem(tr("Code parsing"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::CodeParsing));
  categoryList->addItem(newItem);
  
  newItem = new QListWidgetItem(tr("Code highlighting"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::CodeHighlighting));
  categoryList->addItem(newItem);
  
  newItem = new QListWidgetItem(tr("Colors"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::Colors));
  categoryList->addItem(newItem);
  
  newItem = new QListWidgetItem(tr("Debugging"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::Debugging));
  categoryList->addItem(newItem);
  
  newItem = new QListWidgetItem(tr("Documentation files"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::DocumentationFiles));
  categoryList->addItem(newItem);
  
  newItem = new QListWidgetItem(tr("Keyboard shortcuts"));
  newItem->setData(Qt::UserRole, static_cast<int>(Categories::KeyboardShortcuts));
  categoryList->addItem(newItem);
  
  layout->addWidget(categoryList);
  
  
  // Settings stack
  categoriesLayout = new QStackedLayout();
  categoriesLayout->addWidget(CreateGeneralCategory());
  categoriesLayout->addWidget(CreateAutoCompletionsCorrectionsCategory());
  categoriesLayout->addWidget(CreateCodeParsingCategory());
  categoriesLayout->addWidget(CreateCodeHighlightingCategory());
  categoriesLayout->addWidget(CreateColorsCategory());
  categoriesLayout->addWidget(CreateDebuggingCategory());
  categoriesLayout->addWidget(CreateDocumentationFilesCategory());
  categoriesLayout->addWidget(CreateKeyboardShortcutsCategory());
  
  layout->addLayout(categoriesLayout);
  
  
  QObject::connect(categoryList, &QListWidget::currentItemChanged, [&](QListWidgetItem* current, QListWidgetItem* /*previous*/) {
    if (!current) {
      return;
    }
    int categoryIndex = current->data(Qt::UserRole).toInt();
    categoriesLayout->setCurrentIndex(categoryIndex);
  });
  categoryList->setCurrentRow(0);
  
  QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
  QObject::connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  
  QVBoxLayout* dialogLayout = new QVBoxLayout();
  dialogLayout->addLayout(layout);
  dialogLayout->addWidget(buttonBox);
  setLayout(dialogLayout);
}

QWidget* SettingsDialog::CreateGeneralCategory() {
  QLabel* fontSizeLabel = new QLabel(tr("Font size (floating-point values allowed): "));
  fontSizeEdit = new QLineEdit(QString::number(Settings::Instance().GetFontSize()));
  QHBoxLayout* fontSizeLayout = new QHBoxLayout();
  fontSizeLayout->addWidget(fontSizeLabel);
  fontSizeLayout->addWidget(fontSizeEdit);
  
  QLabel* headerSourceOrderingLabel = new QLabel(tr("Header/source tab ordering: "));
  headerSourceOrderingCombo = new QComboBox();
  headerSourceOrderingCombo->addItem(tr("Source left, header right"), QVariant(true));
  headerSourceOrderingCombo->addItem(tr("Header left, source right"), QVariant(false));
  if (Settings::Instance().GetSourceLeftOfHeaderOrdering()) {
    headerSourceOrderingCombo->setCurrentIndex(0);
  } else {
    headerSourceOrderingCombo->setCurrentIndex(1);
  }
  QHBoxLayout* headerSourceOrderingLayout = new QHBoxLayout();
  headerSourceOrderingLayout->addWidget(headerSourceOrderingLabel);
  headerSourceOrderingLayout->addWidget(headerSourceOrderingCombo);
  
  QLabel* codeCompletionConfirmationKeysLabel = new QLabel(tr("Confirm code completion with: "));
  codeCompletionConfirmationCombo = new QComboBox();
  codeCompletionConfirmationCombo->addItem(tr("Tab"), static_cast<int>(Settings::CodeCompletionConfirmationKeys::Tab));
  if (Settings::Instance().GetCodeCompletionConfirmationKeys() == Settings::CodeCompletionConfirmationKeys::Tab) {
    codeCompletionConfirmationCombo->setCurrentIndex(0);
  }
  codeCompletionConfirmationCombo->addItem(tr("Return"), static_cast<int>(Settings::CodeCompletionConfirmationKeys::Return));
  if (Settings::Instance().GetCodeCompletionConfirmationKeys() == Settings::CodeCompletionConfirmationKeys::Return) {
    codeCompletionConfirmationCombo->setCurrentIndex(1);
  }
  codeCompletionConfirmationCombo->addItem(tr("Tab or Return"), static_cast<int>(Settings::CodeCompletionConfirmationKeys::TabAndReturn));
  if (Settings::Instance().GetCodeCompletionConfirmationKeys() == Settings::CodeCompletionConfirmationKeys::TabAndReturn) {
    codeCompletionConfirmationCombo->setCurrentIndex(2);
  }
  QHBoxLayout* codeCompletionConfirmationLayout = new QHBoxLayout();
  codeCompletionConfirmationLayout->addWidget(codeCompletionConfirmationKeysLabel);
  codeCompletionConfirmationLayout->addWidget(codeCompletionConfirmationCombo);
  
  showColumnMarkerCheck = new QCheckBox(tr("Show column marker after column: "));
  showColumnMarkerCheck->setChecked(Settings::Instance().GetShowColumnMarker());
  columnMarkerEdit = new QLineEdit(QString::number(Settings::Instance().GetColumnMarkerPosition()));
  columnMarkerEdit->setValidator(new QIntValidator(0, std::numeric_limits<int>::max(), columnMarkerEdit));
  columnMarkerEdit->setEnabled(showColumnMarkerCheck->isChecked());
  QHBoxLayout* columnMarkerLayout = new QHBoxLayout();
  columnMarkerLayout->addWidget(showColumnMarkerCheck);
  columnMarkerLayout->addWidget(columnMarkerEdit);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addLayout(fontSizeLayout);
  layout->addLayout(headerSourceOrderingLayout);
  layout->addLayout(codeCompletionConfirmationLayout);
  layout->addLayout(columnMarkerLayout);
  layout->addStretch(1);
  
  // --- Connections ---
  connect(fontSizeEdit, &QLineEdit::textChanged, [&](const QString& text) {
    Settings::Instance().SetFontSize(text.toFloat());
    Settings::Instance().ReloadFonts();
  });
  connect(headerSourceOrderingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [&](int index) {
    Settings::Instance().SetSourceLeftOfHeaderOrdering(index == 0);
  });
  connect(codeCompletionConfirmationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [&](int index) {
    Settings::Instance().SetCodeCompletionConfirmationKeys(
      static_cast<Settings::CodeCompletionConfirmationKeys>(codeCompletionConfirmationCombo->itemData(index, Qt::UserRole).toInt()));
  });
  connect(showColumnMarkerCheck, &QCheckBox::stateChanged, [&](int state) {
    Settings::Instance().SetShowColumnMarker(state == Qt::Checked);
    columnMarkerEdit->setEnabled(state == Qt::Checked);
  });
  connect(columnMarkerEdit, &QLineEdit::textChanged, [&](const QString& text) {
    Settings::Instance().SetColumnMarkerPosition(text.toInt());
  });
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}

QWidget* SettingsDialog::CreateAutoCompletionsCorrectionsCategory() {
  QLabel* descriptionLabel = new QLabel(tr(
      "These auto-completions are applied to words within whitespace."
      " The whitespace after the word will also be removed, so add a"
      " space at the end of the replacment in case you would like to keep it.\n"
      "\n"
      "An optional '$' sign within the replacement text sets the cursor position after the replacement."));
  descriptionLabel->setWordWrap(true);
  
  autoCompletionsList = new QListWidget();
  QPushButton* addAutoCompletionButton = new QPushButton(tr("+"));
  removeAutoCompletionButton = new QPushButton(tr("-"));
  
  QVBoxLayout* autoCompletionsButtonLayout = new QVBoxLayout();
  autoCompletionsButtonLayout->addWidget(addAutoCompletionButton);
  autoCompletionsButtonLayout->addWidget(removeAutoCompletionButton);
  autoCompletionsButtonLayout->addStretch(1);
  QHBoxLayout* autoCompletionsLayout = new QHBoxLayout();
  autoCompletionsLayout->addWidget(autoCompletionsList);
  autoCompletionsLayout->addLayout(autoCompletionsButtonLayout);
  
  QGroupBox* propertiesGroup = new QGroupBox(tr("Properties"));
  QLabel* wordLabel = new QLabel(tr("Word: "));
  autoCompletionWordEdit = new QLineEdit();
  QLabel* replacementLabel = new QLabel(tr("Replacement: "));
  autoCompletionReplacementEdit = new QPlainTextEdit();
  applyIfNonWhitespaceFollowsCheck = new QCheckBox(tr("Apply even if the word is followed by non-whitespace"));
  applyWithinCodeOnlyCheck = new QCheckBox(tr("Apply only within code, not within comments or strings"));
  
  QGridLayout* wordAndReplacementLayout = new QGridLayout();
  wordAndReplacementLayout->addWidget(wordLabel, 0, 0);
  wordAndReplacementLayout->addWidget(autoCompletionWordEdit, 0, 1);
  wordAndReplacementLayout->addWidget(replacementLabel, 1, 0);
  wordAndReplacementLayout->addWidget(autoCompletionReplacementEdit, 1, 1);
  
  QVBoxLayout* propertiesLayout = new QVBoxLayout();
  propertiesLayout->addLayout(wordAndReplacementLayout);
  propertiesLayout->addWidget(applyIfNonWhitespaceFollowsCheck);
  propertiesLayout->addWidget(applyWithinCodeOnlyCheck);
  propertiesGroup->setLayout(propertiesLayout);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(descriptionLabel);
  layout->addLayout(autoCompletionsLayout);
  layout->addWidget(propertiesGroup);
  
  // Fill the list with existing auto-completions
  const std::vector<WordCompletion>& completions = Settings::Instance().GetWordCompletions();
  for (int i = 0; i < completions.size(); ++ i) {
    autoCompletionsList->addItem("");
    UpdateAutoCompletionListItemLabel(autoCompletionsList->count() - 1);
  }
  
  // --- Connections ---
  connect(addAutoCompletionButton, &QPushButton::clicked, [&]() {
    std::vector<WordCompletion> completions = Settings::Instance().GetWordCompletions();
    completions.emplace_back(
        "<insert word>", "<insert replacement>", false, true);
    Settings::Instance().SetWordCompletions(completions);
    
    autoCompletionsList->addItem("");
    UpdateAutoCompletionListItemLabel(autoCompletionsList->count() - 1);
    autoCompletionsList->setCurrentRow(autoCompletionsList->count() - 1);
  });
  connect(removeAutoCompletionButton, &QPushButton::clicked, [&]() {
    int rowToDelete = autoCompletionsList->currentRow();
    if (rowToDelete < 0) {
      return;
    }
    
    delete autoCompletionsList->item(rowToDelete);
    
    std::vector<WordCompletion> completions = Settings::Instance().GetWordCompletions();
    completions.erase(completions.begin() + rowToDelete);
    Settings::Instance().SetWordCompletions(completions);
  });
  connect(autoCompletionsList, &QListWidget::currentRowChanged, [&](int row) {
    autoCompletionWordEdit->setEnabled(row >= 0);
    autoCompletionReplacementEdit->setEnabled(row >= 0);
    applyIfNonWhitespaceFollowsCheck->setEnabled(row >= 0);
    applyWithinCodeOnlyCheck->setEnabled(row >= 0);
    removeAutoCompletionButton->setEnabled(row >= 0);
    if (row < 0) {
      return;
    }
    
    const std::vector<WordCompletion>& completions = Settings::Instance().GetWordCompletions();
    if (row >= completions.size()) {
      qDebug() << "Error: Mismatch between the completions UI and settings (row >= completions.size())";
      return;
    }
    const WordCompletion& completion = completions[row];
    
    disableCompletionEditing = true;
    autoCompletionWordEdit->setText(completion.word);
    autoCompletionReplacementEdit->setPlainText(completion.replacement);
    applyIfNonWhitespaceFollowsCheck->setChecked(completion.applyIfNonWhitespaceFollows);
    applyWithinCodeOnlyCheck->setChecked(completion.applyWithinCodeOnly);
    disableCompletionEditing = false;
  });
  emit autoCompletionsList->currentRowChanged(autoCompletionsList->currentRow());
  
  connect(autoCompletionWordEdit, &QLineEdit::textEdited, [&](const QString& text) {
    EditCurrentAutoCompletion([&](WordCompletion* completion) { completion->word = text; });
  });
  connect(autoCompletionReplacementEdit, &QPlainTextEdit::textChanged, [&]() {
    EditCurrentAutoCompletion([&](WordCompletion* completion) { completion->replacement = autoCompletionReplacementEdit->toPlainText(); });
  });
  connect(applyIfNonWhitespaceFollowsCheck, &QCheckBox::clicked, [&](bool checked) {
    EditCurrentAutoCompletion([&](WordCompletion* completion) { completion->applyIfNonWhitespaceFollows = checked; });
  });
  connect(applyWithinCodeOnlyCheck, &QCheckBox::clicked, [&](bool checked) {
    EditCurrentAutoCompletion([&](WordCompletion* completion) { completion->applyWithinCodeOnly = checked; });
  });
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}

void SettingsDialog::UpdateAutoCompletionListItemLabel(int index) {
  const std::vector<WordCompletion>& completions = Settings::Instance().GetWordCompletions();
  QString oneLineReplacement = completions[index].replacement;
  oneLineReplacement.replace('\n', ' ');
  autoCompletionsList->item(index)->setText(
      completions[index].word + QStringLiteral(" --> ") + oneLineReplacement);
}

QWidget* SettingsDialog::CreateCodeParsingCategory() {
  QVBoxLayout* layout = new QVBoxLayout();
  QLabel* descriptionLabel = new QLabel(tr(
      "A path to a clang binary can be specified here to use as default compiler."
      " For projects with the \"use default compiler\" option activated, this binary will be"
      " used to query default include paths and clang's resource directory (instead of the"
      " compiler configured for building). This allows to properly configure libclang for"
      " parsing even with build directories configured for the gcc compiler, for example."));
  descriptionLabel->setWordWrap(true);
  layout->addWidget(descriptionLabel);
  
  defaultCompilerEdit = new QLineEdit(Settings::Instance().GetDefaultCompiler());
  QPushButton* defaultCompilerChoosePathButton = new QPushButton(tr("..."));
  QHBoxLayout* defaultCompilerLayout = new QHBoxLayout();
  defaultCompilerLayout->addWidget(defaultCompilerEdit);
  defaultCompilerLayout->addWidget(defaultCompilerChoosePathButton);
  
  layout->addLayout(defaultCompilerLayout);
  layout->addStretch(1);
  
  // --- Connections ---
  connect(defaultCompilerChoosePathButton, &QPushButton::clicked, [&]() {
    QString path = QFileDialog::getOpenFileName(
        this,
        tr("Choose default clang compiler binary"),
        QFileInfo(defaultCompilerEdit->text()).dir().path(),
        tr("All files (*)"));
    if (path.isEmpty()) {
      return;
    }
    defaultCompilerEdit->setText(QFileInfo(path).canonicalFilePath());
  });
  
  connect(defaultCompilerEdit, &QLineEdit::textChanged, [&](const QString& path) {
    Settings::Instance().SetDefaultCompiler(path);
  });
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}

QWidget* SettingsDialog::CreateCodeHighlightingCategory() {
  QCheckBox* perVariableColoringCheck = new QCheckBox(tr("Per-variable coloring"));
  perVariableColoringCheck->setChecked(Settings::Instance().GetUsePerVariableColoring());
  
  QCheckBox* highlightCurrentLineCheck = new QCheckBox(tr("Highlight the line in which the cursor is"));
  highlightCurrentLineCheck->setChecked(Settings::Instance().GetHighlightCurrentLine());
  
  QCheckBox* highlightTrailingSpacesCheck = new QCheckBox(tr("Highlight trailing spaces"));
  highlightTrailingSpacesCheck->setChecked(Settings::Instance().GetHighlightTrailingSpaces());
  
  QCheckBox* darkenNonContextRegionsCheck = new QCheckBox(tr("Darken empty lines outside of functions, classes, and similar contexts"));
  darkenNonContextRegionsCheck->setChecked(Settings::Instance().GetDarkenNonContextRegions());
  
  commentMarkerList = new QListWidget();
  QPushButton* addCommentMarkerButton = new QPushButton(tr("+"));
  removeCommentMarkerButton = new QPushButton(tr("-"));
  
  QStringList commentMarkers = Settings::Instance().GetCommentMarkers();
  for (const QString& marker : commentMarkers) {
    commentMarkerList->addItem(marker);
  }
  
  QVBoxLayout* commentMarkerButtonLayout = new QVBoxLayout();
  commentMarkerButtonLayout->addWidget(addCommentMarkerButton);
  commentMarkerButtonLayout->addWidget(removeCommentMarkerButton);
  commentMarkerButtonLayout->addStretch(1);
  QHBoxLayout* commentMarkerLayout = new QHBoxLayout();
  commentMarkerLayout->addWidget(commentMarkerList);
  commentMarkerLayout->addLayout(commentMarkerButtonLayout);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(perVariableColoringCheck);
  layout->addWidget(highlightCurrentLineCheck);
  layout->addWidget(highlightTrailingSpacesCheck);
  layout->addWidget(darkenNonContextRegionsCheck);
  layout->addLayout(commentMarkerLayout);
  layout->addStretch(1);
  
  // --- Connections ---
  connect(perVariableColoringCheck, &QCheckBox::stateChanged, &Settings::Instance(), &Settings::SetUsePerVariableColoring);
  connect(highlightCurrentLineCheck, &QCheckBox::stateChanged, &Settings::Instance(), &Settings::SetHighlightCurrentLine);
  connect(highlightTrailingSpacesCheck, &QCheckBox::stateChanged, &Settings::Instance(), &Settings::SetHighlightTrailingSpaces);
  connect(darkenNonContextRegionsCheck, &QCheckBox::stateChanged, &Settings::Instance(), &Settings::SetDarkenNonContextRegions);
  connect(commentMarkerList, &QListWidget::currentRowChanged, [&](int row) {
    removeCommentMarkerButton->setEnabled(row >= 0);
  });
  connect(addCommentMarkerButton, &QPushButton::clicked, [&]() {
    QString text = QInputDialog::getText(this, tr("Add comment marker"), tr("New marker: "));
    if (!text.isEmpty()) {
      commentMarkerList->addItem(text);
      UpdateCommentMarkers();
    }
  });
  connect(removeCommentMarkerButton, &QPushButton::clicked, [&]() {
    if (commentMarkerList->currentRow() < 0) {
      return;
    }
    delete commentMarkerList->currentItem();
    UpdateCommentMarkers();
  });
  
  removeCommentMarkerButton->setEnabled(commentMarkerList->currentRow() >= 0);
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}

void SettingsDialog::UpdateCommentMarkers() {
  QStringList markers;
  for (int i = 0; i < commentMarkerList->count(); ++ i) {
    markers.push_back(commentMarkerList->item(i)->text());
  }
  Settings::Instance().SetCommentMarkers(markers);
}

QWidget* SettingsDialog::CreateColorsCategory() {
  // "Colors" tab
  colorsTable = new QTableWidget(Settings::Instance().GetNumConfigurableColors(), 2);
  colorsTable->setHorizontalHeaderLabels(QStringList() << "Name" << "Color value");
  colorsTable->setColumnWidth(0, std::max(400, colorsTable->columnWidth(0)));
  colorsTable->verticalHeader()->hide();
  
  for (int row = 0; row < Settings::Instance().GetNumConfigurableColors(); ++ row) {
    const Settings::ConfigurableColor& color = Settings::Instance().GetConfigurableColor(static_cast<Settings::Color>(row));
    
    QTableWidgetItem* newItem = new QTableWidgetItem(color.name);
    newItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    newItem->setData(Qt::UserRole, row);
    colorsTable->setItem(row, 0, newItem);
    
    newItem = new QTableWidgetItem();
    newItem->setFlags(Qt::ItemIsEnabled);
    newItem->setBackground(QBrush(color.value));
    newItem->setData(Qt::UserRole, row);
    colorsTable->setItem(row, 1, newItem);
  }
  
  colorsTable->setSortingEnabled(true);
  colorsTable->sortByColumn(0, Qt::AscendingOrder);
  
  // "Text styles" tab
  QLabel* textStyleExplanationLabel = new QLabel(tr("Note that some text style changes may only affect newly opened editor tabs."));
  textStyleExplanationLabel->setWordWrap(true);
  
  textStylesTable = new QTableWidget(Settings::Instance().GetNumConfigurableTextStyles(), 2);
  textStylesTable->setHorizontalHeaderLabels(QStringList() << "Name" << "Setting");
  textStylesTable->setColumnWidth(0, std::max(400, textStylesTable->columnWidth(0)));
  textStylesTable->verticalHeader()->hide();
  
  for (int row = 0; row < Settings::Instance().GetNumConfigurableTextStyles(); ++ row) {
    const Settings::ConfigurableTextStyle& style = Settings::Instance().GetConfiguredTextStyle(static_cast<Settings::TextStyle>(row));
    
    QTableWidgetItem* newItem = new QTableWidgetItem(style.name);
    newItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    newItem->setData(Qt::UserRole, row);
    textStylesTable->setItem(row, 0, newItem);
    
    newItem = new QTableWidgetItem(tr("example text"));
    newItem->setFlags(Qt::ItemIsEnabled);
    UpdateTextStyleItem(newItem, style);
    newItem->setData(Qt::UserRole, row);
    textStylesTable->setItem(row, 1, newItem);
  }
  
  textStylesTable->setSortingEnabled(true);
  textStylesTable->sortByColumn(0, Qt::AscendingOrder);
  
  affectsTextCheck = new QCheckBox(tr("Affects the text"));
  textColorLabel = new QLabel();
  UpdateTextColorLabel(qRgb(0, 0, 0));  // just for the default text
  textColorButton = new QPushButton(tr("Select"));
  boldCheck = new QCheckBox(tr("Use bold font"));
  
  affectsBackgroundCheck = new QCheckBox(tr("Affects the background color"));
  backgroundColorLabel = new QLabel();
  UpdateBackgroundColorLabel(qRgb(255, 255, 255));  // just for the default text
  backgroundColorButton = new QPushButton(tr("Select"));
  
  QVBoxLayout* textSettingsLayout = new QVBoxLayout();
  textSettingsLayout->addWidget(affectsTextCheck);
  QHBoxLayout* textColorLayout = new QHBoxLayout();
  textColorLayout->addWidget(textColorLabel);
  textColorLayout->addWidget(textColorButton);
  textColorLayout->addStretch(1);
  textSettingsLayout->addLayout(textColorLayout);
  textSettingsLayout->addWidget(boldCheck);
  
  QVBoxLayout* backgroundSettingsLayout = new QVBoxLayout();
  backgroundSettingsLayout->addWidget(affectsBackgroundCheck);
  QHBoxLayout* backgroundColorLayout = new QHBoxLayout();
  backgroundColorLayout->addWidget(backgroundColorLabel);
  backgroundColorLayout->addWidget(backgroundColorButton);
  backgroundColorLayout->addStretch(1);
  backgroundSettingsLayout->addLayout(backgroundColorLayout);
  
  QHBoxLayout* settingsLayout = new QHBoxLayout();
  settingsLayout->addLayout(textSettingsLayout, 1);
  settingsLayout->addLayout(backgroundSettingsLayout, 1);
  
  QVBoxLayout* textStylesLayout = new QVBoxLayout();
  textStylesLayout->addWidget(textStyleExplanationLabel);
  textStylesLayout->addWidget(textStylesTable);
  textStylesLayout->addLayout(settingsLayout);
  
  QWidget* textStylesContainer = new QWidget();
  textStylesContainer->setLayout(textStylesLayout);
  
  // "Local-variable color pool" tab
  QLabel* localColorsLabel = new QLabel(tr("This is the pool of colors that are used for coloring local variables if the \"Per-variable coloring\" option is active. Colors are assigned in the order of appearance."));
  localColorsLabel->setWordWrap(true);
  
  localColorsTable = new QTableWidget(Settings::Instance().GetLocalVariableColorPoolSize(), 1);
  localColorsTable->horizontalHeader()->hide();
  localColorsTable->verticalHeader()->hide();
  UpdateLocalColorsTable();
  localColorsTable->setSortingEnabled(false);
  
  localColorMoveUpButton = new QPushButton(tr("Move up"));
  localColorMoveDownButton = new QPushButton(tr("Move down"));
  localColorChangeButton = new QPushButton(tr("Change"));
  localColorDeleteButton = new QPushButton(tr("Delete"));
  localColorInsertButton = new QPushButton(tr("Insert"));
  
  QPushButton* localColorResetButton = new QPushButton(tr("Reset to defaults"));
  
  QHBoxLayout* localVariableButtonsLayout = new QHBoxLayout();
  localVariableButtonsLayout->addWidget(localColorMoveUpButton);
  localVariableButtonsLayout->addWidget(localColorMoveDownButton);
  localVariableButtonsLayout->addWidget(localColorChangeButton);
  localVariableButtonsLayout->addWidget(localColorDeleteButton);
  localVariableButtonsLayout->addWidget(localColorInsertButton);
  
  QHBoxLayout* localVariableButtons2Layout = new QHBoxLayout();
  localVariableButtons2Layout->addStretch(1);
  localVariableButtons2Layout->addWidget(localColorResetButton);
  
  QVBoxLayout* localColorsLayout = new QVBoxLayout();
  localColorsLayout->addWidget(localColorsLabel);
  localColorsLayout->addWidget(localColorsTable);
  localColorsLayout->addLayout(localVariableButtonsLayout);
  localColorsLayout->addLayout(localVariableButtons2Layout);
  
  QWidget* localColorsContainer = new QWidget();
  localColorsContainer->setLayout(localColorsLayout);
  
  // Tab widget and general layout.
  QTabWidget* tabWidget = new QTabWidget();
  tabWidget->addTab(colorsTable, tr("Colors"));
  tabWidget->addTab(textStylesContainer, tr("Text styles"));
  tabWidget->addTab(localColorsContainer, tr("Local-variable color pool"));
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(tabWidget);
  
  // --- Connections ---
  listenToColorUpdates = true;
  
  connect(colorsTable, &QTableWidget::itemClicked, [&](QTableWidgetItem* item) {
    Settings::Color colorId = static_cast<Settings::Color>(item->data(Qt::UserRole).toInt());
    const Settings::ConfigurableColor& color = Settings::Instance().GetConfigurableColor(colorId);
    
    QColor result = QColorDialog::getColor(color.value, this);
    if (result.isValid()) {
      Settings::Instance().SetConfigurableColor(colorId, result.rgb());
      colorsTable->item(item->row(), 1)->setBackground(QBrush(result));
    }
  });
  
  connect(affectsTextCheck, &QCheckBox::stateChanged, [&](int state) {
    textColorLabel->setEnabled(state == Qt::Checked);
    textColorButton->setEnabled(state == Qt::Checked);
    boldCheck->setEnabled(state == Qt::Checked);
    EditCurrentTextStyle([&](Settings::ConfigurableTextStyle* style) { style->affectsText = state == Qt::Checked; });
  });
  connect(textColorButton, &QPushButton::clicked, [&]() {
    EditCurrentTextStyle([&](Settings::ConfigurableTextStyle* style) {
      QColor result = QColorDialog::getColor(style->textColor, this);
      if (result.isValid()) {
        style->textColor = result.rgb();
        UpdateTextColorLabel(result.rgb());
      }
    });
  });
  connect(boldCheck, &QCheckBox::stateChanged, [&](int state) {
    EditCurrentTextStyle([&](Settings::ConfigurableTextStyle* style) { style->bold = state == Qt::Checked; });
  });
  connect(affectsBackgroundCheck, &QCheckBox::stateChanged, [&](int state) {
    backgroundColorLabel->setEnabled(state == Qt::Checked);
    backgroundColorButton->setEnabled(state == Qt::Checked);
    EditCurrentTextStyle([&](Settings::ConfigurableTextStyle* style) { style->affectsBackground = state == Qt::Checked; });
  });
  connect(backgroundColorButton, &QPushButton::clicked, [&]() {
    EditCurrentTextStyle([&](Settings::ConfigurableTextStyle* style) {
      QColor result = QColorDialog::getColor(style->backgroundColor, this);
      if (result.isValid()) {
        style->backgroundColor = result.rgb();
        UpdateBackgroundColorLabel(result.rgb());
      }
    });
  });
  connect(textStylesTable, &QTableWidget::currentItemChanged, [&](QTableWidgetItem* item, QTableWidgetItem* /*previous*/) {
    affectsTextCheck->setEnabled(item != nullptr);
    textColorLabel->setEnabled(item != nullptr);
    textColorButton->setEnabled(item != nullptr);
    boldCheck->setEnabled(item != nullptr);
    affectsBackgroundCheck->setEnabled(item != nullptr);
    backgroundColorLabel->setEnabled(item != nullptr);
    backgroundColorButton->setEnabled(item != nullptr);
    if (!item) {
      return;
    }
    Settings::TextStyle styleId = static_cast<Settings::TextStyle>(item->data(Qt::UserRole).toInt());
    const Settings::ConfigurableTextStyle& style = Settings::Instance().GetConfiguredTextStyle(styleId);
    
    listenToColorUpdates = false;
    
    affectsTextCheck->setChecked(style.affectsText);
    UpdateTextColorLabel(style.textColor);
    boldCheck->setChecked(style.bold);
    affectsBackgroundCheck->setChecked(style.affectsBackground);
    UpdateBackgroundColorLabel(style.backgroundColor);
    
    listenToColorUpdates = true;
  });
  emit textStylesTable->currentItemChanged(textStylesTable->currentItem(), nullptr);
  
  connect(localColorsTable, &QTableWidget::currentItemChanged, [&](QTableWidgetItem* item, QTableWidgetItem* /*previous*/) {
    localColorMoveUpButton->setEnabled(item != nullptr && item->row() > 0);
    localColorMoveDownButton->setEnabled(item != nullptr && item->row() < localColorsTable->rowCount() - 1);
    localColorChangeButton->setEnabled(item != nullptr);
    localColorDeleteButton->setEnabled(item != nullptr);
  });
  emit localColorsTable->currentItemChanged(localColorsTable->currentItem(), nullptr);
  connect(localColorMoveUpButton, &QPushButton::clicked, [&]() {
    QTableWidgetItem* item = localColorsTable->currentItem();
    if (item == nullptr || item->row() == 0) {
      return;
    }
    QTableWidgetItem* otherItem = localColorsTable->item(item->row() - 1, 0);
    
    QColor temp = item->foreground().color();
    item->setForeground(QBrush(otherItem->foreground().color()));
    otherItem->setForeground(QBrush(temp));
    
    UpdateLocalVariableColorSettings();
  });
  connect(localColorMoveDownButton, &QPushButton::clicked, [&]() {
    QTableWidgetItem* item = localColorsTable->currentItem();
    if (item == nullptr || item->row() >= localColorsTable->rowCount() - 1) {
      return;
    }
    QTableWidgetItem* otherItem = localColorsTable->item(item->row() + 1, 0);
    
    QColor temp = item->foreground().color();
    item->setForeground(QBrush(otherItem->foreground().color()));
    otherItem->setForeground(QBrush(temp));
    
    UpdateLocalVariableColorSettings();
  });
  connect(localColorChangeButton, &QPushButton::clicked, [&]() {
    QTableWidgetItem* item = localColorsTable->currentItem();
    if (item == nullptr) {
      return;
    }
    QColor result = QColorDialog::getColor(item->foreground().color(), this);
    if (result.isValid()) {
      Settings::Instance().SetLocalVariableColor(item->row(), result.rgb());
      Settings::Instance().SaveLocalVariableColorPool();
      item->setForeground(QBrush(result));
    }
  });
  connect(localColorDeleteButton, &QPushButton::clicked, [&]() {
    QTableWidgetItem* item = localColorsTable->currentItem();
    if (item == nullptr) {
      return;
    }
    int row = item->row();
    delete item;
    localColorsTable->removeRow(row);
    UpdateLocalVariableColorSettings();
  });
  connect(localColorInsertButton, &QPushButton::clicked, [&]() {
    QTableWidgetItem* item = localColorsTable->currentItem();
    int row = item ? item->row() : localColorsTable->rowCount();
    localColorsTable->insertRow(row);
    
    QTableWidgetItem* newItem = new QTableWidgetItem(tr("example text"));
    newItem->setFlags(Qt::ItemIsEnabled);
    newItem->setFont(Settings::Instance().GetDefaultFont());
    newItem->setForeground(QBrush(Settings::Instance().GetConfiguredTextStyle(Settings::TextStyle::Default).textColor));
    newItem->setBackground(QBrush(Settings::Instance().GetConfiguredColor(Settings::Color::EditorBackground)));
    localColorsTable->setItem(row, 0, newItem);
    
    UpdateLocalVariableColorSettings();
  });
  connect(localColorResetButton, &QPushButton::clicked, [&]() {
    if (QMessageBox::question(this, tr("Reset to defaults"), tr("Are you sure to reset the local variable color pool to the default?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
      return;
    }
    
    QSettings settings;
    settings.beginWriteArray("local_variable_color_pool");
    settings.remove("");  // remove previous entries in this group
    settings.endArray();
    
    Settings::Instance().LoadLocalVariableColorPool();
    UpdateLocalColorsTable();
  });
  
  connect(tabWidget, &QTabWidget::currentChanged, [&](int index) {
    // Upon chaning to the local-variable colors tab, update the background color
    // of all items in that table to the current configured editor background color.
    if (index == 2) {
      for (int row = 0; row < localColorsTable->rowCount(); ++ row) {
        localColorsTable->item(row, 0)->setBackground(QBrush(Settings::Instance().GetConfiguredColor(Settings::Color::EditorBackground)));
      }
    }
  });
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}

void SettingsDialog::UpdateTextColorLabel(const QRgb& color) {
  textColorLabel->setText(tr("Text color: <span style=\"background-color:#%1\">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</span>").arg(ToHexColorString(color)));
}

void SettingsDialog::UpdateBackgroundColorLabel(const QRgb& color) {
  backgroundColorLabel->setText(tr("Background color: <span style=\"background-color:#%1\">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</span>").arg(ToHexColorString(color)));
}

void SettingsDialog::UpdateTextStyleItem(QTableWidgetItem* item, const Settings::ConfigurableTextStyle& style) {
  QFont font = style.bold ? Settings::Instance().GetBoldFont() : Settings::Instance().GetDefaultFont();
  item->setFont(font);
  
  if (style.affectsText) {
    item->setForeground(QBrush(style.textColor));
  } else {
    QTableWidgetItem defaultItem;
    item->setForeground(QBrush(defaultItem.foreground().color()));
  }
  
  if (style.affectsBackground) {
    item->setBackground(QBrush(style.backgroundColor));
  } else {
    QTableWidgetItem defaultItem;
    item->setBackground(defaultItem.background());
  }
}

void SettingsDialog::UpdateLocalVariableColorSettings() {
  std::vector<QRgb> colors(localColorsTable->rowCount());
  for (int row = 0; row < localColorsTable->rowCount(); ++ row) {
    colors[row] = localColorsTable->item(row, 0)->foreground().color().rgb();
  }
  Settings::Instance().SetLocalVariableColors(colors);
}

void SettingsDialog::UpdateLocalColorsTable() {
  localColorsTable->clearContents();
  localColorsTable->setRowCount(Settings::Instance().GetLocalVariableColorPoolSize());
  
  for (int row = 0; row < Settings::Instance().GetLocalVariableColorPoolSize(); ++ row) {
    QRgb color = Settings::Instance().GetLocalVariableColor(row);
    
    QTableWidgetItem* newItem = new QTableWidgetItem(tr("example text"));
    newItem->setFlags(Qt::ItemIsEnabled);
    newItem->setFont(Settings::Instance().GetDefaultFont());
    newItem->setForeground(QBrush(color));
    newItem->setBackground(QBrush(Settings::Instance().GetConfiguredColor(Settings::Color::EditorBackground)));
    localColorsTable->setItem(row, 0, newItem);
  }
}

QWidget* SettingsDialog::CreateDebuggingCategory() {
  QLabel* gdbBinaryLabel = new QLabel(tr("Path to GDB binary: "));
  QLineEdit* gdbBinaryEdit = new QLineEdit(Settings::Instance().GetGDBPath());
  QHBoxLayout* gdbBinaryLayout = new QHBoxLayout();
  gdbBinaryLayout->addWidget(gdbBinaryLabel);
  gdbBinaryLayout->addWidget(gdbBinaryEdit);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addLayout(gdbBinaryLayout);
  layout->addStretch(1);
  
  // --- Connections ---
  connect(gdbBinaryEdit, &QLineEdit::textEdited, [&](const QString& text) {
    Settings::Instance().SetGDBPath(text);
  });
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}

QWidget* SettingsDialog::CreateDocumentationFilesCategory() {
  QWidget* categoryWidget = new QWidget();
  
  QVBoxLayout* categoryLayout = new QVBoxLayout();
  QLabel* descriptionLabel = new QLabel(tr(
      "In this list, .qch files with external documentation can be specified which"
      " will be searched for when showing code info tooltips for items without documentation comments."));
  descriptionLabel->setWordWrap(true);
  categoryLayout->addWidget(descriptionLabel);
  QHBoxLayout* listLayout = new QHBoxLayout();
  
  documentationFilesList = new QListWidget();
  
  listLayout->addWidget(documentationFilesList);
  
  QVBoxLayout* listButtonsLayout = new QVBoxLayout();
  QPushButton* addButton = new QPushButton(tr("+"));
  listButtonsLayout->addWidget(addButton);
  removeDocumentationFileButton = new QPushButton(tr("-"));
  listButtonsLayout->addWidget(removeDocumentationFileButton);
  listButtonsLayout->addStretch(1);
  listLayout->addLayout(listButtonsLayout);
  
  categoryLayout->addLayout(listLayout);
  
  categoryWidget->setLayout(categoryLayout);
  
  // --- Connections ---
  auto updateButtonsFunc = [&]() {
    removeDocumentationFileButton->setEnabled(documentationFilesList->currentRow() >= 0);
  };
  connect(documentationFilesList, &QListWidget::currentRowChanged, updateButtonsFunc);
  updateButtonsFunc();
  
  auto updateListFunc = [&]() {
    documentationFilesList->clear();
    
    for (const QString& path : QtHelp::Instance().GetRegisteredNamespaces()) {
      documentationFilesList->addItem(path);
    }
  };
  updateListFunc();
  
  connect(addButton, &QPushButton::clicked, [&, updateListFunc]() {
    QString path = QFileDialog::getOpenFileName(
        this,
        tr("Add external documentation file"),
        /*defaultPath*/ "",
        tr("Qt documentation files (*.qch)"));
    if (path.isEmpty()) {
      return;
    }
    
    // Register the external documentation file (*.qch) if not already registered
    QString errorReason;
    if (QtHelp::Instance().RegisterQCHFile(path, &errorReason)) {
      updateListFunc();
    } else {
      QMessageBox::warning(this, tr("Error loading documentation file"), tr("An error occurred when trying to load file %1: %2").arg(path).arg(errorReason));
    }
  });
  
  connect(removeDocumentationFileButton, &QPushButton::clicked, [&, updateListFunc]() {
    if (documentationFilesList->currentRow() < 0) {
      return;
    }
    
    QString namespaceName = documentationFilesList->item(documentationFilesList->currentRow())->text();
    QString errorReason;
    if (QtHelp::Instance().UnregisterNamespace(namespaceName, &errorReason)) {
      updateListFunc();
    } else {
      QMessageBox::warning(this, tr("Error unloading documentation file"), tr("An error occurred when trying to unload namespace %1: %2").arg(namespaceName).arg(errorReason));
    }
  });
  
  return categoryWidget;
}

QWidget* SettingsDialog::CreateKeyboardShortcutsCategory() {
  QStringList allShortcutKeys = Settings::Instance().GetAllConfigurableShortcutKeys();
  
  shortcutsTable = new QTableWidget(allShortcutKeys.size(), 2);
  shortcutsTable->setHorizontalHeaderLabels(QStringList() << "Action" << "Shortcut");
  shortcutsTable->setColumnWidth(0, std::max(400, shortcutsTable->columnWidth(0)));
  shortcutsTable->verticalHeader()->hide();
  
  int row = 0;
  for (const QString& key : allShortcutKeys) {
    const Settings::ConfigurableShortcut& shortcut = Settings::Instance().GetConfiguredShortcut(key.toStdString().c_str());
    
    QTableWidgetItem* newItem = new QTableWidgetItem(shortcut.name);
    newItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    shortcutsTable->setItem(row, 0, newItem);
    
    newItem = new QTableWidgetItem(shortcut.sequence.toString(QKeySequence::NativeText));
    newItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
    newItem->setData(Qt::UserRole, key);
    shortcutsTable->setItem(row, 1, newItem);
    
    ++ row;
  }
  
  shortcutsTable->setSortingEnabled(true);
  shortcutsTable->sortByColumn(0, Qt::AscendingOrder);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(shortcutsTable);
  
  // --- Connections ---
  disableShortcutChangeSlot = false;
  connect(shortcutsTable, &QTableWidget::itemChanged, [&](QTableWidgetItem* item) {
    if (disableShortcutChangeSlot) {
      return;
    }
    
    QKeySequence newShortcut(item->text(), QKeySequence::NativeText);
    disableShortcutChangeSlot = true;
    item->setText(newShortcut.toString(QKeySequence::NativeText));
    disableShortcutChangeSlot = false;
    
    QString key = item->data(Qt::UserRole).toString();
    Settings::Instance().SetConfiguredShortcut(key.toStdString().c_str(), newShortcut);
  });
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
}
