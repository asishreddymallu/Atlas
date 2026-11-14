#pragma once
#include "ui/common/syntax_highlighter.h"
#include "ui/common/kites_editor.h"
#include "ui/editor_tab/editor_commands.h"
#include "ui/editor_tab/find_engine.h"
#include <QCompleter>
#include <QColor>
#include <QTimer>
#include <QEvent>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVariantMap>
#include <map>

namespace Kites
{
class FindBar;
class GotoLineBar;

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
    /**
     * @brief The find overlay, created the first time it is opened.
     *
     * Exposed so tests and the out-of-tree harness can drive it without
     * sending a key event for every assertion. Null until Ctrl+F or Ctrl+H
     * has been pressed once.
     */
    FindBar     *findBar() const;
    GotoLineBar *gotoLineBar() const;
    /// Opens the go-to-line overlay, closing the find bar if it is open.
    void openGotoLine();
    void closeGotoLine();
    /// Open, with the replace row shown or not. Safe to call when open.
    void openFind(bool withReplace);
    void closeFind();
    /// Matches of the current pattern, for the tests and the status line.
    const FindEngine::FindResult &findResult() const;
    /// Index into findResult().matches, or -1 when nothing is current.
    int currentMatchIndex() const;

    bool hasBreakpointAtLine(int line) const;
    void toggleBreakpointAtLine(int line);
    void clearBreakpoints();
    void clearHighlights();


protected:
    // void mouseMoveEvent(QMouseEvent* event) override;
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    /// Keeps the find overlay in the top-right of the viewport.
    void resizeEvent(QResizeEvent *event) override;

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
    /// Re-runs the search and repaints the highlights. `toOrigin` selects
    /// the first match at or after where the caret was when the bar opened,
    /// which is what incremental typing wants; otherwise the current match
    /// is kept where it still exists.
    void runFind(bool toOrigin);
    /**
     * @brief Re-searches, synchronously, if the document moved underneath.
     *
     * The highlights are recomputed on a 150 ms debounce, which is right
     * for repainting but wrong for acting: an edit followed immediately by
     * Replace All, Replace or Next would otherwise use offsets from before
     * the edit and change the wrong characters. Each of those three calls
     * this first. The debounce is untouched - this is a separate, immediate
     * path for the three commands that must not be late.
     */
    void refreshFindIfStale();
    /// Selects match `index` and scrolls to it. -1 clears the selection.
    void goToMatch(int index);
    void stepMatch(bool forward);
    /// Rebuilds the find highlight list and hands it to the one composed
    /// extra-selections function.
    void updateFindHighlights();
    void repositionFindBar();
    /**
     * @brief The two match colours, derived from existing theme colours.
     *
     * No new theme token. The current match is filled with the theme's
     * `info` colour and its text is set to the editor BACKGROUND - info was
     * chosen to sit at 4.5:1 or better against the background, so inverting
     * it gives that same ratio by construction. Every other match gets a
     * weak wash of the same colour over the editor background, so ordinary
     * foreground text stays as readable on it as it is on the page.
     */
    void refreshFindColors();
    void replaceCurrentMatch();
    void replaceEveryMatch();
    void repositionOverlays();

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

    FindBar               *m_findBar      {nullptr};
    FindEngine::FindResult m_findResult;
    int                    m_currentMatch {-1};
    /// Where the caret was when the bar was opened. Incremental typing
    /// searches from here, so the selection does not crawl forward as the
    /// pattern grows.
    int                    m_findOrigin   {0};
    /// Set on every document change while the bar is open; cleared by a
    /// re-search. See refreshFindIfStale().
    bool                   m_findDirty    {false};
    /// Coalesces re-searching while the document or the pattern changes.
    QTimer                *m_findDebounce {nullptr};
    QColor                 m_findMatchColor;
    QColor                 m_findCurrentColor;
    QColor                 m_findCurrentTextColor;
    GotoLineBar           *m_gotoBar {nullptr};
    // QSet<int> m_breakPointsSet; // for quick lookup
    // int m_gutterWidth = 30;
    bool m_isTextEditor; // to differentiate between text editor and disassembly viewer
    bool m_breakpointInteractionEnabled = true;
};
} // namespace Kites
