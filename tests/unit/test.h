#pragma once

// Минимальный каркас модульных тестов: TEST, CHECK, CHECK_EQ, REQUIRE.

#include <cstdint>
#include <string>
#include <vector>

namespace test {

struct Case
{
    const char *name;
    void (*fn)();
};

std::vector<Case> &registry();

struct Registrar
{
    Registrar(const char *name, void (*fn)()) { registry().push_back({name, fn}); }
};

// Ошибка проверки REQUIRE прерывает тест.
struct Abort
{
};

void report(const char *file, int line, const std::string &expr, const std::string &detail);

inline std::string show(const std::string &s) { return "\"" + s + "\""; }
inline std::string show(const char *s) { return show(std::string(s)); }
inline std::string show(bool v) { return v ? "true" : "false"; }
template <class T>
std::string show(const T &v) { return std::to_string(v); }

// Временная директория, удаляемая по завершении теста.
class TempDir
{
public:
    TempDir();
    ~TempDir();
    const std::string &path() const { return dir; }
    std::string file(const std::string &rel) const;

private:
    std::string dir;
};

// Настоящий исполняемый файл (сам тест) — заглушка программы установки там,
// где важен заголовок PE: по нему ищется дописанный CAB.
std::string realStub();

// Запись файла (с созданием директорий).
void writeFile(const std::string &path, const std::string &data);
std::string readFile(const std::string &path);

} // namespace test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                              \
    static void name();                                                         \
    static test::Registrar TEST_CAT(reg_, name)(#name, &name);                  \
    static void name()

#define CHECK(expr)                                                             \
    do                                                                          \
    {                                                                           \
        if (!(expr))                                                            \
            test::report(__FILE__, __LINE__, #expr, std::string());             \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do                                                                          \
    {                                                                           \
        const auto &check_a_ = (a);                                             \
        const auto &check_b_ = (b);                                             \
        if (!(check_a_ == check_b_))                                            \
            test::report(__FILE__, __LINE__, #a " == " #b,                      \
                         test::show(check_a_) + " != " + test::show(check_b_)); \
    } while (0)

// Проверка вызова с выводом текста ошибки err.
#define REQUIRE_OK(expr, err)                                                   \
    do                                                                          \
    {                                                                           \
        if (!(expr))                                                            \
        {                                                                       \
            test::report(__FILE__, __LINE__, #expr, (err));                     \
            throw test::Abort();                                                \
        }                                                                       \
    } while (0)

#define REQUIRE(expr)                                                           \
    do                                                                          \
    {                                                                           \
        if (!(expr))                                                            \
        {                                                                       \
            test::report(__FILE__, __LINE__, #expr, "(тест прерван)");          \
            throw test::Abort();                                                \
        }                                                                       \
    } while (0)
