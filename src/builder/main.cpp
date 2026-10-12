// Сборщик дистрибутивов XPSetup.
//
//   xpsbuild.exe [проект.xsp]                     редактор проекта (Turbo Vision)
//   xpsbuild.exe --build проект.xsp [параметры]   сборка без интерфейса (для CI)

#include <cstdio>
#include <string>
#include <vector>

#include "../common/format.h"
#include "../common/fsutil.h"
#include "../common/log.h"
#include "../common/project.h"
#include "../common/version.h"
#include "app.h"
#include "build.h"

namespace {

const char *const kUsage =
    "XPSetup " XPSETUP_VERSION " — сборщик дистрибутивов\n"
    XPSETUP_COPYRIGHT "\n"
    "\n"
    "xpsbuild.exe [проект.xsp]                 редактор проекта\n"
    "xpsbuild.exe --build проект.xsp [параметры]\n"
    "                                          сборка без интерфейса\n"
    "\n"
    "Параметры сборки:\n"
    "  --mode folder|single|both               вид дистрибутива (по умолчанию из проекта)\n"
    "  --out <директория>                      директория результата\n"
    "  --version <версия>                      версия продукта, например 9.5.123.26\n"
    "  --stub <setup.exe>                      другая программа установки вместо встроенной\n";

// Ход сборки в консоли: этапы и каждый 10-й процент.
class ConsoleProgress : public CabProgress
{
public:
    void onStage(const std::string &text) override
    {
        fsu::printConsole(text + "\n");
        lastPercent = -1;
    }

    bool onBytes(uint64_t done, uint64_t total) override
    {
        int p = total ? (int) (done * 100 / total) : 0;
        if (p / 10 != lastPercent / 10)
        {
            fsu::printConsole("  " + std::to_string(p) + "%\n");
            lastPercent = p;
        }
        return true;
    }

private:
    int lastPercent = -1;
};

int batchBuild(const std::vector<std::string> &args)
{
    std::string projectPath, mode, out, version, stubPath;
    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string &a = args[i];
        auto value = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--mode")
            mode = value();
        else if (a == "--out")
            out = value();
        else if (a == "--version")
            version = value();
        else if (a == "--stub")
            stubPath = value();
        else if (projectPath.empty())
            projectPath = a;
        else
        {
            fsu::printConsole("Неизвестный параметр: " + a + "\n\n" + kUsage);
            return 2;
        }
    }
    if (projectPath.empty())
    {
        fsu::printConsole(kUsage);
        return 2;
    }

    SetupProject p;
    std::string err;
    if (!loadProject(projectPath, p, err))
    {
        fsu::printConsole("Ошибка: " + err + "\n");
        return 1;
    }
    if (!mode.empty())
    {
        if (mode == "folder")
            p.mode = BuildMode::Folder;
        else if (mode == "single")
            p.mode = BuildMode::Single;
        else if (mode == "both")
            p.mode = BuildMode::Both;
        else
        {
            fsu::printConsole("Неизвестный вид дистрибутива: " + mode + "\n");
            return 2;
        }
    }
    if (!out.empty())
        p.outputDir = fsu::fullPath(out);
    if (!version.empty())
        p.version = version;

    std::string stub;
    if (!stubPath.empty() ? !fsu::readFile(stubPath, stub, 64u << 20) : !loadSetupStub(stub, err))
    {
        fsu::printConsole("Ошибка: " + (stubPath.empty() ? err : "не удалось прочитать " + stubPath) + "\n");
        return 1;
    }

    ConsoleProgress progress;
    BuildResult result;
    if (!buildDistribution(p, projectPath, stub, &progress, result, err))
    {
        fsu::printConsole("Ошибка: " + err + "\n");
        return 1;
    }
    fsu::printConsole("Готово: " + pluralFiles(result.files) + ", " + formatSize(result.bytes) + "\n");
    for (const std::string &o : result.outputs)
        fsu::printConsole("  " + o + "\n");
    return 0;
}

} // namespace

int main()
{
    std::vector<std::string> args = fsu::commandLineArgs();
    xlog::open(expandVars("%APPDATA%\\XPSetup\\xpsbuild.log", std::string(), false), "Сборщик");
    if (!args.empty())
    {
        const std::string &opt = args[0];
        if (opt == "-h" || opt == "--help" || opt == "/?")
        {
            fsu::printConsole(kUsage);
            return 0;
        }
        if (opt == "--build")
            return batchBuild(args);
    }

    fsu::setConsoleTitleAndIcon("XPSetup " XPSETUP_VERSION);
    TBuilderApp *app = new TBuilderApp;
    if (!args.empty())
        app->openLater(fsu::fullPath(args[0]));
    app->run();
    TObject::destroy(app);
    return 0;
}
