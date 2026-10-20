// StatusLogTests.cpp - the Main tab status log (Windows DataArea::updateMsgText, DataArea.cpp 10301-10352).
//
// Every expectation below was worked out by hand from that function:
//   msg = "%H.%M.%S" + " " + new text + CR LF + previous log;
//   if strlen(msg) > 8191 the log is cut to 8191 bytes and then back to a CR or LF;
//   trailing CR and LF are removed; nothing happens when the new text is empty.

#include <QtTest>

#include "StatusLog.h"

using rfhqt::StatusLog;

namespace
{
    // 78 characters: "m" + three digits + 74 'x'. With the 9 character stamp that is 87 characters, plus CR LF = 89.
    QString entryText(int n)
    {
        return QString("m%1").arg(n, 3, 10, QChar('0')) + QString(74, QChar('x'));
    }
}

class StatusLogTests : public QObject
{
    Q_OBJECT

private slots:
    void stampIsHourMinuteSecondWithDotsAndOneBlank()
    {
        StatusLog log;
        log.add("Connected to QM1", QTime(12, 3, 9));
        QCOMPARE(log.text(), QString("12.03.09 Connected to QM1"));
    }

    void stampUsesTwentyFourHourClockWithLeadingZeros()
    {
        StatusLog log;
        log.add("x", QTime(0, 0, 0));
        QCOMPARE(log.text(), QString("00.00.00 x"));
        log.clear();
        log.add("x", QTime(23, 59, 58));
        QCOMPARE(log.text(), QString("23.59.58 x"));
    }

    void theClockIsTheDefaultStampSource()
    {
        StatusLog log;
        log.add("now");
        QVERIFY(QRegularExpression("^\\d\\d\\.\\d\\d\\.\\d\\d now$").match(log.text()).hasMatch());
    }

    void newestEntryComesFirstJoinedByCrLf()
    {
        StatusLog log;
        log.add("one", QTime(10, 0, 0));
        log.add("two", QTime(10, 0, 1));
        QCOMPARE(log.text(), QString("10.00.01 two\r\n10.00.00 one"));
    }

    void noTrailingCrLfIsKept()
    {
        StatusLog log;
        log.add("one", QTime(10, 0, 0));
        QVERIFY(!log.text().endsWith('\n'));
        QVERIFY(!log.text().endsWith('\r'));
    }

    void severalMessagesOfOneOperationAreJoinedWithCrLfAndStampedOnce()
    {
        StatusLog log;
        log.add(QStringList{ "first", "second", "third" }, QTime(9, 8, 7));
        QCOMPARE(log.text(), QString("09.08.07 first\r\nsecond\r\nthird"));
        QCOMPARE(StatusLog::join(QStringList{ "a", "b" }), QString("a\r\nb"));
        QCOMPARE(StatusLog::join(QStringList{ "only" }), QString("only"));
        QCOMPARE(StatusLog::join(QStringList()), QString());
    }

    void trailingCrLfOfTheNewTextIsRemovedLikeWindowsDoesAtTheEnd()
    {
        // Windows appends CR LF after the new text and strips only the end of the whole log, so a CR LF that
        // the caller put at the end of the text stays inside the log when older entries follow it.
        StatusLog log;
        log.add("old", QTime(1, 1, 1));
        log.add("new\r\n", QTime(1, 1, 2));
        QCOMPARE(log.text(), QString("01.01.02 new\r\n\r\n01.01.01 old"));
    }

    void emptyTextChangesNothing()
    {
        StatusLog log;
        log.add("one", QTime(10, 0, 0));
        QSignalSpy changed(&log, &StatusLog::changed);
        log.add("", QTime(10, 0, 1));
        log.add(QStringList(), QTime(10, 0, 2));
        QCOMPARE(log.text(), QString("10.00.00 one"));
        QCOMPARE(changed.count(), 0);
    }

    void aLogOf92EntriesIsKeptWhole()
    {
        // 92 entries of 87 characters plus 91 separators: 8186 characters, below the 8191 limit.
        StatusLog log;
        for (int i = 0; i < 92; i++)
        {
            log.add(entryText(i), QTime(0, 0, 0));
        }
        QCOMPARE(log.text().size(), 8186);
        QCOMPARE(log.text().count("\r\n"), 91);
        QVERIFY(log.text().startsWith("00.00.00 m091"));
        QVERIFY(log.text().endsWith(QString(74, QChar('x'))));
        QVERIFY(log.text().contains("00.00.00 m000"));
    }

