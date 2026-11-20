# Нативная Linux-сборка RuTool 2.0.8

## Текущий результат и ограничения

ELF и production QML проверены на NixOS с GCC 15.3.0 / Qt 6.11.2, со строгими
предупреждениями `-Werror`. Нативный исполняемый файл: `build-linux/RuTool`.
Это локальная сборка, привязанная к установленному SDK; **она не является
переносимым AppImage**. Снимок интерфейса: `build-linux/linux-smoke.png`.

Сценарий AppImage подготовлен, но сборка AppImage в данной среде не выполнена:
нет linuxdeploy/appimagetool, shell не может разрешить GitHub DNS, а доступ
к Docker socket отклонён. Dockerfile и полный процесс deployment требуют
проверки в обычной Linux-среде. Бинарная совместимость Windows, Wayland/X11
на реальном дисплее, production API/Boosty и настоящие пакеты не проверены.
Нельзя объявлять этот результат готовым релизом.

## Сборка

Нужны Linux x86_64, CMake >=3.25, Ninja, C++23-компилятор со `std::expected`
(например GCC 13/14), Qt >=6.8 с Core, Gui, Qml, Quick, QuickControls2,
Network, Concurrent и Test. Для упаковки также нужны плагины Wayland,
QtSvg, imageformats, OpenSSL TLS backend и инструменты Qt QML.
Сторонние Mbed TLS и zlib уже включены в исходники; Python не нужен для
обычного запуска тестов.

Из `client-v2`:

```sh
export CMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
./scripts/build-linux.sh
./build-linux/RuTool
```

Или `cmake --preset linux-release`, `cmake --build --preset linux-release`,
`ctest --test-dir build-linux --output-on-failure`.
На NixOS для локальной компиляции нужно предоставить обычные пути к уже
установленным компилятору, CMake/Ninja, Qt и OpenGL через PATH и
CMAKE_PREFIX_PATH. Проект не создаёт flake и не устанавливает Nix-пакеты.

