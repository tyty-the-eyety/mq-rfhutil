// AppDocumentTests.cpp - the document state shared by the tabs, and the enable rules.
//
// The enable table below was built by hand from the Windows source, one row per state the Windows code
// distinguishes. Sources (RFHUtil/):
//   GEN   General::UpdatePageData            General.cpp:504-620   (Main tab buttons and option controls)
//   CLOSE General.cpp:615, 1690              (Close Q follows isConnectionActive(), not the open queue)
//   MENU  CRfhutilView::OnUpdate*            rfhutilView.cpp:1059-1210 (print, edit), 1634-1710 (file),
//                                            1737-1900 (search), 2296-2450 (MQ menu), 3216-3260 (top, bottom)
//   DATA  MSGDATA::updateMsgdata             MSGDATA.cpp:388-410   (Browse Next / Prev on the Data tab)
//   MQMD  MQMDPAGE::OnSetActive              MQMDPAGE.cpp:410-446  (read-only fields)
//   TABS  CRfhutilView::OnSelchangeSampletab rfhutilView.cpp:780-808 (a Find dialog blocks a tab change)
// Rules the Qt program adds are marked NEW: busy (the SessionController job runs), cancel, and the Phase 1
// tabs. Browse Prev follows the core's browse position: Windows' browsePrevActive is off exactly when the
// first message is shown (DataArea.cpp:9172-9184).

#include <QtTest>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "AppDocument.h"

using namespace rfhqt;
using rfh::format::View;

namespace
{
    using Names = std::set<std::string>;

    Names join(std::initializer_list<Names> parts)
    {
        Names all;
        for (const Names & p : parts)
        {
            all.insert(p.begin(), p.end());
        }
        return all;
    }

    // Enabled in every state of the Main, Data and MQMD tabs (Phase 1) unless a row says otherwise.
    //   saveFile, setConnId, exit: never changed (GEN)    menuSave: always, except with a Find dialog (MENU 1634)
    //   menuCopy, menuSelectAll: Enable(TRUE) (MENU 1144, 1151)
    //   mqmdMsgIdEditable: SetReadOnly(FALSE) unconditionally (MQMD 424)
    //   selectorEditable: the default propertiesSupported = true
    //   tabs: Main, Data, MQMD (NEW, Phase 1)
    const Names ALWAYS = {
        "saveFile", "setConnId", "exit", "menuSave", "menuCopy", "menuSelectAll", "mqmdMsgIdEditable", "selectorEditable",
        "tab:Main", "tab:Data", "tab:Mqmd"
    };

    // Main tab, browse not active, no job running (GEN 586-612): the buttons that start a job or replace data,
    // and the Put/Get option controls.
    const Names IDLE_BUTTONS = {
        "openFile", "clearData", "clearAll", "readQ", "purgeQ", "writeQ", "browseQ", "startBrowse", "loadNames",
        "getOptions", "propsRadios"
    };
    const Names MENU_OPEN = { "menuOpen" };		// OnUpdateOpen: not while browsing, not with a Find dialog (MENU 1670)

    // Per tab: Cut is off only on the Data tab (MENU 1110); Top and Bottom are on only there (MENU 3216, 3249).
    const Names ON_MAIN = { "menuCut" };
    const Names ON_MQMD = { "menuCut" };
    const Names ON_DATA = { "menuTop", "menuBottom" };

    struct Row
    {
        std::string name;
        EnableInputs in;
        Names enabled;
    };

    EnableInputs state(Tab tab = Tab::Main)
    {
        EnableInputs in;
        in.tab = tab;
        return in;
    }

    EnableInputs withBrowse(EnableInputs in, bool active, long position)
    {
        in.browse.active = active;
        in.browse.position = position;
        return in;
    }

    EnableInputs withConn(EnableInputs in, bool connected, bool queueOpen)
    {
        in.connected = connected;
        in.queueOpen = queueOpen;
        return in;
    }

    EnableInputs withData(EnableInputs in, View view, bool present = true)
    {
        in.view = view;
        in.dataPresent = present;
        return in;
    }

