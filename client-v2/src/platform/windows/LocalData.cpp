#include <rutool/platform/Crypto.hpp>
#include <QCryptographicHash>
#include <QUuid>
#include <windows.h>
#include <wincrypt.h>

namespace rutool::platform {
namespace {
Error failure(const QString& message) { return {ErrorCode::Io, message, QStringLiteral("Windows local recovery data")}; }
struct LocalBuffer { DATA_BLOB blob{}; ~LocalBuffer() { if (blob.pbData) { SecureZeroMemory(blob.pbData, blob.cbData); LocalFree(blob.pbData); } } };
}
Result<QByteArray> protectUser(const QByteArray& plain, const QByteArray& entropy) {
    if (plain.size() > 1024 * 1024 || entropy.size() > 4096) return std::unexpected(failure(QStringLiteral("Недопустимые данные DPAPI.")));
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()))};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))}; LocalBuffer output;
    if (!CryptProtectData(&input, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.blob))
        return std::unexpected(failure(QStringLiteral("Не удалось защитить локальные данные.")));
    return QByteArray(reinterpret_cast<const char*>(output.blob.pbData), output.blob.cbData);
}
Result<Secret> unprotectUser(const QByteArray& cipher, const QByteArray& entropy) {
    if (cipher.size() > 1024 * 1024 || entropy.size() > 4096) return std::unexpected(failure(QStringLiteral("Недопустимые данные DPAPI.")));
    DATA_BLOB input{static_cast<DWORD>(cipher.size()), reinterpret_cast<BYTE*>(const_cast<char*>(cipher.constData()))};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))}; LocalBuffer output;
    if (!CryptUnprotectData(&input, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.blob))
        return std::unexpected(failure(QStringLiteral("Защищённые данные недоступны этому пользователю.")));
    return Secret(QByteArray(reinterpret_cast<const char*>(output.blob.pbData), output.blob.cbData));
}
Result<QString> installationDeviceId() {
    wchar_t guid[128]{}; DWORD length = sizeof(guid);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, guid, &length) != ERROR_SUCCESS)
        return std::unexpected(failure(QStringLiteral("Идентификатор Windows недоступен.")));
    const QUuid parsed(QString::fromWCharArray(guid));
    if (parsed.isNull()) return std::unexpected(failure(QStringLiteral("Некорректный MachineGuid.")));
    return QString::fromLatin1(QCryptographicHash::hash((QStringLiteral("RuToolInstaller.Installation.v1:") + parsed.toString(QUuid::WithoutBraces).toLower()).toUtf8(), QCryptographicHash::Sha256).toHex());
}
}
