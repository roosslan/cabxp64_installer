#include "build.h"

#include <cstdio>

#include <windows.h>

#include "../common/fsutil.h"
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

// Удаление результатов прошлой сборки (только своих файлов).
void cleanFolderLayout(const std::string &dir)
{
    std::vector<fsu::DirItem> items;
    if (!fsu::listDirectory(dir, items))
        return;
    for (const fsu::DirItem &it : items)
        if (!it.isDir && (fsu::wildMatch("*.cab", it.name) || fsu::upper(it.name) == "SETUP.EXE" ||
                          fsu::upper(it.name) == "SETUP.SIF" || fsu::upper(it.name) == "LICENSE.TXT"))
            fsu::removeFile(fsu::joinPath(dir, it.name));
}

// Директория дистрибутива: setup.sif, лицензия и CAB-архивы.
bool buildFolder(const SetupProject &p, const std::string &projectDir, const std::string &dir,
                 const std::string *stub, CabProgress *progress, BuildResult &result,
                 std::vector<std::string> &layout, std::string &err)
{
    if (!fsu::makeDirs(dir))
    {
        err = "Не удалось создать директорию " + dir + "\n" + fsu::lastErrorText();
        return false;
    }
    cleanFolderLayout(dir);

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
        layout.push_back(cab);
        result.files += files.files.size();
        result.bytes += files.bytes;
    }

    if (!p.license.empty())
    {
        std::string src = fsu::isAbsolutePath(p.license) ? p.license : fsu::joinPath(projectDir, p.license);
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

std::string outputDirectory(const SetupProject &p, const std::string &projectPath)
{
    std::string out = p.outputDir.empty() ? "dist" : p.outputDir;
    if (!fsu::isAbsolutePath(out))
        out = fsu::joinPath(fsu::dirName(fsu::fullPath(projectPath)), out);
    return fsu::fullPath(out);
}

bool collectComponent(const Component &c, const std::string &projectDir, ComponentFiles &out,
                      std::string &err)
{
    std::string src = c.source.empty() ? projectDir
                    : fsu::isAbsolutePath(c.source) ? c.source
                                                    : fsu::joinPath(projectDir, c.source);
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

bool buildDistribution(const SetupProject &p, const std::string &projectPath, const std::string &stub,
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
    std::string out = outputDirectory(p, projectPath);
    // Перед сборкой из директории результата удаляются прежние *.cab,
    // setup.exe, setup.sif и license.txt — она должна быть отдельной.
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
