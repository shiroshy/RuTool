# Нативная Linux-сборка RuTool 2.0.8

## Состояние порта

Работа продолжается поверх существующего `linux-port`. Пользователь проверил
нативный клиент на настоящей Blue Archive: поиск игры, каталог, скачивание,
расшифровку и подпись `.rut`, установку перевода и восстановление оригиналов.
Эти действия не повторяются автоматически при сборке AppImage.
Пользователь также сообщил об успешной проверке AppImage на компьютере друга.

Локальный `build-linux/RuTool`, собранный с SDK NixOS, не является переносимым
релизом. Для AppImage используется отдельная FHS-сборка Ubuntu 24.04 / GCC 14 /
Qt 6.8.3, без Nix-пакетов внутри артефакта. Базовая совместимость: x86_64,
glibc >=2.39; более старые системы этой сборкой не поддерживаются.

Результаты готового артефакта: [APPIMAGE-2.0.8-VALIDATION.md](docs/APPIMAGE-2.0.8-VALIDATION.md).

## Полная сборка AppImage

Из `client-v2`, при доступном Docker daemon и сети:

```sh
./scripts/build-appimage-container.sh dist-appimage
```

Каталог результата должен отсутствовать; для следующей сборки укажи другое имя.
Сценарий выполняет следующие действия:

1. Собирает `sdk-bootstrap` из `packaging/linux/Dockerfile`. Ubuntu закреплена
   digest; aqtinstall — версией 3.3.0, Qt — версией 6.8.3.
2. Размещает Qt SDK в `build-linux-sdk/`, подключая его к `/opt/Qt` контейнера.
   Это позволяет хранить SDK на диске проекта без дублирования большого слоя Docker.
3. Загружает инструменты из конкретных официальных релизов, проверяя SHA-256
   из `packaging/linux/deployment-tools.lock`. Никаких загрузок `latest`.
4. Собирает `build-linux-release-tests/` с тестами и выполняет CTest. Тестовый
   контейнер получает синтетический `/etc/machine-id`, не данные хоста.
5. Отдельно собирает `build-appimage/` с `BUILD_TESTING=OFF` и `-Werror`.
6. Устанавливает только релизные targets и ресурсы, выполняет Qt/QML deployment,
   проверяет AppDir, загружает production QML в offscreen-режиме.
7. Создаёт `dist-appimage/RuTool-2.0.8-x86_64.AppImage` с закреплённым Type 2 runtime,
   проверяет ELF/SquashFS, извлекает результат, проверяет его состав и повторно
   выполняет QML smoke test. SHA-256 сохраняется в `dist-appimage/SHA256SUMS`.

В AppImage входят Qt/QML, XCB/Wayland/offscreen plugins, GIF/SVG/imageformats,
OpenSSL TLS backend, OpenSSL, libstdc++ и libOpenGL. glibc, графические драйверы и доверенные
CA остаются системными. Сценарии не устанавливают перевод и ничего не публикуют.

В этом NixOS обработчик binfmt AppImage использует `/run/binfmt/appimage`.
В обычном контейнере этот путь недоступен, и прямой запуск AppImage может вернуть
`ENOENT`. Поэтому упаковка извлекает **проверенные оригинальные** инструменты
через SquashFS и запускает их содержимое. Аналогично проверяется готовый пакет.
Заголовки, runtime и содержимое выпускаемого AppImage при этом не изменяются.

Контрольные суммы инструментов фиксированы в Git; для Qt plugin старого релиза
GitHub не предоставляет asset digest, его SHA-256 зафиксирована после загрузки
с официального HTTPS URL. Остальные три суммы сверены с GitHub asset digest.
Версии apt-пакетов и транзитивных Python-зависимостей не закреплены; сборка
повторяемая по сценарию, но побитовая воспроизводимость не заявляется.
`SOURCE_DATE_EPOCH` по умолчанию равен `1767225600`.

## Проверка без установленного Qt

```sh
./scripts/verify-appimage-clean.sh \
  dist-appimage/RuTool-2.0.8-x86_64.AppImage build-linux-validation
```

