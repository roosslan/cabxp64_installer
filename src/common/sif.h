#pragma once

#include <string>
#include <vector>

// Файл описания установки в стиле txtsetup.sif: текст в UTF-8, секции [Имя],
// строки «ключ = значение» и строки-списки без ключа. Поля строки-списка
// разделяются запятыми; в кавычках запятая не разделяет поля, а "" означает
// одну кавычку. Строка, начинающаяся с ';', — комментарий.
class Sif
{
public:
    struct Line
    {
        std::string key;        // пустой у строки-списка
        std::string value;
    };

    struct Section
    {
        std::string name;
        std::vector<Line> lines;
    };

    bool parse(const std::string &text, std::string &err);
    bool load(const std::string &path, std::string &err);
    std::string text() const;
    bool save(const std::string &path, std::string &err) const;

    const Section *section(const std::string &name) const;
    Section &addSection(const std::string &name);

    // Значение ключа (без учёта регистра), кавычки по краям снимаются.
    std::string get(const std::string &section, const std::string &key,
                    const std::string &def = std::string()) const;
    int getInt(const std::string &section, const std::string &key, int def) const;

    // Строки секции без ключа (и строки с ключом — целиком, как «ключ = значение»).
    std::vector<std::string> list(const std::string &section) const;

    void set(const std::string &section, const std::string &key, const std::string &value);
    void add(const std::string &section, const std::string &rawLine);

    std::vector<Section> sections;
};

// Разбор строки-списка на поля и обратная сборка.
std::vector<std::string> sifSplit(const std::string &line);
std::string sifJoin(const std::vector<std::string> &fields);
// Поле в кавычках, если в нём есть пробелы, запятые, кавычки или ';'.
std::string sifQuote(const std::string &field);
std::string sifUnquote(const std::string &s);
std::string sifTrim(const std::string &s);
