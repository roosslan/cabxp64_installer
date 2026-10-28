#include "install.h"

#include <algorithm>
#include <ctime>
#include <set>

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include "../common/cabfile.h"
#include "../common/fsutil.h"
#include "../common/icons.h"
#include "../common/log.h"
#include "../common/sif.h"

namespace {

const char *const kLogSignature = "$XPSetupLog$";

HKEY rootKey(const std::string &root)
{
    return fsu::upper(root) == "HKLM" ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

std::string field(const std::vector<std::string> &f, size_t i)
{
    return i < f.size() ? f[i] : std::string();
}

bool regGetString(HKEY root, const std::string &key, const std::string &name, std::string &out)
{
    wchar_t buf[2048];
    DWORD size = sizeof(buf);
    DWORD type = 0;
    if (RegGetValueW(root, fsu::widen(key).c_str(), fsu::widen(name).c_str(),
                     RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &type, buf, &size) != ERROR_SUCCESS)
        return false;
    out = fsu::narrow(buf);
    return true;
}

bool regSetString(HKEY key, const std::string &name, const std::string &value, DWORD type = REG_SZ)
{
    std::wstring v = fsu::widen(value);
    return RegSetValueExW(key, fsu::widen(name).c_str(), 0, type, (const BYTE *) v.c_str(),
                          (DWORD) ((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool regSetDword(HKEY key, const std::string &name, DWORD value)
{
    return RegSetValueExW(key, fsu::widen(name).c_str(), 0, REG_DWORD, (const BYTE *) &value,
                          sizeof(value)) == ERROR_SUCCESS;
}

// Директории от path вверх, которых ещё нет на диске (самая глубокая первой).
void missingDirs(const std::string &path, std::vector<std::string> &out)
{
    std::string d = path;
    while (!d.empty() && !fsu::isRootDir(d) && !fsu::dirExists(d))
    {
        if (std::find(out.begin(), out.end(), d) == out.end())
            out.push_back(d);
        std::string parent = fsu::dirName(d);
        if (parent == d)
            break;
        d = parent;
    }
}

// Распаковка одного архива с пересчётом прогресса на весь дистрибутив.
class CopyProgress : public CabProgress
{
public:
    CopyProgress(InstallJob &job, InstallProgress *ui, const std::string &dest, uint64_t base,
                 uint64_t total) :
        job(job), ui(ui), dest(dest), base(base), total(total)
    {
    }

    bool onFile(const std::string &name) override
    {
        std::string target = cabTargetPath(dest, name, true);
        missingDirs(fsu::dirName(target), job.log.dirs);
        if (std::find(job.log.files.begin(), job.log.files.end(), target) == job.log.files.end())
            job.log.files.push_back(target);
        xlog::write("  " + target);
        if (ui)
            ui->onStatus("Копирование: " + fsu::baseName(name));
        return true;
    }

    bool onBytes(uint64_t done, uint64_t) override
    {
        if (ui && total)
            ui->onPercent((int) ((base + done) * 100 / total));
        return true;
    }

private:
    InstallJob &job;
    InstallProgress *ui;
    std::string dest;
    uint64_t base, total;
};

// Удаление установленного при неудачной установке.
void rollback(InstallJob &job)
{
    xlog::write("Откат: удаление скопированных файлов (" + std::to_string(job.log.files.size()) + ")");
    for (const std::string &f : job.log.files)
    {
        fsu::clearReadOnly(f);
        fsu::removeFile(f);
    }
    std::vector<std::string> dirs = job.log.dirs;
    std::sort(dirs.begin(), dirs.end(), [](const std::string &a, const std::string &b) {
        return a.size() > b.size();
    });
    for (const std::string &d : dirs)
        RemoveDirectoryW(fsu::widen(d).c_str());
    job.log.files.clear();
    job.log.dirs.clear();
}

bool createShortcut(const std::string &lnk, const std::string &target, const std::string &args,
                    const std::string &workDir)
{
    IShellLinkW *link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                (void **) &link)))
        return false;
    link->SetPath(fsu::widen(target).c_str());
    link->SetArguments(fsu::widen(args).c_str());
    link->SetWorkingDirectory(fsu::widen(workDir.empty() ? fsu::dirName(target) : workDir).c_str());
    IPersistFile *file = nullptr;
    bool ok = false;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void **) &file)))
    {
        ok = SUCCEEDED(file->Save(fsu::widen(lnk).c_str(), TRUE));
        file->Release();
    }
    link->Release();
    return ok;
}

