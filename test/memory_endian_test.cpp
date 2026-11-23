/**
 * @file memory_endian_test.cpp
 * @brief The Memory tab's byte-order control, which is a display choice only.
 *
 * Memory holds bytes. The Double Word column is the one place the view
 * has to decide what order to read eight of them in, and until now it
 * only ever read them little-endian, because that is how
 * `MainMemory::readGeneric` assembles them (`main_memory.cpp:79`) and
 * `MemoryModel` showed the result unchanged (`memory_model.cpp:218`).
 *
 * The toggle added here changes **that column's rendering and nothing
 * else**. The assertions below are mostly about what must NOT move: the
 * bytes in memory, the Byte 0-7 cells, the cache's hit and miss counts,
 * and the setting itself across every refresh the model can be put
 * through. A `MemoryController` is default-constructible and touches no
 * files (`memory_controller.cpp:5-11`), so none of this needs a
 * processor or a vm_state directory.
 *
 * On the expected strings: `QString::number` emits no leading zeros, so
 * the eight bytes 01..08 read back as `0x807060504030201`, not
 * `0x0807060504030201`. The hex and decimal spellings below are written
 * out literally, as an oracle independent of the code under test. The
 * binary spelling is 60 characters of 1s and 0s; that one is checked by
 * parsing the cell back to a number, because a hand-copied bit string
 * would be likelier to be wrong than the code it is checking.
 */
#include "processor/memory_controller.h"
#include "ui/common/display_base_types.h"
#include "ui/memory_tab/memory_model.h"

#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include <cstdint>
#include <string>
#include <vector>

using namespace Atlas;

namespace
{
/// Aligned, and clear of the 64-byte default window so the central
/// address has to be moved on purpose.
constexpr uint64_t kAddress = 256;

/// The eight bytes, lowest address first.
constexpr uint64_t kLittle = 0x0807060504030201ULL;
constexpr uint64_t kBig    = 0x0102030405060708ULL;

struct Fixture
{
    MemoryController controller;
    MemoryModel      model{nullptr, &controller};

    Fixture()
    {
        fill(controller);
        model.setCentralAddress(kAddress);
    }

    /// 01 02 03 .. 08 at kAddress, written past the cache so that the
    /// cache counters start at zero and stay comparable.
    static void fill(MemoryController &target)
    {
        for (uint8_t i = 0; i < 8; ++i)
            target.writeByte_d(kAddress + i, static_cast<uint8_t>(i + 1));
    }

    /// The row whose Address cell is `kAddress`. Found rather than
    /// computed, so the test does not depend on how many rows the model
    /// happens to be showing.
    int row() const
    {
        const QString want = QStringLiteral("0x%1").arg(QString::number(kAddress, 16).toUpper());
        for (int r = 0; r < model.rowCount(); ++r)
            if (model.index(r, 0).data().toString() == want)
                return r;
        return -1;
    }

    QString cell(int column) const
    {
        return model.index(row(), column).data().toString();
    }

    QString doubleWord() const
    {
        return cell(1);
    }

    /// The eight single-byte columns, in column order.
    QStringList bytes() const
    {
        QStringList out;
        for (int column = 2; column < 10; ++column)
            out << cell(column);
        return out;
    }
};

/// The numeric value a cell is showing, whatever base it is in.
uint64_t valueOf(const QString &text, Base base)
{
    QString digits = text;
    if (base == Base::Hexadecimal)
        digits.remove(0, 2);
    else if (base == Base::Binary)
        digits.remove(0, 2);
    bool ok = false;
    const uint64_t value = digits.toULongLong(&ok, static_cast<int>(base));
    EXPECT_TRUE(ok) << "could not read '" << text.toStdString() << "' as base "
                    << static_cast<int>(base);
    return value;
}
} // namespace

TEST(MemoryByteOrder, LittleIsTheDefaultAndIsWhatTheMachineReads)
{
    Fixture fixture;
    ASSERT_NE(fixture.row(), -1) << "the model is not showing address " << kAddress;

    EXPECT_EQ(fixture.model.byteOrder(), ByteOrder::Little);
    EXPECT_EQ(fixture.doubleWord().toStdString(), "0x807060504030201");
    EXPECT_EQ(valueOf(fixture.doubleWord(), Base::Hexadecimal), kLittle);
}

