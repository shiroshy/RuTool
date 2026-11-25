# RuTool на Linux

Ветка содержит нативную Linux-версию RuTool 2.1.1. Она использует тот же
формат переводов, установщик и интерфейс, что и Windows-версия.
Для распространения предусмотрен AppImage для x86_64 с включёнными Qt и QML.

Установка перевода, запуск игры и восстановление оригинальных файлов проверены
на одном Linux-компьютере. Проверок на других дистрибутивах пока нет.
Подробности — в [результатах проверок](docs/LINUX-VALIDATION.md).

## Запуск

Для текущей сборки нужен Linux x86_64 с glibc 2.39 или новее,
графическая сессия Wayland или X11 и права записи в папку игры.
Для каталога, скачивания и серверной проверки пакетов нужен интернет.
Qt отдельно устанавливать не нужно.

Все команды ниже выполняются из `client-v2`, если не указано иное.
Готовый AppImage запускается из каталога, в котором он находится:

```sh
chmod +x RuTool-2.1.1-x86_64.AppImage
./RuTool-2.1.1-x86_64.AppImage
```

Если FUSE недоступен, можно запустить его с извлечением:

```sh
./RuTool-2.1.1-x86_64.AppImage --appimage-extract-and-run
```

Папка игры и локальный `.rut` выбираются в настройках. Если папка ещё не
сохранена, программа пытается найти игру в библиотеках Steam и каталогах Proton.
Ручной выбор доступен всегда.

Для обновления нужно заменить AppImage вручную. Кнопка «Обновление Linux»
показывает эту подсказку. Автоматического обновления Linux-сборки пока нет.
Windows-обновлятор на Linux не запускается.

## Где хранятся данные

Каталоги ниже указаны относительно `$XDG_DATA_HOME`. Если переменная не задана,
используется `.local/share` в домашнем каталоге:

| Каталог или файл | Назначение |
| --- | --- |
| `RuToolInstaller/TranslationDownloads/` | Скачанные переводы и незавершённые загрузки |
| `RuToolInstaller/Backups/` | Исходные файлы для восстановления игры |
| `RuToolInstaller/private/local.key` | Ключ для защищённых журналов и сохранённых кодов |
| `RuToolInstaller/client-v2-settings.json` | Настройки установки и выбранные пути |
| `RuTool/RuToolInstaller/settings.ini` | Тема и настройки окна |

Скачанный через каталог пакет удаляется после успешной установки.
Незавершённая загрузка может сохраняться для докачки. Выбранный вручную `.rut`
программа не удаляет. Резервные копии оригиналов сохраняются отдельно.

Не удаляй `private/local.key`, если нужны сохранённые коды или восстановление
прерванной операции. Ключ защищён правами файла `0600` и каталога `0700`;
программы того же пользователя потенциально могут его прочитать.
Защищённые данные Windows автоматически не переносятся.

Публичная конфигурация находится в `app/config/` рядом с исполняемым файлом,
а внутри AppImage — в `usr/bin/app/config/`. Запись внутрь AppImage не требуется.

## Сборка из исходников

Нужны CMake >=3.25, Ninja и компилятор C++23 с поддержкой `std::expected`.
Qt >=6.8 должен содержать Core, Gui, Qml, Quick, QuickControls2, Network
и Concurrent. Для тестов также нужны Qt Test и Python >=3.9.
Mbed TLS и zlib уже включены в исходники.

Если Qt установлен в `build-linux-sdk/6.8.3/gcc_64`:

```sh
export CMAKE_PREFIX_PATH="$PWD/build-linux-sdk/6.8.3/gcc_64"
./scripts/build-linux.sh
```

Скрипт собирает приложение с тестами и запускает CTest. Число потоков задаётся
через `RUTOOL_BUILD_JOBS`, по умолчанию — 4. Отдельная сборка без тестов:

```sh
./scripts/build-linux.sh build-linux-release -DBUILD_TESTING=OFF
```

Проверка интерфейса без сети и работы с игрой:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ./build-linux/RuTool --smoke-test --smoke-screenshot build-linux/window.png
```

Обычный запуск, в отличие от этого режима, обращается к серверу каталога.

## Сборка AppImage в контейнере

Нужны Docker с Linux-контейнерами и интернет для подготовки среды.
Рецепт образа находится в `packaging/linux/Dockerfile`.
Он использует Ubuntu 24.04, GCC 14 и aqtinstall 3.3.0.
Qt SDK хранится отдельно, чтобы не включать его в каждый слой образа.

Подготовка образа и каталогов:

```sh
docker build --target sdk-bootstrap --tag rutool-linux-bootstrap - < packaging/linux/Dockerfile
mkdir -p build-linux-sdk build-linux-tools
export RUTOOL_QT_SDK="$PWD/build-linux-sdk"
export RUTOOL_APPIMAGE_TOOLS="$PWD/build-linux-tools"
```

Загрузка Qt 6.8.3 и инструментов упаковки:

```sh
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$PWD:/work" -v "$RUTOOL_QT_SDK:/opt/Qt" \
  rutool-linux-bootstrap bash -c '
    /opt/aqt/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
      --outputdir /opt/Qt --modules qtimageformats qtshadertools &&
    ./scripts/fetch-appimage-tools.sh build-linux-tools
  '
```

Версии и SHA-256 инструментов закреплены в `packaging/linux/deployment-tools.lock`.
Готовая среда используется без сети; SDK и инструменты подключаются только
для чтения:

```sh
./scripts/build-linux-container.sh build-linux-artifacts/dist-linux-preview
```

Скрипт запускает тесты, делает отдельную сборку без тестов, упаковывает AppImage
и проверяет состав и запуск извлечённого приложения. Результат:

```text
build-linux-artifacts/dist-linux-preview/RuTool-2.1.1-x86_64.AppImage
build-linux-artifacts/dist-linux-preview/SHA256SUMS
```

Существующий выходной каталог не перезаписывается. Для повторной упаковки
укажи другое имя, например `build-linux-artifacts/dist-linux-preview-2`.
Тестовая и релизная сборки сохраняются в `build-linux-preview` и
`build-linux-release`. Их можно изменить через `RUTOOL_TEST_BUILD_DIR` и
`RUTOOL_RELEASE_BUILD_DIR`; допускаются имена `build-*` без слешей.
`RUTOOL_BUILD_IMAGE` позволяет выбрать другой подготовленный образ или image ID.

Для тестовой сборки без упаковки достаточно:

```sh
./scripts/build-linux-container.sh
```

Версии apt-пакетов и зависимостей aqtinstall не закреплены, поэтому побайтная
воспроизводимость не гарантируется.

## Ограничения

Рабочие каталоги должны находиться на локальной файловой системе и быть
недоступны для записи недоверенным процессам. NTFS, exFAT и сетевые диски
не проверены. Внутренние пути с символическими и жёсткими ссылками отвергаются;
алиас выбранной папки игры сначала приводится к реальному пути.

Блокировки согласуют операции RuTool, но не запрещают другим программам
изменять игровые файлы. Перед установкой и восстановлением игру нужно закрыть.
Проверка процесса через `/proc` не предотвращает его последующий запуск.

AppImage пока не подписан для публикации. `SHA256SUMS` проверяет совпадение
файла с указанным хешем, но не подтверждает издателя.
Другие дистрибутивы, FUSE-запуск и отдельные Premium-сценарии ещё требуют проверки.
