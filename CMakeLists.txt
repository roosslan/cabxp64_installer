cmake_minimum_required(VERSION 3.16)

project(XPSetup LANGUAGES CXX RC)

if (NOT WIN32)
    message(FATAL_ERROR "XPSetup работает только под Windows: используется Cabinet API (FCI/FDI).")
endif()

# Статическая RTL: программы не зависят от VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(COMMON_SOURCES
    src/common/cabfile.cpp
    src/common/format.cpp
    src/common/fsutil.cpp
    src/common/icons.cpp
    src/common/log.cpp
    src/common/project.cpp
    src/common/sif.cpp
)

# ---------------------------------------------------------------------------
# Программа установки (и удаления): текстовый режим без Turbo Vision.
# ---------------------------------------------------------------------------
add_executable(setup
    src/setup/main.cpp
    src/setup/install.cpp
    src/setup/screen.cpp
    src/setup/ui.cpp
    src/setup/setup.rc
    ${COMMON_SOURCES}
)
target_compile_features(setup PRIVATE cxx_std_17)
target_compile_definitions(setup PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(setup PRIVATE cabinet shell32 ole32 user32 advapi32)
if (MSVC)
    target_compile_options(setup PRIVATE /W3 /utf-8)
    # Манифест задаётся ресурсом setup.rc.
    target_link_options(setup PRIVATE /MANIFEST:NO)
endif()

# ---------------------------------------------------------------------------
# Turbo Vision подключён git-субмодулем tvision (magiblot/tvision) и собирается
# из исходников, как в Cabine: файл строк tvtext2.cpp заменён русифицированным.
# ---------------------------------------------------------------------------
set(TV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tvision" CACHE PATH "Путь к исходникам Turbo Vision")
if (NOT EXISTS "${TV_ROOT}/include/tvision/tv.h")
    message(FATAL_ERROR "Не найден Turbo Vision в ${TV_ROOT}. Необходимо выполнить "
                        "git submodule update --init либо указать -DTV_ROOT=<путь>.")
endif()

file(GLOB_RECURSE TV_SOURCES "${TV_ROOT}/source/*/*.cpp")
list(REMOVE_ITEM TV_SOURCES
    "${TV_ROOT}/source/tvision/geninc.cpp"
    "${TV_ROOT}/source/tvision/tvtext2.cpp"
)

add_library(tvision STATIC ${TV_SOURCES} src/tv/tvtext_ru.cpp)
target_compile_features(tvision PUBLIC cxx_std_14)
target_include_directories(tvision PUBLIC "${TV_ROOT}/include")
target_include_directories(tvision PRIVATE
    "${TV_ROOT}/include/tvision"
    "${TV_ROOT}/include/tvision/compat"
)
target_compile_definitions(tvision
    PUBLIC _CRT_SECURE_NO_WARNINGS
    PRIVATE TVISION_NO_STL
)
if (MSVC)
    target_compile_options(tvision PUBLIC
        /wd4068 /wd4146 /wd4166 /wd4244 /wd4250 /wd4267 /wd4996
        /permissive- /Zc:__cplusplus /Zc:externConstexpr /Zc:inline
        /utf-8
    )
endif()
target_precompile_headers(tvision PRIVATE "${TV_ROOT}/include/pch.h")
set_source_files_properties(src/tv/tvtext_ru.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# ---------------------------------------------------------------------------
# Сборщик дистрибутивов. Программа установки встраивается в него ресурсом.
# ---------------------------------------------------------------------------
add_executable(xpsbuild
    src/builder/main.cpp
    src/builder/app.cpp
    src/builder/build.cpp
    src/builder/projdlg.cpp
    src/builder/textview.cpp
    src/builder/builder.rc
    src/tv/dialogs.cpp
    src/tv/hotkeys.cpp
    ${COMMON_SOURCES}
)
target_compile_features(xpsbuild PRIVATE cxx_std_17)
target_compile_definitions(xpsbuild PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
# Модули из Cabine (src/tv) подключают общие заголовки без пути.
target_include_directories(xpsbuild PRIVATE src/common)
target_link_libraries(xpsbuild PRIVATE tvision cabinet shell32 shlwapi ole32 user32 advapi32)
if (MSVC)
    target_compile_options(xpsbuild PRIVATE /W3)
endif()

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/setupstub/$<CONFIG>/setup_stub_path.h"
     CONTENT "#define XPSETUP_SETUP_STUB \"$<TARGET_FILE:setup>\"
")
target_include_directories(xpsbuild PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/setupstub/$<CONFIG>")
add_dependencies(xpsbuild setup)
# Ресурс пересобирается при изменении setup.exe.
set_source_files_properties(src/builder/builder.rc PROPERTIES OBJECT_DEPENDS "$<TARGET_FILE:setup>")

# ---------------------------------------------------------------------------
# Проверка текстового интерфейса через псевдоконсоль (см. tests/README.md).
# ---------------------------------------------------------------------------
add_executable(tuitest tests/tuitest.cpp)
target_compile_features(tuitest PRIVATE cxx_std_17)
target_compile_definitions(tuitest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
if (MSVC)
    target_compile_options(tuitest PRIVATE /W3 /utf-8)
endif()

# ---------------------------------------------------------------------------
# Модульные и интеграционные тесты (ctest -C Release).
# ---------------------------------------------------------------------------
enable_testing()
add_executable(unittests
    tests/unit/main.cpp
    tests/unit/test_build.cpp
    tests/unit/test_cab.cpp
    tests/unit/test_icons.cpp
    tests/unit/test_install.cpp
    tests/unit/test_log.cpp
    tests/unit/test_project.cpp
    tests/unit/test_sif.cpp
    src/builder/build.cpp
    src/setup/install.cpp
    ${COMMON_SOURCES}
)
target_compile_features(unittests PRIVATE cxx_std_17)
target_compile_definitions(unittests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(unittests PRIVATE cabinet shell32 ole32 user32 advapi32)
if (MSVC)
    target_compile_options(unittests PRIVATE /W3 /utf-8)
endif()
add_test(NAME unittests COMMAND unittests)
