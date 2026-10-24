#include <algorithm>
#include <stdexcept>

#include <windows.h>

#include "../../src/builder/build.h"
#include "../../src/common/cabfile.h"
#include "../../src/common/fsutil.h"
#include "../../src/common/sif.h"
#include "test.h"

namespace {

// Программа установки: настоящий PE, по нему ищется дописанный CAB.
std::string fakeStub()
{
    static const std::string stub = test::realStub();
    return stub;
}

// Проект с двумя компонентами в директории t\proj.
SetupProject makeProject(const test::TempDir &t)
{
    test::writeFile(t.file("proj\\files\\app.exe"), "exe");
    test::writeFile(t.file("proj\\files\\app.pdb"), "pdb");
    test::writeFile(t.file("proj\\files\\lib\\core.dll"), "dll");
    test::writeFile(t.file("proj\\files\\Files\\plugin.addin"), "addin");
    test::writeFile(t.file("proj\\files\\Files\\app.config"), "cfg");
    test::writeFile(t.file("proj\\license.txt"), "Лицензия");

    SetupProject p;
    p.name = "Тест";
    p.productCode = "{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}";
    p.installDir = "%TEMP%\\xps-unit";
    p.license = "license.txt";
    p.directories[1] = "%INSTALLDIR%";
    p.directories[2] = "%INSTALLDIR%\\addins";
    Component main;
    main.id = "main";
    main.cabinet = "main.cab";
    main.directory = 1;
    main.source = "files";
    main.exclude = "*.pdb;Files";
    Component add;
    add.id = "addins";
    add.cabinet = "addins.cab";
    add.directory = 2;
    add.source = "files\\Files";
    add.include = "*.addin";
    add.recurse = false;
    p.components = {main, add};
    p.outputDir = "dist";
    return p;
}

std::vector<std::string> names(const ComponentFiles &f)
{
    std::vector<std::string> r;
    for (const CabSource &s : f.files)
        r.push_back(s.nameInCab);
    std::sort(r.begin(), r.end());
    return r;
}

} // namespace

TEST(build_collect_masks)
{
    test::TempDir t;
    SetupProject p = makeProject(t);
    std::string dir = t.file("proj"), err;
    ComponentFiles f;
    REQUIRE_OK(collectComponent(p.components[0], dir, f, err), err);
    CHECK(names(f) == (std::vector<std::string>{"app.exe", "lib\\core.dll"}));
    ComponentFiles a;
    REQUIRE_OK(collectComponent(p.components[1], dir, a, err), err);
    CHECK(names(a) == (std::vector<std::string>{"plugin.addin"}));
    CHECK_EQ(a.bytes, (uint64_t) 5);
}

TEST(build_collect_exclude_by_path)
{
    test::TempDir t;
    SetupProject p = makeProject(t);
    Component c = p.components[0];
    c.exclude = "Files\\*.config;lib";
    ComponentFiles f;
    std::string err;
    REQUIRE_OK(collectComponent(c, t.file("proj"), f, err), err);
    CHECK(names(f) == (std::vector<std::string>{"Files\\plugin.addin", "app.exe", "app.pdb"}));
}

TEST(build_collect_single_file_and_errors)
{
    test::TempDir t;
    SetupProject p = makeProject(t);
    Component c = p.components[0];
    c.source = "license.txt";
    ComponentFiles f;
    std::string err;
    REQUIRE_OK(collectComponent(c, t.file("proj"), f, err), err);
    CHECK(names(f) == (std::vector<std::string>{"license.txt"}));

    c.source = "нет такой";
    ComponentFiles g;
    CHECK(!collectComponent(c, t.file("proj"), g, err));
    CHECK(err.find("не найден источник") != std::string::npos);

    c.source = "files";
    c.include = "*.xyz";
    ComponentFiles h;
    CHECK(!collectComponent(c, t.file("proj"), h, err));
    CHECK(err.find("нет подходящих файлов") != std::string::npos);
}

TEST(build_folder_distribution)
{
    test::TempDir t;
    SetupProject p = makeProject(t);
    std::string project = t.file("proj\\p.xsp"), err;
    // Остаток прошлой сборки должен быть удалён.
    test::writeFile(t.file("proj\\dist\\old.cab"), "x");
    test::writeFile(t.file("proj\\dist\\keep.txt"), "x");
    BuildResult r;
    REQUIRE_OK(buildDistribution(p, project, fakeStub(), nullptr, r, err), err);
    REQUIRE(r.outputs.size() == 1);
    CHECK_EQ(r.outputs[0], t.file("proj\\dist\\setup.exe"));
    CHECK_EQ(r.files, (uint64_t) 3);
    CHECK(!fsu::fileExists(t.file("proj\\dist\\old.cab")));
    CHECK(fsu::fileExists(t.file("proj\\dist\\keep.txt")));
    CHECK_EQ(test::readFile(t.file("proj\\dist\\setup.exe")), fakeStub());
    CHECK_EQ(test::readFile(t.file("proj\\dist\\license.txt")), std::string("Лицензия"));
    CabInfo info;
    REQUIRE_OK(cabRead(t.file("proj\\dist\\main.cab"), info, err), err);
    CHECK_EQ(info.entries.size(), (size_t) 2);

    SetupProject s;
    REQUIRE_OK(loadProject(t.file("proj\\dist\\setup.sif"), s, err), err);
    CHECK_EQ(s.components[0].source, std::string());
    CHECK_EQ(s.license, std::string("license.txt"));
}

