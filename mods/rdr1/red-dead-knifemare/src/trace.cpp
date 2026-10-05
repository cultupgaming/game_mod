#include "trace.h"

#include <cstddef>
#include <cstdio>
#include <cwchar>

namespace
{
    // Public release default: do not create or flush RedDeadKnifemare.trace.log.
    // Flip this only for a dedicated diagnostic build.
    constexpr bool TRACE_ENABLED = false;
    constexpr const wchar_t* TRACE_FILE_NAME = L"RedDeadKnifemare.trace.log";
    HANDLE g_traceFile = INVALID_HANDLE_VALUE;

    bool Open(const wchar_t* path)
    {
        g_traceFile = CreateFileW(
            path,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
            nullptr);

        return g_traceFile != INVALID_HANDLE_VALUE;
    }

    bool OpenBesideModule(HMODULE moduleHandle)
    {
        wchar_t path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(moduleHandle, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return false;

        DWORD end = length;
        while (end > 0 && path[end - 1] != L'\\' && path[end - 1] != L'/')
            --end;

        if (end == 0)
            return false;

        const int written = std::swprintf(
            path + end,
            MAX_PATH - end,
            L"%ls",
            TRACE_FILE_NAME);
        if (written < 0)
            return false;

        return Open(path);
    }

    bool OpenInTemp()
    {
        wchar_t directory[MAX_PATH] = {};
        const DWORD length = GetTempPathW(MAX_PATH, directory);
        if (length == 0 || length >= MAX_PATH)
            return false;

        wchar_t path[MAX_PATH] = {};
        const int written = std::swprintf(
            path,
            MAX_PATH,
            L"%ls%ls",
            directory,
            TRACE_FILE_NAME);
        if (written < 0)
            return false;

        return Open(path);
    }
}

void InitTrace(HMODULE moduleHandle)
{
    if (!TRACE_ENABLED)
        return;

    if (g_traceFile != INVALID_HANDLE_VALUE)
        return;

    const char* location = "beside the ASI";
    if (!OpenBesideModule(moduleHandle))
    {
        location = "%TEMP%";
        if (!OpenInTemp())
            return;
    }

    char header[256] = {};
    std::snprintf(
        header,
        sizeof(header),
        "--- Red Dead Knifemare, built " __DATE__ " " __TIME__ ", trace %s ---",
        location);
    WriteTrace(header);
}

void WriteTrace(const char* text)
{
    if (!TRACE_ENABLED)
        return;

    if (g_traceFile == INVALID_HANDLE_VALUE)
        return;

    // Execution geometry/forensic samples exceed 768 bytes. Preserve the
    // trailing link/protection fields and newline instead of truncating them.
    char line[2048] = {};
    const int formatted = std::snprintf(
        line,
        sizeof(line),
        "[%llu] %s\r\n",
        static_cast<unsigned long long>(GetTickCount64()),
        text);
    if (formatted <= 0)
        return;

    const std::size_t count =
        static_cast<std::size_t>(formatted) < sizeof(line)
        ? static_cast<std::size_t>(formatted)
        : sizeof(line) - 1;

    DWORD written = 0;
    WriteFile(g_traceFile, line, static_cast<DWORD>(count), &written, nullptr);
    FlushFileBuffers(g_traceFile);
}

void Trace(const char* step)
{
    WriteTrace(step);
}

void Trace(const char* step, long long value)
{
    if (!TRACE_ENABLED)
        return;

    char line[384] = {};
    std::snprintf(line, sizeof(line), "%s = %lld", step, value);
    WriteTrace(line);
}

void TraceFloat(const char* step, float value)
{
    if (!TRACE_ENABLED)
        return;

    char line[384] = {};
    std::snprintf(line, sizeof(line), "%s = %.3f", step, static_cast<double>(value));
    WriteTrace(line);
}
