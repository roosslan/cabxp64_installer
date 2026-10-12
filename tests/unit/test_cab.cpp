#include "../../src/common/cabfile.h"
#include "../../src/common/fsutil.h"
#include "test.h"

namespace {

std::vector<CabSource> makeFiles(const test::TempDir &t)
{
    test::writeFile(t.file("src\\readme.txt"), "Прочти меня");
    test::writeFile(t.file("src\\Документы\\отчёт.txt"), std::string(100000, 'x'));
    test::writeFile(t.file("src\\a\\b\\deep.bin"), std::string(5000, '\x01'));
    return {
        {t.file("src\\readme.txt"), "readme.txt"},
        {t.file("src\\Документы\\отчёт.txt"), "Документы\\отчёт.txt"},
        {t.file("src\\a\\b\\deep.bin"), "a\\b\\deep.bin"},
    };
}

class Counter : public CabProgress
{
public:
    bool onFile(const std::string &name) override
    {
        names.push_back(name);
        return true;
    }
    bool onBytes(uint64_t done, uint64_t total) override
    {
        last = done;
        all = total;
        return true;
    }
    std::vector<std::string> names;
    uint64_t last = 0, all = 0;
};

} // namespace

TEST(cab_create_extract_roundtrip)
{
    test::TempDir t;
    std::vector<CabSource> files = makeFiles(t);
    std::string err;
    for (CabCompression comp : {CabCompression::None, CabCompression::MSZIP, CabCompression::LZX})
    {
        CompressionSpec spec;
        spec.type = comp;
        std::string cab = t.file("test" + std::to_string((int) comp) + ".cab");
        REQUIRE_OK(cabCreate(cab, files, spec, nullptr, err), err);
        CabInfo info;
        REQUIRE_OK(cabRead(cab, info, err), err);
        CHECK_EQ(info.entries.size(), (size_t) 3);
        CHECK_EQ(info.offset, (uint64_t) 0);
        CHECK_EQ(info.totalSize, (uint64_t) (21 + 100000 + 5000));   // «Прочти меня» в UTF-8 — 21 байт

        std::string dest = t.file("x" + std::to_string((int) comp));
        Counter c;
        REQUIRE_OK(cabExtract(cab, dest, {}, true, Overwrite::Always, &c, err), err);
        CHECK_EQ(c.names.size(), (size_t) 3);
        CHECK_EQ(c.last, info.totalSize);
        CHECK_EQ(test::readFile(fsu::joinPath(dest, "readme.txt")), std::string("Прочти меня"));
        CHECK_EQ(test::readFile(fsu::joinPath(dest, "Документы\\отчёт.txt")).size(), (size_t) 100000);
        CHECK(fsu::fileExists(fsu::joinPath(dest, "a\\b\\deep.bin")));
    }
}

TEST(cab_target_path)
{
    CHECK_EQ(cabTargetPath("C:\\D", "a\\b.txt", true), std::string("C:\\D\\a\\b.txt"));
    CHECK_EQ(cabTargetPath("C:\\D", "a\\b.txt", false), std::string("C:\\D\\b.txt"));
    // Имена с выходом за пределы директории не покидают её.
    CHECK_EQ(cabTargetPath("C:\\D", "..\\..\\evil.txt", true).compare(0, 5, "C:\\D\\"), 0);
}

TEST(cab_appended_to_program)
{
    test::TempDir t;
    std::vector<CabSource> files = makeFiles(t);
    std::string stub = test::realStub();
    std::string err;
    CompressionSpec none;
    none.type = CabCompression::None;
    std::string exe = t.file("setup.exe");
    REQUIRE_OK(cabCreate(exe, files, none, nullptr, err, stub), err);
    CabInfo info;
    REQUIRE_OK(cabRead(exe, info, err), err);
    CHECK_EQ(info.offset, (uint64_t) stub.size());
    CHECK(cabHasSignature(exe, true));
    REQUIRE_OK(cabExtract(exe, t.file("x"), {}, true, Overwrite::Always, nullptr, err), err);
    CHECK(fsu::fileExists(t.file("x\\Документы\\отчёт.txt")));
}

TEST(cab_extract_selected_and_overwrite_never)
{
    test::TempDir t;
    std::vector<CabSource> files = makeFiles(t);
    std::string err;
    std::string cab = t.file("t.cab");
    REQUIRE_OK(cabCreate(cab, files, CompressionSpec(), nullptr, err), err);
    test::writeFile(t.file("x\\readme.txt"), "старое");
    CabResult r;
    REQUIRE(cabExtract(cab, t.file("x"), {"readme.txt"}, true, Overwrite::Never, nullptr, err, &r));
    CHECK_EQ(r.skipped, 1);
    CHECK_EQ(test::readFile(t.file("x\\readme.txt")), std::string("старое"));
    CHECK(!fsu::fileExists(t.file("x\\a\\b\\deep.bin")));
}

TEST(cab_read_damaged)
{
    test::TempDir t;
    test::writeFile(t.file("bad.cab"), "MSCF this is not a cabinet");
    CabInfo info;
    std::string err;
    CHECK(!cabRead(t.file("bad.cab"), info, err));
    CHECK(!cabRead(t.file("missing.cab"), info, err));
}
