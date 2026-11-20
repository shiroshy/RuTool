# RuTool 2.0.8 AppImage — результат проверки 2026-11-20

## Артефакт

* Путь: `/mnt/2tb/Projects/RuToolRecompile/RuTool/client-v2/dist-appimage/RuTool-2.0.8-x86_64.AppImage`.
* Размер: **49 908 216 байт** (47.60 MiB).
* SHA-256: `3f4504f6adca673f4daded9c7fc256bc6f60332596df74fb8174eb6bf144650e`.
* x86_64 ELF64, AppImage **Type 2**, статический runtime, SquashFS начинается с offset **944632**.
* Проверены 1709 обычных файлов, в том числе 182 ELF, внутри извлечённого пакета.
* База сборки: Ubuntu 24.04, GCC 14, Qt 6.8.3. Заявленная граница — **glibc >=2.39**.
* Сборка подготовлена поверх HEAD `78abdbc` ветки `linux-port`.

## Действительно пройденные проверки

| Проверка | Результат |
| --- | --- |
| Release CMake/Ninja, C++23, `-Werror`, `BUILD_TESTING=OFF` | Успех, отдельный `build-appimage/` |
| Автоматические тесты FHS-сборки с `BUILD_TESTING=ON` | CTest 2/2; QtTest 12 passed, 0 failed |
| Конфигурация | Оригинальный `installer-keys.json` побайтно совпадает с упакованным; `update-public.der` также совпадает |
| Поиск конфигурации | Тест штатного файла, пользовательского XDG override, неверного override и битой ссылки |
| ELF и зависимости | Все 182 ELF — x86_64; нет `/nix/store` в ELF interpreter/DT_NEEDED/RPATH/RUNPATH; зависимости разрешаются |
| Независимость от Qt SDK | Qt, OpenSSL и libstdc++ разрешаются внутри AppDir; отдельный запуск без установленного Qt успешен |
| Qt/QML | Включены QtQuick, Controls, Dialogs, Shapes, Templates, Window, Layouts, WorkerScript; production QML загружается |
| Plugins | XCB, Wayland, offscreen, imageformats/GIF/SVG, OpenSSL TLS, Wayland integration присутствуют |
| Нежелательные файлы | Нет QtTest/CTest, tests/fixtures, `.rut`/ZIP, Python-генераторов, CMake cache, объектов, статических библиотек и ELF debug info |
| Ссылки | Нет ссылок за пределы AppDir |
| Готовый AppImage | Реально создан appimagetool; проверены заголовок Type 2, ELF, SquashFS; содержимое извлечено и повторно проверено |
| AppDir offscreen | `AppRun --smoke-test --smoke-screenshot` успешен |
| Извлечённый AppImage offscreen | `AppRun --smoke-test --smoke-screenshot` успешен |
| Чистый Ubuntu-контейнер | Нет Qt/SDK и `/nix/store`, окружение `env -i`; production QML отрисован успешно |
| NixOS | Готовый файл запущен через `/run/binfmt/appimage` (appimage-run), offscreen smoke успешен |
| Настоящий Wayland-дисплей | Извлечённый payload этого AppImage запущен в Docker через сокет дисплея, software rendering; smoke и screenshot успешны |
| Git/shell | `git diff --check` и `bash -n scripts/*.sh` успешны |
| Ручная проверка на другом компьютере | Пользователь сообщил, что AppImage успешно сработал на компьютере друга; ОС и перечень проверенных действий не указаны |

В чистом контейнере установлены только обычные системные runtime-библиотеки,
шрифты, CA и инструменты извлечения/проверки. Qt и дерево исходников туда не
подключались. Пропущенный при первом прогоне `libOpenGL.so.0` добавлен в AppImage;
после исправления чистый запуск повторён успешно.

В AppImage не включён настоящий `/etc/machine-id`; тестовый контейнер использует
синтетическое значение. Значение `RutPassword` не выводилось и не помещалось
в отчёты; конфигурация остаётся отдельным JSON-файлом.

## Границы проверки

* Прямой `--appimage-extract-and-run` внутри Docker перехватывается обработчиком
  binfmt хоста NixOS, указывающим на отсутствующий в контейнере `/run/binfmt/appimage`.
  Поэтому использован эквивалент: извлечение SquashFS и запуск `AppRun`.
  Оригинальные инструменты, runtime и итоговый файл не патчились.
