// Установка и удаление без интерфейса: файлы во временной директории, значения
// реестра в HKCU\Software\XPSetupUnitTest, запись в «Программах и компонентах»
// под случайным кодом продукта (удаляется в конце теста).

#include <algorithm>
#include <functional>
#include <stdexcept>

#include <windows.h>

#include "../../src/builder/build.h"
#include "../../src/common/fsutil.h"
#include "../../src/setup/install.h"
#include "test.h"

namespace {

const char *const kTestKey = "Software\\XPSetupUnitTest";

bool regExists(const std::string &key)
{
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, fsu::widen(key).c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS)
        return false;
    RegCloseKey(h);
    return true;
}

std::string regString(const std::string &key, const std::string &name)
{
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, fsu::widen(key).c_str(), fsu::widen(name).c_str(), RRF_RT_REG_SZ,
                     nullptr, buf, &size) != ERROR_SUCCESS)
        return {};
    return fsu::narrow(buf);
}

// Дистрибутив, программа установки и директория установки для одного теста.
struct Fixture
{
    test::TempDir t;
    SetupProject p;
    std::string stub = "MZ" + std::string(3000, '\x90');
    std::string self;           // «setup.exe» с дописанными данными
    std::string installDir;

    explicit Fixture(const std::string &version, bool extraFile = false)
    {
        test::writeFile(t.file("proj\\files\\app.exe"), "приложение " + version);
        test::writeFile(t.file("proj\\files\\data\\config.ini"), "[a]\r\nb=1\r\n");
        if (extraFile)
            test::writeFile(t.file("proj\\files\\old.dll"), "устаревший файл");
        test::writeFile(t.file("proj\\addins\\x.addin"), "<addin/>");

        p.name = "Модульный тест";
        p.version = version;
        p.publisher = "roosslan";
        p.productCode = newGuid();
        p.installDir = t.file("inst\\Программа");
        p.directories[1] = "%INSTALLDIR%";
        p.directories[2] = t.file("inst\\Модули");
        Component a;
        a.id = "main";
        a.cabinet = "main.cab";
        a.directory = 1;
        a.source = "files";
        Component b;
        b.id = "addins";
        b.cabinet = "addins.cab";
        b.directory = 2;
        b.source = "addins";
        p.components = {a, b};
        p.registry.push_back({"HKCU", std::string(kTestKey) + "\\" + p.productCode + "\\Параметры", "Путь",
                              "REG_SZ", "%INSTALLDIR%"});
        p.registry.push_back({"HKCU", std::string(kTestKey) + "\\" + p.productCode + "\\Параметры", "Число",
                              "REG_DWORD", "42"});
        p.outputDir = "dist";
        installDir = p.installDir;

        self = t.file("setup.exe");
        test::writeFile(self, stub + "ДАННЫЕ ПОСЛЕ ПРОГРАММЫ");
    }

    // Сборка дистрибутива с другой версией в той же директории проекта.
    void build()
    {
        BuildResult r;
        std::string err;
        if (!buildDistribution(p, t.file("proj\\p.xsp"), stub, nullptr, r, err))
            throw std::runtime_error("сборка: " + err);
    }

    InstallJob job()
    {
        InstallJob j;
        j.project = p;
        j.distDir = t.file("proj\\dist");
        j.installDir = installDir;
        j.selfPath = self;
        j.selfStubSize = stub.size();
        j.existing = findExisting(p);
        return j;
    }

    ~Fixture()
    {
        // Страховка, если тест прервался до удаления.
        RegDeleteTreeW(HKEY_CURRENT_USER, fsu::widen(uninstallKeyPath(p.productCode)).c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, fsu::widen(std::string(kTestKey) + "\\" + p.productCode).c_str());
        HKEY h;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, fsu::widen(kTestKey).c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS)
        {
            DWORD sub = 0, val = 0;
            RegQueryInfoKeyW(h, nullptr, nullptr, nullptr, &sub, nullptr, nullptr, &val, nullptr, nullptr,
                             nullptr, nullptr);
            RegCloseKey(h);
            if (sub == 0 && val == 0)
                RegDeleteKeyW(HKEY_CURRENT_USER, fsu::widen(kTestKey).c_str());
        }
    }
};

