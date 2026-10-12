#include <windows.h>

#include "../../src/common/fsutil.h"
#include "../../src/common/project.h"
#include "../../src/common/sif.h"
#include "test.h"

namespace {

SetupProject sample()
{
    SetupProject p;
    p.name = "Демо, программа";
    p.version = "2.3.4.5";
    p.publisher = "roosslan";
    p.productCode = "{11111111-2222-3333-4444-555555555555}";
    p.installDir = "%LOCALAPPDATA%\\Programs\\Демо";
    p.allowChangeDir = false;
    p.perMachine = true;
    p.license = "docs\\license.txt";
    p.directories[1] = "%INSTALLDIR%";
    p.directories[5] = "%APPDATA%\\Демо, данные";
    Component c;
    c.id = "main";
    c.description = "Файлы; основные";
    c.cabinet = "main.cab";
    c.directory = 1;
    c.source = "..\\Build";
    c.include = "*.exe;*.dll";
    c.exclude = "*.pdb;Files";
    c.recurse = false;
    p.components.push_back(c);
    p.shortcuts.push_back({ShortcutPlace::Programs, "Группа\\Ярлык", "%INSTALLDIR%\\a.exe", "-x \"1\"", ""});
    p.registry.push_back({"HKLM", "Software\\Демо", "", "REG_EXPAND_SZ", "\"%INSTALLDIR%\\a.exe\""});
    p.killProcesses = {"a.exe", "b c.exe"};
    p.runAfter.push_back({"%INSTALLDIR%\\a.exe", "/start"});
    p.outputDir = "out dir";
    p.singleFile = "demo.exe";
    p.mode = BuildMode::Both;
    p.compression.type = CabCompression::LZX;
    p.compression.lzxWindow = 18;
    return p;
}

} // namespace

TEST(project_save_load_roundtrip)
{
    test::TempDir t;
    SetupProject p = sample();
    std::string err;
    REQUIRE_OK(saveProject(t.file("p.xsp"), p, false, err), err);
    SetupProject r;
    REQUIRE_OK(loadProject(t.file("p.xsp"), r, err), err);
    CHECK_EQ(r.name, p.name);
    CHECK_EQ(r.version, p.version);
    CHECK_EQ(r.productCode, p.productCode);
    CHECK_EQ(r.installDir, p.installDir);
    CHECK_EQ(r.allowChangeDir, false);
    CHECK_EQ(r.perMachine, true);
    CHECK_EQ(r.license, p.license);
    CHECK(r.directories == p.directories);
    REQUIRE(r.components.size() == 1);
    CHECK_EQ(r.components[0].description, p.components[0].description);
    CHECK_EQ(r.components[0].source, p.components[0].source);
    CHECK_EQ(r.components[0].include, p.components[0].include);
    CHECK_EQ(r.components[0].exclude, p.components[0].exclude);
    CHECK_EQ(r.components[0].recurse, false);
    REQUIRE(r.shortcuts.size() == 1);
    CHECK(r.shortcuts[0].place == ShortcutPlace::Programs);
    CHECK_EQ(r.shortcuts[0].name, p.shortcuts[0].name);
    CHECK_EQ(r.shortcuts[0].args, p.shortcuts[0].args);
    REQUIRE(r.registry.size() == 1);
    CHECK_EQ(r.registry[0].name, std::string());
    CHECK_EQ(r.registry[0].type, std::string("REG_EXPAND_SZ"));
    CHECK_EQ(r.registry[0].value, p.registry[0].value);
    CHECK(r.killProcesses == p.killProcesses);
    REQUIRE(r.runAfter.size() == 1);
    CHECK_EQ(r.runAfter[0].args, std::string("/start"));
    CHECK_EQ(r.outputDir, p.outputDir);
    CHECK_EQ(r.singleFile, p.singleFile);
    CHECK(r.mode == BuildMode::Both);
    CHECK(r.compression.type == CabCompression::LZX);
    CHECK_EQ(r.compression.lzxWindow, 18);
}