    std::vector<Row> buildTable()
    {
        std::vector<Row> t;

        // ---- connection and queue, Main tab -------------------------------------------------------------
        t.push_back({ "idle: nothing connected",
            state(),
            // MQConn on: Main tab, not connected, not browsing (MENU 2296). Close Q off: not connected (CLOSE).
            join({ ALWAYS, ON_MAIN, IDLE_BUTTONS, MENU_OPEN, { "menuMqConn" } }) });
        t.push_back({ "connected, no queue open",
            withConn(state(), true, false),
            // Close Q follows the connection (CLOSE). MQDisc, and the three MQOpen items (MENU 2315, 2335-2415).
            join({ ALWAYS, ON_MAIN, IDLE_BUTTONS, MENU_OPEN,
                   { "closeQ", "menuMqDisc", "menuMqOpenGet", "menuMqOpenPut", "menuMqOpenBrowse" } }) });
        t.push_back({ "connected, queue open",
            withConn(state(), true, true),
            // the MQOpen items are off while a queue is open; MQClose on (MENU 2416)
            join({ ALWAYS, ON_MAIN, IDLE_BUTTONS, MENU_OPEN, { "closeQ", "menuMqDisc", "menuMqClose" } }) });
        t.push_back({ "queue open but not connected (cannot happen; the rules as written)",
            withConn(state(), false, true),
            // Close Q reads the connection only (CLOSE); MQClose reads the open queue only (MENU 2416);
            // MQConn is on because nothing is connected.
            join({ ALWAYS, ON_MAIN, IDLE_BUTTONS, MENU_OPEN, { "menuMqConn", "menuMqClose" } }) });
        t.push_back({ "connected, queue closed, Data tab: the MQ items need the Main tab",
            withConn(state(Tab::Data), true, false),
            // MQConn and the MQOpen items test PAGE_MAIN (MENU 2306, 2343, 2370, 2397); MQDisc does not (2315)
            join({ ALWAYS, ON_DATA, IDLE_BUTTONS, MENU_OPEN, { "closeQ", "menuMqDisc" } }) });
        t.push_back({ "connected, queue open, MQMD tab",
            withConn(state(Tab::Mqmd), true, true),
            join({ ALWAYS, ON_MQMD, IDLE_BUTTONS, MENU_OPEN, { "closeQ", "menuMqDisc", "menuMqClose" } }) });

        // ---- browse -------------------------------------------------------------------------------------
        // Browse active (GEN 546-580): off are Open, Clear Data, Clear All, Read, Purge, Write, Browse, Start,
        // Close Q, Load Names, the option controls; on are Next and End Browse, Prev only after message 1.
        // MQ menu: Conn, Disc, MQClose all test !browseActive (MENU 2296-2430); End Browse needs Main or Data.
        t.push_back({ "browse active at message 1, Main tab",
            withBrowse(withConn(state(), true, true), true, 1),
            join({ ALWAYS, ON_MAIN, { "browseNext", "endBrowse", "dataBrowseNext", "menuMqEndBrowse" } }) });
        t.push_back({ "browse active at message 2, Main tab: Prev on",
            withBrowse(withConn(state(), true, true), true, 2),
            join({ ALWAYS, ON_MAIN, { "browseNext", "browsePrev", "endBrowse", "dataBrowseNext", "dataBrowsePrev", "menuMqEndBrowse" } }) });
        t.push_back({ "browse active, position unknown (0): Prev off",
            withBrowse(withConn(state(), true, true), true, 0),
            join({ ALWAYS, ON_MAIN, { "browseNext", "endBrowse", "dataBrowseNext", "menuMqEndBrowse" } }) });
        t.push_back({ "browse not active, stale position 5: Prev stays off (General.cpp:596-598)",
            withBrowse(withConn(state(), true, true), false, 5),
            join({ ALWAYS, ON_MAIN, IDLE_BUTTONS, MENU_OPEN, { "closeQ", "menuMqDisc", "menuMqClose" } }) });
        t.push_back({ "browse active at message 3, Data tab, data present",
            withData(withBrowse(withConn(state(Tab::Data), true, true), true, 3), View::Character),
            // Find, Find Hex, Go To and Print: Data tab, data present, Character view (MENU 1059, 1737, 1786, 1873)
            join({ ALWAYS, ON_DATA, { "browseNext", "browsePrev", "endBrowse", "dataBrowseNext", "dataBrowsePrev",
                   "menuMqEndBrowse", "menuPrint", "menuFind", "menuFindHex", "menuGoto" } }) });
        t.push_back({ "browse active at message 1, MQMD tab: End Browse menu needs Main or Data (MENU 2435)",
            withBrowse(withConn(state(Tab::Mqmd), true, true), true, 1),
            join({ ALWAYS, ON_MQMD, { "browseNext", "endBrowse", "dataBrowseNext" } }) });

        // ---- busy (NEW) ---------------------------------------------------------------------------------
        t.push_back({ "busy, not cancellable: only what is not a job stays on",
            [] { EnableInputs in = withConn(state(), true, true); in.busy = true; return in; }(),
            // the option controls are not jobs and Windows does not switch them with anything but browse
            join({ ALWAYS, ON_MAIN, { "getOptions", "propsRadios" } }) });
        t.push_back({ "busy, cancellable: Cancel on",
            [] { EnableInputs in = withConn(state(), true, true); in.busy = true; in.cancellable = true; return in; }(),
            join({ ALWAYS, ON_MAIN, { "getOptions", "propsRadios", "cancel" } }) });
        t.push_back({ "busy, cancellable, browsing at message 2 (a Browse Prev walk): Next, Prev, End all off",
            [] { EnableInputs in = withBrowse(withConn(state(), true, true), true, 2); in.busy = true; in.cancellable = true; return in; }(),
            join({ ALWAYS, ON_MAIN, { "cancel" } }) });
        t.push_back({ "not busy but the cancellable flag is stale: Cancel stays off",
            [] { EnableInputs in = state(); in.cancellable = true; return in; }(),
            join({ ALWAYS, ON_MAIN, IDLE_BUTTONS, MENU_OPEN, { "menuMqConn" } }) });
        t.push_back({ "busy on the Data tab with a message: Find and Print are not jobs and stay on",
            [] { EnableInputs in = withData(withConn(state(Tab::Data), true, true), View::Hex); in.busy = true; return in; }(),
            join({ ALWAYS, ON_DATA, { "getOptions", "propsRadios", "menuPrint", "menuFind", "menuFindHex", "menuGoto" } }) });

        // ---- Find dialog open (MENU 1634, 1670, 1737-1900; TABS 788) ---------------------------------------
        t.push_back({ "Find dialog open, Data tab, data present: Save, Open and the Search items are off, tabs blocked",
            [] { EnableInputs in = withData(state(Tab::Data), View::Character); in.findDialogOpen = true; return in; }(),
            // menuSave off (OnUpdateFileWrite), menuOpen off (OnUpdateOpen), Find/FindHex/Goto off; Print has no such test
            join({ Names{ "saveFile", "setConnId", "exit", "menuCopy", "menuSelectAll", "mqmdMsgIdEditable", "selectorEditable",
                          "tab:Main", "tab:Data", "tab:Mqmd", "tabChangeBlocked" },
                   ON_DATA, IDLE_BUTTONS, { "menuPrint" } }) });

        // ---- Find, Find Hex, Go To by display (MENU 1737-1900) ---------------------------------------------
        const Names findAll = { "menuFind", "menuFindHex", "menuGoto", "menuPrint" };
        const Names findOnly = { "menuFind", "menuPrint" };
        const Names printOnly = { "menuPrint" };
        struct ViewCase { const char * name; View view; Names extra; };
        const std::vector<ViewCase> views = {
            { "Character", View::Character, findAll }, { "Hex", View::Hex, findAll }, { "Both", View::Both, findAll },
            { "Xml", View::Xml, findOnly }, { "Parsed", View::Parsed, findOnly }, { "Json", View::Json, findOnly },
            { "Fixed", View::Fixed, findOnly },
            { "Cobol", View::Cobol, printOnly },	// COBOL is in none of the three lists
        };
        for (const ViewCase & v : views)
        {
            t.push_back({ std::string("Data tab, data present, ") + v.name + " view", withData(state(Tab::Data), v.view),
                join({ ALWAYS, ON_DATA, IDLE_BUTTONS, MENU_OPEN, v.extra }) });
        }
        t.push_back({ "Data tab, no data: Print, Find, Find Hex and Go To off (fileSize == 0)",
            withData(state(Tab::Data), View::Character, false),
            join({ ALWAYS, ON_DATA, IDLE_BUTTONS, MENU_OPEN }) });
        t.push_back({ "Main tab, data present: Find and Go To need the Data tab, Print has no Main branch",
            withData(state(Tab::Main), View::Character),
            join({ ALWAYS, ON_MAIN, IDLE_BUTTONS, MENU_OPEN, { "menuMqConn" } }) });
        t.push_back({ "MQMD tab, data present: as the Main tab",
            withData(state(Tab::Mqmd), View::Character),
            join({ ALWAYS, ON_MQMD, IDLE_BUTTONS, MENU_OPEN }) });

        // ---- clipboard (MENU 1120): Paste on every tab but Data, when the clipboard holds text ----------------
        t.push_back({ "clipboard text, Main tab: Paste on",
            [] { EnableInputs in = state(); in.clipboardText = true; return in; }(),
            join({ ALWAYS, ON_MAIN, IDLE_BUTTONS, MENU_OPEN, { "menuMqConn", "menuPaste" } }) });
        t.push_back({ "clipboard text, Data tab: Paste off (nothing on the page supports it)",
            [] { EnableInputs in = state(Tab::Data); in.clipboardText = true; return in; }(),
            join({ ALWAYS, ON_DATA, IDLE_BUTTONS, MENU_OPEN }) });
        t.push_back({ "clipboard text, MQMD tab: Paste on",
            [] { EnableInputs in = state(Tab::Mqmd); in.clipboardText = true; return in; }(),
            join({ ALWAYS, ON_MQMD, IDLE_BUTTONS, MENU_OPEN, { "menuPaste" } }) });

        // ---- properties not supported (General.cpp:1302): the Selector edit is read-only ------------------------
        t.push_back({ "queue manager library without properties: Selector read-only",
            [] { EnableInputs in = state(); in.propertiesSupported = false; return in; }(),
            [] {
                Names n = join({ ALWAYS, ON_MAIN, IDLE_BUTTONS, MENU_OPEN, { "menuMqConn" } });
                n.erase("selectorEditable");
                return n;
            }() });

        // ---- MQMD tab read-only fields (MQMD 410-446) -------------------------------------------------------
        // User Id and Accounting Token open up with Set Iden, Set All or Alternate User; the other context
        // fields only with Set All.
        const Names mqmdBase = join({ ALWAYS, ON_MQMD, IDLE_BUTTONS, MENU_OPEN });
        t.push_back({ "MQMD tab, no context option: User Id and the put application fields are read-only",
            state(Tab::Mqmd), mqmdBase });
        t.push_back({ "MQMD tab, Set Iden Context",
            [] { EnableInputs in = state(Tab::Mqmd); in.setIdentity = true; return in; }(),
            join({ mqmdBase, { "mqmdUserIdEditable" } }) });
        t.push_back({ "MQMD tab, Alternate User Id",
            [] { EnableInputs in = state(Tab::Mqmd); in.alternateUser = true; return in; }(),
            join({ mqmdBase, { "mqmdUserIdEditable" } }) });
        t.push_back({ "MQMD tab, Set All Context: everything opens up",
            [] { EnableInputs in = state(Tab::Mqmd); in.setAll = true; return in; }(),
            join({ mqmdBase, { "mqmdUserIdEditable", "mqmdPutApplEditable" } }) });
        t.push_back({ "MQMD tab, Set Iden and Set All",
            [] { EnableInputs in = state(Tab::Mqmd); in.setIdentity = true; in.setAll = true; return in; }(),
            join({ mqmdBase, { "mqmdUserIdEditable", "mqmdPutApplEditable" } }) });

        return t;
    }