bool copyStub(const std::string &from, uint64_t size, const std::string &to, std::string &err)
{
    std::string data;
    if (!fsu::readFile(from, data, 256u << 20))
    {
        err = "Не удалось прочитать " + from;
        return false;
    }
    if (size && size < data.size())
        data.resize((size_t) size);
    fsu::clearReadOnly(to);
    FILE *f = _wfopen(fsu::widen(to).c_str(), L"wb");
    if (!f)
    {
        err = "Не удалось создать " + to + "\n" + fsu::lastErrorText();
        return false;
    }
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = fclose(f) == 0 && ok;
    if (!ok)
        err = "Ошибка записи " + to;
    return ok;
}

uint64_t dirSizeKb(const std::vector<std::string> &files)
{
    uint64_t total = 0;
    for (const std::string &f : files)
        total += fsu::fileSize(f);
    return (total + 1023) / 1024;
}

} // namespace

// ---------------------------------------------------------------------------
// Журнал
// ---------------------------------------------------------------------------

bool InstallLog::load(const std::string &path, std::string &err)
{
    Sif sif;
    if (!sif.load(path, err))
        return false;
    if (sif.get("Version", "Signature") != kLogSignature)
    {
        err = path + "\nФайл не является журналом установки";
        return false;
    }
    *this = InstallLog();
    name = sif.get("Product", "Name");
    version = sif.get("Product", "Version");
    publisher = sif.get("Product", "Publisher");
    productCode = sif.get("Product", "ProductCode");
    installDir = sif.get("Product", "InstallDir");
    perMachine = fsu::upper(sif.get("Product", "Scope")) == "MACHINE";
    for (const std::string &l : sif.list("Files"))
        files.push_back(sifUnquote(l));
    for (const std::string &l : sif.list("Directories"))
        dirs.push_back(sifUnquote(l));
    for (const std::string &l : sif.list("Registry"))
    {
        std::vector<std::string> f = sifSplit(l);
        RegistryValue v;
        v.root = field(f, 0);
        v.key = field(f, 1);
        v.name = field(f, 2);
        values.push_back(v);
    }
    for (const std::string &l : sif.list("RegistryKeys"))
        keys.push_back(sifUnquote(l));
    for (const std::string &l : sif.list("Shortcuts"))
        shortcuts.push_back(sifUnquote(l));
    for (const std::string &l : sif.list("KillProcesses"))
        killProcesses.push_back(sifUnquote(l));
    return true;
}

bool InstallLog::save(const std::string &path, std::string &err) const
{
    Sif sif;
    sif.set("Version", "Signature", std::string("\"") + kLogSignature + "\"");
    sif.set("Product", "Name", sifQuote(name));
    sif.set("Product", "Version", version);
    sif.set("Product", "Publisher", sifQuote(publisher));
    sif.set("Product", "ProductCode", productCode);
    sif.set("Product", "InstallDir", sifQuote(installDir));
    sif.set("Product", "Scope", perMachine ? "machine" : "user");
    sif.addSection("Files");
    for (const std::string &f : files)
        sif.add("Files", sifQuote(f));
    sif.addSection("Directories");
    for (const std::string &d : dirs)
        sif.add("Directories", sifQuote(d));
    sif.addSection("Registry");
    for (const RegistryValue &v : values)
        sif.add("Registry", sifJoin({v.root, v.key, v.name}));
    sif.addSection("RegistryKeys");
    for (const std::string &k : keys)
        sif.add("RegistryKeys", sifQuote(k));
    sif.addSection("Shortcuts");
    for (const std::string &s : shortcuts)
        sif.add("Shortcuts", sifQuote(s));
    sif.addSection("KillProcesses");
    for (const std::string &k : killProcesses)
        sif.add("KillProcesses", sifQuote(k));
    return sif.save(path, err);
}