TEST(project_setup_sif_has_no_builder_fields)
{
    test::TempDir t;
    std::string err;
    REQUIRE_OK(saveProject(t.file("setup.sif"), sample(), true, err), err);
    Sif s;
    REQUIRE_OK(s.load(t.file("setup.sif"), err), err);
    CHECK(s.section("Build") == nullptr);
    CHECK_EQ(s.get("Component.main", "Source"), std::string());
    CHECK_EQ(s.get("Component.main", "Cabinet"), std::string("main.cab"));
    CHECK_EQ(s.get("Product", "License"), std::string("license.txt"));
}

TEST(project_load_rejects_foreign_file)
{
    test::TempDir t;
    test::writeFile(t.file("x.sif"), "[Product]\nName = x\n");
    SetupProject p;
    std::string err;
    CHECK(!loadProject(t.file("x.sif"), p, err));
    CHECK(err.find("$XPSetup$") != std::string::npos);
}

TEST(project_validate)
{
    SetupProject p = sample();
    CHECK_EQ(validateProject(p), std::string());
    SetupProject a = p;
    a.name.clear();
    CHECK(!validateProject(a).empty());
    SetupProject b = p;
    b.components[0].directory = 7;
    CHECK(validateProject(b).find("несуществующую директорию") != std::string::npos);
    SetupProject c = p;
    c.components.push_back(c.components[0]);
    c.components[1].id = "other";
    CHECK(validateProject(c).find("Повторяется имя CAB") != std::string::npos);
    SetupProject d = p;
    d.components.clear();
    CHECK(!validateProject(d).empty());
}

TEST(project_compare_versions)
{
    CHECK_EQ(compareVersions("1.2.3.4", "1.2.3.4"), 0);
    CHECK_EQ(compareVersions("1.2.10", "1.2.9"), 1);
    CHECK_EQ(compareVersions("1.2", "1.2.0.0"), 0);
    CHECK_EQ(compareVersions("1.2", "1.2.0.1"), -1);
    CHECK_EQ(compareVersions("9.5.123.26", "9.5.99.26"), 1);
    CHECK_EQ(compareVersions("", "0"), 0);
}

TEST(project_expand_vars)
{
    SetEnvironmentVariableW(L"XPS_TEST_VAR", L"значение");
    CHECK_EQ(expandVars("%INSTALLDIR%\\bin", "C:\\Prog", false), std::string("C:\\Prog\\bin"));
    CHECK_EQ(expandVars("[%XPS_TEST_VAR%]", "", false), std::string("[значение]"));
    CHECK_EQ(expandVars("100% и %NO_SUCH_VAR_XPS%", "", false), std::string("100% и %NO_SUCH_VAR_XPS%"));
    CHECK_EQ(expandVars("50%%INSTALLDIR%", "D", false), std::string("50%D"));
    std::string appdata = expandVars("%APPDATA%", "", false);
    CHECK(!appdata.empty() && appdata.find('%') == std::string::npos);
    CHECK(expandVars("%DESKTOP%", "", false) != expandVars("%DESKTOP%", "", true));
    std::string temp = expandVars("%TEMP%", "", false);
    CHECK(!temp.empty() && temp.back() != '\\');
}

TEST(project_directory_path)
{
    SetupProject p = sample();
    CHECK_EQ(directoryPath(p, 1, "C:\\X"), std::string("C:\\X"));
    CHECK_EQ(directoryPath(p, 0, "C:\\X"), std::string("C:\\X"));
    CHECK_EQ(directoryPath(p, 9, "C:\\X"), std::string());
    CHECK(directoryPath(p, 5, "C:\\X").find("Демо, данные") != std::string::npos);
}

TEST(project_new_guid)
{
    std::string a = newGuid(), b = newGuid();
    CHECK_EQ(a.size(), (size_t) 38);
    CHECK(a.front() == '{' && a.back() == '}');
    CHECK(a != b);
}

TEST(project_place_names)
{
    for (ShortcutPlace p : {ShortcutPlace::Desktop, ShortcutPlace::Programs, ShortcutPlace::Startup})
        CHECK(placeFromName(placeName(p)) == p);
    CHECK(placeFromName("unknown") == ShortcutPlace::Desktop);
}
