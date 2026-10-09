#include "project.h"

#include <cstdio>
#include <cstdlib>

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

#include "fsutil.h"
#include "sif.h"

namespace {

const char *const kSignature = "$XPSetup$";

std::string compressionText(const CompressionSpec &c)
{
    switch (c.type)
    {
        case CabCompression::None:  return "NONE";
        case CabCompression::MSZIP: return "MSZIP";
        default:                    return "LZX:" + std::to_string(c.lzxWindow);
    }
}

CompressionSpec compressionParse(const std::string &s)
{
    CompressionSpec c;
    std::string u = fsu::upper(s);
    if (u == "NONE")
        c.type = CabCompression::None;
    else if (u.compare(0, 3, "LZX") == 0)
    {
        c.type = CabCompression::LZX;
        if (u.size() > 4 && u[3] == ':')
        {
            int w = atoi(u.c_str() + 4);
            if (w >= 15 && w <= 21)
                c.lzxWindow = w;
        }
    }
    return c;
}

const char *modeText(BuildMode m)
{
    return m == BuildMode::Single ? "single" : m == BuildMode::Both ? "both" : "folder";
}

BuildMode modeParse(const std::string &s)
{
    std::string u = fsu::upper(s);
    return u == "SINGLE" ? BuildMode::Single : u == "BOTH" ? BuildMode::Both : BuildMode::Folder;
}

std::string field(const std::vector<std::string> &f, size_t i)
{
    return i < f.size() ? f[i] : std::string();
}

std::string knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)))
        r = fsu::narrow(p);
    CoTaskMemFree(p);
    return r;
}

std::string envVar(const std::string &name)
{
    std::wstring w = fsu::widen(name);
    DWORD n = GetEnvironmentVariableW(w.c_str(), nullptr, 0);
    if (n == 0)
        return {};
    std::wstring buf(n, L'\0');
    n = GetEnvironmentVariableW(w.c_str(), &buf[0], n);
    buf.resize(n);
    return fsu::narrow(buf);
}

} // namespace

const char *placeName(ShortcutPlace place)
{
    switch (place)
    {
        case ShortcutPlace::Programs: return "Programs";
        case ShortcutPlace::Startup:  return "Startup";
        default:                      return "Desktop";
    }
}

ShortcutPlace placeFromName(const std::string &name)
{
    std::string u = fsu::upper(name);
    if (u == "PROGRAMS")
        return ShortcutPlace::Programs;
    if (u == "STARTUP")
        return ShortcutPlace::Startup;
    return ShortcutPlace::Desktop;
}

const char *placeTitle(ShortcutPlace place)
{
    switch (place)
    {
        case ShortcutPlace::Programs: return "Меню «Пуск»";
        case ShortcutPlace::Startup:  return "Автозагрузка";
        default:                      return "Рабочий стол";
    }
}

bool loadProject(const std::string &path, SetupProject &p, std::string &err)
{
    Sif sif;
    if (!sif.load(path, err))
        return false;
    if (sif.get("Version", "Signature") != kSignature)
    {
        err = path + "\nФайл не является описанием установки (нет подписи " + kSignature + ")";
        return false;
    }
    p = SetupProject();
    p.name = sif.get("Product", "Name");
    p.version = sif.get("Product", "Version", p.version);
    p.publisher = sif.get("Product", "Publisher");
    p.productCode = sif.get("Product", "ProductCode");
    p.installDir = sif.get("Product", "InstallDir");
    p.allowChangeDir = sif.getInt("Product", "AllowChangeDir", 1) != 0;
    p.perMachine = fsu::upper(sif.get("Product", "Scope", "user")) == "MACHINE";
    p.license = sif.get("Product", "License");

    if (const Sif::Section *s = sif.section("Directories"))
        for (const Sif::Line &l : s->lines)
            if (!l.key.empty())
                p.directories[atoi(l.key.c_str())] = sifUnquote(l.value);

    for (const std::string &line : sif.list("Components"))
    {
        std::string id = sifUnquote(line);
        if (id.empty())
            continue;
        std::string sec = "Component." + id;
        Component c;
        c.id = id;
        c.description = sif.get(sec, "Description");
        c.cabinet = sif.get(sec, "Cabinet", id + ".cab");
        c.directory = sif.getInt(sec, "Directory", 1);
        c.source = sif.get(sec, "Source");
        c.include = sif.get(sec, "Include", "*.*");
        c.exclude = sif.get(sec, "Exclude");
        c.recurse = sif.getInt(sec, "Recurse", 1) != 0;
        p.components.push_back(c);
    }

    for (const std::string &line : sif.list("Shortcuts"))
    {
        std::vector<std::string> f = sifSplit(line);
        Shortcut s;
        s.place = placeFromName(field(f, 0));
        s.name = field(f, 1);
        s.target = field(f, 2);
        s.args = field(f, 3);
        s.workDir = field(f, 4);
        if (!s.name.empty())
            p.shortcuts.push_back(s);
    }

    for (const std::string &line : sif.list("Registry"))
    {
        std::vector<std::string> f = sifSplit(line);
        RegistryValue r;
        r.root = fsu::upper(field(f, 0));
        r.key = field(f, 1);
        r.name = field(f, 2);
        r.type = fsu::upper(field(f, 3));
        r.value = field(f, 4);
        if (r.type.empty())
            r.type = "REG_SZ";
        if (!r.key.empty())
            p.registry.push_back(r);
    }

    for (const std::string &line : sif.list("KillProcesses"))
        if (!sifUnquote(line).empty())
            p.killProcesses.push_back(sifUnquote(line));

    for (const std::string &line : sif.list("RunAfterInstall"))
    {
        std::vector<std::string> f = sifSplit(line);
        RunEntry r{field(f, 0), field(f, 1)};
        if (!r.program.empty())
            p.runAfter.push_back(r);
    }

    p.outputDir = sif.get("Build", "Output", p.outputDir);
    p.singleFile = sif.get("Build", "SingleFile");
    p.mode = modeParse(sif.get("Build", "Mode", "folder"));
    p.compression = compressionParse(sif.get("Build", "Compression", "MSZIP"));
    return true;
}