// ---------------------------------------------------------------------------
// Вспомогательное
// ---------------------------------------------------------------------------

std::string uninstallKeyPath(const std::string &productCode)
{
    return "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + productCode;
}

ExistingInstall findExisting(const SetupProject &p)
{
    ExistingInstall e;
    HKEY root = p.perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    std::string key = uninstallKeyPath(p.productCode);
    std::string dir;
    if (!regGetString(root, key, "InstallLocation", dir) || dir.empty())
        return e;
    // Запись без журнала (например, оставшаяся после ручного удаления
    // файлов) прежней установкой не считается.
    if (!fsu::fileExists(fsu::joinPath(dir, kLogName)))
        return e;
    e.found = true;
    e.dir = dir;
    regGetString(root, key, "DisplayVersion", e.version);
    return e;
}

bool isElevated()
{
    return IsUserAnAdmin() != FALSE;
}

uint64_t payloadSize(const SetupProject &p, const std::string &distDir)
{
    uint64_t total = 0;
    for (const Component &c : p.components)
    {
        CabInfo info;
        std::string err;
        if (cabRead(fsu::joinPath(distDir, c.cabinet), info, err))
            total += info.totalSize;
    }
    return total;
}

void killProcesses(const std::vector<std::string> &names, InstallProgress *progress)
{
    if (names.empty())
        return;
    DWORD mySession = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &mySession);
    for (const std::string &name : names)
    {
        if (progress)
            progress->onStatus("Остановка: " + name);
        std::wstring wname = fsu::widen(name);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            continue;
        PROCESSENTRY32W pe = {sizeof(pe)};
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        {
            if (_wcsicmp(pe.szExeFile, wname.c_str()) != 0)
                continue;
            // Процессы других сеансов не трогаем: у пользователя нет на это прав.
            DWORD session = 0;
            if (!ProcessIdToSessionId(pe.th32ProcessID, &session) || session != mySession)
                continue;
            HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
            std::string what = name + " (PID " + std::to_string(pe.th32ProcessID) + ")";
            if (!h)
            {
                xlog::lastError("Не удалось открыть процесс " + what);
                continue;
            }
            if (!TerminateProcess(h, 1))
                xlog::lastError("Не удалось завершить процесс " + what);
            else if (WaitForSingleObject(h, 5000) != WAIT_OBJECT_0)
                xlog::error("Процесс " + what + " не завершился за 5 с");
            else
                xlog::write("Завершён процесс " + what);
            CloseHandle(h);
        }
        CloseHandle(snap);
    }
}

// ---------------------------------------------------------------------------
// Установка
// ---------------------------------------------------------------------------

