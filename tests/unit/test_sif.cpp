#include "../../src/common/sif.h"
#include "test.h"

TEST(sif_split_plain_and_quoted)
{
    std::vector<std::string> f = sifSplit(R"(HKCU, "Software\Демо, тест", Name , REG_SZ, """C:\a b\x.exe""")");
    REQUIRE(f.size() == 5);
    CHECK_EQ(f[0], std::string("HKCU"));
    CHECK_EQ(f[1], std::string("Software\\Демо, тест"));
    CHECK_EQ(f[2], std::string("Name"));
    CHECK_EQ(f[3], std::string("REG_SZ"));
    CHECK_EQ(f[4], std::string("\"C:\\a b\\x.exe\""));
}

TEST(sif_split_empty_fields)
{
    std::vector<std::string> f = sifSplit("a,,  ,\"\"");
    REQUIRE(f.size() == 4);
    CHECK_EQ(f[1], std::string());
    CHECK_EQ(f[2], std::string());
    CHECK_EQ(f[3], std::string());
}

TEST(sif_split_keeps_spaces_inside_quotes)
{
    std::vector<std::string> f = sifSplit("x,   \" a \"  , y");
    REQUIRE(f.size() == 3);
    CHECK_EQ(f[1], std::string(" a "));
    CHECK_EQ(f[2], std::string("y"));
}

TEST(sif_quote_join_roundtrip)
{
    std::vector<std::string> in = {"Programs", "Группа\\Имя, с запятой", "%INSTALLDIR%\\a.exe", "-x \"y\"", "a;b"};
    std::vector<std::string> out = sifSplit(sifJoin(in));
    CHECK(out == in);
    CHECK_EQ(sifQuote("plain"), std::string("plain"));
    CHECK_EQ(sifQuote(""), std::string("\"\""));
    CHECK_EQ(sifQuote("a=b"), std::string("\"a=b\""));
}

TEST(sif_join_drops_trailing_empty)
{
    CHECK_EQ(sifJoin({"Desktop", "Имя", "C:\\a.exe", "", ""}), std::string("Desktop, Имя, C:\\a.exe"));
}

TEST(sif_unquote)
{
    CHECK_EQ(sifUnquote("  \"a \"\"b\"\" c\"  "), std::string("a \"b\" c"));
    CHECK_EQ(sifUnquote("plain"), std::string("plain"));
    CHECK_EQ(sifUnquote("\""), std::string("\""));
}

TEST(sif_parse_sections_keys_lists)
{
    Sif s;
    std::string err;
    REQUIRE(s.parse("\xEF\xBB\xBF; комментарий\r\n[Product]\r\nName = \"Демо, программа\"\r\nVersion=1.2\r\n\r\n"
                    "[Список]\r\nпервый\r\n\"второй = не ключ\"\r\nтретий, \"x=y\"\r\n",
                    err));
    CHECK_EQ(s.get("product", "NAME"), std::string("Демо, программа"));
    CHECK_EQ(s.getInt("Product", "Version", 0), 0);   // не целое
    CHECK_EQ(s.get("Product", "Нет", "по умолчанию"), std::string("по умолчанию"));
    std::vector<std::string> l = s.list("Список");
    REQUIRE(l.size() == 3);
    CHECK_EQ(l[0], std::string("первый"));
    CHECK_EQ(l[1], std::string("\"второй = не ключ\""));
    CHECK_EQ(l[2], std::string("третий, \"x=y\""));
}

TEST(sif_parse_errors)
{
    Sif s;
    std::string err;
    CHECK(!s.parse("key = value\n", err));
    CHECK(err.find("вне секции") != std::string::npos);
    CHECK(!s.parse("[Незакрытая\n", err));
}

TEST(sif_parse_ansi_fallback)
{
    // «Привет» в кодировке 1251.
    Sif s;
    std::string err;
    REQUIRE_OK(s.parse("[A]\nk = \xCF\xF0\xE8\xE2\xE5\xF2\n", err), err);
    CHECK_EQ(s.get("A", "k"), std::string("Привет"));
}

TEST(sif_save_load_roundtrip)
{
    test::TempDir t;
    Sif s;
    s.set("Раздел", "Ключ", sifQuote("значение с пробелом"));
    s.add("Раздел", sifJoin({"a", "b c"}));
    s.set("Раздел", "Ключ", "новое");    // замена существующего ключа
    std::string err;
    REQUIRE_OK(s.save(t.file("x.sif"), err), err);
    Sif r;
    REQUIRE_OK(r.load(t.file("x.sif"), err), err);
    CHECK_EQ(r.get("Раздел", "Ключ"), std::string("новое"));
    std::vector<std::string> l = r.list("Раздел");
    REQUIRE(l.size() == 2);
    CHECK_EQ(l[1], std::string("a, \"b c\""));
    CHECK(test::readFile(t.file("x.sif")).compare(0, 3, "\xEF\xBB\xBF") == 0);
}