bool saveProject(const std::string &path, const SetupProject &p, bool forSetup, std::string &err)
{
    Sif sif;
    sif.set("Version", "Signature", std::string("\"") + kSignature + "\"");
    sif.set("Version", "SifVersion", "1");

    sif.set("Product", "Name", sifQuote(p.name));
    sif.set("Product", "Version", p.version);
    sif.set("Product", "Publisher", sifQuote(p.publisher));
    sif.set("Product", "ProductCode", p.productCode);
    sif.set("Product", "InstallDir", sifQuote(p.installDir));
    sif.set("Product", "AllowChangeDir", p.allowChangeDir ? "1" : "0");
    sif.set("Product", "Scope", p.perMachine ? "machine" : "user");
    sif.set("Product", "License", sifQuote(forSetup && !p.license.empty() ? "license.txt" : p.license));

    sif.addSection("Directories");
    for (const auto &d : p.directories)
        sif.set("Directories", std::to_string(d.first), sifQuote(d.second));

    sif.addSection("Components");
    for (const Component &c : p.components)
        sif.add("Components", c.id);
    for (const Component &c : p.components)
    {
        std::string sec = "Component." + c.id;
        sif.set(sec, "Description", sifQuote(c.description));
        sif.set(sec, "Cabinet", sifQuote(c.cabinet));
        sif.set(sec, "Directory", std::to_string(c.directory));
        if (!forSetup)
        {
            sif.set(sec, "Source", sifQuote(c.source));
            sif.set(sec, "Include", sifQuote(c.include));
            sif.set(sec, "Exclude", sifQuote(c.exclude));
            sif.set(sec, "Recurse", c.recurse ? "1" : "0");
        }
    }

    sif.addSection("Shortcuts");
    for (const Shortcut &s : p.shortcuts)
        sif.add("Shortcuts", sifJoin({placeName(s.place), s.name, s.target, s.args, s.workDir}));

    sif.addSection("Registry");
    for (const RegistryValue &r : p.registry)
        sif.add("Registry", sifJoin({r.root, r.key, r.name, r.type, r.value}));

    sif.addSection("KillProcesses");
    for (const std::string &k : p.killProcesses)
        sif.add("KillProcesses", sifQuote(k));

    sif.addSection("RunAfterInstall");
    for (const RunEntry &r : p.runAfter)
        sif.add("RunAfterInstall", sifJoin({r.program, r.args}));

    if (!forSetup)
    {
        sif.set("Build", "Output", sifQuote(p.outputDir));
        sif.set("Build", "SingleFile", sifQuote(p.singleFile));
        sif.set("Build", "Mode", modeText(p.mode));
        sif.set("Build", "Compression", compressionText(p.compression));
    }

    // Пустые значения в кавычках ("") для читаемости убираем.
    for (Sif::Section &s : sif.sections)
        for (Sif::Line &l : s.lines)
            if (l.value == "\"\"")
                l.value.clear();

    return sif.save(path, err);
}

