# Сборка Installer

Актуальный инсталлер — C++/Qt из `client-v2`. В полном workspace корневой
`build-installer.ps1` собирает его и запускает тесты. Сохранённый WPF 1.4.0 собирается только явно
через `build-installer-legacy.ps1`; этот сценарий использует прежние secrets/signing.

```powershell
./client-v2/scripts/bootstrap-windows.ps1
./client-v2/scripts/build-windows.ps1 -Configuration Release
```

Для SDK/tooling сначала используются существующие каталоги workspace `work`,
затем установленные bootstrap-каталоги `%LOCALAPPDATA%/RuTool2Toolchain`.
Параметры `-SdkRoot`, `-ToolingRoot`, `-Cppcheck`, `-BuildDirectory` задают пути явно.
Нужны Qt 6.8.3, MinGW 13.1 и CMake/Ninja/Cppcheck. Для частных диагностических
тестов также нужен Python 3.9+; их QML-сцены генерируются из production QML
с проверкой явных overlay-патчей, а форма репортов проверяется напрямую.

Для полной запускаемой папки и проверенного ZIP:

```powershell
./client-v2/scripts/package-windows.ps1 -ReleaseDirectory 'D:/builds/RuTool-local' -KeysFile 'C:/private/installer-keys.json' -ArchivePath 'D:/builds/RuTool-local.zip'
```

Использовать новый/пустой ReleaseDirectory и новый ZIP. Файл ключей не создаётся
и не меняется автоматически; он копируется в `app/config/installer-keys.json`.
`update-public.der` берётся из `client-v2/config`. Запускать корневой
`RuToolInstaller.exe` в полной папке. Одна EXE/DLL из build-каталога не является
готовым дистрибутивом. Публикацию на сервер/GitHub эти команды не выполняют.

Полная локальная упаковка по умолчанию запускает тесты. В публичном исходном
архиве частные tests могут отсутствовать: тогда нужен явный `-SkipTests`.
Для сохранения временных deployment/archive-check каталогов при диагностике
есть `-KeepTemporaryFiles` у `client-v2/scripts/package-windows.ps1`.
При успешной обычной упаковке удаляются только созданные этим запуском
временные каталоги с проверкой пути и отсутствия reparse points.

Отдельная проверка подписанного манифеста не использует исторические константы:
перед запуском protocol tests задаются `RUTOOL_PRODUCTION_ENVELOPE`,
`RUTOOL_EXPECTED_VERSION`, `RUTOOL_EXPECTED_FILES`, `RUTOOL_EXPECTED_SEQUENCE`.
При другом ключе также задаётся `RUTOOL_RELEASE_PUBLIC_KEY`. Без манифеста
эта отдельная проверка явно отмечается как SKIP.