* Запуск самого готового файла через NixOS appimage-run проверен в offscreen.
  Попытка Wayland через appimage-run из песочницы получила `wl_display: Operation
  not permitted`. Wayland успешно проверен отдельно через Docker и сокет дисплея.
* GUI-проверка — автоматический smoke с отрисовкой окна на настоящем дисплее;
  полный интерактивный сценарий и аппаратное ускорение не проверялись.
* X11, FUSE mounting, Windows-сборка и матрица совместимости дистрибутивов
  автоматически не проверялись.
* Во время автоматической проверки реальная игра не изменялась, её копии не
  создавались, перевод не устанавливался. Production API, скачивание настоящего
  `.rut` и установка ранее проверены пользователем на нативном Linux-клиенте.
  Последующая успешная проверка AppImage на другом компьютере зафиксирована
  со слов пользователя, без уточнения отдельных операций.
* Побитовая воспроизводимость не заявляется: версии apt и транзитивных зависимостей
  aqt не закреплены полностью. Инструменты deployment закреплены URL и SHA-256 в Git.

## Локальные свидетельства

* `dist-appimage/SHA256SUMS`, `deployment-tools.sha256` — контрольные суммы.
* `dist-appimage/appdir-smoke.png`, `appimage-smoke.png` — проверки упаковки.
* `build-linux-validation/package.log`, `ctest.log`, `final-tests.log` — логи.
* `build-linux-validation/clean-smoke.log`, `clean-smoke.png` — контейнер без Qt.
* `build-linux-validation/nixos-smoke.png`, `wayland-smoke.png` — NixOS и дисплей.
* `build-linux-validation/contents.txt` — список извлечённого содержимого.

Промежуточные SDK/build/логи и неудачные попытки находятся вне AppDir/AppImage,
в игнорируемых `build-linux-*`. В репозиторий они не добавлялись.

## Изменённые и добавленные файлы

* `src/core/InstallerConfig.cpp` — штатный файл с приоритетным XDG override.
* `CMakeLists.txt` — копирование и установка оригинального JSON только на Linux.
* `tests/linux/LinuxTests.cpp` — регрессионная проверка поиска конфигурации.
* `scripts/build-linux.sh` — корректная поддержка `BUILD_TESTING=OFF`.
* `scripts/package-appimage.sh` — извлечение закреплённых инструментов, доступ к Qt
  при deployment, libOpenGL, проверки готового пакета и повторный smoke.
* `scripts/check-appdir.py` — оригинальный JSON, запрет тестов/фикстур/build/debug,
  обязательные библиотеки и QML-модули.
* `scripts/extract-appimage.py` — проверяемое извлечение Type 2 без FUSE/binfmt.
* `scripts/fetch-appimage-tools.sh`, `packaging/linux/deployment-tools.lock` — загрузка
  конкретных официальных релизов с проверкой закреплённых SHA-256.
* `scripts/build-appimage-container.sh` — полный сценарий с отдельными тестовой и
  релизной сборками; SDK на диске проекта.
* `scripts/verify-appimage-clean.sh` — проверка в новом контейнере без Qt.
* `packaging/linux/Dockerfile` — digest Ubuntu и отдельная стадия sdk-bootstrap.
* `.gitignore` — исключение каталогов повторной сборки `dist-appimage*`.
* `BUILDING-LINUX.md`, этот отчёт — актуальные инструкции и результаты.

Механизмы установки, восстановления, криптографии и QML-интерфейс не переписывались.
Оригинальный `config/installer-keys.json` не изменён. Изменение CMake ограничено
Linux-ветвью; поведение Windows сохранено по статическому просмотру.

## Команды

Запуск на текущем NixOS:

```sh
/run/binfmt/appimage /mnt/2tb/Projects/RuToolRecompile/RuTool/client-v2/dist-appimage/RuTool-2.0.8-x86_64.AppImage
```

Обычный эквивалент при наличии команды в PATH:

```sh
appimage-run ./RuTool-2.0.8-x86_64.AppImage
```

Повторная сборка из `client-v2`, в новый каталог:

```sh
./scripts/build-appimage-container.sh dist-appimage-rebuild
./scripts/verify-appimage-clean.sh \
  dist-appimage-rebuild/RuTool-2.0.8-x86_64.AppImage build-linux-validation-rebuild
```

Проверка существующего файла:

```sh
(cd dist-appimage && sha256sum --check SHA256SUMS)
```