std::string validateProject(const SetupProject &p)
{
    if (p.name.empty())
        return "Не указано название продукта.";
    if (p.productCode.empty())
        return "Не указан код продукта.";
    if (p.installDir.empty())
        return "Не указана директория установки.";
    if (p.components.empty())
        return "Не задано ни одного компонента.";
    for (const Component &c : p.components)
    {
        if (c.cabinet.empty())
            return "У компонента " + c.id + " не указано имя CAB.";
        if (c.directory != 0 && !p.directories.count(c.directory))
            return "Компонент " + c.id + " ссылается на несуществующую директорию " +
                   std::to_string(c.directory) + ".";
    }
    for (size_t i = 0; i < p.components.size(); ++i)
        for (size_t j = i + 1; j < p.components.size(); ++j)
        {
            if (fsu::upper(p.components[i].id) == fsu::upper(p.components[j].id))
                return "Повторяется идентификатор компонента " + p.components[i].id + ".";
            if (fsu::upper(p.components[i].cabinet) == fsu::upper(p.components[j].cabinet))
                return "Повторяется имя CAB " + p.components[i].cabinet + ".";
        }
    return {};
}

std::string newGuid()
{
    GUID g;
    if (FAILED(CoCreateGuid(&g)))
        return {};
    char buf[64];
    snprintf(buf, sizeof(buf), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
             (unsigned long) g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
             g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

int compareVersions(const std::string &a, const std::string &b)
{
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size())
    {
        long x = 0, y = 0;
        while (i < a.size() && a[i] != '.')
            x = x * 10 + (isdigit((unsigned char) a[i]) ? a[i] - '0' : 0), ++i;
        while (j < b.size() && b[j] != '.')
            y = y * 10 + (isdigit((unsigned char) b[j]) ? b[j] - '0' : 0), ++j;
        if (x != y)
            return x < y ? -1 : 1;
        ++i, ++j;
    }
    return 0;
}

std::string expandVars(const std::string &s, const std::string &installDir, bool perMachine)
{
    std::string r;
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t a = s.find('%', pos);
        size_t b = a == std::string::npos ? a : s.find('%', a + 1);
        if (b == std::string::npos)
        {
            r += s.substr(pos);
            break;
        }
        r += s.substr(pos, a - pos);
        std::string name = s.substr(a + 1, b - a - 1);
        std::string u = fsu::upper(name);
        std::string v;
        bool known = true;
        if (u == "INSTALLDIR")
            v = installDir;
        else if (u == "APPDATA")
            v = knownFolder(FOLDERID_RoamingAppData);
        else if (u == "LOCALAPPDATA")
            v = knownFolder(FOLDERID_LocalAppData);
        else if (u == "PROGRAMFILES")
            v = knownFolder(FOLDERID_ProgramFiles);
        else if (u == "PROGRAMFILES(X86)")
            v = knownFolder(FOLDERID_ProgramFilesX86);
        else if (u == "PROGRAMDATA" || u == "COMMONAPPDATA")
            v = knownFolder(FOLDERID_ProgramData);
        else if (u == "USERPROFILE")
            v = knownFolder(FOLDERID_Profile);
        else if (u == "DOCUMENTS")
            v = knownFolder(FOLDERID_Documents);
        else if (u == "DESKTOP")
            v = knownFolder(perMachine ? FOLDERID_PublicDesktop : FOLDERID_Desktop);
        else if (u == "PROGRAMS")
            v = knownFolder(perMachine ? FOLDERID_CommonPrograms : FOLDERID_Programs);
        else if (u == "STARTUP")
            v = knownFolder(perMachine ? FOLDERID_CommonStartup : FOLDERID_Startup);
        else if (u == "WINDIR")
            v = knownFolder(FOLDERID_Windows);
        else if (u == "SYSTEM")
            v = knownFolder(FOLDERID_System);
        else if (u == "TEMP")
        {
            wchar_t buf[MAX_PATH + 1];
            DWORD n = GetTempPathW(MAX_PATH + 1, buf);
            v = n ? fsu::narrow(std::wstring(buf, n)) : std::string();
            while (!v.empty() && v.back() == '\\')
                v.pop_back();
        }
        else if (!name.empty())
        {
            v = envVar(name);
            known = !v.empty();
        }
        else
            known = false;

        if (known)
        {
            r += v;
            pos = b + 1;
        }
        else
        {
            // Неизвестная переменная остаётся как есть; закрывающий % может
            // оказаться началом следующей.
            r += '%';
            pos = a + 1;
        }
    }
    return r;
}

std::string directoryPath(const SetupProject &p, int id, const std::string &installDir)
{
    if (id == 0)
        return installDir;
    auto it = p.directories.find(id);
    if (it == p.directories.end())
        return {};
    return expandVars(it->second, installDir, p.perMachine);
}