bool installFiles(InstallJob &job, InstallProgress *progress, std::string &err)
{
    const SetupProject &p = job.project;
    job.log = InstallLog();
    job.log.name = p.name;
    job.log.version = p.version;
    job.log.publisher = p.publisher;
    job.log.productCode = p.productCode;
    job.log.installDir = job.installDir;
    job.log.perMachine = p.perMachine;
    job.log.killProcesses = p.killProcesses;

    xlog::write("Установка " + p.name + " " + p.version + " в " + job.installDir);
    xlog::write("Дистрибутив: " + job.distDir);
    if (progress)
        progress->onPercent(0);
    killProcesses(p.killProcesses, progress);

    // Прежняя версия удаляется по её журналу; запись в «Программах и
    // компонентах» перезапишет новая установка.
    if (job.existing.found)
    {
        InstallLog old;
        std::string lerr;
        xlog::write("Удаление прежней версии " + job.existing.version + " из " + job.existing.dir);
        if (!old.load(fsu::joinPath(job.existing.dir, kLogName), lerr))
            xlog::error("Журнал прежней установки не прочитан: " + lerr);
        else
        {
            killProcesses(old.killProcesses, progress);
            uninstall(old, progress, true, lerr);
            // Созданное прежней установкой удалит деинсталляция новой версии.
            job.log.dirs = old.dirs;
            job.log.keys = old.keys;
        }
    }

    missingDirs(job.installDir, job.log.dirs);
    if (!fsu::makeDirs(job.installDir))
    {
        err = "Программа установки не может создать директорию\n" + job.installDir + "\n" +
              fsu::lastErrorText();
        xlog::error(err);
        rollback(job);
        return false;
    }

    uint64_t total = payloadSize(p, job.distDir);
    uint64_t base = 0;
    for (const Component &c : p.components)
    {
        std::string cab = fsu::joinPath(job.distDir, c.cabinet);
        std::string dest = directoryPath(p, c.directory, job.installDir);
        CabInfo info;
        std::string cerr;
        if (!cabRead(cab, info, cerr))
        {
            err = "Программа установки не может найти файл " + c.cabinet + "\n" + cerr;
            xlog::error(err);
            rollback(job);
            return false;
        }
        xlog::write("Компонент " + c.id + ": " + c.cabinet + " -> " + dest + " (файлов: " +
                    std::to_string(info.entries.size()) + ")");
        for (;;)
        {
            missingDirs(dest, job.log.dirs);
            if (!fsu::makeDirs(dest))
                cerr = "Не удалось создать директорию " + dest + "\n" + fsu::lastErrorText();
            else
            {
                CopyProgress cp(job, progress, dest, base, total);
                if (cabExtract(cab, dest, {}, true, Overwrite::Always, &cp, cerr))
                    break;
            }
            xlog::error("Компонент " + c.id + ": " + cerr);
            if (progress && progress->askRetry(cerr))
            {
                xlog::write("Повтор распаковки " + c.cabinet);
                continue;
            }
            err = cerr;
            rollback(job);
            return false;
        }
        base += info.totalSize;
    }
    if (progress)
        progress->onPercent(100);
    return true;
}

