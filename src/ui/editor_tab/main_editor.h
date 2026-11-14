#pragma once
#include "ui/common/syntax_highlighter.h"
#include "ui/common/kites_editor.h"
#include "ui/editor_tab/editor_commands.h"
#include <QCompleter>
#include <QEvent>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVariantMap>
#include <map>

namespace Kites
{
class Editor : public KitesEditor
{
    friend class BreakPointGutterColumn;
public:
    Editor(QWidget *parent = nullptr);
    void setErrorMessage(int line, const QString &message);
    void resetErrors();
    void setLinesToHighlight(const std::vector<std::pair<int,std::string>> &linesToHighlight);

    // these fuctions are for the line number
    // and breakpoint area
    // int breakpointAreaWidth() const;
    // void breakpointAreaPaintEvent(QPaintEvent *event);
    // void breakpointAreaMousePressEvent(QMouseEvent *event);

    /**
     * @brief The breakpoints, as 1-based source lines.
     *
     * Unchanged in type and meaning: this is what MainWindow hands to
     * ProcessorManager at Run and Debug Run, and what the Expanded pane is
     * given to display. What changed is that the numbers now follow their
     * text through an edit instead of staying put while the text moves.
     */
    std::vector<uint64_t> getBreakpoints() const;
    void setBreakpoints(const std::vector<uint64_t> &breakpoints);
    void setBreakpointInteractionEnabled(bool enabled);

    /// The gutter's three operations. They go through here rather than at
    /// the vector directly, so the anchors that do the following stay in
    /// step and the invariants are enforced in one place.
    bool hasBreakpointAtLine(int line) const;
    void toggleBreakpointAtLine(int line);
    void clearBreakpoints();
    void clearHighlights();


protected:
    // void mouseMoveEvent(QMouseEvent* event) override;
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    // void resizeEvent(QResizeEvent *event) override;

private slots:
    void insertCompletion(const QString &completion);

private:
    // int leftViewMargin() const override;
    QString textUnderCursor() const;

    /**
     * @brief The VS Code-style editing keys, handled here and nowhere else.
     *
     * Returns true when the key was one of them and has been dealt with.
     *
     * Deliberately NOT QActions or QShortcuts. The window owns exactly seven
     * key sequences and a test asserts that; a QShortcut would be invisible
     * to that test while still being able to swallow a key, and a QAction
     * would make it eight. Handling the keys in the widget keeps the
     * application-level set exactly where it was and keeps the keys confined
     * to the editor that has focus.
     *
     * No ShortcutOverride handling is needed with it: none of the seven
     * window shortcuts - F5, F6, F9, F10, Ctrl+O, Ctrl+S, Ctrl+Shift+M -
     * collides with any key below, so none of them is swallowed on the way.
     */
    bool handleEditingKey(QKeyEvent *event);

    /**
     * @brief Applies one command result as a SINGLE undo step.
     *
     * Wrapped in beginEditBlock/endEditBlock, and it replaces only the range
     * the command named, so one Ctrl+Z puts the document back exactly and a
     * command near the end of a long file does not rewrite the whole thing.
     */
    void applyEdit(const EditorCommands::Edit &edit);

    /// Puts whole-line text on the clipboard, tagged so paste knows.
    void putLineOnClipboard(const QString &lineText);

    /**
     * @brief How a breakpoint follows its line.
     *
     * Each breakpoint carries a QTextCursor SELECTING its whole line and the
     * newline after it. Qt maintains the selection through every ordinary
     * edit, so the line number is read back from the anchor, and the
     * selection going empty is the signal that the line itself was deleted -
     * which a bare cursor cannot tell from a line that merely moved.
     *
     * The editor's own line commands are the exception: they apply as one
     * insertText over a range, inside which anchors collapse, so applyEdit()
     * remaps through the command's LineMap instead and re-anchors after.
     *
     * A line that does not exist (possible only through setBreakpoints or a
     * document swap) gets a null anchor and keeps its number.
     */
    QTextCursor anchorForLine(int line) const;
    void        reanchorBreakpoints();
    void        refreshBreakpointsFromAnchors();
    /// Sorts, removes duplicates, and drops anything outside the document.
    void        normaliseBreakpoints();

    QCompleter *m_autoCompleter = nullptr;

    std::map<int, QString> m_errorMessages;
    SyntaxHighlighter *m_syntaxHighlighter;
    std::vector<std::pair<int, std::string>> m_LinesToHighlight;
    std::vector<uint64_t> m_breakPoints; // to pass to the vm
    // One per entry of m_breakPoints, in the same order. Null for a line the
    // document does not have.
    QList<QTextCursor> m_breakpointAnchors;
    // Lines can only be created or destroyed when this changes, which is how
    // an anchor whose selection went empty is told apart from a line whose
    // text merely got deleted.
    int  m_lastBlockCount {1};
    // Set while applyEdit() is making its own change, because that one is
    // remapped through the command's LineMap rather than read from anchors.
    bool m_remappingBreakpoints {false};
    // QSet<int> m_breakPointsSet; // for quick lookup
    // int m_gutterWidth = 30;
    bool m_isTextEditor; // to differentiate between text editor and disassembly viewer
    bool m_breakpointInteractionEnabled = true;
};
} // namespace Kites
