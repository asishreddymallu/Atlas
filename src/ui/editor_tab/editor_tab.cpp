#include "editor_tab.h"
#include "custom_pseudo_manager/custom_pseudo_manager.h"
#include "ui/theme/theme_manager.h"
#include "ui_editor_tab.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextDocument>

namespace Kites
{

EditorTab::EditorTab(QWidget *parent, ProcessorManager *vmManager)
    : KitesTab(parent), m_processorManager(vmManager), ui(new Ui::EditorTab)
{
    ui->setupUi(this);
    ui->editorViewButton->setChecked(true); // default to editor view
    ui->stackedWidget->setCurrentIndex(0);  // show editor view by default

    ui->stackedWidget->layout()->setContentsMargins(0, 0, 0, 0);
    ui->stackedWidget->layout()->setSpacing(0);
    ui->stackedWidget->widget(0)->layout()->setContentsMargins(0, 0, 0, 0);
    ui->stackedWidget->widget(1)->layout()->setContentsMargins(0, 0, 0, 0);

    m_editor = ui->assemblyTextEdit;
    m_disassemblyView = ui->disassemblyTextEdit;
    m_expandedView = ui->expandedTextEdit;
    m_expandedView->setReadOnly(true);
    m_expandedView->setBreakpointInteractionEnabled(false);
    m_expandedView->setBreakpoints(m_editor->getBreakpoints());
    m_editor->setPlaceholderText(
        "Write RISC-V assembly here, or press Ctrl+O to open a file.");
    m_disassemblyView->setPlaceholderText(
        "Disassembly appears here once you run the program (F5).");
    m_expandedView->setPlaceholderText(
        "Pseudo-instructions are expanded here when you run the program.");
    m_squiggleFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    m_squiggleFormat.setUnderlineColor(ThemeManager::getInstance().getEditorErrorColor());
    connect(&ThemeManager::getInstance(), &ThemeManager::editorThemeChangedSignal, this,
            [this](const QString &)
            {
                m_squiggleFormat.setUnderlineColor(
                    ThemeManager::getInstance().getEditorErrorColor());
            });

    connect(ui->expandedViewButton, &QPushButton::toggled, this,
            &EditorTab::onExpandButtonClicked);

    m_languageService = new LanguageService(this);
    connect(m_languageService, &LanguageService::diagnosticsReadySignal, this,
            [this](const QVector<Diagnostic> &diagnostics)
            {
                if (m_editor->isReadOnly())
                    return; // a Run started while this request was in flight; Run owns the
                            // squiggles now, don't clobber them with a stale live result
                if (m_buildDiagnosticsActive)
                    return; // TODO(Step 7): build errors stay on screen until the next Run
                            // clears them; live diagnostics are not implemented yet and would
                            // otherwise wipe them with an empty result on the first keystroke
                applyDiagnostics(diagnostics);
            });

    m_diagnosticsDebounceTimer = new QTimer(this);
    m_diagnosticsDebounceTimer->setSingleShot(true);
    m_diagnosticsDebounceTimer->setInterval(300);
    connect(m_diagnosticsDebounceTimer, &QTimer::timeout, this,
            &EditorTab::requestLiveDiagnostics);
    // connect(m_diagnosticsDebounceTimer, &QTimer::timeout, this,
    //         [this]()
    //         {
                
    //         });
    connect(m_editor, &QPlainTextEdit::textChanged, this,
            [this]() { m_diagnosticsDebounceTimer->start(); });
}

void EditorTab::requestLiveDiagnostics()
{
    if (m_editor->isReadOnly())
        return; // running/debugging - Run's own diagnostics own the squiggles right now
    m_languageService->requestDiagnostics(QString::fromStdString(getRawText()));
}

void EditorTab::onExpandButtonClicked(bool checked)
{
    if (m_expandedLocked && !checked)
    {
        ui->expandedViewButton->setChecked(true);
        return;
    }

    if (checked)
    {
        std::string expandedCode = customPseudoManager::expandPseudoInstruction(getRawText());
        m_expandedView->setPlainText(QString::fromStdString(expandedCode));
        m_expandedView->setBreakpoints(m_editor->getBreakpoints());
        ui->stackedWidget->setCurrentIndex(1);
    }
    else
    {
        ui->stackedWidget->setCurrentIndex(0);
    }
    // Both buttons are autoExclusive, so every switch - including one made by
    // checking the other button - arrives here. One emit therefore covers
    // them all.
    emit viewChangedSignal(checked);
}

bool EditorTab::isExpandedView() const
{
    return ui->expandedViewButton->isChecked();
}

bool EditorTab::isExpandedLocked() const
{
    return m_expandedLocked;
}

void EditorTab::switchToExpandedView()
{
    ui->expandedViewButton->setChecked(true);
}

void EditorTab::switchToEditorView()
{
    ui->editorViewButton->setChecked(true);
}

void EditorTab::setExpandedLocked(bool locked)
{
    m_expandedLocked = locked;
    ui->editorViewButton->setEnabled(!locked);

    if (locked)
    {
        ui->expandedViewButton->setChecked(true);
        ui->stackedWidget->setCurrentIndex(1);
    }
    emit expandedLockChangedSignal(locked);
}

EditorTab::~EditorTab()
{
    delete ui;
}

std::string EditorTab::getRawText()
{
    return m_editor->toPlainText().toStdString();
}

std::vector<uint64_t> EditorTab::getBreakpoints() const
{
    return m_editor->getBreakpoints();
}

void EditorTab::setRawText(const QString &text)
{
    m_editor->setPlainText(text);
}

void EditorTab::resetErrorLines()
{
    m_buildDiagnosticsActive = false;
    m_editor->clearDiagnosticSelections();
    m_expandedView->clearDiagnosticSelections(); // applyDiagnostics() squiggles both panes,
                                                 // so both have to be cleared here too
    m_editor->resetErrors();
    m_expandedView->resetErrors(); // applyDiagnostics() sets tooltips on both panes
    emit diagnosticsChangedSignal({}); // and the dock clears with them
}

void EditorTab::updateDisassemblyView(const std::string &disassembledCode)
{
    m_disassemblyView->setPlainText(QString::fromStdString(disassembledCode));
}

void EditorTab::highlightLines(const std::vector<std::pair<int,std::string>> &editorLines, 
const std::vector<std::pair<int,std::string>> &disassemblyLines)
{
    m_editor->setLinesToHighlight(editorLines);
    m_disassemblyView->setLinesToHighlight(disassemblyLines);
    m_expandedView->setLinesToHighlight(
        editorLines); // we want the expanded view to have the same highlights as the editor
}

void EditorTab::setCanWrite(bool canWrite)
{
    m_editor->setReadOnly(!canWrite);
}

void EditorTab::showRuntimeError(int line, const QString &message)
{
    if (line <= 0)
        return;
    m_editor->setLinesToHighlight({{line, "Runtime Error"}});
    m_editor->setErrorMessage(line - 1, message);
}

void EditorTab::clearHighlights()
{
    m_editor->clearHighlights();
    m_expandedView->clearHighlights();
    m_disassemblyView->clearHighlights();
}

void EditorTab::setErrorLinesFromFile(const std::filesystem::path &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "Could not open error file:" << filePath.string();
        return;
    }

