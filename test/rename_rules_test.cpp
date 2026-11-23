/**
 * @file rename_rules_test.cpp
 * @brief What a label may be renamed to, as a table.
 *
 * GUARDs on the refusal reasons the rename overlay shows. The rules come
 * from the assembler, read and not modified.
 */
#include "ui/editor_tab/label_index.h"
#include "ui/editor_tab/label_overlays.h"

#include <gtest/gtest.h>

using namespace Atlas;

namespace
{
LabelIndex::Index indexOf(const QString &text) { return LabelIndex::findLabels(text); }
const QString kDoc = "main:\n    nop\nloop:\n    j loop\n";
} // namespace

TEST(RenameRules, AnOrdinaryNameIsAccepted)
{
    const RenameCheck check = checkRename("loop", "again", indexOf(kDoc));
    EXPECT_TRUE(check.ok) << check.reason.toStdString();
    EXPECT_TRUE(check.reason.isEmpty());
}

TEST(RenameRules, AnEmptyNameIsRefused)
{
    const RenameCheck check = checkRename("loop", "", indexOf(kDoc));
    EXPECT_FALSE(check.ok);
    EXPECT_FALSE(check.reason.isEmpty()) << "refused without saying why";
}

TEST(RenameRules, AnIllegalCharacterIsRefusedAndNamed)
{
    for (const QString &bad : {QStringLiteral("has space"), QStringLiteral("has.dot"),
                               QStringLiteral("has$dollar"), QStringLiteral("has-dash")})
    {
        const RenameCheck check = checkRename("loop", bad, indexOf(kDoc));
        EXPECT_FALSE(check.ok) << bad.toStdString() << " was accepted";
        EXPECT_FALSE(check.reason.isEmpty());
    }
}

TEST(RenameRules, ALeadingDigitIsRefused)
{
    // The lexer sends a leading digit to number(), so such a name could
    // never be read back as a label.
    const RenameCheck check = checkRename("loop", "1start", indexOf(kDoc));
    EXPECT_FALSE(check.ok);
    EXPECT_TRUE(check.reason.contains("digit")) << check.reason.toStdString();
}

TEST(RenameRules, AnAlreadyDefinedNameIsRefusedWithItsLine)
{
    const RenameCheck check = checkRename("loop", "main", indexOf(kDoc));
    EXPECT_FALSE(check.ok);
    EXPECT_TRUE(check.reason.contains("1")) << "it did not say which line: "
                                            << check.reason.toStdString();
}

TEST(RenameRules, ARegisterNameIsRefused)
{
    // A register-named label can be DEFINED but never REFERENCED: the lexer
    // resolves a register before it considers a label reference, so the
    // rename would silently break every use.
    for (const QString &reg : {QStringLiteral("sp"), QStringLiteral("x10"),
                               QStringLiteral("ra"), QStringLiteral("t0")})
    {
        const RenameCheck check = checkRename("loop", reg, indexOf(kDoc));
        EXPECT_FALSE(check.ok) << reg.toStdString() << " was accepted as a label name";
        EXPECT_TRUE(check.reason.contains("register")) << check.reason.toStdString();
    }
}

TEST(RenameRules, RenamingToTheSameNameIsAccepted)
{
    // The caller treats this as a no-op; the rules do not refuse it.
    const RenameCheck check = checkRename("loop", "loop", indexOf(kDoc));
    EXPECT_TRUE(check.ok);
}

TEST(RenameRules, AMnemonicNameIsAllowed)
{
    // Deliberately NOT refused: the assembler reads an identifier in
    // operand position as a label reference even when it is a mnemonic,
    // so `add:` ... `j add` works. Refusing it would be stricter than the
    // language.
    const RenameCheck check = checkRename("loop", "add", indexOf(kDoc));
    EXPECT_TRUE(check.ok) << check.reason.toStdString();
}
