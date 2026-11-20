#include "Posix.hpp"
#include <rutool/platform/Crypto.hpp>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <mbedtls/gcm.h>
#include <sys/file.h>
#include <sys/random.h>

namespace rutool::platform {
namespace {
Result<QByteArray> randomBytes(qsizetype size) {
    QByteArray bytes(size, '\0'); qsizetype offset{};
    while (offset < size) {
        const auto count = ::getrandom(bytes.data() + offset, static_cast<size_t>(size - offset), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return std::unexpected(posix::io(QStringLiteral("Генератор случайных чисел недоступен.")));
        offset += count;
    }
    return bytes;
}
Result<Secret> userKey(bool create) {
    const auto directory = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("RuToolInstaller/private"));
    if (create && !QDir().mkpath(directory)) return std::unexpected(posix::io(QStringLiteral("Не удалось создать каталог ключа.")));
    auto parent = posix::directory(directory); if (!parent) return std::unexpected(parent.error());
    struct stat info{};
    if (::fstat(parent->value, &info) != 0 || info.st_uid != ::getuid() || ::fchmod(parent->value, 0700) != 0)
        return std::unexpected(posix::io(QStringLiteral("Небезопасный каталог ключа.")));
    // Lock the directory, not the partially initialized key file. Crashes leave no usable partial key.
    if (::flock(parent->value, LOCK_EX) != 0) return std::unexpected(posix::io(QStringLiteral("Ключ занят.")));
    auto key = posix::file(QDir(directory).filePath(QStringLiteral("local.key")), O_RDONLY);
    if (!key && create && errno == ENOENT) {
        auto bytes = randomBytes(32); if (!bytes) return std::unexpected(bytes.error());
        Secret secret(std::move(*bytes));
        posix::Fd fresh(::openat(parent->value, "local.key.new", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (!fresh) return std::unexpected(posix::io(QStringLiteral("Не удалось создать ключ; проверьте незавершённый local.key.new.")));
        if (::write(fresh.value, secret.bytes().constData(), 32) != 32 || ::fsync(fresh.value) != 0 ||
            ::renameat(parent->value, "local.key.new", parent->value, "local.key") != 0 || ::fsync(parent->value) != 0)
            return std::unexpected(posix::io(QStringLiteral("Не удалось надёжно сохранить ключ.")));
        return secret;
    }
    if (!key) return std::unexpected(key.error());
    if (::fstat(key->value, &info) != 0 || info.st_uid != ::getuid() || (info.st_mode & 0077) != 0 || info.st_size != 32)
        return std::unexpected(posix::io(QStringLiteral("Ключ повреждён или доступен другим пользователям.")));
    Secret secret(QByteArray(32, '\0'));
    if (::read(key->value, secret.bytes().data(), 32) != 32) return std::unexpected(posix::io(QStringLiteral("Не удалось прочитать ключ.")));
    return secret;
}
const unsigned char* view(const QByteArray& b) { return reinterpret_cast<const unsigned char*>(b.constData()); }
unsigned char* edit(QByteArray& b) { return reinterpret_cast<unsigned char*>(b.data()); }
struct Gcm {
    mbedtls_gcm_context context;
    Gcm() { mbedtls_gcm_init(&context); }
    ~Gcm() { mbedtls_gcm_free(&context); }
};
constexpr char magic[] = "RULOC1";
}
Result<QByteArray> protectUser(const QByteArray& plain, const QByteArray& entropy) {
    if (plain.size() > 1024 * 1024 || entropy.size() > 4096)
        return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Недопустимый размер локальных данных.")});
    auto key = userKey(true); if (!key) return std::unexpected(key.error());
    auto nonce = randomBytes(12); if (!nonce) return std::unexpected(nonce.error());
    QByteArray cipher(plain.size(), '\0'), tag(16, '\0'); Gcm gcm;
    const auto aad = QByteArray(magic) + entropy;
    if (mbedtls_gcm_setkey(&gcm.context, MBEDTLS_CIPHER_ID_AES, view(key->bytes()), 256) != 0 ||
        mbedtls_gcm_crypt_and_tag(&gcm.context, MBEDTLS_GCM_ENCRYPT, static_cast<size_t>(plain.size()),
            view(*nonce), 12, view(aad), static_cast<size_t>(aad.size()), view(plain), edit(cipher), 16, edit(tag)) != 0)
        return std::unexpected(Error{ErrorCode::Signature, QStringLiteral("Не удалось защитить локальные данные AES-GCM.")});
    return QByteArray(magic) + *nonce + tag + cipher;
}
Result<Secret> unprotectUser(const QByteArray& cipher, const QByteArray& entropy) {
    if (cipher.size() < 34 || cipher.size() > 1024 * 1024 + 34 || !cipher.startsWith(magic) || entropy.size() > 4096)
        return std::unexpected(Error{ErrorCode::Signature, QStringLiteral("Неизвестный или повреждённый формат локальных данных Linux.")});
    auto key = userKey(false); if (!key) return std::unexpected(key.error());
    return decryptGcm(*key, cipher.mid(6, 12), cipher.mid(34), cipher.mid(18, 16), QByteArray(magic) + entropy);
}
Result<QString> installationDeviceId() {
    QFile machine(QStringLiteral("/etc/machine-id"));
    if (!machine.open(QIODevice::ReadOnly)) return std::unexpected(posix::io(QStringLiteral("machine-id недоступен.")));
    const auto id = machine.read(256).trimmed();
    if (id.size() != 32 || QByteArray::fromHex(id).size() != 16 || id == QByteArray(32, '0'))
        return std::unexpected(Error{ErrorCode::InvalidInput, QStringLiteral("Некорректный machine-id.")});
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray("RuToolInstaller.Installation.Linux.v1:") +
        id + ':' + QByteArray::number(::getuid()), QCryptographicHash::Sha256).toHex());
}
}
