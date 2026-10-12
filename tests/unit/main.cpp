// Модульные и интеграционные тесты XPSetup.
//
//   unittests.exe [подстрока имени теста]

#include <cstdio>
#include <exception>
#include <stdexcept>

#include <windows.h>
#include <objbase.h>

#include "../../src/common/fsutil.h"
#include "test.h"

namespace test {

namespace {
int g_failures = 0;
const char *g_current = "";
}

std::vector<Case> &registry()
{
    static std::vector<Case> cases;
    return cases;
}

void report(const char *file, int line, const std::string &expr, const std::string &detail)
{
    ++g_failures;
    std::string f = file;
    size_t slash = f.find_last_of("\\/");
    fsu::printConsole("  ОШИБКА [" + std::string(g_current) + "] " + f.substr(slash + 1) + ":" +
                      std::to_string(line) + ": " + expr + (detail.empty() ? "" : "  " + detail) + "\n");
}

TempDir::TempDir() : dir(fsu::makeTempDir())
{
    if (dir.empty())
        throw std::runtime_error("не создана временная директория");
}

TempDir::~TempDir()
{
    fsu::removeTree(dir);
}

std::string TempDir::file(const std::string &rel) const
{
    return fsu::joinPath(dir, rel);
}

void writeFile(const std::string &path, const std::string &data)
{
    fsu::makeDirs(fsu::dirName(path));
    FILE *f = _wfopen(fsu::widen(path).c_str(), L"wb");
    if (!f)
        throw std::runtime_error("не создан файл " + path);
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

std::string realStub()
{
    return readFile(fsu::exePath());
}

std::string readFile(const std::string &path)
{
    std::string data;
    fsu::readFile(path, data, 256u << 20);
    return data;
}

} // namespace test

int main()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<std::string> args = fsu::commandLineArgs();
    std::string filter = args.empty() ? std::string() : args[0];

    int run = 0, failedTests = 0;
    for (const test::Case &c : test::registry())
    {
        if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos)
            continue;
        test::g_current = c.name;
        int before = test::g_failures;
        try
        {
            c.fn();
        }
        catch (const test::Abort &)
        {
        }
        catch (const std::exception &e)
        {
            test::report(__FILE__, __LINE__, "исключение", e.what());
        }
        ++run;
        bool ok = test::g_failures == before;
        if (!ok)
            ++failedTests;
        fsu::printConsole(std::string(ok ? "  ok    " : "  СБОЙ  ") + c.name + "\n");
    }
    fsu::printConsole("\nТестов: " + std::to_string(run) + ", с ошибками: " + std::to_string(failedTests) +
                      ", проверок не прошло: " + std::to_string(test::g_failures) + "\n");
    CoUninitialize();
    return failedTests == 0 && run > 0 ? 0 : 1;
}
