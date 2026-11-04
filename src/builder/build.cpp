#include "build.h"

#include <cstdio>
#include <algorithm>

#include <windows.h>

#include "../common/format.h"
#include "../common/fsutil.h"
#include "../common/icons.h"
#include "../common/log.h"
#include "../common/sif.h"
#include "builderres.h"

namespace {

const char *const kSetupExe = "setup.exe";
const char *const kSifName = "setup.sif";
const char *const kLicenseName = "license.txt";

bool writeFile(const std::string &path, const std::string &data, std::string &err)
{
    fsu::clearReadOnly(path);
    FILE *f = _wfopen(fsu::widen(path).c_str(), L"wb");
    if (!f)
    {
        err = "Не удалось создать файл " + path + "\n" + fsu::lastErrorText();
        return false;
    }
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = fclose(f) == 0 && ok;
    if (!ok)
        err = "Ошибка записи файла " + path;
    return ok;
}

void walk(const std::string &root, const std::string &rel, const Component &c, ComponentFiles &out)
{
    std::vector<fsu::DirItem> items;
    if (!fsu::listDirectory(rel.empty() ? root : fsu::joinPath(root, rel), items))
        return;
    for (const fsu::DirItem &it : items)
    {
        std::string relPath = rel.empty() ? it.name : rel + "\\" + it.name;
        if (!c.exclude.empty() && (fsu::wildMatch(c.exclude, it.name) || fsu::wildMatch(c.exclude, relPath)))
            continue;
        if (it.isDir)
        {
            if (c.recurse)
                walk(root, relPath, c, out);
        }
        else if (fsu::wildMatch(c.include.empty() ? "*" : c.include, it.name))
        {
            out.files.push_back({fsu::joinPath(root, relPath), relPath});
            out.bytes += it.size;
        }
    }
}

// Директория дистрибутива: setup.sif, лицензия и CAB-архивы.
bool stageFolder(const SetupProject &p, const std::string &projectDir, const std::string &dir,
                 const std::string *stub, CabProgress *progress, BuildResult &result,
                 std::vector<std::string> &layout, std::string &err)
{
    if (!fsu::makeDirs(dir))
    {
        err = "Не удалось создать директорию " + dir + "\n" + fsu::lastErrorText();
        return false;
    }

    for (const Component &c : p.components)
    {
        if (progress)
            progress->onStage("Компонент " + c.id + ": поиск файлов");
        ComponentFiles files;
        if (!collectComponent(c, projectDir, files, err))
            return false;
        if (progress)
            progress->onStage("Компонент " + c.id + ": " + c.cabinet);
        std::string cab = fsu::joinPath(dir, c.cabinet);
        if (!cabCreate(cab, files.files, p.compression, progress, err))
            return false;
        xlog::write("  " + c.cabinet + ": " + pluralFiles(files.files.size()) + ", " + formatSize(files.bytes) +
                    " -> " + formatSize(fsu::fileSize(cab)));
        layout.push_back(cab);
        result.files += files.files.size();
        result.bytes += files.bytes;
    }

    if (!p.license.empty())
    {
        std::string src;
        if (!expandBuildPath(p.license, src, err))
            return false;
        if (!fsu::isAbsolutePath(src))
            src = fsu::joinPath(projectDir, src);
        std::string text;
        if (!fsu::readFile(src, text, 4u << 20))
        {
            err = "Не удалось прочитать файл лицензии " + src;
            return false;
        }
        std::string dst = fsu::joinPath(dir, kLicenseName);
        if (!writeFile(dst, text, err))
            return false;
        layout.push_back(dst);
    }

    std::string sif = fsu::joinPath(dir, kSifName);
    if (!saveProject(sif, p, true, err))
        return false;
    layout.push_back(sif);

    if (stub)
    {
        std::string exe = fsu::joinPath(dir, kSetupExe);
        if (!writeFile(exe, *stub, err))
            return false;
    }
    return true;
}

// Сначала полностью собираем результат; затем публикуем только свои файлы.
bool buildFolder(const SetupProject &p, const std::string &projectDir, const std::string &dir,
                 const std::string *stub, CabProgress *progress, BuildResult &result,
                 std::vector<std::string> &layout, std::string &err)
{
    struct Temp {
        std::string path = fsu::makeTempDir();
        bool retain = false;
        ~Temp() { if (!retain && !path.empty()) fsu::removeTree(path); }
    } stage, backup;
    if (stage.path.empty() || backup.path.empty()) { err = "Не удалось создать временную директорию"; return false; }
    // Выходной каталог не должен пересекаться с источниками компонентов.
    const std::string output = fsu::upper(fsu::withSlash(fsu::fullPath(dir)));
    for (const auto &c : p.components)
    {
        std::string source;
        if (!expandBuildPath(c.source, source, err)) return false;
        if (!fsu::isAbsolutePath(source)) source = fsu::joinPath(projectDir, source);
        std::string root = fsu::upper(fsu::withSlash(fsu::fullPath(source)));
        if (output.compare(0, root.size(), root) == 0 || root.compare(0, output.size(), output) == 0)
        { err = "Директория результата пересекается с источником компонента " + c.id; return false; }
    }
    std::vector<std::string> staged;
    if (!stageFolder(p, projectDir, stage.path, stub, progress, result, staged, err)) return false;
    std::vector<std::string> names;
    for (const auto &path : staged) names.push_back(fsu::baseName(path));
    if (stub) names.push_back(kSetupExe);
    std::vector<std::string> affected = names;
    SetupProject previous;
    std::string ignored;
    if (loadProject(fsu::joinPath(dir, kSifName), previous, ignored) && previous.productCode == p.productCode)
    {
        for (const auto &c : previous.components)
            if (c.cabinet == fsu::baseName(c.cabinet)) affected.push_back(c.cabinet);
        if (!previous.license.empty()) affected.push_back(kLicenseName);
    }
    std::sort(affected.begin(), affected.end());
    affected.erase(std::unique(affected.begin(), affected.end()), affected.end());
    if (!fsu::makeDirs(dir)) { err = "Не удалось создать директорию " + dir; return false; }
    for (const auto &name : affected)
    {
        std::string dst = fsu::joinPath(dir, name);
        if (fsu::fileExists(dst) && !CopyFileW(fsu::widen(dst).c_str(), fsu::widen(fsu::joinPath(backup.path, name)).c_str(), TRUE))
        { err = "Не удалось сохранить прежний результат " + dst; return false; }
    }
    bool ok = true;
    for (const auto &name : affected)
    {
        std::string dst = fsu::joinPath(dir, name), src = fsu::joinPath(stage.path, name);
        if (fsu::fileExists(src))
        {
            fsu::clearReadOnly(dst);
            if (!CopyFileW(fsu::widen(src).c_str(), fsu::widen(dst).c_str(), FALSE)) { ok = false; break; }
        }
        else if (fsu::fileExists(dst) && !fsu::removeFile(dst)) { ok = false; break; }
    }
    if (!ok)
    {
        err = "Не удалось опубликовать дистрибутив в " + dir;
        for (const auto &name : affected)
        {
            std::string dst = fsu::joinPath(dir, name), src = fsu::joinPath(backup.path, name);
            if (fsu::fileExists(src))
            {
                fsu::clearReadOnly(dst);
                if (!CopyFileW(fsu::widen(src).c_str(), fsu::widen(dst).c_str(), FALSE)) backup.retain = true;
            }
            else if (fsu::fileExists(dst) && !fsu::removeFile(dst)) backup.retain = true;
        }
        if (backup.retain) err += "\nРезервные копии: " + backup.path;
        return false;
    }
    for (const auto &path : staged) layout.push_back(fsu::joinPath(dir, fsu::baseName(path)));
    return true;
}

} // namespace