    std::string describe(const Names & names)
    {
        std::string s;
        for (const std::string & n : names)
        {
            s += n + " ";
        }
        return s;
    }

    Names enabledNames(const Enables & e)
    {
        Names n;
        for (const auto & f : e.fields())
        {
            if (f.second)
            {
                n.insert(f.first);
            }
        }
        return n;
    }

    Names allNames()
    {
        Names n;
        for (const auto & f : Enables().fields())
        {
            n.insert(f.first);
        }
        return n;
    }

    // The buttons and menu items that start a job on the worker thread.
    const char * kJobStarters[] = {
        "readQ", "purgeQ", "writeQ", "browseQ", "startBrowse", "browseNext", "browsePrev", "endBrowse", "closeQ", "loadNames",
        "dataBrowseNext", "dataBrowsePrev",
        "menuMqConn", "menuMqDisc", "menuMqOpenGet", "menuMqOpenPut", "menuMqOpenBrowse", "menuMqClose", "menuMqEndBrowse",
    };
    // Replace the document's data while a job is about to deliver a message into it (NEW: also off while busy).
    const char * kDocumentReplacers[] = { "openFile", "menuOpen", "clearData", "clearAll" };

    rfh::Message messageWith(size_t bodyLen, size_t headerLen = 0)
    {
        rfh::Message m;
        m.body.assign(bodyLen, 'b');
        m.headers.assign(headerLen, 'h');
        return m;
    }
}

