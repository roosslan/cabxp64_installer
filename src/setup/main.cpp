// Программа установки в стиле текстового режима Windows XP.
//
//   setup.exe                  установка из дистрибутива (данные внутри exe
//                              либо setup.sif и CAB-архивы рядом)
//   uninst.exe /uninstall      удаление установленной программы

#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include "../common/cabfile.h"
#include "../common/fsutil.h"
#include "../common/log.h"
#include "../common/project.h"
#include "install.h"
#include "screen.h"
#include "ui.h"

namespace {

const char *const kSifName = "setup.sif";
const char *const kConhostFlag = "/conhost";
const char *const kUninstallFlag = "/uninstall";
const char *const kUninstallRunFlag = "/uninstall-run";
const char *const kLogFlag = "/log";
const char *const kDefaultLog = "%TEMP%\\XPSetup.log";

bool hasArg(const std::vector<std::string> &args, const char *flag)
{
    for (const std::string &a : args)
        if (_stricmp(a.c_str(), flag) == 0)
            return true;
    return false;
}

std::string argAfter(const std::vector<std::string> &args, const char *flag)
{
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (_stricmp(args[i].c_str(), flag) == 0)
            return args[i + 1];
    return {};
}

std::string quoteArg(const std::string &a)
{
    if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos)
        return a;
    std::string r = "\"";
    for (char c : a)
        r += c == '"' ? std::string("\\\"") : std::string(1, c);
    return r + "\"";
}

std::string joinArgs(const std::vector<std::string> &args)
{
    std::string r;
    for (const std::string &a : args)
        r += (r.empty() ? "" : " ") + quoteArg(a);
    return r;
}