TEST(build_single_and_both)
{
    test::TempDir t;
    SetupProject p = makeProject(t);
    std::string project = t.file("proj\\p.xsp"), err;
    p.mode = BuildMode::Single;
    p.singleFile = "one.exe";
    BuildResult r;
    REQUIRE_OK(buildDistribution(p, project, fakeStub(), nullptr, r, err), err);
    REQUIRE(r.outputs.size() == 1);
    CabInfo info;
    REQUIRE_OK(cabRead(r.outputs[0], info, err), err);
    CHECK_EQ(info.offset, (uint64_t) fakeStub().size());
    std::vector<std::string> inside;
    for (const CabEntry &e : info.entries)
        inside.push_back(e.name);
    std::sort(inside.begin(), inside.end());
    CHECK(inside == (std::vector<std::string>{"addins.cab", "license.txt", "main.cab", "setup.sif"}));
    CHECK(!fsu::fileExists(t.file("proj\\dist\\setup.exe")));

    p.mode = BuildMode::Both;
    REQUIRE_OK(buildDistribution(p, project, fakeStub(), nullptr, r, err), err);
    REQUIRE(r.outputs.size() == 2);
    CHECK_EQ(r.outputs[0], t.file("proj\\dist\\setup\\setup.exe"));
    CHECK_EQ(r.outputs[1], t.file("proj\\dist\\one.exe"));
}

TEST(build_refuses_bad_input)
{
    test::TempDir t;
    SetupProject p = makeProject(t);
    std::string project = t.file("proj\\p.xsp"), err;
    BuildResult r;

    SetupProject same = p;
    same.outputDir = ".";
    CHECK(!buildDistribution(same, project, fakeStub(), nullptr, r, err));
    CHECK(err.find("совпадает с директорией проекта") != std::string::npos);

    CHECK(!buildDistribution(p, project, "not an exe", nullptr, r, err));

    SetupProject invalid = p;
    invalid.productCode.clear();
    CHECK(!buildDistribution(invalid, project, fakeStub(), nullptr, r, err));
}

TEST(build_single_file_name)
{
    SetupProject p;
    p.name = "Имя: с/запрещёнными*символами";
    CHECK_EQ(singleFileName(p), std::string("Имя_ с_запрещёнными_символами_setup.exe"));
    p.singleFile = "x.exe";
    CHECK_EQ(singleFileName(p), std::string("x.exe"));
}

TEST(build_normalize_version)
{
    std::string v, err;
    REQUIRE_OK(normalizeVersion("v1.2.3", v, err), err);
    CHECK_EQ(v, std::string("1.2.3"));
    REQUIRE_OK(normalizeVersion("9.5.123.26", v, err), err);
    CHECK_EQ(v, std::string("9.5.123.26"));
    REQUIRE_OK(normalizeVersion("V7", v, err), err);
    CHECK_EQ(v, std::string("7"));
    for (const char *bad : {"", "v", "1.2.3.4.5", "1..2", "1.2.", ".1", "1.2-beta", "v1.x", "версия"})
        CHECK(!normalizeVersion(bad, v, err));
}

TEST(build_expand_path_variables)
{
    SetEnvironmentVariableW(L"XPS_UNIT_PAYLOAD", L"C:\\Сборка");
    std::string out, err;
    REQUIRE_OK(expandBuildPath("%XPS_UNIT_PAYLOAD%\\bin", out, err), err);
    CHECK_EQ(out, std::string("C:\\Сборка\\bin"));
    REQUIRE_OK(expandBuildPath("files", out, err), err);
    CHECK_EQ(out, std::string("files"));
    CHECK(!expandBuildPath("%XPS_UNIT_NOT_SET%\\bin", out, err));
    CHECK(err.find("XPS_UNIT_NOT_SET") != std::string::npos);
    SetEnvironmentVariableW(L"XPS_UNIT_PAYLOAD", nullptr);
}

TEST(build_source_from_environment)
{
    test::TempDir t;
    SetupProject p = makeProject(t);
    SetEnvironmentVariableW(L"XPS_UNIT_SRC", fsu::widen(t.file("proj\\files")).c_str());
    SetEnvironmentVariableW(L"XPS_UNIT_OUT", fsu::widen(t.file("out")).c_str());
    p.components[0].source = "%XPS_UNIT_SRC%";
    p.components[1].source = "%XPS_UNIT_SRC%\\Files";
    p.outputDir = "%XPS_UNIT_OUT%";
    std::string err;
    BuildResult r;
    REQUIRE_OK(buildDistribution(p, t.file("proj\\p.xsp"), fakeStub(), nullptr, r, err), err);
    CHECK_EQ(r.files, (uint64_t) 3);
    CHECK(fsu::fileExists(t.file("out\\setup.exe")));

    p.components[0].source = "%XPS_UNIT_MISSING%";
    CHECK(!buildDistribution(p, t.file("proj\\p.xsp"), fakeStub(), nullptr, r, err));
    CHECK(err.find("XPS_UNIT_MISSING") != std::string::npos);
    SetEnvironmentVariableW(L"XPS_UNIT_SRC", nullptr);
    SetEnvironmentVariableW(L"XPS_UNIT_OUT", nullptr);
}