class AppDocumentTests : public QObject
{
    Q_OBJECT

private slots:
    // ---- the enable table -------------------------------------------------------------------------------

    void fieldNamesAreUnique()
    {
        const auto fields = Enables().fields();
        Names seen;
        for (const auto & f : fields)
        {
            QVERIFY2(seen.insert(f.first).second, f.first.c_str());
        }
        QVERIFY(fields.size() > 60);
    }

    void tableExpectationsNameRealFields()
    {
        const Names all = allNames();
        for (const Row & row : buildTable())
        {
            for (const std::string & n : row.enabled)
            {
                QVERIFY2(all.count(n) == 1, (row.name + ": unknown field " + n).c_str());
            }
        }
    }

    void everyTableRow()
    {
        const std::vector<Row> table = buildTable();
        QCOMPARE(table.size(), (size_t)38);
        for (const Row & row : table)
        {
            const Names actual = enabledNames(AppDocument::enablesFor(row.in));
            if (actual != row.enabled)
            {
                Names missing, extra;
                for (const std::string & n : row.enabled) { if (!actual.count(n)) missing.insert(n); }
                for (const std::string & n : actual) { if (!row.enabled.count(n)) extra.insert(n); }
                printf("ROW FAILED: %s\n  expected on but off: %s\n  on but expected off: %s\n",
                    row.name.c_str(), describe(missing).c_str(), describe(extra).c_str());
                QFAIL(row.name.c_str());
            }
        }
    }

    // ---- invariants over every combination of the inputs ----------------------------------------------------

