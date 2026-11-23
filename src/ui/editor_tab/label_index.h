#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace Atlas
{
/**
 * @brief Where the labels are, as pure functions.
 *
 * No widget, no cursor, no document - the same shape as editor_commands and
 * completion_model, so the rules can be table-tested without a window.
 *
 * The character set and the definition rule are taken from the assembler,
 * read but never modified. See docs for the full list; the three that drive
 * the code here are:
 *
 *   - a name is [A-Za-z0-9_]+ : the lexer's identifier scan also accepts
 *     '.', but a name containing one is explicitly rejected as a label, and
 *     '$' is not in the set at all (unlike the editor's completion word).
 *   - a definition is a name immediately followed by ':'.
 *   - an identifier in OPERAND position - straight after an opcode or a
 *     comma, on the same line - is a label reference even when its text is
 *     an instruction mnemonic. `j add` references a label called add.
 */
namespace LabelIndex
{

struct Definition
{
    QString name;
    int     line{0};   ///< 0-based
    int     column{0}; ///< 0-based, within the line
    int     offset{0}; ///< into the whole document
};

/// Same shape; kept distinct so a caller cannot mix the two up by accident.
struct Reference
{
    QString name;
    int     line{0};
    int     column{0};
    int     offset{0};
};

struct Index
{
    QList<Definition> definitions;
    QList<Reference>  references;

    /// Every definition of `name`, in document order. Empty when undefined.
    QList<Definition> definitionsOf(const QString &name) const;
    /// True when `name` is defined at least once.
    bool isDefined(const QString &name) const;
    /// Names defined more than once.
    QStringList duplicates() const;
};

/// True for the characters an assembler label name may contain.
bool isLabelChar(QChar c);

/**
 * @brief Scans `text` for definitions and references.
 *
 * Comment- and string-aware in the same way the pair rules are: nothing
 * after a '#' OR a ';' counts - the lexer treats both as starting a
 * comment - and nothing inside a "..." or '...' counts.
 *
 * Only names that are DEFINED somewhere are reported as references, so an
 * ordinary instruction operand is not mistaken for one. That also means the
 * index cannot be used to find a typo'd reference - see the docs.
 */
Index findLabels(const QString &text);

/// The label name at `offset`, or empty when the caret is not on one. The
/// whole token is returned however far into it the offset sits.
QString nameAt(const QString &text, int offset);

/**
 * @brief The labels DEFINED on one line, by the same rule findLabels() uses.
 *
 * The single definition of "this line defines a label". The editor's
 * completion scanner calls this rather than carrying a second copy of the
 * rule, so the two can never drift - a consistency test asserts they agree
 * over the corpus and the trap files.
 */
QStringList definitionsOnLine(const QString &line);

} // namespace LabelIndex
} // namespace Atlas
