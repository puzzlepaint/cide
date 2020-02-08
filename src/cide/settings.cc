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
  AddConfigurableShortcut(tr("Quit program"), quitShortcut, QKeySequence::Quit);
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
  AddConfigurableColor(Color::ErrorLine, tr("Background color for line with error"), "error_line", qRgb(255, 229, 229));
  AddConfigurableColor(Color::ErrorUnderline, tr("Underlining color errors"), "error_underline", qRgb(255, 0, 0));
  AddConfigurableColor(Color::WarningLine, tr("Background color for line with warning"), "warning_line", qRgb(229, 255, 229));
  AddConfigurableColor(Color::WarningUnderline, tr("Underlining color warnings"), "warning_underline", qRgb(0, 255, 0));
  AddConfigurableColor(Color::ColumnMarker, tr("Column marker line color"), "column_marker", qRgb(230, 230, 230));
  AddConfigurableColor(Color::GitDiffAdded, tr("Git diff: Added lines marker"), "git_diff_add", qRgb(0, 255, 0));
  AddConfigurableColor(Color::GitDiffModified, tr("Git diff: Modified lines marker"), "git_diff_modified", qRgb(255, 255, 0));
  AddConfigurableColor(Color::GitDiffRemoved, tr("Git diff: Removed lines marker"), "git_diff_removed", qRgb(255, 0, 0));
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
    newItem->setBackgroundColor(color.value);
    newItem->setData(Qt::UserRole, row);
    colorsTable->setItem(row, 1, newItem);
  }
  
  colorsTable->setSortingEnabled(true);
  colorsTable->sortByColumn(0, Qt::AscendingOrder);
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(colorsTable);
  
  // --- Connections ---
  connect(colorsTable, &QTableWidget::itemClicked, [&](QTableWidgetItem* item) {
    Settings::Color colorId = static_cast<Settings::Color>(item->data(Qt::UserRole).toInt());
    const Settings::ConfigurableColor& color = Settings::Instance().GetConfigurableColor(colorId);
    
    QColor result = QColorDialog::getColor(color.value, this);
    if (result.isValid()) {
      Settings::Instance().SetConfigurableColor(colorId, result.rgb());
      colorsTable->item(item->row(), 1)->setBackgroundColor(result);
    }
  });
  
  QWidget* categoryWidget = new QWidget();
  categoryWidget->setLayout(layout);
  return categoryWidget;
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