bool startProcess(const std::string &exe, const std::string &args, DWORD flags)
{
    std::wstring cmd = fsu::widen(quoteArg(exe) + (args.empty() ? "" : " " + args));
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// Windows Terminal не поддерживает растровый шрифт и смену палитры:
// программа перезапускается в классическом окне консоли.
bool relaunchInConhost(const std::vector<std::string> &args)
{
    wchar_t wt[8];
    if (GetEnvironmentVariableW(L"WT_SESSION", wt, 8) == 0 || hasArg(args, kConhostFlag))
        return false;
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::string conhost = fsu::joinPath(fsu::narrow(sys), "conhost.exe");
    std::vector<std::string> a = args;
    a.push_back(kConhostFlag);
    return startProcess(conhost, quoteArg(fsu::exePath()) + " " + joinArgs(a), DETACHED_PROCESS);
}

bool relaunchElevated(const std::vector<std::string> &args)
{
    std::wstring exe = fsu::widen(fsu::exePath());
    std::wstring params = fsu::widen(joinArgs(args));
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

std::string readLicense(const std::string &path)
{
    std::string data;
    if (!fsu::readFile(path, data, 4u << 20))
        return {};
    if (data.size() >= 3 && (unsigned char) data[0] == 0xEF && (unsigned char) data[1] == 0xBB &&
        (unsigned char) data[2] == 0xBF)
        data.erase(0, 3);
    if (!fsu::isValidUtf8(data))
        data = fsu::fromCodePage(data, 1251);
    return data;
}

// Удаление временной копии программы удаления после её завершения.
void deleteSelfLater(const std::string &tempDir)
{
    std::string cmd = "/c ping -n 3 127.0.0.1 >nul & del /f /q " + quoteArg(fsu::exePath()) +
                      " & rd /s /q " + quoteArg(tempDir);
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    startProcess(fsu::joinPath(fsu::narrow(sys), "cmd.exe"), cmd, CREATE_NO_WINDOW);
}

// Распаковка данных, дописанных к setup.exe, во временную директорию.
class LoadProgress : public CabProgress
{
public:
    explicit LoadProgress(const std::string &title) : title(title) {}
    bool onFile(const std::string &name) override
    {
        ui::loading(title, name);
        Sleep(150);     // как у оригинала: имена файлов успевают прочитать
        return true;
    }

private:
    std::string title;
};

// ---------------------------------------------------------------------------
// Удаление
// ---------------------------------------------------------------------------

int runUninstall(const std::string &dir, const std::vector<std::string> &args)
{
    InstallLog log;
    std::string err;
    std::string logPath = fsu::joinPath(dir, kLogName);
    if (!log.load(logPath, err))
    {
        if (scr::init("Удаление программы"))
        {
            ui::error("Удаление программы", "Не найден журнал установки:\n" + err);
            scr::restore();
        }
        return 1;
    }
    xlog::write("Журнал установки: " + logPath + " (" + log.name + " " + log.version + ")");
    if (log.perMachine && !isElevated())
    {
        xlog::write("Перезапуск с правами администратора");
        return relaunchElevated(args) ? 0 : 1;
    }

    std::string title = "Удаление " + log.name;
    if (!scr::init(title))
        return 1;
    int code = 0;
    if (ui::confirmUninstall(log))
    {
        scr::lockClose(true);
        ui::ProgressScreen screen(title,
                                  "Подождите, пока программа удаления удалит файлы " + log.name +
                                      " с этого компьютера.",
                                  "Программа удаления удаляет файлы:");
        killProcesses(log.killProcesses, &screen);
        bool ok = uninstall(log, &screen, false, err);
        scr::lockClose(false);
        std::string text = "   " + log.name + " удалена с этого компьютера.\n";
        if (!ok)
        {
            text += "\n   Некоторые файлы удалить не удалось, они будут удалены после\n"
                    "   перезагрузки или их необходимо удалить вручную.\n";
            code = 1;
        }
        text += "\n   Чтобы завершить работу программы удаления, нажмите <ВВОД>.";
        ui::finish(title, text, "Программа удаления будет завершена через {time}...",
                   "ВВОД=Завершить");
    }
    else
    {
        xlog::write("Удаление отменено пользователем");
        code = 2;
    }
    scr::restore();
    return code;
}

int uninstallMode(const std::vector<std::string> &args)
{
    // Временная копия получает директорию установки параметром.
    std::string runDir = argAfter(args, kUninstallRunFlag);
    if (!runDir.empty())
    {
        int code = runUninstall(runDir, args);
        deleteSelfLater(fsu::dirName(fsu::exePath()));
        return code;
    }

    // uninst.exe не может удалить сам себя: работу выполняет его копия во
    // временной директории, а этот процесс сразу завершается.
    std::string dir = fsu::dirName(fsu::exePath());
    std::string temp = fsu::makeTempDir();
    std::string copy = fsu::joinPath(temp, kUninstallerName);
    if (temp.empty() || !CopyFileW(fsu::widen(fsu::exePath()).c_str(), fsu::widen(copy).c_str(), FALSE))
        return runUninstall(dir, args);     // запасной вариант: без самоудаления
    std::vector<std::string> a = {kUninstallFlag, kUninstallRunFlag, dir};
    if (hasArg(args, kConhostFlag))
        a.push_back(kConhostFlag);
    if (!argAfter(args, kLogFlag).empty())
        a.insert(a.end(), {kLogFlag, argAfter(args, kLogFlag)});
    xlog::write("Запуск временной копии программы удаления: " + copy);
    return startProcess(copy, joinArgs(a), 0) ? 0 : runUninstall(dir, args);
}

// ---------------------------------------------------------------------------
// Установка
// ---------------------------------------------------------------------------

int installMode(const std::vector<std::string> &args)
{
    std::string self = fsu::exePath();
    const std::string loadingTitle = "Программа установки";
    if (!scr::init(loadingTitle))
        return 1;

    ui::checkingHardware();
    ui::loading(loadingTitle, kSifName);

    // Данные внутри exe (один файл) или рядом с ним (директория).
    std::string distDir = fsu::dirName(self);
    std::string tempDir;
    uint64_t stubSize = 0;
    CabInfo selfInfo;
    std::string err;
    if (cabRead(self, selfInfo, err) && selfInfo.offset > 0)
    {
        tempDir = fsu::makeTempDir();
        LoadProgress lp(loadingTitle);
        if (tempDir.empty() ||
            !cabExtract(self, tempDir, {}, true, Overwrite::Always, &lp, err))
        {
            ui::error(loadingTitle, "Программа установки не может загрузить файлы.\n" + err);
            scr::restore();
            if (!tempDir.empty())
                fsu::removeTree(tempDir);
            return 1;
        }
        distDir = tempDir;
        stubSize = selfInfo.offset;
        xlog::write("Данные внутри программы установки распакованы в " + tempDir);
    }
    else
        xlog::write("Дистрибутив в директории " + distDir);
    auto cleanup = [&]() {
        scr::restore();
        if (!tempDir.empty())
            fsu::removeTree(tempDir);
    };

    InstallJob job;
    if (!loadProject(fsu::joinPath(distDir, kSifName), job.project, err))
    {
        ui::error(loadingTitle, "Программа установки не может прочитать описание установки.\n" + err);
        cleanup();
        return 1;
    }
    SetupProject &p = job.project;
    std::string vmsg = validateProject(p);
    if (!vmsg.empty())
    {
        ui::error(loadingTitle, "Описание установки повреждено.\n" + vmsg);
        cleanup();
        return 1;
    }
    xlog::write("Продукт: " + p.name + " " + p.version + " " + p.productCode +
                (p.perMachine ? ", для всех пользователей" : ", для текущего пользователя"));
    if (p.perMachine && !isElevated())
    {
        xlog::write("Перезапуск с правами администратора");
        cleanup();
        return relaunchElevated(args) ? 0 : 1;
    }

    // Один экземпляр на продукт.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, fsu::widen("Local\\XPSetup-" + p.productCode).c_str());
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        ui::error(p.title(), "Программа установки " + p.name + " уже запущена.");
        cleanup();
        return 1;
    }

    for (const Component &c : p.components)
    {
        ui::loading(p.title(), c.cabinet);
        if (!fsu::fileExists(fsu::joinPath(distDir, c.cabinet)))
        {
            ui::error(p.title(), "Программа установки не может найти файл " + c.cabinet +
                                     ".\nДистрибутив неполон.");
            cleanup();
            return 1;
        }
        Sleep(150);
    }
    std::string licenseText = p.license.empty() ? std::string()
                                                : readLicense(fsu::joinPath(distDir, p.license));
    fsu::setConsoleTitleAndIcon(p.title());

    job.distDir = distDir;
    job.selfPath = self;
    job.selfStubSize = stubSize;
    job.existing = findExisting(p);
    job.installDir = job.existing.found ? job.existing.dir : expandVars(p.installDir, std::string(), p.perMachine);
    uint64_t need = payloadSize(p, distDir);
    if (job.existing.found)
        xlog::write("Найдена установленная версия " + job.existing.version + " в " + job.existing.dir);

    bool go = ui::welcome(p.name) &&
              (licenseText.empty() || ui::license(p.name, licenseText)) &&
              (job.existing.found ? ui::existing(p, job.existing)
                                  : ui::chooseDirectory(p, job.installDir, need));
    if (!go)
    {
        xlog::write("Установка отменена пользователем");
        cleanup();
        return 2;
    }
    xlog::write("Директория установки: " + job.installDir);

    scr::lockClose(true);
    {
        ui::ProgressScreen screen(p.title(),
                                  "Подождите, пока программа установки завершит копирование файлов в "
                                  "директории установки " + p.name +
                                      ". Для этого может потребоваться несколько минут.",
                                  "Программа установки копирует файлы:", true);
        if (!installFiles(job, &screen, err))
        {
            scr::lockClose(false);
            ui::error(p.title(), p.name + " не установлена.\n\n" + err);
            cleanup();
            return 1;
        }
    }
    {
        ui::ProgressScreen screen(p.title(),
                                  "Подождите, пока программа установки сохранит параметры " + p.name + ".",
                                  "Программа установки инициализирует конфигурацию:");
        if (!installConfig(job, &screen, err))
        {
            scr::lockClose(false);
            ui::error(p.title(), "Файлы " + p.name + " скопированы, но настройка не завершена.\n\n" + err +
                                     "\n\nДля исправления необходимо запустить программу установки повторно.");
            cleanup();
            return 1;
        }
    }
    scr::lockClose(false);
    xlog::write("Установка завершена: файлов " + std::to_string(job.log.files.size()));
    // Копия журнала этого сеанса остаётся рядом с программой (удаляется вместе с ней).
    xlog::saveSession(fsu::joinPath(job.installDir, kSetupLogName));

    std::string text = "   Установка " + p.name + " успешно завершена.\n";
    if (!p.runAfter.empty())
        text += "\n   После выхода из программы установки будет запущена программа\n   " +
                fsu::baseName(expandVars(p.runAfter.front().program, job.installDir, p.perMachine)) + ".\n";
    text += "\n   Чтобы завершить работу программы установки, нажмите <ВВОД>.";
    ui::finish(p.title(), text, "Программа установки будет завершена через {time}...", "ВВОД=Завершить");

    cleanup();
    runAfterInstall(job);
    if (mutex)
        CloseHandle(mutex);
    return 0;
}

} // namespace

int main()
{
    std::vector<std::string> args = fsu::commandLineArgs();   // без имени программы
    if (relaunchInConhost(args))
        return 0;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool removing = hasArg(args, kUninstallFlag);
    std::string logPath = argAfter(args, kLogFlag);
    if (logPath.empty())
        logPath = expandVars(kDefaultLog, std::string(), false);
    xlog::open(logPath, removing ? "Удаление программы" : "Установка программы");
    int code = removing ? uninstallMode(args) : installMode(args);
    xlog::write("Код завершения: " + std::to_string(code));
    xlog::close();
    CoUninitialize();
    return code;
}
