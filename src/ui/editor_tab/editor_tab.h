#ifndef EDITORTAB__H
#define EDITORTAB__H

#include "main_editor.h"
#include "disassembly_editor.h"
#include "language_service/diagnostic.h"
#include "language_service/language_service.h"
#include "processor/processor_manager.h"
#include "ui/common/kites_tab.h"
#include <QPlainTextEdit>
#include <QString>
#include <QTextCharFormat>
#include <QTimer>
#include <filesystem>
#include <vector>

namespace Kites
{
namespace Ui
{
class EditorTab;
}

class EditorTab : public KitesTab
{
    Q_OBJECT

public:
    explicit EditorTab(QWidget *parent = nullptr, ProcessorManager *vmManager = nullptr);
    ~EditorTab();
    void updateDisassemblyView(const std::string &disassembledCode);

    /**
     * @brief Loads a file's contents into the Input pane.
     *
     * The File > Open path, named so it can be driven without a modal file
     * dialog. It clears the breakpoints: they are line numbers in the file
     * that was open, and they mean nothing in a different one.
     *
     * Deliberately NOT done in Editor::setPlainText. Other code sets the
     * text - the Expanded view, a reload - and has always left the numbers
     * alone; only opening a file is a new file.
     */
    void loadFileContent(const QString &content);
    void setErrorLinesFromFile(const std::filesystem::path &filepath);
    /// Mapping produced by customPseudoManager::expandPseudoInstruction:
    /// index = expanded line - 1, value = 1-based original source line. Set by
    /// MainWindow just before assembling so diagnostics reported against the
    /// expanded text can be pointed back at the line the user wrote. With no
    /// custom pseudo-instructions defined this is the identity.
    void setExpansionMapping(const std::vector<int> &expandedToOriginalLine);
    /**
     * @brief The Input pane's breakpoints, as lines of the EXPANDED text.
     *
     * The same mapping read the other way: for each breakpoint, the FIRST
     * expanded line that came from its source line. A pseudo-instruction
     * that expands to several lines therefore shows one dot, on the first
     * of them, rather than one per line.
     *
     * A breakpoint on a source line that produces no expanded line at all
     * gets no dot - there is nowhere to put it.
     *
     * `expandedToOriginalLine` is indexed by expanded line - 1 and holds
     * 1-based original lines, exactly as
     * customPseudoManager::expandPseudoInstruction fills it.
     */
    std::vector<uint64_t>
    breakpointsForExpandedView(const std::vector<int> &expandedToOriginalLine) const;

    /// 1-based original source line for a 1-based expanded line. Returns the
    /// input unchanged when no mapping was recorded (lines are then 1:1).
    int mappedOriginalLine(int expandedLine) const;
    /// Moves the Input pane's cursor to `diagnostic`, selecting exactly what
    /// the squiggle underlines, and switches away from the Expanded view.
    /// Returns false, changing nothing, when the diagnostic has no usable line.
    bool goToDiagnostic(const Diagnostic &diagnostic);
    /// Lines in the user's source, for the Problems dock's bounds check.
    int sourceLineCount() const;
    std::string getRawText();
    void setRawText(const QString &text);
    void resetErrorLines();
    void highlightLines(const std::vector<std::pair<int,std::string>> &editorLines, 
      const std::vector<std::pair<int,std::string>> &disassemblyLines);
    void setCanWrite(bool canWrite);
    void setExpandedLocked(bool locked);
    std::vector<uint64_t> getBreakpoints() const;
    void clearHighlights();
    // Flags a runtime/debugger fault at `line` (1-based), visually distinct from
    // compile-time squiggles - a full-line tint (like the pipeline-stage highlight)
    // rather than a wavy underline, since this is "execution died here" not a
    // syntax error. No-op if line <= 0 (PC couldn't be resolved to a source line).
    void showRuntimeError(int line, const QString &message);

    /// True while the Expanded pane is the one on screen. Lets a second
    /// control that offers the same choice - the ribbon's View group - show
    /// which one is current without keeping a copy of the state.
    bool isExpandedView() const;
    /// True while a run has pinned the Expanded pane. A second control must
    /// honour this too, or it would be a way around the lock.
    bool isExpandedLocked() const;

public slots:
    void onExpandButtonClicked(bool checked);
    void switchToExpandedView();
    void switchToEditorView();

private slots:
    void requestLiveDiagnostics();
    void applyDiagnostics(const QVector<Diagnostic> &diagnostics);

private:
    /// Where a diagnostic should land in the Input pane: the mapped line, and
    /// the span to select there (0, 0 meaning the whole line).
    struct InputTarget
    {
        int line = 0;
        int startColumn = 0;
        int endColumn = 0;
    };
    InputTarget inputTargetFor(const Diagnostic &diagnostic) const;

    std::vector<int> m_expandedToOriginalLine;
    Editor *m_editor = nullptr;
    DisassemblyEditor *m_disassemblyView = nullptr;
    Editor *m_expandedView = nullptr;
    // m_disassemblyView is an object of the same class as m_editor
    // but well make it read only and use it to show disassembled code
    // were doing this cause of the overriden paint event in Editor class to show hightlights
    QTextCharFormat m_squiggleFormat; // stores how the quiggles will look
    ProcessorManager *m_processorManager = nullptr;
    QTextCursor m_userCursorPosition;
    // to save user cursor position when updating disassembly view
    bool m_expandedLocked = false;
    // true while squiggles produced by a failed Run are on screen. Live
    // diagnostics are suppressed until the next Run calls resetErrorLines(),
    // otherwise the first keystroke would clear the build errors.
    bool m_buildDiagnosticsActive = false;
    LanguageService *m_languageService = nullptr;
    QTimer *m_diagnosticsDebounceTimer = nullptr;
    // public slots:
    Ui::EditorTab *ui;
signals:
    void loadProgramSignal(std::string& sourceText);
    /// The visible pane changed, however it was changed. The in-tab buttons
    /// remain the state; this only reports it, so another view of the same
    /// choice can follow along instead of holding a second copy.
    void viewChangedSignal(bool expanded);
    /// setExpandedLocked() ran. Emitted so another view of the choice can
    /// disable itself in step with the in-tab Editor button.
    void expandedLockChangedSignal(bool locked);
    /// Diagnostics currently shown, each with originalLine resolved. Emitted
    /// empty by resetErrorLines() so the Problems dock clears with the editor.
    void diagnosticsChangedSignal(const QVector<Diagnostic> &diagnostics); 
};
} // namespace Kites
#endif // EDITORTAB__H