    void sweepInvariants()
    {
        const Tab tabs[] = { Tab::Main, Tab::Data, Tab::Mqmd };
        const View allViews[] = { View::Character, View::Hex, View::Both, View::Xml, View::Parsed, View::Cobol, View::Json, View::Fixed };
        const struct { bool active; long pos; } browses[] = { { false, 0 }, { true, 0 }, { true, 1 }, { true, 2 } };
        int combos = 0;

        for (Tab tab : tabs)
        for (int connected = 0; connected < 2; connected++)
        for (int open = 0; open < 2; open++)
        for (const auto & b : browses)
        for (int busy = 0; busy < 2; busy++)
        for (int cancellable = 0; cancellable < 2; cancellable++)
        for (int find = 0; find < 2; find++)
        for (View view : allViews)
        for (int data = 0; data < 2; data++)
        {
            EnableInputs in;
            in.tab = tab; in.connected = connected; in.queueOpen = open;
            in.browse.active = b.active; in.browse.position = b.pos;
            in.busy = busy; in.cancellable = cancellable; in.findDialogOpen = find;
            in.view = view; in.dataPresent = data;
            const Enables e = AppDocument::enablesFor(in);
            const Names on = enabledNames(e);
            combos++;

            // never changed by any state
            QVERIFY(e.saveFile && e.setConnId && e.exit && e.mqmdMsgIdEditable);
            // not ported in Phase 1
            QVERIFY(!e.displayQ && !e.loadQ && !e.saveQ && !e.moveQ && !e.dataAllowEdit && !e.dataWriteQ && !e.dataBom);
            // the Phase 1 tabs
            for (int i = 0; i < (int)Tab::Count; i++)
            {
                QCOMPARE(e.tabEnabled((Tab)i), i <= (int)Tab::Mqmd);
            }
            QCOMPARE(e.tabChangeBlocked, (bool)find);
            // busy: nothing that starts a job, nothing that replaces the data; Cancel only for a cancellable job
            if (busy)
            {
                for (const char * n : kJobStarters) { QVERIFY2(!on.count(n), n); }
                for (const char * n : kDocumentReplacers) { QVERIFY2(!on.count(n), n); }
            }
            QCOMPARE(e.cancel, (bool)(busy && cancellable));
            // browsing: the idle buttons and the option controls are off (General.cpp:546-572)
            if (b.active)
            {
                QVERIFY(!e.openFile && !e.clearData && !e.clearAll && !e.readQ && !e.purgeQ && !e.writeQ && !e.browseQ);
                QVERIFY(!e.startBrowse && !e.closeQ && !e.loadNames && !e.getOptions && !e.propsRadios && !e.menuOpen);
                QVERIFY(!e.menuMqConn && !e.menuMqDisc && !e.menuMqClose && !e.menuMqOpenGet);
                QCOMPARE(e.browseNext, !busy);
                QCOMPARE(e.endBrowse, !busy);
                QCOMPARE(e.browsePrev, !busy && b.pos > 1);
                QCOMPARE(e.dataBrowseNext, e.browseNext);
                QCOMPARE(e.dataBrowsePrev, e.browsePrev);
            }
            else
            {
                QVERIFY(!e.browseNext && !e.browsePrev && !e.endBrowse && !e.dataBrowseNext && !e.dataBrowsePrev && !e.menuMqEndBrowse);
                QCOMPARE(e.readQ, !busy);
                QCOMPARE(e.closeQ, !busy && connected);
            }
            // the search items need the Data tab, data and no open dialog
            if (tab != Tab::Data || !data || find)
            {
                QVERIFY(!e.menuFind && !e.menuFindHex && !e.menuGoto);
            }
            QCOMPARE(e.menuSave, !find);
            QCOMPARE(e.menuTop, tab == Tab::Data);
            QCOMPARE(e.menuCut, tab != Tab::Data);
        }
        printf("sweep: %d combinations\n", combos);
        QCOMPARE(combos, 3 * 2 * 2 * 4 * 2 * 2 * 2 * 8 * 2);
    }

    // ---- confirm before discard ------------------------------------------------------------------------------

    void onlyPurgeAsksForConfirmation()
    {
        // General.cpp:900 is the only MB_YESNO in the Main, Data and MQMD code (grep). Open File, Clear Data, Clear All,
        // Exit (General.cpp:1029) and every read replace the data without asking.
        QVERIFY(AppDocument::confirmsBeforeDiscard(DiscardAction::PurgeQ));
        const DiscardAction silent[] = {
            DiscardAction::OpenFile, DiscardAction::ClearData, DiscardAction::ClearAll, DiscardAction::ReadQ,
            DiscardAction::BrowseQ, DiscardAction::StartBrowse, DiscardAction::CloseQ, DiscardAction::Exit
        };
        for (DiscardAction a : silent)
        {
            QVERIFY(!AppDocument::confirmsBeforeDiscard(a));
        }
        QCOMPARE(AppDocument::purgeConfirmation("RFHUTIL.TEST.A"), QString("Purge all messages from queue RFHUTIL.TEST.A?"));
    }

    // ---- the document -----------------------------------------------------------------------------------------

    void startsEmpty()
    {
        AppDocument doc;
        QVERIFY(!doc.hasData());
        QCOMPARE(doc.dataSize(), (size_t)0);
        QVERIFY(!doc.hasFile());
        QVERIFY(!doc.dirty());
        QVERIFY(!doc.connected());
        QVERIFY(!doc.queueOpen());
        QVERIFY(!doc.browse().active);
        QCOMPARE(doc.dataSource(), std::string("Message Data"));
        QCOMPARE(doc.shell().tab, Tab::Main);
    }