В новом Ubuntu-контейнере устанавливаются только системные runtime-библиотеки,
шрифты и инструменты проверки. Qt/SDK и исходники приложения туда не подключаются.
Сценарий проверяет отсутствие установленного Qt и `/nix/store`, извлекает AppImage
и запускает production QML с пустым окружением через `AppRun --smoke-test`.
Результат: `build-linux-validation/clean-smoke.log` и `clean-smoke.png`.
Это проверка загрузки и отрисовки QML, а не установка перевода или проверка API.

`check-appdir.py` проверяет x86_64 ELF, интерпретаторы и динамические зависимости,
отсутствие `/nix/store` и внешних ссылок, Qt/QML/TLS plugins, наличие оригинальной
конфигурации. Проверка отклоняет QtTest/CTest, тесты/фикстуры, `.rut`/ZIP,
Python-генераторы, исходники C++, CMake cache, объектные и статические библиотеки.
Она применяется и к AppDir, и к извлечённому готовому AppImage.

## Конфигурация и данные

* Оригинальный публичный `config/installer-keys.json` устанавливается рядом с
  `update-public.der` в `usr/bin/app/config/`. Он не встроен в исполняемый код.
  Приложение находит этот файл автоматически.
* `${XDG_CONFIG_HOME:-$HOME/.config}/rutool/installer-keys.json` имеет приоритет.
  Отсутствующий файл означает использование штатного; ошибочный, нечитаемый
  файл или битая ссылка приводят к ошибке чтения, без молчаливого fallback.
* Значение `RutPassword` не выводится. Проверка пакета сравнивает исходный и
  упакованный файлы по SHA-256 без раскрытия содержимого.
* Операционные данные и оригинальные копии:
  `${XDG_DATA_HOME:-$HOME/.local/share}/RuToolInstaller/`.
* Локальный ключ AES-GCM: `RuToolInstaller/private/local.key`, права `0600`;
  каталог `private` — `0700`. Сохраняй ключ вместе с резервными данными.
* Настройки окна:
  `${XDG_DATA_HOME:-$HOME/.local/share}/RuTool/RuToolInstaller/settings.ini`.

DPAPI-данные Windows автоматически не переносятся. Linux использует формат
`RULOC1` с AES-GCM; расширение журнала `.baretool-transaction.dpapi` сохранено.
Linux self-update отключён: AppImage заменяется вручную. Проверки подписей,
серверные attestations, TLS и аутентификация остаются включены.

## Запуск

На обычном Linux:

```sh
chmod +x RuTool-2.0.8-x86_64.AppImage
./RuTool-2.0.8-x86_64.AppImage
# Без FUSE:
./RuTool-2.0.8-x86_64.AppImage --appimage-extract-and-run
```

На NixOS используется FHS-окружение `appimage-run`:

```sh
appimage-run ./RuTool-2.0.8-x86_64.AppImage
# На текущем хосте зарегистрирован также этот путь:
/run/binfmt/appimage ./RuTool-2.0.8-x86_64.AppImage
```

Qt отдельно устанавливать не нужно. Для безопасного smoke test добавь
`--smoke-test`; он использует временные XDG-каталоги, не запускает поиск игры,
сетевые операции или установку. Для offscreen задай `QT_QPA_PLATFORM=offscreen` и `QT_QUICK_BACKEND=software`. Для проверки X11 можно задать `QT_QPA_PLATFORM=xcb`.

## Сборка клиента и тестов без упаковки

Нужны C++23 со `std::expected`, CMake >=3.25, Ninja и Qt >=6.8 с Core, Gui,
Qml, Quick, QuickControls2, Network, Concurrent и Test. Mbed TLS и zlib уже
находятся в исходниках.

```sh
export CMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
./scripts/build-linux.sh
# Только релизный клиент:
./scripts/build-linux.sh build-release -DBUILD_TESTING=OFF
```

Для ручной упаковки существующей FHS-сборки:

```sh
./scripts/fetch-appimage-tools.sh build-linux-tools
export QMAKE=/path/to/Qt/6.8.3/gcc_64/bin/qmake
export RUTOOL_TOOLS_DIR="$PWD/build-linux-tools"
./scripts/package-appimage.sh build-release dist-manual
```

Тесты используют `QTemporaryDir` и синтетические данные. Они остаются в
репозитории и не устанавливаются в AppImage. Пересоздание криптографических
фикстур требует Python `cryptography >=43`; обычному запуску тестов он не нужен.
