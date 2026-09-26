// Test executables report fatal errors to their captured stderr and fail the
// process. A modal CRT/WER dialog must not leave an unattended test suspended.
// Application executables do not link this translation unit.
#include <windows.h>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace {
#ifdef _DEBUG
int reportFailure(int type, char* message, int* result) {
    if (type != _CRT_ASSERT && type != _CRT_ERROR) return FALSE;
    if (result) *result = 0;
    std::fputs("Fatal test CRT diagnostic: ", stderr);
    if (message) std::fputs(message, stderr);
    std::fflush(stderr);
    std::_Exit(3);
}
#endif
struct TestProcessDiagnostics {
    TestProcessDiagnostics() {
        SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(_WRITE_ABORT_MSG, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, reportFailure);
#endif
        std::set_terminate([] {
            std::fputs("Fatal test termination: uncaught exception or noexcept violation.\n", stderr);
            std::fflush(stderr);
            std::_Exit(3);
        });
    }
};
const TestProcessDiagnostics configureTestProcess;
}