TEST(MemoryByteOrder, BigReversesTheDoubleWordInEveryBase)
{
    Fixture fixture;
    ASSERT_NE(fixture.row(), -1);

    // Hexadecimal, written out rather than computed.
    fixture.model.setDisplayBase(Base::Hexadecimal);
    fixture.model.setByteOrder(ByteOrder::Little);
    EXPECT_EQ(fixture.doubleWord().toStdString(), "0x807060504030201");
    fixture.model.setByteOrder(ByteOrder::Big);
    EXPECT_EQ(fixture.doubleWord().toStdString(), "0x102030405060708");

    // Decimal: 0x0807060504030201 and 0x0102030405060708.
    fixture.model.setDisplayBase(Base::Decimal);
    fixture.model.setByteOrder(ByteOrder::Little);
    EXPECT_EQ(fixture.doubleWord().toStdString(), "578437695752307201");
    fixture.model.setByteOrder(ByteOrder::Big);
    EXPECT_EQ(fixture.doubleWord().toStdString(), "72623859790382856");

    // Binary, by value - see the note at the top of this file.
    fixture.model.setDisplayBase(Base::Binary);
    fixture.model.setByteOrder(ByteOrder::Little);
    EXPECT_TRUE(fixture.doubleWord().startsWith(QStringLiteral("0b")))
        << fixture.doubleWord().toStdString();
    EXPECT_EQ(valueOf(fixture.doubleWord(), Base::Binary), kLittle);
    fixture.model.setByteOrder(ByteOrder::Big);
    EXPECT_TRUE(fixture.doubleWord().startsWith(QStringLiteral("0b")));
    EXPECT_EQ(valueOf(fixture.doubleWord(), Base::Binary), kBig);
}

TEST(MemoryByteOrder, TheByteColumnsStayInAddressOrder)
{
    Fixture fixture;
    ASSERT_NE(fixture.row(), -1);

    for (Base base : {Base::Hexadecimal, Base::Decimal, Base::Binary})
    {
        fixture.model.setDisplayBase(base);

        fixture.model.setByteOrder(ByteOrder::Little);
        const QStringList little = fixture.bytes();
        fixture.model.setByteOrder(ByteOrder::Big);
        const QStringList big = fixture.bytes();

        ASSERT_EQ(little.size(), 8);
        EXPECT_EQ(big, little) << "the byte columns moved in base " << static_cast<int>(base);
    }

    // And they really are the bytes as written, lowest address first.
    fixture.model.setDisplayBase(Base::Decimal);
    const QStringList bytes = fixture.bytes();
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(bytes.at(i).toStdString(), std::to_string(i + 1))
            << "byte column " << i;
}

TEST(MemoryByteOrder, TogglingTwiceReturnsTheOriginalRow)
{
    Fixture fixture;
    ASSERT_NE(fixture.row(), -1);

    for (Base base : {Base::Hexadecimal, Base::Decimal, Base::Binary})
    {
        fixture.model.setDisplayBase(base);
        const QString     word  = fixture.doubleWord();
        const QStringList bytes = fixture.bytes();

        fixture.model.setByteOrder(ByteOrder::Big);
        fixture.model.setByteOrder(ByteOrder::Little);

        EXPECT_EQ(fixture.model.byteOrder(), ByteOrder::Little);
        EXPECT_EQ(fixture.doubleWord(), word) << "base " << static_cast<int>(base);
        EXPECT_EQ(fixture.bytes(), bytes);
    }
}

TEST(MemoryByteOrder, TheColumnHeaderSaysWhichOrderIsOnScreen)
{
    Fixture fixture;
    auto header = [&fixture] {
        return fixture.model.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString().toStdString();
    };

    EXPECT_EQ(header(), "Double Word (LE)");
    fixture.model.setByteOrder(ByteOrder::Big);
    EXPECT_EQ(header(), "Double Word (BE)");
    fixture.model.setByteOrder(ByteOrder::Little);
    EXPECT_EQ(header(), "Double Word (LE)");

    // The other headers are untouched.
    EXPECT_EQ(fixture.model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString().toStdString(),
              "Address");
    EXPECT_EQ(fixture.model.headerData(2, Qt::Horizontal, Qt::DisplayRole).toString().toStdString(),
              "Byte 0");
}

