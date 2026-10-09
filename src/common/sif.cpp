#include "sif.h"

#include <cstdlib>

#include "fsutil.h"

std::string sifTrim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string sifUnquote(const std::string &s)
{
    std::string t = sifTrim(s);
    if (t.size() < 2 || t.front() != '"' || t.back() != '"')
        return t;
    std::string r;
    for (size_t i = 1; i + 1 < t.size(); ++i)
    {
        if (t[i] == '"' && i + 2 < t.size() && t[i + 1] == '"')
            ++i;
        r += t[i];
    }
    return r;
}

std::vector<std::string> sifSplit(const std::string &line)
{
    std::vector<std::string> fields;
    std::string cur;
    bool quoted = false, wasQuoted = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];
        if (quoted)
        {
            if (c == '"')
            {
                if (i + 1 < line.size() && line[i + 1] == '"')
                {
                    cur += '"';
                    ++i;
                }
                else
                    quoted = false;
            }
            else
                cur += c;
        }
        else if (c == '"')
        {
            if (sifTrim(cur).empty())
                cur.clear();    // пробелы перед открывающей кавычкой
            quoted = wasQuoted = true;
        }
        else if (c == ',')
        {
            fields.push_back(wasQuoted ? cur : sifTrim(cur));
            cur.clear();
            wasQuoted = false;
        }
        else if (!(wasQuoted && (c == ' ' || c == '\t')))
            cur += c;
    }
    fields.push_back(wasQuoted ? cur : sifTrim(cur));
    return fields;
}

std::string sifQuote(const std::string &field)
{
    if (!field.empty() && field.find_first_of(" \t,\";=") == std::string::npos)
        return field;
    std::string r = "\"";
    for (char c : field)
    {
        if (c == '"')
            r += '"';
        r += c;
    }
    return r + "\"";
}

std::string sifJoin(const std::vector<std::string> &fields)
{
    std::string r;
    for (size_t i = 0; i < fields.size(); ++i)
    {
        if (i)
            r += ", ";
        r += sifQuote(fields[i]);
    }
    // Пустые поля в конце не пишем.
    while (r.size() >= 4 && r.compare(r.size() - 4, 4, ", \"\"") == 0)
        r.resize(r.size() - 4);
    return r;
}

static bool sameName(const std::string &a, const std::string &b)
{
    return fsu::upper(a) == fsu::upper(b);
}

bool Sif::parse(const std::string &src, std::string &err)
{
    sections.clear();
    std::string text = src;
    if (text.size() >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB &&
        (unsigned char) text[2] == 0xBF)
        text.erase(0, 3);
    if (!fsu::isValidUtf8(text))
        text = fsu::fromCodePage(text, 1251);   // старые файлы в ANSI

    Section *cur = nullptr;
    size_t pos = 0;
    int lineNo = 0;
    while (pos <= text.size())
    {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = sifTrim(text.substr(pos, eol - pos));
        pos = eol + 1;
        ++lineNo;
        if (line.empty() || line[0] == ';')
            continue;
        if (line[0] == '[')
        {
            size_t close = line.find(']');
            if (close == std::string::npos)
            {
                err = "Строка " + std::to_string(lineNo) + ": не закрыто имя секции";
                return false;
            }
            cur = &addSection(sifTrim(line.substr(1, close - 1)));
            continue;
        }
        if (!cur)
        {
            err = "Строка " + std::to_string(lineNo) + ": данные вне секции";
            return false;
        }
        // Ключ есть, если '=' стоит до первой кавычки и запятой.
        size_t eq = line.find('=');
        size_t stop = line.find_first_of("\",");
        Line l;
        if (eq != std::string::npos && (stop == std::string::npos || eq < stop))
        {
            l.key = sifTrim(line.substr(0, eq));
            l.value = sifTrim(line.substr(eq + 1));
        }
        else
            l.value = line;
        cur->lines.push_back(l);
    }
    return true;
}

bool Sif::load(const std::string &path, std::string &err)
{
    std::string data;
    if (!fsu::readFile(path, data, 16u << 20))
    {
        err = "Не удалось прочитать файл " + path + "\n" + fsu::lastErrorText();
        return false;
    }
    std::string perr;
    if (!parse(data, perr))
    {
        err = path + "\n" + perr;
        return false;
    }
    return true;
}

std::string Sif::text() const
{
    std::string r;
    for (size_t i = 0; i < sections.size(); ++i)
    {
        if (i)
            r += "\r\n";
        r += "[" + sections[i].name + "]\r\n";
        for (const Line &l : sections[i].lines)
            r += (l.key.empty() ? l.value : l.key + " = " + l.value) + "\r\n";
    }
    return r;
}

bool Sif::save(const std::string &path, std::string &err) const
{
    std::string data = "\xEF\xBB\xBF" + text();
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

const Sif::Section *Sif::section(const std::string &name) const
{
    for (const Section &s : sections)
        if (sameName(s.name, name))
            return &s;
    return nullptr;
}

Sif::Section &Sif::addSection(const std::string &name)
{
    for (Section &s : sections)
        if (sameName(s.name, name))
            return s;
    sections.push_back(Section{name, {}});
    return sections.back();
}

std::string Sif::get(const std::string &sec, const std::string &key, const std::string &def) const
{
    if (const Section *s = section(sec))
        for (const Line &l : s->lines)
            if (!l.key.empty() && sameName(l.key, key))
                return sifUnquote(l.value);
    return def;
}

int Sif::getInt(const std::string &sec, const std::string &key, int def) const
{
    std::string v = get(sec, key);
    if (v.empty())
        return def;
    char *end = nullptr;
    long n = strtol(v.c_str(), &end, 10);
    return end && *end == 0 ? (int) n : def;
}

std::vector<std::string> Sif::list(const std::string &sec) const
{
    std::vector<std::string> r;
    if (const Section *s = section(sec))
        for (const Line &l : s->lines)
            r.push_back(l.key.empty() ? l.value : l.key + " = " + l.value);
    return r;
}

void Sif::set(const std::string &sec, const std::string &key, const std::string &value)
{
    Section &s = addSection(sec);
    for (Line &l : s.lines)
        if (!l.key.empty() && sameName(l.key, key))
        {
            l.value = value;
            return;
        }
    s.lines.push_back(Line{key, value});
}

void Sif::add(const std::string &sec, const std::string &rawLine)
{
    addSection(sec).lines.push_back(Line{std::string(), rawLine});
}
