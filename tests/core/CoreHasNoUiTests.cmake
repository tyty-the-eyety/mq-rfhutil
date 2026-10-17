# Fails when a source or header under core/ mentions a UI toolkit, an MFC call or a process exit.
#
# Scan (what core.NoUi runs):
#   cmake -DSCAN_DIR=<dir> -P CoreHasNoUiTests.cmake
# Self-test (what core.NoUiSelfTest runs; it proves the scan can fail and has no false positives):
#   cmake -DSELFTEST=1 -DWORK_DIR=<build dir> -P CoreHasNoUiTests.cmake
#
# CMake regular expressions have no \b, so every pattern spells its word boundaries out:
# a hit needs a character that cannot be part of an identifier (or the start or end of the
# file) on each side. Patterns are case sensitive. Comments are scanned too, on purpose:
# the core should not even talk about dialogs.

set(NOT_ID "[^A-Za-z0-9_]")
set(START "(^|${NOT_ID})")
set(END "(${NOT_ID}|$)")

# Each entry is "label|regex".
set(PATTERNS
    "Qt include|#[ \t]*include[ \t]*[<\"]Q[A-Za-z]"
    "QMessageBox|${START}QMessageBox${END}"
    "AfxMessageBox|${START}AfxMessageBox${END}"
    "MessageBox(|${START}MessageBox[ \t]*\\("
    "GetDlgItem|${START}GetDlgItem${END}"
    "UpdateData|${START}UpdateData${END}"
    "CWnd|${START}CWnd${END}"
    "CString|${START}CString${END}"
    "std::cin|${START}std::cin${END}"
    "exit(|${START}exit[ \t]*\\("
    "abort(|${START}abort[ \t]*\\("
    "std::quick_exit|${START}std::quick_exit${END}"
)

# Allowed hits, one "relative/path|label|reason" entry each. Empty: the core has no legitimate use.
set(ALLOW_LIST
)

# rfh_scan(<dir> <hits variable>): appends "file:line: label" for every hit under <dir>.
function(rfh_scan dir hits_var)
    file(GLOB_RECURSE files RELATIVE "${dir}" "${dir}/*.h" "${dir}/*.cpp")
    list(SORT files)
    if(NOT IS_ABSOLUTE "${dir}" OR NOT files)
        # A wrong or relative path would otherwise scan nothing and pass.
        message(FATAL_ERROR "no .h or .cpp files found under '${dir}' (give an absolute path)")
    endif()
    set(hits "")
    foreach(rel IN LISTS files)
        file(READ "${dir}/${rel}" content)
        foreach(entry IN LISTS PATTERNS)
            string(FIND "${entry}" "|" bar)
            string(SUBSTRING "${entry}" 0 ${bar} label)
            math(EXPR from "${bar} + 1")
            string(SUBSTRING "${entry}" ${from} -1 regex)

            set(allowed FALSE)
            foreach(allow IN LISTS ALLOW_LIST)
                string(FIND "${allow}" "${rel}|${label}|" where)
                if(where EQUAL 0)
                    set(allowed TRUE)
                endif()
            endforeach()
            if(allowed)
                continue()
            endif()

            # Walk the file match by match so that every hit gets its line number.
            set(rest "${content}")
            set(consumed 0)
            while(rest MATCHES "${regex}")
                string(REGEX MATCH "${regex}" hit "${rest}")
                string(FIND "${rest}" "${hit}" at)
                string(LENGTH "${hit}" hit_length)
                math(EXPR cut "${at} + ${hit_length}")
                string(SUBSTRING "${rest}" 0 ${cut} before)
                string(SUBSTRING "${content}" 0 ${consumed} head)
                string(REGEX MATCHALL "\n" head_newlines "${head}${before}")
                list(LENGTH head_newlines count)
                math(EXPR line "${count} + 1")
                list(APPEND hits "${rel}:${line}: ${label}")
                math(EXPR consumed "${consumed} + ${cut}")
                string(SUBSTRING "${rest}" ${cut} -1 rest)
            endwhile()
        endforeach()
    endforeach()
    set(${hits_var} "${hits}" PARENT_SCOPE)
