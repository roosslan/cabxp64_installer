#include "log.h"

#include <cstdio>

#include <windows.h>
#include <shlobj.h>

#include "fsutil.h"
#include "version.h"

namespace xlog {

namespace {

FILE *g_file = nullptr;
std::string g_path;
long g_sessionStart = 0;

std::string stamp()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u ", t.wYear, t.wMonth, t.wDay,
             t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}

std::string windowsVersion()
{
    // GetVersionEx без манифеста совместимости занижает версию; номер
    // сборки берётся из реестра.
    wchar_t build[32] = L"", name[128] = L"";
    DWORD size = sizeof(build);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuild",
                 RRF_RT_REG_SZ, nullptr, build, &size);
    size = sizeof(name);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName",
                 RRF_RT_REG_SZ, nullptr, name, &size);
    return fsu::narrow(name) + ", сборка " + fsu::narrow(build);
}

void line(const std::string &text)
{
    if (!g_file)
        return;
    std::string s = stamp() + text + "\r\n";
    fwrite(s.data(), 1, s.size(), g_file);
    fflush(g_file);
}

} // namespace

bool open(const std::string &path, const std::string &program)
{
    close();
    fsu::makeDirs(fsu::dirName(path));
    bool fresh = !fsu::fileExists(path) || fsu::fileSize(path) == 0;
    g_file = _wfopen(fsu::widen(path).c_str(), L"ab");
    if (!g_file)
        return false;
    g_path = path;
    if (fresh)
        fwrite("\xEF\xBB\xBF", 1, 3, g_file);
    else
        fwrite("\r\n", 1, 2, g_file);
    g_sessionStart = ftell(g_file);
    std::string args;
    for (const std::string &a : fsu::commandLineArgs())
        args += " " + a;
    line("=== " + program + " (XPSetup " XPSETUP_VERSION ")");
    line("Программа: " + fsu::exePath() + args);
    line("Windows: " + windowsVersion() + (IsUserAnAdmin() ? ", права администратора" : ", без прав администратора"));
    return true;
}

void close()
{
    if (g_file)
        fclose(g_file);
    g_file = nullptr;
}

const std::string &path()
{
    return g_path;
}

void write(const std::string &msg)
{
    line(msg);
}

void error(const std::string &msg)
{
    std::string m = msg;
    for (size_t p = m.find('\n'); p != std::string::npos; p = m.find('\n', p + 3))
        m.replace(p, 1, "\r\n    ");
    line("ОШИБКА: " + m);
}

void lastError(const std::string &msg)
{
    error(msg + ": " + fsu::lastErrorText());
}

bool saveSession(const std::string &to)
{
    if (!g_file)
        return false;
    fflush(g_file);
    std::string data;
    if (!fsu::readFile(g_path, data, 64u << 20) || (size_t) g_sessionStart > data.size())
        return false;
    data = "\xEF\xBB\xBF" + data.substr((size_t) g_sessionStart);
    FILE *f = _wfopen(fsu::widen(to).c_str(), L"wb");
    if (!f)
        return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    return fclose(f) == 0 && ok;
}

} // namespace xlog