    void the93rdEntryDropsTheOldestAndCutsAtALineBoundary()
    {
        // Before the cut the log is 89 * 93 - 2 = 8275 characters. The first 8191 characters end inside the
        // oldest entry (m000), so the cut goes back to the line end before it: 8188, then the CR LF is removed.
        StatusLog log;
        for (int i = 0; i < 93; i++)
        {
            log.add(entryText(i), QTime(0, 0, 0));
        }
        QCOMPARE(log.text().size(), 8186);
        QCOMPARE(log.text().count("\r\n"), 91);
        QVERIFY(log.text().startsWith("00.00.00 m092"));
        QVERIFY(log.text().contains("00.00.00 m001"));
        QVERIFY(!log.text().contains("m000"));
        QVERIFY(log.text().endsWith(QString(74, QChar('x'))));
    }

    void theLogNeverExceeds8191CharactersWhateverIsAdded()
    {
        StatusLog log;
        for (int i = 0; i < 1000; i++)
        {
            log.add(entryText(i % 1000), QTime(0, 0, 0));
            QVERIFY(log.text().size() <= StatusLog::MaxChars);
        }
        QCOMPARE(StatusLog::MaxChars, 8191);
    }

    void aSingleEntryUpToTheLimitIsKept()
    {
        // stamp 9 + text 8180 + CR LF 2 = 8191, not above the limit: kept, then the CR LF is removed (8189).
        StatusLog log;
        log.add(QString(8180, QChar('a')), QTime(0, 0, 0));
        QCOMPARE(log.text().size(), 8189);
    }

    void aSingleEntryOneOverTheLimitLosesOnlyItsLineFeed()
    {
        // 9 + 8181 + 2 = 8192 > 8191: the cut at 8191 characters ends exactly on the CR, a line end, so it
        // stays and is then removed as a trailing CR: 9 + 8181 = 8190 characters remain.
        StatusLog log;
        log.add(QString(8181, QChar('a')), QTime(0, 0, 0));
        QCOMPARE(log.text().size(), 8190);
    }

    void anEntryWithNoLineEndInsideTheLimitIsTruncatedAtMaxChars()
    {
        // 9 + 8182 = 8191 characters come before the CR, so the first MaxChars contain no CR or LF.
        // The Linux port keeps the entry truncated at MaxChars (unlike Windows DataArea.cpp 10328-10332,
        // which runs the length down to 0 and wipes the log).
        StatusLog log;
        log.add("kept", QTime(0, 0, 0));
        log.add(QString(8182, QChar('a')), QTime(0, 0, 1));
        QCOMPARE(log.text().size(), StatusLog::MaxChars);
        QVERIFY(log.text().startsWith(QStringLiteral("00.00.01 ")));
    }

    void aLongEntryWithLineBreaksIsCutAtTheLastLineBreakInsideTheLimit()
    {
        // text = 4000 'a', CR LF, 4300 'b': msg = 9 + 4000 + 2 + 4300 + 2 = 8313. The first 8191 bytes end
        // in the b run; going back, the last CR or LF is the LF at index 4010 (stamp 0-8, a 9-4008, CR 4009,
        // LF 4010). The cut keeps 4011 characters, then removes the CR LF: 9 + 4000 = 4009.
        StatusLog log;
        log.add(QString(4000, QChar('a')) + "\r\n" + QString(4300, QChar('b')), QTime(0, 0, 0));
        QCOMPARE(log.text().size(), 4009);
        QVERIFY(log.text().endsWith('a'));
    }

    void clearEmptiesTheLogAndItCanBeUsedAgain()
    {
        StatusLog log;
        log.add("one", QTime(10, 0, 0));
        QSignalSpy changed(&log, &StatusLog::changed);
        log.clear();
        QCOMPARE(log.text(), QString());
        QCOMPARE(changed.count(), 1);
        log.add("two", QTime(10, 0, 1));
        QCOMPARE(log.text(), QString("10.00.01 two"));
        QCOMPARE(changed.count(), 2);
    }

    void clearingAnEmptyLogSendsNoSignal()
    {
        StatusLog log;
        QSignalSpy changed(&log, &StatusLog::changed);
        log.clear();
        QCOMPARE(changed.count(), 0);
    }

    void textForTheClipboardUsesLineFeedsOnly()
    {
        StatusLog log;
        log.add("one", QTime(10, 0, 0));
        log.add(QStringList{ "a", "b" }, QTime(10, 0, 1));
        QCOMPARE(log.textForClipboard(), QString("10.00.01 a\nb\n10.00.00 one"));
        // the log itself is unchanged
        QCOMPARE(log.text(), QString("10.00.01 a\r\nb\r\n10.00.00 one"));
    }

    void addSendsOneChangedSignal()
    {
        StatusLog log;
        QSignalSpy changed(&log, &StatusLog::changed);
        log.add("one", QTime(10, 0, 0));
        QCOMPARE(changed.count(), 1);
    }
};

QTEST_MAIN(StatusLogTests)
#include "StatusLogTests.moc"