std::string singleFileName(const SetupProject &p)
{
    if (!p.singleFile.empty())
        return p.singleFile;
    std::string name = p.name.empty() ? "product" : p.name;
    for (char &c : name)
        if (strchr("\\/:*?\"<>|", c))
            c = '_';
    return name + "_setup.exe";
}

bool expandBuildPath(const std::string &path, std::string &out, std::string &err)
{
    out = expandVars(path, std::string(), false);
    size_t a = out.find('%');
    size_t b = a == std::string::npos ? a : out.find('%', a + 1);
    if (b != std::string::npos && b > a + 1)
    {
        err = "не задана переменная окружения " + out.substr(a + 1, b - a - 1) + " (путь " + path + ")";
        return false;
    }
    return true;
}

bool normalizeVersion(const std::string &in, std::string &out, std::string &err)
{
    std::string v = in;
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V'))
        v.erase(0, 1);
    int parts = 0;
    bool digit = false;
    for (char c : v)
    {
        if (c >= '0' && c <= '9')
            digit = true;
        else if (c == '.' && digit)
        {
            ++parts;
            digit = false;
        }
        else
        {
            digit = false;
            parts = 99;
            break;
        }
    }
    if (!digit || parts > 3)
    {
        err = "Недопустимая версия \"" + in + "\": нужно от одного до четырёх чисел через точку, например 1.2.3";
        return false;
    }
    out = v;
    return true;
}