class Recorder : public InstallProgress
{
public:
    void onStatus(const std::string &s) override { statuses.push_back(s); }
    void onPercent(int p) override
    {
        if (p < last)
            backwards = true;
        last = p;
    }
    bool askRetry(const std::string &err) override
    {
        ++retries;
        lastError = err;
        if (beforeRetry)
            beforeRetry();
        return retries <= allowRetries;
    }

    std::vector<std::string> statuses;
    int last = 0;
    bool backwards = false;
    int retries = 0, allowRetries = 0;
    std::string lastError;
    std::function<void()> beforeRetry;
};

} // namespace

TEST(install_and_uninstall_leave_nothing)
{
    Fixture f("1.0.0.1");
    f.build();
    bool parentExisted = regExists(kTestKey);
    InstallJob job = f.job();
    CHECK(!job.existing.found);
    Recorder rec;
    std::string err;
    REQUIRE_OK(installFiles(job, &rec, err), err);
    REQUIRE_OK(installConfig(job, &rec, err), err);
    CHECK_EQ(rec.last, 100);

    CHECK_EQ(test::readFile(fsu::joinPath(f.installDir, "app.exe")), std::string("приложение 1.0.0.1"));
    CHECK(fsu::fileExists(fsu::joinPath(f.installDir, "data\\config.ini")));
    CHECK(fsu::fileExists(f.t.file("inst\\Модули\\x.addin")));
    CHECK_EQ(test::readFile(fsu::joinPath(f.installDir, kUninstallerName)), f.stub);
    CHECK(std::find(rec.statuses.begin(), rec.statuses.end(), "Копирование: config.ini") != rec.statuses.end());

    std::string params = std::string(kTestKey) + "\\" + f.p.productCode + "\\Параметры";
    CHECK_EQ(regString(params, "Путь"), f.installDir);
    std::string arp = uninstallKeyPath(f.p.productCode);
    CHECK_EQ(regString(arp, "DisplayName"), std::string("Модульный тест"));
    CHECK_EQ(regString(arp, "DisplayVersion"), std::string("1.0.0.1"));
    CHECK_EQ(regString(arp, "UninstallString"),
             "\"" + fsu::joinPath(f.installDir, kUninstallerName) + "\" /uninstall");

    ExistingInstall e = findExisting(f.p);
    CHECK(e.found);
    CHECK_EQ(e.version, std::string("1.0.0.1"));
    CHECK_EQ(e.dir, f.installDir);

    InstallLog log;
    REQUIRE_OK(log.load(fsu::joinPath(f.installDir, kLogName), err), err);
    CHECK_EQ(log.files.size(), (size_t) 3);
    CHECK_EQ(log.values.size(), (size_t) 2);
    CHECK(!log.keys.empty());

    REQUIRE_OK(uninstall(log, nullptr, false, err), err);
    CHECK(!fsu::dirExists(f.t.file("inst")));
    CHECK(!regExists(arp));
    CHECK(!regExists(std::string(kTestKey) + "\\" + f.p.productCode));
    // Промежуточные разделы, созданные установкой, тоже удалены.
    if (!parentExisted)
        CHECK(!regExists(kTestKey));
    CHECK(!findExisting(f.p).found);
}

TEST(install_upgrade_removes_old_files)
{
    Fixture f("1.0.0.1", true);
    f.build();
    InstallJob first = f.job();
    std::string err;
    REQUIRE_OK(installFiles(first, nullptr, err), err);
    REQUIRE_OK(installConfig(first, nullptr, err), err);
    REQUIRE(fsu::fileExists(fsu::joinPath(f.installDir, "old.dll")));

    // Версия 2: без old.dll.
    fsu::removeFile(f.t.file("proj\\files\\old.dll"));
    f.p.version = "1.0.0.2";
    test::writeFile(f.t.file("proj\\files\\app.exe"), "приложение 1.0.0.2");
    f.build();
    InstallJob second = f.job();
    REQUIRE(second.existing.found);
    CHECK_EQ(second.existing.version, std::string("1.0.0.1"));
    REQUIRE_OK(installFiles(second, nullptr, err), err);
    REQUIRE_OK(installConfig(second, nullptr, err), err);

    CHECK(!fsu::fileExists(fsu::joinPath(f.installDir, "old.dll")));
    CHECK_EQ(test::readFile(fsu::joinPath(f.installDir, "app.exe")), std::string("приложение 1.0.0.2"));
    CHECK_EQ(findExisting(f.p).version, std::string("1.0.0.2"));

    // Директории, созданные первой установкой, удаляются вместе со второй.
    InstallLog log;
    REQUIRE_OK(log.load(fsu::joinPath(f.installDir, kLogName), err), err);
    REQUIRE_OK(uninstall(log, nullptr, false, err), err);
    CHECK(!fsu::dirExists(f.t.file("inst")));
}

