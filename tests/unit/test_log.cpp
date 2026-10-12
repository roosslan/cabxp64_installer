#include "../../src/common/log.h"
#include "test.h"

TEST(log_write_and_session)
{
    test::TempDir t;
    std::string path = t.file("logs\\x.log");
    REQUIRE(xlog::open(path, "Первый"));
    xlog::write("строка один");
    xlog::close();
    REQUIRE(xlog::open(path, "Второй"));
    xlog::write("строка два");
    xlog::error("сбой\nподробности");
    REQUIRE(xlog::saveSession(t.file("session.log")));
    xlog::close();

    std::string all = test::readFile(path);
    CHECK(all.compare(0, 3, "\xEF\xBB\xBF") == 0);
    CHECK(all.find("=== Первый") != std::string::npos);
    CHECK(all.find("=== Второй") != std::string::npos);
    CHECK(all.find("ОШИБКА: сбой\r\n    подробности") != std::string::npos);

    std::string session = test::readFile(t.file("session.log"));
    CHECK(session.find("строка два") != std::string::npos);
    CHECK(session.find("строка один") == std::string::npos);
    CHECK(session.find("=== Первый") == std::string::npos);
}

TEST(log_closed_is_harmless)
{
    xlog::close();
    xlog::write("никуда");
    CHECK(!xlog::saveSession("C:\\nonexistent\\x.log"));
}