    // JSON -> Diagnostic lives in parseDiagnosticsJson() so it can be tested
    // without standing up the UI; the behaviour here is unchanged.
    const QVector<Diagnostic> diagnostics = parseDiagnosticsJson(file.readAll());
    if (diagnostics.isEmpty())
    {
        qWarning() << "Failed to parse error JSON.";
    }

    m_buildDiagnosticsActive = true;
    applyDiagnostics(diagnostics);
}

namespace
{
/// Text of 1-based `line` in `document`, or a null QString when out of range.
QString lineText(const QTextDocument *document, int line)
{
    if (!document || line <= 0)
        return {};
    const QTextBlock block = document->findBlockByNumber(line - 1);
    return block.isValid() ? block.text() : QString();
}

/// Converts a 1-based BYTE column, as the lexer counts them, into a 0-based
/// UTF-16 offset into `text`, as QTextCursor counts them.
///
/// The lexer advances its column once per std::string byte, so a UTF-8
/// multi-byte character costs as many columns as it has bytes while a
/// QTextCursor sees it as one or two UTF-16 units. Walking the string and
/// adding each code point's UTF-8 length keeps the two in step. The result is
/// clamped to the end of the text, so a column past the end of the line
/// selects up to the end rather than producing an invalid cursor.
int utf16OffsetForByteColumn(const QString &text, int byteColumn)
{
    if (byteColumn <= 1)
        return 0;

    const int targetBytes = byteColumn - 1;
    int bytes = 0;
    int offset = 0;
    while (offset < text.size() && bytes < targetBytes)
    {
        const QChar unit = text.at(offset);
        char32_t codePoint = unit.unicode();
        int units = 1;
        if (unit.isHighSurrogate() && offset + 1 < text.size() &&
            text.at(offset + 1).isLowSurrogate())
        {
            codePoint = QChar::surrogateToUcs4(unit, text.at(offset + 1));
            units = 2;
        }
        bytes += codePoint < 0x80 ? 1 : codePoint < 0x800 ? 2 : codePoint < 0x10000 ? 3 : 4;
        offset += units;
    }
    return offset;
}

/// Builds a squiggle selection for 1-based `line` of `document`.
///
/// Spans [startColumn, endColumn) are 1-based byte columns from the lexer and
/// are converted to UTF-16 offsets and clamped to the block before use.
///
/// Passing startColumn <= 0 (or a span that does not fit) highlights the whole
/// line, which is the documented fallback for a diagnostic with no column.
bool makeCursor(QTextDocument *document, int line, int startColumn, int endColumn,
                QTextCursor &cursorOut)
{
    if (!document || line <= 0)
        return false;

    const QTextBlock block = document->findBlockByNumber(line - 1);
    if (!block.isValid())
        return false;

    const QString text = block.text();
    const int length = text.size();
    QTextCursor cursor(block);

    const bool haveSpan = startColumn > 0 && endColumn > startColumn && length > 0;
    if (haveSpan)
    {
        // byte columns -> UTF-16 offsets, both clamped to the block
        const int from = std::min(utf16OffsetForByteColumn(text, startColumn), length);
        const int to   = std::min(utf16OffsetForByteColumn(text, endColumn), length);
        cursor.setPosition(block.position() + from);
        cursor.setPosition(block.position() + to, QTextCursor::KeepAnchor);
    }
    if (!haveSpan || !cursor.hasSelection())
    {
        cursor = QTextCursor(block);
        cursor.movePosition(QTextCursor::StartOfLine);
        cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    }

    cursorOut = cursor;
    return true;
}

/// Squiggle selection over the same span makeCursor() would select, so the
/// dock's navigation and the editor's underline can never disagree.
bool makeSquiggle(QTextDocument *document, int line, int startColumn, int endColumn,
                  const QTextCharFormat &format, QTextEdit::ExtraSelection &selectionOut)
{
    QTextCursor cursor;
    if (!makeCursor(document, line, startColumn, endColumn, cursor))
        return false;
    selectionOut.format = format;
    selectionOut.cursor = cursor;
    return true;
}
} // namespace