std::string outputDirectory(const SetupProject &p, const std::string &projectPath)
{
    std::string out = p.outputDir.empty() ? "dist" : expandVars(p.outputDir, std::string(), false);
    if (!fsu::isAbsolutePath(out))
        out = fsu::joinPath(fsu::dirName(fsu::fullPath(projectPath)), out);
    return fsu::fullPath(out);
}

bool collectComponent(const Component &c, const std::string &projectDir, ComponentFiles &out,
                      std::string &err)
{
    std::string src;
    if (!expandBuildPath(c.source, src, err))
    {
        err = "Компонент " + c.id + ": " + err;
        return false;
    }
    src = src.empty() ? projectDir : fsu::isAbsolutePath(src) ? src : fsu::joinPath(projectDir, src);
    src = fsu::fullPath(src);
    if (fsu::fileExists(src))
    {
        // Источник — отдельный файл.
        out.files.push_back({src, fsu::baseName(src)});
        out.bytes += fsu::fileSize(src);
    }
    else if (fsu::dirExists(src))
        walk(src, std::string(), c, out);
    else
    {
        err = "Компонент " + c.id + ": не найден источник\n" + src;
        return false;
    }
    if (out.files.empty())
    {
        err = "Компонент " + c.id + ": в источнике нет подходящих файлов\n" + src;
        return false;
    }
    if (out.files.size() > 65535)
    {
        err = "Компонент " + c.id + ": более 65535 файлов (ограничение формата CAB)";
        return false;
    }
    return true;
}

static bool buildImpl(const SetupProject &p, const std::string &projectPath, const std::string &stub,
                      CabProgress *progress, BuildResult &result, std::string &err);

// Иконки проекта в копии программы установки.
static bool applyIcons(const SetupProject &p, const std::string &projectPath, std::string &stub,
                       std::string &err)
{
    if (p.icon.empty() && p.uninstallIcon.empty())
        return true;
    std::string projectDir = fsu::dirName(fsu::fullPath(projectPath));
    std::string temp = fsu::makeTempDir();
    if (temp.empty())
    {
        err = "Не удалось создать временную директорию";
        return false;
    }
    std::string exe = fsu::joinPath(temp, "setup.exe");
    bool ok = writeFile(exe, stub, err);
    const std::pair<const std::string *, int> icons[] = {{&p.icon, kSetupIconGroup},
                                                         {&p.uninstallIcon, kUninstallIconGroup}};
    for (const auto &ic : icons)
    {
        if (!ok || ic.first->empty())
            continue;
        std::string path;
        ok = expandBuildPath(*ic.first, path, err);
        if (ok && !fsu::isAbsolutePath(path))
            path = fsu::joinPath(projectDir, path);
        ok = ok && setIconGroup(exe, ic.second, path, err);
        if (ok)
            xlog::write("  иконка " + std::to_string(ic.second) + ": " + path);
    }
    ok = ok && fsu::readFile(exe, stub, 256u << 20);
    fsu::removeTree(temp);
    return ok;
}

