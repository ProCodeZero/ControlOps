# RusAgroBack_Control_Operations

**Кратко:** это C++-утилита для расчёта сроков и статусов технологических операций на больших объёмах (миллионы записей). Получает/читает данные из базы (Postgres), выполняет расчёты (модуль `SapControlTech` и прочее) и сохраняет результаты (в том числе массовыми вставками). Проект ориентирован на кроссплатформенную сборку (Windows / Linux) через CMake.

## Что делает программа
- Парсит входные данные из PostgreSQL (через libpqxx).
- Выполняет расчёты дат/статусов операций (модуль `SapControlTech` и др.).
- Записывает агрегированные результаты в БД быстрыми bulk-операциями.

## Ключевые возможности
- Быстрые bulk INSERT (pqxx::stream_to / COPY).
- Параллельные вычисления с per-thread DB connections.
- Конфигурация через `config/db_config.yml` (файл с секретами **не** хранится в репозитории).

## Структура репозитория
```
/RusAgroBack_Control_Operations
  /src                # исходники .cpp .hpp
  /include            # публичные заголовки (если есть)
  /config             # config/db_config.yml (локально), db_config.yml.example (в репо)
  /logs               # ротация логов (ignored)
  CMakeLists.txt
  README.md
```

## Требования
- CMake >= 3.18
- vcpkg (рекомендовано для Windows) или системные пакеты на Linux
- Visual Studio 2022 (MSVC) или GCC/Clang на Linux
- libpqxx (Postgres C++ client)
- Boost (component `date_time`)
- spdlog
- yaml-cpp (опционально, если используется)

## Конфигурация
В репозитории хранится только `config/db_config.yml.example`.  
Скопируйте пример и заполните реальные значения:
```bash
cp config/db_config.yml.example config/db_config.yml
# затем отредактируйте config/db_config.yml
```
Файл `config/db_config.yml` автоматически копируется в папку с бинарником при сборке (см. CMake).

## Сборка — Linux (пример)
```bash
sudo apt update
sudo apt install build-essential cmake libboost-date-time-dev libpqxx-dev libyaml-cpp-dev libssl-dev pkg-config

mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```
### Debug vs Release (коротко)
- `-DCMAKE_BUILD_TYPE=Debug` включает отладочную информацию и отключает оптимизации.  
- `-DCMAKE_BUILD_TYPE=Release` включает оптимизации и отключает/минимизирует отладочную информацию.  
- На Linux/WSL с генераторами типа "Unix Makefiles" или "Ninja" нужно указывать `CMAKE_BUILD_TYPE` при конфигурировании.  
- Visual Studio и другие *multi-config* генераторы не используют `CMAKE_BUILD_TYPE` на этапе конфигурации; для них указывают конфигурацию при сборке: `cmake --build . --config Release`.

---

## Сборка — Windows (GCC / MinGW-w64 через MSYS2) — рекомендовано для работы с GCC на Windows
1. Установите MSYS2: https://www.msys2.org  
2. Откройте **MSYS2 MinGW 64-bit** shell и выполните:
```bash
pacman -Syu
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-pkg-config mingw-w64-x86_64-make
# опционально:
pacman -S mingw-w64-x86_64-libpqxx mingw-w64-x86_64-yaml-cpp mingw-w64-x86_64-spdlog
```
3. Перейдите в проект (в MSYS2 пути к дискам выглядят как `/d/...`) и соберите:
```bash
cd /d/Projects/Cpp/RusAgroBack_Control_Operations
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -- -j$(nproc)
```

Если вы хотите сборку Release:
```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)
```

### Запуск сборки из обычного `cmd.exe`
Если вы предпочитаете работать в `cmd.exe`, добавьте в PATH каталог с MinGW (пример `C:\msys64\mingw64\bin`) и вызывайте CMake с указанием генератора и компиляторов:
```cmd
set PATH=C:\msys64\mingw64\bin;%PATH%
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe ^
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
cmake --build build -- -j4
```
**ВАЖНО:** Если в каталоге `build` уже есть кэш от другого генератора (например `NMake Makefiles`/Visual Studio), удалите папку `build` или как минимум `build\CMakeCache.txt` и `build\CMakeFiles` перед переключением генератора.

---

## Сборка — Windows (MSVC + vcpkg)
(Если предпочитаете MSVC / vcpkg)
1. Установите vcpkg и нужные пакеты:
```powershell
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\vcpkg install spdlog libpqxx boost-date-time yaml-cpp
```
2. Сборка (из PowerShell):
```powershell
mkdir build; cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -A x64 -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
```

---

## Запуск
Запустите бинарник из папки сборки:
```bash
# Linux / WSL / MSYS2 (пример)
./build/RusAgroBack_Control_Operations -full

# Windows (MinGW build)
build\RusAgroBack_Control_Operations.exe -full

# Windows (MSVC, multi-config)
build\Debug\RusAgroBack_Control_Operations.exe -full
```

## Рекомендации
- Для работы с GCC на Windows используйте MSYS2 + MinGW-w64: это даёт pkg-config, mingw32-make и упрощает установку зависимостей.  
- WSL (Ubuntu) — ещё одна стабильная опция; сборки и пакеты в WSL больше похожи на Linux-среду.  
- Для CI предпочтительнее использовать скрипт сборки, который создаёт чистый каталог `build` при каждом прогоне.

---

## Безопасность и развертывание
- Не храните пароли в репозитории — используйте CI secrets или KeyVault.
- На продакшене создавайте `config/db_config.yml` во время деплоя.
- Для больших загрузок используйте временные таблицы и индексацию после загрузки.

## Troubleshooting — частые ошибки
- **CMake пытается вызвать `nmake` / ошибка `CMAKE_CXX_COMPILER not set`**  
  Решение: указать генератор (`-G "MinGW Makefiles"` или `-G "Ninja"`) и/или явно задать `-DCMAKE_C_COMPILER`/`-DCMAKE_CXX_COMPILER`, либо запускать в MSYS2 shell, где установлен `g++`.  
- **Ошибка: `generator does not match the generator used previously`**  
  Удалите `build\CMakeCache.txt` и `build\CMakeFiles` или создайте новый каталог сборки.  
- **libpqxx / yaml-cpp / spdlog не найдены**  
  Установите dev-пакеты через пакетный менеджер (apt, pacman, vcpkg) или укажите пути вручную (`-D..._ROOT` или `-D..._DIR`).  
- **Отсутствуют DLL при запуске в Windows**  
  Добавьте в PATH папку с DLL (например `C:\msys64\mingw64\bin` или `vcpkg_installed\x64-windows\bin`), или скопируйте требуемые DLL рядом с exe.

---