void EditorTab::setExpansionMapping(const std::vector<int> &expandedToOriginalLine)
{
    m_expandedToOriginalLine = expandedToOriginalLine;
}

void EditorTab::applyDiagnostics(const QVector<Diagnostic> &diagnostics)
{
    m_editor->resetErrors();
    m_expandedView->resetErrors();

    // Each pane needs cursors built from its OWN document: an ExtraSelection
    // whose cursor belongs to another document is not a valid selection for
    // the widget it is handed to. Previously both panes were given cursors
    // from m_editor's document.
    QList<QTextEdit::ExtraSelection> inputSelections;
    QList<QTextEdit::ExtraSelection> expandedSelections;

    QTextDocument *inputDocument    = m_editor->document();
    QTextDocument *expandedDocument = m_expandedView->document();

    QVector<Diagnostic> resolved;
    resolved.reserve(diagnostics.size());

    for (const Diagnostic &diagnostic : diagnostics)
    {
        if (diagnostic.line <= 0)
            continue; // Skip invalid lines

        const InputTarget target = inputTargetFor(diagnostic);
        const int originalLine = target.line;

        Diagnostic withOriginal = diagnostic;
        withOriginal.originalLine = originalLine;
        resolved.append(withOriginal);

        QTextEdit::ExtraSelection inputSelection;
        if (makeSquiggle(inputDocument, target.line, target.startColumn, target.endColumn,
                         m_squiggleFormat, inputSelection))
        {
            inputSelections.append(inputSelection);
            // tooltip lives on the line the user can actually edit
            m_editor->setErrorMessage(originalLine - 1, diagnostic.message);
        }

        QTextEdit::ExtraSelection expandedSelection;
        if (makeSquiggle(expandedDocument, diagnostic.line, diagnostic.startColumn,
                         diagnostic.endColumn, m_squiggleFormat, expandedSelection))
        {
            expandedSelections.append(expandedSelection);
            m_expandedView->setErrorMessage(diagnostic.line - 1, diagnostic.message);
        }
    }

    m_editor->setDiagnosticSelections(inputSelections);
    m_expandedView->setDiagnosticSelections(expandedSelections);

    emit diagnosticsChangedSignal(resolved);
}