endfunction()

if(SELFTEST)
    if(NOT WORK_DIR)
        message(FATAL_ERROR "SELFTEST needs -DWORK_DIR=<directory in the build tree>")
    endif()
    set(bad "${WORK_DIR}/bad")
    set(clean "${WORK_DIR}/clean")
    file(REMOVE_RECURSE "${bad}" "${clean}")
    file(MAKE_DIRECTORY "${bad}/sub" "${clean}")

    # One offending line per pattern, in the forms the core could plausibly contain.
    file(WRITE "${bad}/a.h"
        "#include <QWidget>\n"
        "#include \"QString\"\n"
        "QMessageBox box;\n"
        "AfxMessageBox(\"x\");\n"
        "::MessageBox(0, \"a\", \"b\", 0);\n"
        "GetDlgItem(IDOK);\n"
        "UpdateData(TRUE);\n")
    file(WRITE "${bad}/sub/b.cpp"
        "void f(CWnd *w, CString s)\n"
        "{\n"
        "    std::cin >> s;\n"
        "    if (!w) exit(1);\n"
        "    std::exit(2);\n"
        "    abort();\n"
        "    std::quick_exit(3);\n"
        "}\n")

    # Ordinary words that merely contain those letters.
    file(WRITE "${clean}/ok.cpp"
        "int exitCode = 0;\n"
        "bool abortOnError = false;\n"
        "void reexit();\n"
        "void my_exit(int);\n"
        "void do_abort_now();\n"
        "int exits(int);\n"
        "int aborts(int);\n"
        "class QueueName; class QMessageBoxLike; class CStringList; class MyCWnd;\n"
        "void MessageBoxCount(); void GetDlgItemCount(); void UpdateDataNow();\n"
        "// the abort path and the exit path are described here without a call\n"
        "int abortedCount, exited;\n"
        "#include <string>\n")

    rfh_scan("${bad}" bad_hits)
    rfh_scan("${clean}" clean_hits)

    set(failures "")
    foreach(entry IN LISTS PATTERNS)
        string(FIND "${entry}" "|" bar)
        string(SUBSTRING "${entry}" 0 ${bar} label)
        set(found FALSE)
        foreach(hit IN LISTS bad_hits)
            string(FIND "${hit}" ": ${label}" where REVERSE)
            string(LENGTH "${hit}" hit_length)
            string(LENGTH ": ${label}" tail_length)
            math(EXPR tail_at "${hit_length} - ${tail_length}")
            if(where EQUAL tail_at)
                set(found TRUE)
            endif()
        endforeach()
        if(NOT found)
            list(APPEND failures "pattern '${label}' did not report the offending fixture")
        endif()
    endforeach()
    if(NOT "${bad_hits}" MATCHES "sub/b.cpp:3: std::cin")
        list(APPEND failures "line numbers are wrong: expected sub/b.cpp:3: std::cin in '${bad_hits}'")
    endif()
    if(clean_hits)
        list(APPEND failures "false positives in the clean fixture: ${clean_hits}")
    endif()
    if(failures)
        string(REPLACE ";" "\n  " text "${failures}")
        message(FATAL_ERROR "core.NoUiSelfTest failed:\n  ${text}")
    endif()
    list(LENGTH bad_hits n)
    message(STATUS "self-test passed: ${n} hits in the bad fixture, none in the clean one")
    return()
endif()

if(NOT SCAN_DIR)
    message(FATAL_ERROR "give -DSCAN_DIR=<directory to scan>")
endif()
rfh_scan("${SCAN_DIR}" hits)
if(hits)
    string(REPLACE ";" "\n  " text "${hits}")
    message(FATAL_ERROR "UI toolkit, MFC or process-exit mention in the core:\n  ${text}")
endif()
message(STATUS "no UI mentions under ${SCAN_DIR}")
