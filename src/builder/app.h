#pragma once

#include <string>

#define Uses_TApplication
#define Uses_TWindow
#include <tvision/tv.h>

#include "../common/project.h"

class TTextScroller;

// Окно проекта: сводка всех разделов.
class TProjectWindow : public TWindow
{
public:
    explicit TProjectWindow(const TRect &bounds);
    void update(const SetupProject &p, const std::string &path, bool modified);

private:
    TTextScroller *text;
};

class TBuilderApp : public TApplication
{
public:
    TBuilderApp();

    static TMenuBar *initMenuBar(TRect r);
    static TStatusLine *initStatusLine(TRect r);

    void getEvent(TEvent &event) override;
    void handleEvent(TEvent &event) override;
    void idle() override;
    Boolean valid(ushort command) override;

    void openLater(const std::string &path) { pending = path; }

private:
    void start();
    void newProject();
    bool openProject(const std::string &path);
    bool save(bool askName);
    bool confirmDiscard();
    void edited(bool changed);
    void refresh();
    void build();
    void runSetup();
    std::string projectDir() const;

    SetupProject project;
    std::string path;           // пусто у несохранённого проекта
    bool modified = false;
    std::string lastSetup;      // последний собранный setup.exe
    std::string pending;
    bool started = false;
    TProjectWindow *window = nullptr;
};