TEST(MemoryByteOrder, TheSettingSurvivesEveryRefreshTheModelCanBePutThrough)
{
    Fixture fixture;
    ASSERT_NE(fixture.row(), -1);
    fixture.model.setByteOrder(ByteOrder::Big);
    const QString big = fixture.doubleWord();
    ASSERT_EQ(big.toStdString(), "0x102030405060708");

    // A memory reset, i.e. what the controller's memoryResetSignal does.
    fixture.model.memoryResetSlot();
    EXPECT_EQ(fixture.model.byteOrder(), ByteOrder::Big) << "after a model reset";
    EXPECT_EQ(fixture.doubleWord(), big);

    // A step: the controller reports one address changed.
    fixture.model.updateMemory(kAddress);
    EXPECT_EQ(fixture.model.byteOrder(), ByteOrder::Big) << "after a memory update";
    EXPECT_EQ(fixture.doubleWord(), big);

    // A scroll, there and back.
    fixture.model.offsetCentralAddress(1);
    fixture.model.offsetCentralAddress(-1);
    EXPECT_EQ(fixture.model.byteOrder(), ByteOrder::Big) << "after a scroll";
    EXPECT_EQ(fixture.doubleWord(), big);

    // A resize of the view.
    fixture.model.setRowsVisible(14);
    EXPECT_EQ(fixture.model.byteOrder(), ByteOrder::Big) << "after a resize";
    ASSERT_NE(fixture.row(), -1);
    EXPECT_EQ(fixture.doubleWord(), big);
}

TEST(MemoryByteOrder, TheSettingSurvivesAProcessorChange)
{
    // A processor change destroys the processor and hands the tab the new
    // one's controller (main_window.cpp:952 -> memory_model.cpp:66). The
    // second controller gets the same bytes, so the rendering is
    // comparable across the swap.
    Fixture fixture;
    fixture.model.setByteOrder(ByteOrder::Big);
    fixture.model.setDisplayBase(Base::Hexadecimal);
    ASSERT_NE(fixture.row(), -1);
    const QString before = fixture.doubleWord();

    MemoryController replacement;
    Fixture::fill(replacement);
    fixture.model.changeMemoryController(&replacement);

    EXPECT_EQ(fixture.model.byteOrder(), ByteOrder::Big);
    ASSERT_NE(fixture.row(), -1) << "the model lost its place across the swap";
    EXPECT_EQ(fixture.doubleWord(), before);
    EXPECT_EQ(fixture.doubleWord().toStdString(), "0x102030405060708");
}

TEST(MemoryByteOrder, TogglingTouchesNeitherMemoryNorTheCaches)
{
    Fixture fixture;
    ASSERT_NE(fixture.row(), -1);

    struct Counts
    {
        size_t l1Hit, l1Miss, l2Hit, l2Miss, iHit, iMiss;
        bool   operator==(const Counts &) const = default;
    };
    auto counts = [&fixture] {
        MemoryController &c = fixture.controller;
        return Counts{c.getL1Cache()->getHitCount(),          c.getL1Cache()->getMissCount(),
                      c.getL2Cache()->getHitCount(),          c.getL2Cache()->getMissCount(),
                      c.getInstructionCache()->getHitCount(), c.getInstructionCache()->getMissCount()};
    };
    auto memory = [&fixture] {
        std::vector<uint8_t> out;
        for (uint64_t i = 0; i < 8; ++i)
            out.push_back(fixture.controller.peekByte(kAddress + i));
        return out;
    };

    const std::vector<uint8_t> bytesBefore = memory();
    const Counts               before      = counts();

    for (int i = 0; i < 3; ++i)
    {
        fixture.model.setByteOrder(ByteOrder::Big);
        (void)fixture.doubleWord();
        (void)fixture.bytes();
        fixture.model.setByteOrder(ByteOrder::Little);
        (void)fixture.doubleWord();
        (void)fixture.bytes();
    }

    EXPECT_EQ(memory(), bytesBefore) << "the bytes in memory changed";
    EXPECT_TRUE(counts() == before)
        << "a cache counter moved: L1 " << before.l1Hit << "/" << before.l1Miss << " -> "
        << counts().l1Hit << "/" << counts().l1Miss;

    // The bytes are still 01..08, i.e. nothing was swapped in place.
    for (uint8_t i = 0; i < 8; ++i)
        EXPECT_EQ(fixture.controller.peekByte(kAddress + i), i + 1u) << "byte " << int(i);
}