bool installConfig(InstallJob &job, InstallProgress *progress, std::string &err)
{
    const SetupProject &p = job.project;
    int steps = (int) (p.registry.size() + p.shortcuts.size()) + 3;
    int step = 0;
    auto advance = [&](const std::string &status) {
        if (progress)
        {
            progress->onStatus(status);
            progress->onPercent(++step * 100 / steps);
        }
    };

    for (const RegistryValue &r : p.registry)
    {
        advance("Реестр: " + (r.name.empty() ? fsu::baseName(r.key) : r.name));
        HKEY root = rootKey(r.root);
        HKEY key = nullptr;
        std::string path = expandVars(r.key, job.installDir, p.perMachine);
        // RegCreateKeyEx создаёт и недостающие родительские разделы: в журнал
        // попадают все, которых не было (при удалении — только пустые).
        for (size_t end = 0; end != std::string::npos;)
        {
            end = path.find('\\', end + 1);
            std::string part = path.substr(0, end);
            HKEY probe = nullptr;
            if (RegOpenKeyExW(root, fsu::widen(part).c_str(), 0, KEY_READ, &probe) == ERROR_SUCCESS)
            {
                RegCloseKey(probe);
                continue;
            }
            std::string logged = fsu::upper(r.root) + "\\" + part;
            if (std::find(job.log.keys.begin(), job.log.keys.end(), logged) == job.log.keys.end())
                job.log.keys.push_back(logged);
        }
        if (RegCreateKeyExW(root, fsu::widen(path).c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                            &key, nullptr) != ERROR_SUCCESS)
        {
            err = "Программа установки не может создать раздел реестра\n" + r.root + "\\" + path;
            xlog::error(err);
            return false;
        }
        std::string value = expandVars(r.value, job.installDir, p.perMachine);
        bool ok;
        if (r.type == "REG_DWORD")
            ok = regSetDword(key, r.name, (DWORD) strtoul(value.c_str(), nullptr, 0));
        else
            ok = regSetString(key, r.name, value, r.type == "REG_EXPAND_SZ" ? REG_EXPAND_SZ : REG_SZ);
        RegCloseKey(key);
        if (!ok)
        {
            err = "Программа установки не может записать значение реестра\n" + r.root + "\\" + path +
                  "\\" + r.name;
            xlog::error(err);
            return false;
        }
        xlog::write("Реестр: " + r.root + "\\" + path + " : " + r.name + " = " + value);
        RegistryValue entry = r;
        entry.key = path;
        job.log.values.push_back(entry);
    }

    for (const Shortcut &s : p.shortcuts)
    {
        advance("Ярлык: " + fsu::baseName(s.name));
        const char *var = s.place == ShortcutPlace::Programs ? "%PROGRAMS%"
                        : s.place == ShortcutPlace::Startup  ? "%STARTUP%"
                                                             : "%DESKTOP%";
        std::string lnk = fsu::joinPath(expandVars(var, job.installDir, p.perMachine), s.name + ".lnk");
        missingDirs(fsu::dirName(lnk), job.log.dirs);
        fsu::makeDirs(fsu::dirName(lnk));
        if (!createShortcut(lnk, expandVars(s.target, job.installDir, p.perMachine),
                            expandVars(s.args, job.installDir, p.perMachine),
                            expandVars(s.workDir, job.installDir, p.perMachine)))
        {
            err = "Программа установки не может создать ярлык\n" + lnk;
            xlog::error(err);
            return false;
        }
        xlog::write("Ярлык: " + lnk);
        job.log.shortcuts.push_back(lnk);
    }

    advance("Программа удаления");
    std::string uninst = fsu::joinPath(job.installDir, kUninstallerName);
    if (!copyStub(job.selfPath, job.selfStubSize, uninst, err))
    {
        xlog::error(err);
        return false;
    }
    xlog::write("Программа удаления: " + uninst);
    // Проводник показывает первую группу иконок: без группы установки у
    // uninst.exe видна иконка удаления. Ошибка здесь установку не прерывает.
    if (!iconGroup(uninst, kUninstallIconGroup).empty())
    {
        std::string ierr;
        if (!setIconGroup(uninst, kSetupIconGroup, std::string(), ierr))
            xlog::error(ierr);
    }

    advance("Журнал установки");
    std::string logPath = fsu::joinPath(job.installDir, kLogName);
    if (!job.log.save(logPath, err))
    {
        xlog::error(err);
        return false;
    }

    advance("Регистрация программы");
    HKEY root = p.perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, fsu::widen(uninstallKeyPath(p.productCode)).c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        err = "Программа установки не может зарегистрировать программу в системе";
        xlog::error(err);
        return false;
    }
    std::string icon = uninst + ",0";
    for (const Shortcut &s : p.shortcuts)
    {
        icon = expandVars(s.target, job.installDir, p.perMachine) + ",0";
        break;
    }
    char date[16];
    time_t t = time(nullptr);
    strftime(date, sizeof(date), "%Y%m%d", localtime(&t));
    regSetString(key, "DisplayName", p.name);
    regSetString(key, "DisplayVersion", p.version);
    regSetString(key, "Publisher", p.publisher);
    regSetString(key, "InstallLocation", job.installDir);
    regSetString(key, "DisplayIcon", icon);
    regSetString(key, "UninstallString", "\"" + uninst + "\" /uninstall");
    regSetString(key, "InstallDate", date);
    regSetDword(key, "EstimatedSize", (DWORD) dirSizeKb(job.log.files));
    regSetDword(key, "NoModify", 1);
    regSetDword(key, "NoRepair", 1);
    RegCloseKey(key);
    xlog::write(std::string("Регистрация: ") + (p.perMachine ? "HKLM\\" : "HKCU\\") +
                uninstallKeyPath(p.productCode));

    if (progress)
        progress->onPercent(100);
    return true;
}

void runAfterInstall(const InstallJob &job)
{
    const SetupProject &p = job.project;
    for (const RunEntry &r : p.runAfter)
    {
        std::string prog = expandVars(r.program, job.installDir, p.perMachine);
        std::string args = expandVars(r.args, job.installDir, p.perMachine);
        HINSTANCE r = ShellExecuteW(nullptr, L"open", fsu::widen(prog).c_str(),
                                    args.empty() ? nullptr : fsu::widen(args).c_str(),
                                    fsu::widen(fsu::dirName(prog)).c_str(), SW_SHOWNORMAL);
        if ((INT_PTR) r > 32)
            xlog::write("Запущено: " + prog + (args.empty() ? "" : " " + args));
        else
            xlog::error("Не удалось запустить " + prog + " (код " + std::to_string((INT_PTR) r) + ")");
    }
}

