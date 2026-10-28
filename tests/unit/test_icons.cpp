#include <cstring>

#include "../../src/builder/build.h"
#include "../../src/common/fsutil.h"
#include "../../src/common/icons.h"
#include "../../src/setup/install.h"
#include "test.h"

namespace {

// ICO из однотонных 32-битных изображений указанных размеров.
std::string makeIco(std::initializer_list<int> sizes, unsigned char shade)
{
    std::string dir, body;
    dir.resize(6, '\0');
    dir[2] = 1;
    dir[4] = (char) sizes.size();
    size_t offset = 6 + 16 * sizes.size();
    for (int s : sizes)
    {
        std::string img(40, '\0');
        int32_t v[] = {40, s, s * 2};
        memcpy(&img[0], &v[0], 4);
        memcpy(&img[4], &v[1], 4);
        memcpy(&img[8], &v[2], 4);
        img[12] = 1;    // planes
        img[14] = 32;   // bpp
        for (int i = 0; i < s * s; ++i)
            img += std::string{(char) shade, (char) shade, (char) shade, (char) 0xFF};
        img += std::string((size_t) (((s + 31) / 32) * 4 * s), '\0');
        unsigned char e[16] = {(unsigned char) (s % 256), (unsigned char) (s % 256), 0, 0, 1, 0, 32, 0};
        uint32_t bytes = (uint32_t) img.size(), off = (uint32_t) (offset + body.size());
        memcpy(e + 8, &bytes, 4);
        memcpy(e + 12, &off, 4);
        dir.append((const char *) e, 16);
        body += img;
    }
    return dir + body;
}

std::vector<int> widths(const std::vector<IconImage> &g)
{
    std::vector<int> r;
    for (const IconImage &i : g)
        r.push_back(i.width);
    return r;
}

} // namespace

TEST(icons_replace_and_delete_groups)
{
    test::TempDir t;
    std::string exe = t.file("prog.exe");
    test::writeFile(exe, test::realStub());
    test::writeFile(t.file("a.ico"), makeIco({16, 32, 48}, 0x40));
    test::writeFile(t.file("b.ico"), makeIco({16, 256}, 0xC0));
    std::string err;

    REQUIRE_OK(setIconGroup(exe, kSetupIconGroup, t.file("a.ico"), err), err);
    REQUIRE_OK(setIconGroup(exe, kUninstallIconGroup, t.file("b.ico"), err), err);
    CHECK(widths(iconGroup(exe, kSetupIconGroup)) == (std::vector<int>{16, 32, 48}));
    CHECK(widths(iconGroup(exe, kUninstallIconGroup)) == (std::vector<int>{16, 256}));

    // Повторная замена не оставляет прежних изображений.
    REQUIRE_OK(setIconGroup(exe, kSetupIconGroup, t.file("b.ico"), err), err);
    CHECK(widths(iconGroup(exe, kSetupIconGroup)) == (std::vector<int>{16, 256}));

    REQUIRE_OK(setIconGroup(exe, kSetupIconGroup, std::string(), err), err);
    CHECK(iconGroup(exe, kSetupIconGroup).empty());
    CHECK(widths(iconGroup(exe, kUninstallIconGroup)) == (std::vector<int>{16, 256}));
    // Файл остаётся рабочей программой с прежним размером кода.
    CHECK(test::readFile(exe).compare(0, 2, "MZ") == 0);
}

TEST(icons_bad_file)
{
    test::TempDir t;
    std::string exe = t.file("prog.exe");
    test::writeFile(exe, test::realStub());
    test::writeFile(t.file("bad.ico"), "not an icon");
    std::string err;
    CHECK(!checkIconFile(t.file("bad.ico"), err));
    CHECK(!setIconGroup(exe, kSetupIconGroup, t.file("bad.ico"), err));
    CHECK(err.find("bad.ico") != std::string::npos);
    CHECK(!checkIconFile(t.file("missing.ico"), err));
}

TEST(icons_project_icons_in_distribution)
{
    test::TempDir t;
    test::writeFile(t.file("proj\\files\\app.exe"), "exe");
    test::writeFile(t.file("proj\\art\\app.ico"), makeIco({16, 32}, 0x30));
    test::writeFile(t.file("proj\\art\\del.ico"), makeIco({24}, 0xA0));
    SetupProject p;
    p.name = "Иконки";
    p.productCode = "{12345678-1234-1234-1234-123456789ABC}";
    p.installDir = "%TEMP%\\xps-icons";
    p.directories[1] = "%INSTALLDIR%";
    Component c;
    c.id = "main";
    c.cabinet = "main.cab";
    c.source = "files";
    p.components.push_back(c);
    p.icon = "art\\app.ico";
    p.uninstallIcon = "art\\del.ico";
    p.mode = BuildMode::Both;
    p.singleFile = "one.exe";
    std::string err;
    BuildResult r;
    REQUIRE_OK(buildDistribution(p, t.file("proj\\p.xsp"), test::realStub(), nullptr, r, err), err);
    REQUIRE(r.outputs.size() == 2);
    for (const std::string &exe : r.outputs)
    {
        CHECK(widths(iconGroup(exe, kSetupIconGroup)) == (std::vector<int>{16, 32}));
        CHECK(widths(iconGroup(exe, kUninstallIconGroup)) == (std::vector<int>{24}));
    }

    p.icon = "art\\нет.ico";
    CHECK(!buildDistribution(p, t.file("proj\\p.xsp"), test::realStub(), nullptr, r, err));
}

TEST(icons_uninstaller_shows_uninstall_icon)
{
    test::TempDir t;
    std::string stubFile = t.file("stub.exe");
    test::writeFile(stubFile, test::realStub());
    test::writeFile(t.file("a.ico"), makeIco({32}, 0x50));
    test::writeFile(t.file("b.ico"), makeIco({16}, 0xB0));
    std::string err;
    REQUIRE_OK(setIconGroup(stubFile, kSetupIconGroup, t.file("a.ico"), err), err);
    REQUIRE_OK(setIconGroup(stubFile, kUninstallIconGroup, t.file("b.ico"), err), err);
    std::string stub = test::readFile(stubFile);

    test::writeFile(t.file("proj\\files\\app.exe"), "exe");
    InstallJob job;
    job.project.name = "Иконка удаления";
    job.project.productCode = newGuid();
    job.project.installDir = t.file("inst");
    job.project.directories[1] = "%INSTALLDIR%";
    Component c;
    c.id = "main";
    c.cabinet = "main.cab";
    c.source = "files";
    job.project.components.push_back(c);
    job.project.outputDir = "dist";
    BuildResult r;
    REQUIRE_OK(buildDistribution(job.project, t.file("proj\\p.xsp"), stub, nullptr, r, err), err);

    // Программа установки — setup.exe с дописанными данными.
    job.selfPath = t.file("setup.exe");
    test::writeFile(job.selfPath, stub + "ДАННЫЕ");
    job.selfStubSize = stub.size();
    job.distDir = t.file("proj\\dist");
    job.installDir = t.file("inst");
    REQUIRE_OK(installFiles(job, nullptr, err), err);
    REQUIRE_OK(installConfig(job, nullptr, err), err);

    std::string uninst = fsu::joinPath(job.installDir, kUninstallerName);
    CHECK(iconGroup(uninst, kSetupIconGroup).empty());
    CHECK(widths(iconGroup(uninst, kUninstallIconGroup)) == (std::vector<int>{16}));

    InstallLog log;
    REQUIRE_OK(log.load(fsu::joinPath(job.installDir, kLogName), err), err);
    REQUIRE_OK(uninstall(log, nullptr, false, err), err);
}
