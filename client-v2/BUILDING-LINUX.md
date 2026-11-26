# Сборка RuTool для Linux

Команды выполняются из каталога приложения. Для обычного запуска готового
AppImage см. [README](../README.md).

## Зависимости

Нужны CMake >=3.25, Ninja и компилятор C++23 с `std::expected`.
Qt >=6.8 должен содержать Core, Gui, Qml, Quick, QuickControls2,
Network и Concurrent. Для тестов нужны Qt Test и Python >=3.9.
Mbed TLS и zlib включены в исходники.

Если Qt установлен отдельно, добавь его каталог в `CMAKE_PREFIX_PATH`:

```sh
export CMAKE_PREFIX_PATH="$PWD/build-linux-sdk/6.8.3/gcc_64"
./scripts/build-linux.sh
```

Скрипт собирает приложение и запускает CTest. Число потоков задаётся через
`RUTOOL_BUILD_JOBS`, по умолчанию — 4. Сборка без тестов:

```sh
./scripts/build-linux.sh build-linux-release -DBUILD_TESTING=OFF
```

## AppImage

Для переносимой сборки используется Ubuntu 22.04, GCC 12 и Qt 6.8.3.
Нужен Docker; интернет требуется только при подготовке среды.

```sh
docker build --target sdk-bootstrap --tag rutool-linux-bootstrap-glibc235 - < packaging/linux/Dockerfile
mkdir -p build-linux-sdk build-linux-tools
export RUTOOL_QT_SDK="$PWD/build-linux-sdk"
export RUTOOL_APPIMAGE_TOOLS="$PWD/build-linux-tools"
```

Загрузи SDK и закреплённые инструменты упаковки:

```sh
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$PWD:/work" -v "$RUTOOL_QT_SDK:/opt/Qt" \
  rutool-linux-bootstrap-glibc235 bash -c '
    /opt/aqt/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
      --outputdir /opt/Qt --modules qtimageformats qtshadertools &&
    ./scripts/fetch-appimage-tools.sh build-linux-tools
  '
```

Сборка с официальным каналом обновлений:

```sh
./scripts/build-linux-container.sh build-linux-artifacts/dist-linux-2.1.1L \
  -DRUTOOL_LINUX_UPDATE_REPOSITORY=shiroshy/RuTool \
  -DRUTOOL_LINUX_UPDATE_CHANNEL_REF=linux-updates-v1 \
  -DRUTOOL_LINUX_UPDATE_PUBLIC_KEY=/work/config/linux-update-public.der
```

Скрипт работает без сети, подключает SDK и инструменты только для чтения,
выполняет тесты, собирает отдельный релиз без тестов и упаковывает AppImage.
Выходной каталог должен быть новым. В нём появятся AppImage и `SHA256SUMS`.
Без параметров канала приложение собирается с ручным обновлением.
Закрытый ключ подписи для сборки не нужен.

Имена каталогов сборки меняются через `RUTOOL_TEST_BUILD_DIR` и
`RUTOOL_RELEASE_BUILD_DIR`. Допускаются имена `build-*` без слешей.
При смене компилятора используй новые каталоги. Версии apt-пакетов
и зависимостей aqtinstall не закреплены; побайтная воспроизводимость
не гарантируется.

## Версия и проверка запуска

`RUTOOL_APP_VERSION` — числовая версия для совместимости пакетов и обновлений.
Linux-интерфейс и имя AppImage используют `RUTOOL_APP_DISPLAY_VERSION`
с суффиксом `L`. Маршруты подписанного канала сохраняют числовые версии.

Проверка интерфейса без сети и работы с игрой:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ./build-linux/RuTool --smoke-test --smoke-screenshot build-linux/window.png
```

Обычный запуск может обращаться к серверу и работать с выбранной игрой.
В AppImage не включаются SDK, исходники, тесты, логи и закрытые ключи.