    void setMessageMovesTheBytesAndSignalsOnce()
    {
        AppDocument doc;
        QSignalSpy message(&doc, &AppDocument::messageChanged);
        QSignalSpy dirty(&doc, &AppDocument::dirtyChanged);
        QSignalSpy state(&doc, &AppDocument::stateChanged);
        QSignalSpy mqmd(&doc, &AppDocument::mqmdChanged);

        rfh::Message m = messageWith(1000, 36);
        const unsigned char * body = m.body.data();
        const unsigned char * headers = m.headers.data();
        doc.setMessage(std::move(m), "Message Data (1000) msg 1 of 3 from Q1");

        QCOMPARE((const void *)doc.message().body.data(), (const void *)body);			// moved, not copied
        QCOMPARE((const void *)doc.message().headers.data(), (const void *)headers);
        QCOMPARE(doc.dataSize(), (size_t)1000);
        QVERIFY(doc.hasData());
        QCOMPARE(doc.dataSource(), std::string("Message Data (1000) msg 1 of 3 from Q1"));
        QVERIFY(doc.dirty());
        QCOMPARE(message.count(), 1);
        QCOMPARE(dirty.count(), 1);
        QCOMPARE(state.count(), 1);
        QCOMPARE(mqmd.count(), 0);
        QVERIFY(message.first().isEmpty());		// signals carry no payload

        doc.setMessage(messageWith(5), "x", true);		// replaces; dirty did not change
        QCOMPARE(message.count(), 2);
        QCOMPARE(dirty.count(), 1);
        QCOMPARE(state.count(), 2);
    }

    void dataSizeCountsTheBodyOnly()
    {
        AppDocument doc;
        doc.setMessage(messageWith(0, 36));
        QVERIFY(!doc.hasData());								// Windows fileSize: headers do not count
        QVERIFY(!AppDocument::enablesFor(doc.inputs()).menuPrint);
    }

    void fileReadIsNotDirtyAndSavingClearsDirty()
    {
        AppDocument doc;
        QSignalSpy dirty(&doc, &AppDocument::dirtyChanged);
        doc.setMessage(messageWith(4), "Message Data (4) from /tmp/a.msg", false);
        QVERIFY(!doc.dirty());
        QCOMPARE(dirty.count(), 0);
        doc.setMessage(messageWith(8), "Message Data (8) msg 1 of 1 from Q");
        QVERIFY(doc.dirty());
        QCOMPARE(dirty.count(), 1);

        QSignalSpy file(&doc, &AppDocument::fileChanged);
        doc.markSaved("/tmp/dir/out.msg");
        QVERIFY(!doc.dirty());
        QCOMPARE(dirty.count(), 2);
        QCOMPARE(file.count(), 1);
        QCOMPARE(doc.filePath(), QString("/tmp/dir/out.msg"));
        QCOMPARE(doc.fileName(), QString("out.msg"));
        QVERIFY(doc.hasFile());

        doc.markSaved("/tmp/dir/out.msg");		// nothing changed
        QCOMPARE(dirty.count(), 2);
        QCOMPARE(file.count(), 1);
    }

    void fileStateDoesNotChangeTheEnableRules()
    {
        // No Windows handler looks at the file name: Save File is on with or without data or a file.
        AppDocument doc;
        const Names none = enabledNames(doc.enables());
        doc.setFilePath("/tmp/x.msg");
        QCOMPARE(enabledNames(doc.enables()), none);
        doc.setMessage(messageWith(3), "Message Data (3) from /tmp/x.msg", false);
        QVERIFY(doc.enables().saveFile);
        QVERIFY(doc.enables().openFile);
    }

    void fileNameOfAPathWithoutDirectory()
    {
        AppDocument doc;
        doc.setFilePath("plain.msg");
        QCOMPARE(doc.fileName(), QString("plain.msg"));
        doc.setFilePath("");
        QCOMPARE(doc.fileName(), QString());
        QVERIFY(!doc.hasFile());
    }

    void connectionStateAndSignals()
    {
        AppDocument doc;
        QSignalSpy conn(&doc, &AppDocument::connectionChanged);
        QSignalSpy browse(&doc, &AppDocument::browseChanged);
        QSignalSpy state(&doc, &AppDocument::stateChanged);

        doc.setConnected(true);
        doc.setConnected(true);			// no change
        QCOMPARE(conn.count(), 1);
        QCOMPARE(state.count(), 1);
        doc.setQueueOpen(true);
        QCOMPARE(conn.count(), 2);
        doc.setBrowse({ true, 3 });
        QCOMPARE(browse.count(), 1);
        doc.setBrowse({ true, 3 });		// no change
        QCOMPARE(browse.count(), 1);

        // a disconnect closes the queue and ends the browse: one stateChanged for the whole step
        conn.clear(); browse.clear(); state.clear();
        doc.setConnected(false);
        QVERIFY(!doc.queueOpen());
        QVERIFY(!doc.browse().active);
        QCOMPARE(doc.browse().position, 0L);
        QCOMPARE(conn.count(), 1);
        QCOMPARE(browse.count(), 1);
        QCOMPARE(state.count(), 1);
    }