Для безопасной проверки production QML без API, поиска игры и постоянных
настроек:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ./build-linux/RuTool --smoke-test --smoke-screenshot ./build-linux/window.png
```

Два CTest-набора проверяют production QML и Linux-платформу. В платформенном
наборе: PBKDF2/AES-GCM/P-256, локальное шифрование и права ключа, ссылки,
блокировки, fsync/rename, Steam/Proton, процесс игры, многоблочный `.rut`,
установка/отказ commit guard/восстановление, повреждённый журнал, отмена,
скачивание/докачка/хеши/HTTP Range/redirect через искусственный транспорт.
Все изменяемые файлы создаются в QTemporaryDir. Тестовый EC scalar 1 —
общеизвестная фикстура, не производственный ключ; реальные ключи подписи
не нужны. Для пересоздания фикстур требуется Python `cryptography >=43`:
`python3 tests/linux/generate_fixtures.py`.

## Конфигурация и данные

* Публичный `update-public.der` копируется в `build-linux/app/config` при
  конфигурации и устанавливается в `usr/bin/app/config` при упаковке.
* Пароль `.rut` читается из `${XDG_CONFIG_HOME:-$HOME/.config}/rutool/installer-keys.json`.
  Формат прежний: `FormatVersion: 1`, `RutPassword: строка`.
  **Используй свой разрешённый файл; реальные значения не включаются в
  примеры, AppDir или AppImage.** Без него production-пакет не расшифруется.
* Операционные настройки, загрузки, оригинальные копии:
  `${XDG_DATA_HOME:-$HOME/.local/share}/RuToolInstaller/`.
* Ключ локального AES-GCM: `RuToolInstaller/private/local.key` внутри этого
  каталога, права `0600`; каталог `private` — `0700`.
  Сохраняй этот ключ вместе с резервными данными: его утрата делает сохранённые
  коды и незавершённые журналы восстановления недоступными.
* Настройки окна: `${XDG_DATA_HOME:-$HOME/.local/share}/RuTool/RuToolInstaller/settings.ini`.

DPAPI-данные Windows не переносятся автоматически. Расширение журнала
`.baretool-transaction.dpapi` сохранено ради общей логики, но Linux-содержимое
имеет собственный заголовок `RULOC1` и аутентифицировано AES-GCM.
Linux-обновление приложения отключено: заменяй AppImage вручную. Проверки
подписей пакетов, серверные attestations и аутентификация сохранены.
Steam-поиск учитывает libraryfolders.vdf, appmanifest, Flatpak и типовые
каталоги Proton. Обнаруженные алиасы Steam приводятся к реальному пути.
Если установок несколько, автоматически выбирается первая по сортировке;
для другой используй существующую кнопку выбора папки.

## AppImage

Собирать переносимый релиз нужно в обычном Linux SDK с подходящим glibc,
например в Ubuntu 24.04 x86_64. Такая сборка ориентирована на glibc >=2.39;
поддержку более старых систем нужно проверять отдельной сборкой на старой базе.
Сценарий намеренно отвергает ELF с зависимостями от `/nix/store`.
Qt/QML, Qt плагины, OpenSSL и libstdc++ включаются в AppDir; glibc и
графические драйверы остаются системными. Доверенные CA берутся из ОС.

Подготовь отдельный каталог инструментов, скачанных из официальных релизов:

* https://github.com/linuxdeploy/linuxdeploy/releases
* https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases
* https://github.com/AppImage/appimagetool/releases
* https://github.com/AppImage/type2-runtime/releases

В нём должны лежать `linuxdeploy-x86_64.AppImage`,
`linuxdeploy-plugin-qt-x86_64.AppImage`, `appimagetool-x86_64.AppImage`,
`runtime-x86_64` и `SHA256SUMS`. Первые три файла должны быть исполняемыми.
Выбирай конкретные версии/релизные артефакты, проверяй их происхождение и
фиксируй SHA-256 каждого файла в SHA256SUMS; сценарий не скачивает `latest`.
Копию lock-файла он сохраняет рядом с результатом.

```sh
export QMAKE=/path/to/Qt/6.8.3/gcc_64/bin/qmake
export RUTOOL_TOOLS_DIR=/path/to/pinned-tools
export SOURCE_DATE_EPOCH=1767225600
./scripts/package-appimage.sh ./build-linux ./dist-linux
```

Каталог результата должен быть новым. Сценарий выполняет CTest, устанавливает
AppDir, собирает Qt/QML и динамические плагины, проверяет ELF-зависимости,
запрещает секретные файлы и внешние ссылки, выполняет QML smoke-test, создаёт
`dist-linux/RuTool-2.0.8-x86_64.AppImage`, запускает smoke-test AppImage и
считает SHA-256. Проверки не отключаются ради получения файла.
Для строгой побитовой воспроизводимости дополнительно зафиксируй digest
контейнера, apt snapshot/версии пакетов, все Python-зависимости aqt и Qt SDK;
одного SOURCE_DATE_EPOCH недостаточно. Текущий Dockerfile служит рецептом
среды, а не утверждением о побитовой воспроизводимости.

Пример сборки в контейнере (из client-v2; BASE_IMAGE задай как
`ubuntu:24.04@sha256:...` с проверенным digest):

```sh
docker build --build-arg BASE_IMAGE="$BASE_IMAGE" \
  -f packaging/linux/Dockerfile -t rutool-linux-builder .
docker run --rm --user "$(id -u):$(id -g)" \
  -e HOME=/tmp -v "$PWD:/work" -v "$RUTOOL_TOOLS_DIR:/tools:ro" \
  rutool-linux-builder bash -lc \
  './scripts/build-linux.sh build-appimage && ./scripts/package-appimage.sh build-appimage dist-appimage'
```

Контекст Docker исключает config/installer-keys.json; сам Dockerfile не
копирует исходники. Не публикуй контейнерный build context или артефакты с
другими локальными секретами. Сценарии не выполняют публикацию.

## Запуск

```sh
chmod +x RuTool-2.0.8-x86_64.AppImage
./RuTool-2.0.8-x86_64.AppImage
```

Без FUSE: `APPIMAGE_EXTRACT_AND_RUN=1 ./RuTool-2.0.8-x86_64.AppImage`.
На NixOS обычно нужен предоставляемый системой `appimage-run`:
`appimage-run ./RuTool-2.0.8-x86_64.AppImage`.
Он обеспечивает FHS-окружение и ELF loader; Qt вручную устанавливать не нужно.
Это запуск стандартного AppImage, не отдельная Nix-упаковка приложения.
При проблеме Wayland можно проверить `QT_QPA_PLATFORM=xcb`; реальный
Wayland/X11 и запуск без установленного Qt требуют тестов готового артефакта.