// ---------------------------------------------------------------------------
// Удаление
// ---------------------------------------------------------------------------

bool uninstall(const InstallLog &log, InstallProgress *progress, bool keepRegistration,
               std::string &err)
{
    std::string uninst = fsu::joinPath(log.installDir, kUninstallerName);
    std::string logPath = fsu::joinPath(log.installDir, kLogName);

    size_t steps = log.files.size() + log.shortcuts.size() + log.values.size() + 2;
    size_t step = 0;
    auto advance = [&](const std::string &status) {
        if (progress)
        {
            progress->onStatus(status);
            progress->onPercent((int) (++step * 100 / steps));
        }
    };

    xlog::write("Удаление " + log.name + " " + log.version + " из " + log.installDir +
                (keepRegistration ? " (обновление)" : ""));
    std::vector<std::string> failed;
    for (const std::string &f : log.files)
    {
        advance("Удаление: " + fsu::baseName(f));
        if (!fsu::fileExists(f))
            continue;
        fsu::clearReadOnly(f);
        if (fsu::removeFile(f))
            xlog::write("  удалён " + f);
        else
        {
            xlog::lastError("  не удалён " + f);
            // Занятый файл удаляется при следующей перезагрузке (если хватает прав).
            if (MoveFileExW(fsu::widen(f).c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
                xlog::write("  будет удалён при перезагрузке");
            else
                failed.push_back(f);
        }
    }
    for (const std::string &s : log.shortcuts)
    {
        advance("Ярлык: " + fsu::baseName(s));
        fsu::removeFile(s);
    }
    for (const RegistryValue &v : log.values)
    {
        advance("Реестр: " + (v.name.empty() ? fsu::baseName(v.key) : v.name));
        HKEY key = nullptr;
        if (RegOpenKeyExW(rootKey(v.root), fsu::widen(v.key).c_str(), 0, KEY_SET_VALUE, &key) ==
            ERROR_SUCCESS)
        {
            RegDeleteValueW(key, fsu::widen(v.name).c_str());
            RegCloseKey(key);
        }
    }
    // Созданные установкой разделы удаляются, только если они пусты.
    std::vector<std::string> keys = log.keys;
    std::sort(keys.begin(), keys.end(), [](const std::string &a, const std::string &b) {
        return a.size() > b.size();
    });
    for (const std::string &k : keys)
    {
        size_t slash = k.find('\\');
        if (slash == std::string::npos)
            continue;
        HKEY root = rootKey(k.substr(0, slash));
        std::wstring path = fsu::widen(k.substr(slash + 1));
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        DWORD subkeys = 0, values = 0;
        RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values,
                         nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(key);
        if (subkeys == 0 && values == 0)
            RegDeleteKeyW(root, path.c_str());
    }

    advance("Регистрация программы");
    if (!keepRegistration)
    {
        HKEY root = log.perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        RegDeleteKeyW(root, fsu::widen(uninstallKeyPath(log.productCode)).c_str());
        fsu::removeFile(logPath);
        fsu::removeFile(uninst);
        fsu::removeFile(fsu::joinPath(log.installDir, kSetupLogName));
        xlog::write("Регистрация в системе удалена");
    }

    advance("Удаление директорий");
    std::vector<std::string> dirs = log.dirs;
    std::sort(dirs.begin(), dirs.end(), [](const std::string &a, const std::string &b) {
        return a.size() > b.size();
    });
    for (const std::string &d : dirs)
        RemoveDirectoryW(fsu::widen(d).c_str());
    if (!keepRegistration)
        RemoveDirectoryW(fsu::widen(log.installDir).c_str());

    if (progress)
        progress->onPercent(100);
    xlog::write(failed.empty() ? std::string("Удаление завершено")
                               : "Удаление завершено, не удалено файлов: " + std::to_string(failed.size()));
    if (!failed.empty())
    {
        err = "Не удалось удалить файлы:";
        for (size_t i = 0; i < failed.size() && i < 5; ++i)
            err += "\n" + failed[i];
        if (failed.size() > 5)
            err += "\n...";
        return false;
    }
    return true;
}
