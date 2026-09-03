#include "platform/win32/exe_dir.h"

#include <windows.h>

namespace sk {

std::string ExecutableDirectory() {
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return ".";
    }
    std::string full(path, len);
    size_t slash = full.find_last_of("\\/");
    if (slash == std::string::npos) {
        return ".";
    }
    return full.substr(0, slash);
}

}  // namespace sk
