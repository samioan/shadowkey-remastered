#include "platform/win32/console_tee.h"

#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <thread>

namespace sk {

namespace {

// Reads whatever the process writes to stdout/stderr (redirected into
// pipeRead below) and fans each chunk out to both the real console handle
// (consoleHandle, duplicated before redirection so it stays valid) and
// the log file -- a real OS-level tee, not a `freopen`. Runs for the
// whole process lifetime; there's nothing to join on a normal game exit
// (the OS tears the thread down with the rest of the process), so
// StartConsoleTeeLog() below detaches it.
void TeeThreadMain(HANDLE pipeRead, HANDLE consoleHandle, HANDLE logFile) {
    char buffer[4096];
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(pipeRead, buffer, sizeof(buffer), &bytesRead, nullptr) || bytesRead == 0) {
            break;  // pipe closed (process exiting) -- nothing left to tee
        }
        DWORD written = 0;
        if (consoleHandle != INVALID_HANDLE_VALUE) {
            WriteFile(consoleHandle, buffer, bytesRead, &written, nullptr);
        }
        if (logFile != INVALID_HANDLE_VALUE) {
            WriteFile(logFile, buffer, bytesRead, &written, nullptr);
            FlushFileBuffers(logFile);
        }
    }
}

}  // namespace

bool StartConsoleTeeLog(const std::string& logPath) {
    // Preserve wherever stdout currently points (a real console window in
    // the normal double-click/launch case, but this also does the right
    // thing if the caller already redirected stdout themselves, e.g.
    // `shadowkey_port.exe > somewhere.txt`) -- DuplicateHandle before
    // SetStdHandle repoints STD_OUTPUT_HANDLE out from under it, below.
    //
    // M113: failing to duplicate it is no longer fatal. When the launcher
    // starts the game with CREATE_NO_WINDOW there is no console at all, so
    // STD_OUTPUT_HANDLE is null and this call fails -- and the old code
    // gave up on the log entirely at exactly the moment the log matters
    // most, since a shipped player has no console to read instead. The tee
    // thread already treats INVALID_HANDLE_VALUE as "write to the file
    // only", so carrying on costs nothing.
    HANDLE process = GetCurrentProcess();
    HANDLE originalStdout = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(process, GetStdHandle(STD_OUTPUT_HANDLE), process, &originalStdout, 0,
                          FALSE, DUPLICATE_SAME_ACCESS)) {
        originalStdout = INVALID_HANDLE_VALUE;
    }

    HANDLE logFile = CreateFileA(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logFile == INVALID_HANDLE_VALUE) {
        std::printf("StartConsoleTeeLog: could not open %s for writing -- log disabled\n",
                    logPath.c_str());
        CloseHandle(originalStdout);
        return false;
    }

    // A generous buffer (this game only ever prints modest, bursty debug
    // text, never anything close to this) -- keeps ReadFile's consumer
    // thread from ever having to race a producer that could otherwise
    // block on a full pipe.
    HANDLE pipeRead = nullptr, pipeWrite = nullptr;
    if (!CreatePipe(&pipeRead, &pipeWrite, nullptr, 1 << 20)) {
        std::printf("StartConsoleTeeLog: CreatePipe failed -- log disabled\n");
        CloseHandle(originalStdout);
        CloseHandle(logFile);
        return false;
    }

    // Point the CRT's stdout/stderr FILE* streams (every existing
    // std::printf/std::fprintf(stderr, ...) call site in this codebase)
    // at the pipe's write end, unbuffered so text reaches the tee thread
    // -- and so both the console and the log -- promptly rather than
    // sitting in a libc buffer.
    int pipeWriteFd = _open_osfhandle(reinterpret_cast<intptr_t>(pipeWrite), _O_TEXT);
    if (pipeWriteFd == -1 || _dup2(pipeWriteFd, _fileno(stdout)) != 0 ||
        _dup2(pipeWriteFd, _fileno(stderr)) != 0) {
        std::printf("StartConsoleTeeLog: could not redirect stdout/stderr -- log disabled\n");
        CloseHandle(originalStdout);
        CloseHandle(logFile);
        CloseHandle(pipeRead);
        CloseHandle(pipeWrite);
        return false;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    std::thread(TeeThreadMain, pipeRead, originalStdout, logFile).detach();
    std::printf("StartConsoleTeeLog: also logging this session to %s\n", logPath.c_str());
    return true;
}

}  // namespace sk