    void queueNamesAndFactsSignalOncePerChange()
    {
        AppDocument doc;
        QSignalSpy queue(&doc, &AppDocument::queueChanged);
        doc.setQueueManager("QM1");
        doc.setQueueManager("QM1");
        doc.setQueueName("RFHUTIL.TEST.A");
        doc.setRemoteQueueManager("QM2");
        QCOMPARE(queue.count(), 3);
        rfh::QueueInfo info;
        info.type = MQQT_LOCAL;
        info.depth = 7;
        doc.setQueueInfo(info);
        doc.setQueueInfo(info);
        QCOMPARE(queue.count(), 4);
        QCOMPARE(doc.queueManager(), QString("QM1"));
        QCOMPARE(doc.queueName(), QString("RFHUTIL.TEST.A"));
        QCOMPARE(doc.remoteQueueManager(), QString("QM2"));
        QCOMPARE(doc.queueInfo().depth, (MQLONG)7);
    }

    void shellChangesSignalAndFeedTheRules()
    {
        AppDocument doc;
        QSignalSpy shell(&doc, &AppDocument::shellChanged);
        ShellState s;
        s.tab = Tab::Data;
        s.view = View::Xml;
        doc.setShell(s);
        doc.setShell(s);
        QCOMPARE(shell.count(), 1);
        doc.setMessage(messageWith(10));
        QVERIFY(doc.enables().menuFind);
        QVERIFY(!doc.enables().menuGoto);			// Xml view
        s.busy = true;
        s.cancellable = true;
        doc.setShell(s);
        QVERIFY(doc.enables().cancel);
        QVERIFY(!doc.enables().readQ);
    }

    void applyJobResultMirrorsTheConnection()
    {
        AppDocument doc;
        QSignalSpy conn(&doc, &AppDocument::connectionChanged);
        JobResult r;
        r.connected = true;
        r.queueManager.platform = MQPL_UNIX;
        r.queueManager.commandLevel = 940;
        r.queueManager.maxMsgLength = 4194304;
        doc.applyJobResult("connect", r);
        QVERIFY(doc.connected());
        QCOMPARE(doc.queueManagerInfo().commandLevel, (MQLONG)940);
        QCOMPARE(conn.count(), 1);

        JobResult q;
        q.connected = true;
        q.hasQueueInfo = true;
        q.queue.type = MQQT_ALIAS;
        q.queue.depth = 12;
        doc.applyJobResult("queueInfo", q);
        QCOMPARE(doc.queueInfo().depth, (MQLONG)12);
        QCOMPARE(doc.queueManagerInfo().commandLevel, (MQLONG)940);		// only "connect" replaces these

        JobResult d;						// disconnect: connected false, the queue facts of this result are not taken
        d.connected = false;
        doc.applyJobResult("disconnect", d);
        QVERIFY(!doc.connected());
        QCOMPARE(doc.queueManagerInfo().commandLevel, (MQLONG)0);		// unknown again, as MqSession::queueManagerInfo()
    }

    void applyJobResultMirrorsTheOpenQueueOfTheSession()
    {
        // MqSession::isQueueOpen() is the only truth about the queue handle; every job reports it (Task 23).
        AppDocument doc;
        JobResult connect;
        connect.connected = true;
        doc.applyJobResult("connect", connect);
        QVERIFY(!doc.queueOpen());

        JobResult get;
        get.connected = true;
        get.queueOpen = true;
        doc.applyJobResult("get", get);
        QVERIFY(doc.queueOpen());

        QSignalSpy conn(&doc, &AppDocument::connectionChanged);
        doc.applyJobResult("get", get);
        QCOMPARE(conn.count(), 0);						// nothing changed, nothing signalled

        JobResult closed;
        closed.connected = true;
        closed.queueOpen = false;
        doc.applyJobResult("close", closed);
        QVERIFY(!doc.queueOpen());
        QVERIFY(doc.connected());
        QCOMPARE(conn.count(), 1);

        doc.applyJobResult("get", get);
        JobResult lost;							// the connection went: the handle went with it
        lost.connected = false;
        lost.queueOpen = false;
        doc.applyJobResult("get", lost);
        QVERIFY(!doc.connected());
        QVERIFY(!doc.queueOpen());
    }