TEST(install_rollback_on_missing_cab)
{
    Fixture f("1.0.0.1");
    f.build();
    fsu::removeFile(f.t.file("proj\\dist\\addins.cab"));
    InstallJob job = f.job();
    std::string err;
    CHECK(!installFiles(job, nullptr, err));
    CHECK(err.find("addins.cab") != std::string::npos);
    // Файлы первого компонента и созданные директории удалены.
    CHECK(!fsu::dirExists(f.t.file("inst")));
    CHECK(!findExisting(f.p).found);
}

TEST(install_retry_after_error)
{
    Fixture f("1.0.0.1");
    f.build();
    // Файл на месте директории второго компонента: создать её не удастся.
    std::string blocker = f.t.file("inst\\Модули");
    test::writeFile(blocker, "мешает");
    InstallJob job = f.job();
    Recorder rec;
    rec.allowRetries = 1;
    rec.beforeRetry = [&] { fsu::removeFile(blocker); };
    std::string err;
    REQUIRE_OK(installFiles(job, &rec, err), err);
    CHECK_EQ(rec.retries, 1);
    CHECK(rec.lastError.find("Модули") != std::string::npos);
    CHECK(fsu::fileExists(f.t.file("inst\\Модули\\x.addin")));
    CHECK(!rec.backwards);
}

TEST(install_retry_refused_rolls_back)
{
    Fixture f("1.0.0.1");
    f.build();
    test::writeFile(f.t.file("inst\\Модули"), "мешает");
    InstallJob job = f.job();
    Recorder rec;
    rec.allowRetries = 0;
    std::string err;
    CHECK(!installFiles(job, &rec, err));
    CHECK_EQ(rec.retries, 1);
    CHECK(!fsu::fileExists(fsu::joinPath(f.installDir, "app.exe")));
    CHECK(fsu::fileExists(f.t.file("inst\\Модули")));     // чужой файл не тронут
}

TEST(install_log_roundtrip)
{
    test::TempDir t;
    InstallLog a;
    a.name = "Имя, с запятой";
    a.version = "1.2";
    a.productCode = "{X}";
    a.installDir = "C:\\Program Files\\Имя";
    a.perMachine = true;
    a.files = {"C:\\a b\\c.txt", "C:\\d\\\"e\".txt"};
    a.dirs = {"C:\\a b"};
    a.values = {{"HKLM", "Software\\X", "", "REG_SZ", ""}};
    a.keys = {"HKLM\\Software\\X"};
    a.shortcuts = {"C:\\s.lnk"};
    a.killProcesses = {"x.exe"};
    std::string err;
    REQUIRE_OK(a.save(t.file("u.sif"), err), err);
    InstallLog b;
    REQUIRE_OK(b.load(t.file("u.sif"), err), err);
    CHECK_EQ(b.name, a.name);
    CHECK_EQ(b.installDir, a.installDir);
    CHECK(b.perMachine);
    CHECK(b.files == a.files);
    CHECK(b.dirs == a.dirs);
    REQUIRE(b.values.size() == 1);
    CHECK_EQ(b.values[0].name, std::string());
    CHECK(b.keys == a.keys);
    CHECK(b.shortcuts == a.shortcuts);
    CHECK(b.killProcesses == a.killProcesses);
}

TEST(install_payload_size)
{
    Fixture f("1.0.0.1");
    f.build();
    std::string app = "приложение 1.0.0.1";
    uint64_t expect = app.size() + std::string("[a]\r\nb=1\r\n").size() + std::string("<addin/>").size();
    CHECK_EQ(payloadSize(f.p, f.t.file("proj\\dist")), expect);
}