EditorTab::InputTarget EditorTab::inputTargetFor(const Diagnostic &diagnostic) const
{
    // The assembler reports against the expanded text; map back to the line the
    // user actually wrote. Without custom pseudo-instructions the mapping is
    // the identity and this is a no-op.
    int originalLine = diagnostic.originalLine;
    if (originalLine <= 0)
    {
        originalLine = mappedOriginalLine(diagnostic.line);
    }

    // Columns describe the expanded line. They only carry over to the original
    // when that line survived expansion unchanged; if the line was generated or
    // rewritten, fall back to the whole line.
    const QString expandedLine = lineText(m_expandedView->document(), diagnostic.line);
    const QString originalText = lineText(m_editor->document(), originalLine);
    const bool columnsApply = !expandedLine.isNull() && expandedLine == originalText;

    return {originalLine,
            columnsApply ? diagnostic.startColumn : 0,
            columnsApply ? diagnostic.endColumn : 0};
}

bool EditorTab::goToDiagnostic(const Diagnostic &diagnostic)
{
    // Same helper the squiggle uses, so the selection the dock makes is
    // exactly what the Input pane underlines.
    const InputTarget target = inputTargetFor(diagnostic);

    QTextCursor cursor;
    if (!makeCursor(m_editor->document(), target.line, target.startColumn, target.endColumn,
                    cursor))
    {
        return false; // no line to go to: do nothing rather than jump anywhere
    }

    switchToEditorView(); // the Expanded view is read-only generated code
    m_editor->setTextCursor(cursor);
    m_editor->centerCursor();
    m_editor->setFocus();
    return true;
}

int EditorTab::sourceLineCount() const
{
    return m_editor->document()->blockCount();
}

int EditorTab::mappedOriginalLine(int expandedLine) const
{
    if (expandedLine <= 0)
        return 0;
    const size_t index = static_cast<size_t>(expandedLine) - 1;
    if (index >= m_expandedToOriginalLine.size())
        return expandedLine; // no mapping recorded: lines are 1:1
    return m_expandedToOriginalLine[index];
}
} // namespace Kites