    void clearDataKeepsHeadersMqmdFileAndCopyBook()
    {
        // Windows clearFileData -> resetDataArea (DataArea.cpp:2148): fileSize, fileCcsid, fileIntFormat, the data
        // and the displays only. The header tabs, the MQMD tab and fileName stay.
        AppDocument doc;
        rfh::Message m = messageWith(50, 36);
        m.mqmd.Version = MQMD_VERSION_2;
        m.mqmd.Priority = 5;
        doc.setMessage(std::move(m), "Message Data (50) from /tmp/f.msg", false);
        doc.markSaved("/tmp/f.msg");
        rfh::MqmdFields f;
        f.format = "MQSTR";
        f.priority = "5";
        doc.setMqmd(f);
        rfh::format::RenderOptions r;
        r.ccsid = 1208;
        r.intFormat = NUMERIC_HOST;
        r.copybookPath = "/tmp/book.cpy";
        doc.setRender(r);

        QSignalSpy message(&doc, &AppDocument::messageChanged);
        QSignalSpy render(&doc, &AppDocument::renderChanged);
        QSignalSpy mqmd(&doc, &AppDocument::mqmdChanged);
        QSignalSpy file(&doc, &AppDocument::fileChanged);
        QSignalSpy state(&doc, &AppDocument::stateChanged);
        doc.clearData();

        QVERIFY(!doc.hasData());
        QCOMPARE(doc.message().headers.size(), (size_t)36);
        QCOMPARE(doc.message().mqmd.Priority, (MQLONG)5);
        QCOMPARE(doc.mqmd().format, std::string("MQSTR"));
        QCOMPARE(doc.filePath(), QString("/tmp/f.msg"));
        QCOMPARE(doc.dataSource(), std::string("Message Data"));
        QCOMPARE(doc.render().ccsid, 0);
        QCOMPARE(doc.render().intFormat, (int)NUMERIC_PC);
        QCOMPARE(doc.render().copybookPath, std::string("/tmp/book.cpy"));
        QVERIFY(!doc.dirty());
        QCOMPARE(message.count(), 1);
        QCOMPARE(render.count(), 1);
        QCOMPARE(mqmd.count(), 0);
        QCOMPARE(file.count(), 0);
        QCOMPARE(state.count(), 1);
    }

    void clearDataOnAnEmptyDocumentSignalsNothing()
    {
        AppDocument doc;
        QSignalSpy state(&doc, &AppDocument::stateChanged);
        doc.clearData();
        QCOMPARE(state.count(), 0);
    }

    void clearAllResetsEverythingWindowsResets()
    {
        // DeleteContents (DataArea.cpp:1996) + ClearData (2175): data, headers, MQMD, file name, copy book name,
        // queue depth 0. Not reset: the connection, the queue names, the queue type (m_queue_type), the display
        // choices (crlf, indent, ...).
        AppDocument doc;
        rfh::Message m = messageWith(50, 36);
        m.mqmd.Priority = 5;
        doc.setMessage(std::move(m), "Message Data (50) from /tmp/f.msg");
        doc.markSaved("/tmp/f.msg");
        rfh::MqmdFields f;
        f.format = "MQSTR";
        f.priority = "5";
        f.persistence = MQPERSIST_YES;
        doc.setMqmd(f);
        rfh::format::RenderOptions r;
        r.ccsid = 1208;
        r.crlf = true;
        r.copybookPath = "/tmp/book.cpy";
        doc.setRender(r);
        doc.setConnected(true);
        doc.setQueueName("Q1");
        rfh::QueueInfo info;
        info.type = MQQT_LOCAL;
        info.depth = 9;
        doc.setQueueInfo(info);

        QSignalSpy state(&doc, &AppDocument::stateChanged);
        QSignalSpy mqmd(&doc, &AppDocument::mqmdChanged);
        QSignalSpy file(&doc, &AppDocument::fileChanged);
        doc.clearAll();

        QVERIFY(!doc.hasData());
        QVERIFY(doc.message().headers.empty());
        QCOMPARE(doc.message().mqmd.Version, (MQLONG)MQMD_VERSION_2);
        QCOMPARE(doc.message().mqmd.Priority, (MQLONG)MQPRI_PRIORITY_AS_Q_DEF);
        rfh::MqmdFields cleared;
        rfh::mqmd_fields::clear(cleared);
        QCOMPARE(doc.mqmd().format, cleared.format);
        QCOMPARE(doc.mqmd().priority, cleared.priority);
        QCOMPARE(doc.mqmd().persistence, cleared.persistence);
        QVERIFY(!doc.hasFile());
        QCOMPARE(doc.dataSource(), std::string("Message Data"));
        QCOMPARE(doc.render().copybookPath, std::string());
        QCOMPARE(doc.render().ccsid, 0);
        QVERIFY(doc.render().crlf);						// a display choice, kept
        QCOMPARE(doc.queueInfo().depth, (MQLONG)0);
        QCOMPARE(doc.queueInfo().type, (MQLONG)MQQT_LOCAL);
        QVERIFY(doc.connected());
        QCOMPARE(doc.queueName(), QString("Q1"));
        QVERIFY(!doc.dirty());
        QCOMPARE(state.count(), 1);						// one notification for the whole step
        QCOMPARE(mqmd.count(), 1);
        QCOMPARE(file.count(), 1);
    }
};

QTEST_MAIN(AppDocumentTests)
#include "AppDocumentTests.moc"