bool buildDistribution(const SetupProject &p, const std::string &projectPath, const std::string &stubIn,
                       CabProgress *progress, BuildResult &result, std::string &err)
{
    std::string stub = stubIn;
    if (!applyIcons(p, projectPath, stub, err))
    {
        xlog::error(err);
        return false;
    }
    xlog::write("Сборка " + fsu::fullPath(projectPath) + ": " + p.name + " " + p.version + " -> " +
                outputDirectory(p, projectPath));
    bool ok = buildImpl(p, projectPath, stub, progress, result, err);
    if (!ok)
        xlog::error(err.empty() || err == kCabCancelled ? std::string("сборка прервана") : err);
    else
        for (const std::string &o : result.outputs)
            xlog::write("Готово: " + o + " (" + formatSize(fsu::fileSize(o)) + ")");
    return ok;
}

static bool buildImpl(const SetupProject &p, const std::string &projectPath, const std::string &stub,
                      CabProgress *progress, BuildResult &result, std::string &err)
{
    result = BuildResult();
    std::string problem = validateProject(p);
    if (!problem.empty())
    {
        err = problem;
        return false;
    }
    if (stub.size() < 1024 || stub.compare(0, 2, "MZ") != 0)
    {
        err = "Программа установки (setup.exe) повреждена или отсутствует";
        return false;
    }

    std::string projectDir = fsu::dirName(fsu::fullPath(projectPath));
    std::string expanded;
    if (!expandBuildPath(p.outputDir, expanded, err))
        return false;
    std::string out = outputDirectory(p, projectPath);
    // Дистрибутив публикуется в отдельную директорию результата.
    if (fsu::upper(fsu::withSlash(out)) == fsu::upper(fsu::withSlash(projectDir)))
    {
        err = "Директория результата совпадает с директорией проекта. Необходимо указать "
              "отдельную директорию, например dist.";
        return false;
    }

    if (p.mode == BuildMode::Folder || p.mode == BuildMode::Both)
    {
        std::string dir = p.mode == BuildMode::Both ? fsu::joinPath(out, "setup") : out;
        std::vector<std::string> layout;
        if (!buildFolder(p, projectDir, dir, &stub, progress, result, layout, err))
            return false;
        result.outputs.push_back(fsu::joinPath(dir, kSetupExe));

        if (p.mode == BuildMode::Both)
        {
            if (progress)
                progress->onStage("Один файл: " + singleFileName(p));
            std::vector<CabSource> files;
            for (const std::string &f : layout)
                files.push_back({f, fsu::baseName(f)});
            std::string exe = fsu::joinPath(out, singleFileName(p));
            CompressionSpec none;
            none.type = CabCompression::None;
            if (!cabCreate(exe, files, none, progress, err, stub))
                return false;
            result.outputs.push_back(exe);
        }
        return true;
    }

    // Только один файл: директория собирается во временной и упаковывается.
    std::string temp = fsu::makeTempDir();
    if (temp.empty())
    {
        err = "Не удалось создать временную директорию";
        return false;
    }
    std::vector<std::string> layout;
    bool ok = buildFolder(p, projectDir, temp, nullptr, progress, result, layout, err);
    if (ok)
    {
        if (progress)
            progress->onStage("Один файл: " + singleFileName(p));
        std::vector<CabSource> files;
        for (const std::string &f : layout)
            files.push_back({f, fsu::baseName(f)});
        std::string exe = fsu::joinPath(out, singleFileName(p));
        CompressionSpec none;
        none.type = CabCompression::None;
        ok = fsu::makeDirs(out) && cabCreate(exe, files, none, progress, err, stub);
        if (ok)
            result.outputs.push_back(exe);
        else if (err.empty())
            err = "Не удалось создать директорию " + out;
    }
    fsu::removeTree(temp);
    return ok;
}

bool loadSetupStub(std::string &data, std::string &err)
{
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_SETUP_STUB), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    HGLOBAL h = res ? LoadResource(nullptr, res) : nullptr;
    const void *ptr = h ? LockResource(h) : nullptr;
    DWORD size = res ? SizeofResource(nullptr, res) : 0;
    if (!ptr || size == 0)
    {
        err = "В сборщике отсутствует программа установки (setup.exe)";
        return false;
    }
    data.assign(static_cast<const char *>(ptr), size);
    return true;
}
