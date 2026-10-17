// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "out_guard.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

namespace out::guard {
namespace {
void check(NTSTATUS s) {
    if (s < 0) {
        char message[96];
        std::snprintf(message, sizeof(message), "OUT Guard: Windows CNG operation failed (0x%08lx)",
                      static_cast<unsigned long>(s));
        throw std::runtime_error(message);
    }
}
struct Algorithm {
    BCRYPT_ALG_HANDLE h = nullptr;
    Algorithm(LPCWSTR name, ULONG flags = 0) {
        check(BCryptOpenAlgorithmProvider(&h, name, nullptr, flags));
    }
    ~Algorithm() {
        if (h)
            BCryptCloseAlgorithmProvider(h, 0);
    }
    Algorithm(const Algorithm&) = delete;
};
struct HashHandle {
    BCRYPT_HASH_HANDLE h = nullptr;
    ~HashHandle() {
        if (h)
            BCryptDestroyHash(h);
    }
};
struct KeyHandle {
    BCRYPT_KEY_HANDLE h = nullptr;
    ~KeyHandle() {
        if (h)
            BCryptDestroyKey(h);
    }
};
struct FileHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~FileHandle() {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};
struct Secret {
    Key key{};
    ~Secret() {
        SecureZeroMemory(key.data(), key.size());
    }
};
struct WipingBytes : Bytes {
    using Bytes::Bytes;
    ~WipingBytes() {
        SecureZeroMemory(data(), size());
    }
};
Algorithm& digestAlgorithm(bool mac) {
    static Algorithm plain(BCRYPT_SHA256_ALGORITHM);
    static Algorithm keyed(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    return mac ? keyed : plain;
}
struct Hash {
    std::array<uint8_t, 4096> object{};
    HashHandle handle;
    explicit Hash(const Key* key = nullptr) {
        auto& algorithm = digestAlgorithm(key != nullptr);
        ULONG needed = 0, written = 0;
        check(BCryptGetProperty(algorithm.h, BCRYPT_OBJECT_LENGTH,
                                reinterpret_cast<PUCHAR>(&needed), sizeof(needed), &written, 0));
        if (needed > object.size())
            throw std::runtime_error("CNG hash object exceeds bounded buffer");
        check(BCryptCreateHash(algorithm.h, &handle.h, object.data(), needed,
                               key ? const_cast<PUCHAR>(key->data()) : nullptr,
                               key ? ULONG(key->size()) : 0, 0));
    }
    ~Hash() {
        if (handle.h) {
            BCryptDestroyHash(handle.h);
            handle.h = nullptr;
        }
        SecureZeroMemory(object.data(), object.size());
    }
    void add(const uint8_t* p, size_t n) {
        if (n > ULONG_MAX || (!p && n))
            throw std::invalid_argument("Invalid hash input");
        if (n)
            check(BCryptHashData(handle.h, const_cast<PUCHAR>(p), ULONG(n), 0));
    }
    Key finish() {
        Key key{};
        check(BCryptFinishHash(handle.h, key.data(), ULONG(key.size()), 0));
        return key;
    }
};
void append(Bytes& b, uint64_t v, int n) {
    for (int i = 0; i < n; i++)
        b.push_back(uint8_t(v >> (8 * i)));
}
uint64_t read(const Bytes& b, size_t at, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++)
        v |= uint64_t(b.at(at + size_t(i))) << (8 * i);
    return v;
}
void real(Bytes& b, double d) {
    uint64_t u;
    std::memcpy(&u, &d, 8);
    append(b, u, 8);
}
void string(Bytes& b, const std::string& s) {
    uint32_t n = uint32_t(s.size());
    while (n >= 128) {
        b.push_back(uint8_t(n | 128));
        n >>= 7;
    }
    b.push_back(uint8_t(n));
    b.insert(b.end(), s.begin(), s.end());
}
std::filesystem::path utf8Path(const std::string& s) {
    validateText(s, 32768);
    if (s.find('\0') != std::string::npos)
        throw std::invalid_argument("NUL path");
    return std::filesystem::u8path(s);
}
Key authenticate(const Key& key, const std::string& context, const Bytes& b, size_t length) {
    validateText(context, 4096);
    Bytes prefix;
    append(prefix, context.size(), 4);
    prefix.insert(prefix.end(), context.begin(), context.end());
    Hash hash(&key);
    hash.add(prefix.data(), prefix.size());
    hash.add(b.data(), length);
    return hash.finish();
}
Bytes aes(const Key& secret, const uint8_t* iv, const uint8_t* input, size_t length, bool encrypt) {
    uint8_t emptyInput = 0;
    if (!input && length == 0)
        input = &emptyInput;
    Algorithm algorithm(BCRYPT_AES_ALGORITHM);
    check(BCryptSetProperty(algorithm.h, BCRYPT_CHAINING_MODE,
                            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                            sizeof(BCRYPT_CHAIN_MODE_CBC), 0));
    ULONG needed = 0, written = 0;
    check(BCryptGetProperty(algorithm.h, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&needed),
                            sizeof(needed), &written, 0));
    WipingBytes object(needed);
    KeyHandle key;
    check(BCryptGenerateSymmetricKey(algorithm.h, &key.h, object.data(), ULONG(object.size()),
                                     const_cast<PUCHAR>(secret.data()), ULONG(secret.size()), 0));
    std::array<uint8_t, 16> copy{};
    std::copy_n(iv, 16, copy.begin());
    ULONG size = 0;
    auto operation = encrypt ? BCryptEncrypt : BCryptDecrypt;
    check(operation(key.h, const_cast<PUCHAR>(input), ULONG(length), nullptr, copy.data(), 16,
                    nullptr, 0, &size, BCRYPT_BLOCK_PADDING));
    Bytes out(size);
    std::copy_n(iv, 16, copy.begin());
    check(operation(key.h, const_cast<PUCHAR>(input), ULONG(length), nullptr, copy.data(), 16,
                    out.data(), ULONG(out.size()), &written, BCRYPT_BLOCK_PADDING));
    out.resize(written);
    return out;
}
Key challengeMac(const Key& key, const Challenge& c, const Key& digest) {
    Bytes b;
    string(b, "OUT.Guard/challenge/v1");
    append(b, uint64_t(c.sequence), 8);
    string(b, c.nonce);
    real(b, c.expiresAt);
    b.insert(b.end(), digest.begin(), digest.end());
    return hmac(key, b.data(), b.size());
}
Key stateDigest(const Bytes& b) {
    if (b.size() > 65536)
        throw std::invalid_argument("Checkpoint exceeds 64 KiB");
    return sha256(b.data(), b.size());
}
std::wstring finalPath(HANDLE h) {
    std::wstring path(32768, L'\0');
    DWORD n = GetFinalPathNameByHandleW(h, path.data(), DWORD(path.size()),
                                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= path.size())
        throw std::runtime_error("Cannot resolve integrity handle path");
    path.resize(n);
    return path;
}
} // namespace
Key randomKey() {
    Key k{};
    check(BCryptGenRandom(nullptr, k.data(), ULONG(k.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    return k;
}
uint64_t random64() {
    uint64_t value = 0;
    check(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    return value;
}
Key sha256(const uint8_t* p, size_t n) {
    Hash hash;
    hash.add(p, n);
    return hash.finish();
}
Key hmac(const Key& key, const uint8_t* p, size_t n) {
    Hash hash(&key);
    hash.add(p, n);
    return hash.finish();
}
uint64_t memoryTag(uint64_t encoded, uint64_t key, uint64_t binding) {
    static const Secret secret{randomKey()};
    uint8_t input[24];
    for (int i = 0; i < 8; i++) {
        input[i] = uint8_t(encoded >> (i * 8));
        input[8 + i] = uint8_t(key >> (i * 8));
        input[16 + i] = uint8_t(binding >> (i * 8));
    }
    auto tag = hmac(secret.key, input, sizeof(input));
    uint64_t value = 0;
    for (int i = 0; i < 8; i++)
        value |= uint64_t(tag[i]) << (i * 8);
    return value;
}
bool equal(const uint8_t* a, const uint8_t* b, size_t n) noexcept {
    uint8_t difference = 0;
    for (size_t i = 0; i < n; i++)
        difference |= a[i] ^ b[i];
    return difference == 0;
}
std::string hex(const uint8_t* p, size_t n) {
    static const char digits[] = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; i++) {
        s[i * 2] = digits[p[i] >> 4];
        s[i * 2 + 1] = digits[p[i] & 15];
    }
    return s;
}
Bytes unhex(const std::string& s) {
    if (s.size() % 2)
        throw std::invalid_argument("Odd hex length");
    auto nibble = [](char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        throw std::invalid_argument("Invalid hexadecimal character");
    };
    Bytes b(s.size() / 2);
    for (size_t i = 0; i < b.size(); i++)
        b[i] = uint8_t((nibble(s[i * 2]) << 4) | nibble(s[i * 2 + 1]));
    return b;
}
std::string base64(const uint8_t* p, size_t n) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = uint32_t(p[i]) << 16;
        if (i + 1 < n)
            v |= uint32_t(p[i + 1]) << 8;
        if (i + 2 < n)
            v |= p[i + 2];
        s += alphabet[(v >> 18) & 63];
        s += alphabet[(v >> 12) & 63];
        s += i + 1 < n ? alphabet[(v >> 6) & 63] : '=';
        s += i + 2 < n ? alphabet[v & 63] : '=';
    }
    return s;
}
void validateText(const std::string& s, size_t maximum, bool allowEmpty) {
    if (s.size() > maximum || (!allowEmpty && s.empty()))
        throw std::invalid_argument("UTF-8 text length");
    for (size_t i = 0; i < s.size();) {
        uint32_t c = uint8_t(s[i++]);
        if (c < 128)
            continue;
        int n = 0;
        uint32_t minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            n = 1;
            c &= 31;
            minimum = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            n = 2;
            c &= 15;
            minimum = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 3;
            c &= 7;
            minimum = 65536;
        } else
            throw std::invalid_argument("Malformed UTF-8");
        if (i + size_t(n) > s.size())
            throw std::invalid_argument("Truncated UTF-8");
        for (int j = 0; j < n; j++) {
            uint8_t d = uint8_t(s[i++]);
            if ((d & 0xc0) != 0x80)
                throw std::invalid_argument("Malformed UTF-8 continuation");
            c = (c << 6) | (d & 63);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            throw std::invalid_argument("Noncanonical UTF-8");
    }
}
Codec::Codec(Key key) : master_(key) {}
Codec::~Codec() {
    dispose();
}
void Codec::dispose() noexcept {
    SecureZeroMemory(master_.data(), master_.size());
    disposed_ = true;
}
Key Codec::derive(const uint8_t* salt, const char* label) const {
    if (disposed_)
        throw std::logic_error("Disposed codec");
    Bytes b(salt, salt + 16);
    std::string domain = std::string("OUT.Guard/v1/") + label;
    b.insert(b.end(), domain.begin(), domain.end());
    return hmac(master_, b.data(), b.size());
}
Bytes Codec::encode(const Bytes& payload, const std::string& context) const {
    validateText(context, 4096);
    if (payload.size() > MaximumPayloadBytes)
        throw std::invalid_argument("Payload exceeds 8 MiB");
    Key random = randomKey();
    Secret encryption{derive(random.data(), "encryption")},
        authentication{derive(random.data(), "authentication")};
    Bytes ciphertext =
              aes(encryption.key, random.data() + 16, payload.data(), payload.size(), true),
          result = {'O', 'G', '0', '1'};
    result.insert(result.end(), random.begin(), random.end());
    append(result, ciphertext.size(), 4);
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());
    auto tag = authenticate(authentication.key, context, result, result.size());
    result.insert(result.end(), tag.begin(), tag.end());
    return result;
}
bool Codec::decode(const Bytes& envelope, const std::string& context, Bytes& payload) const {
    payload.clear();
    if (disposed_)
        throw std::logic_error("Disposed codec");
    validateText(context, 4096);
    if (envelope.size() < 88 || envelope.size() > MaximumPayloadBytes + 88 ||
        std::memcmp(envelope.data(), "OG01", 4))
        return false;
    uint64_t n = read(envelope, 36, 4);
    if (!n || n % 16 || n != envelope.size() - 72)
        return false;
    Secret encryption{derive(envelope.data() + 4, "encryption")},
        authentication{derive(envelope.data() + 4, "authentication")};
    auto tag = authenticate(authentication.key, context, envelope, size_t(40 + n));
    if (!equal(tag.data(), envelope.data() + 40 + n, 32))
        return false;
    try {
        auto decoded =
            aes(encryption.key, envelope.data() + 20, envelope.data() + 40, size_t(n), false);
        if (decoded.size() > MaximumPayloadBytes)
            return false;
        payload = std::move(decoded);
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}
Key Codec::deriveUserKey(const Key& token, const std::string& user, const std::string& app) {
    validateText(user);
    validateText(app);
    Bytes b;
    string(b, "OUT.Guard/user-key/v1");
    append(b, app.size(), 4);
    b.insert(b.end(), app.begin(), app.end());
    append(b, user.size(), 4);
    b.insert(b.end(), user.begin(), user.end());
    return hmac(token, b.data(), b.size());
}
std::string Codec::userScope(const std::string& user) {
    validateText(user);
    auto digest = sha256(reinterpret_cast<const uint8_t*>(user.data()), user.size());
    return hex(digest.data(), digest.size());
}
SaveStore::SaveStore(std::filesystem::path directory, Key key, const std::string& user)
    : directory_(std::filesystem::absolute(directory).lexically_normal()), codec_(key) {
    if (directory.empty())
        throw std::invalid_argument("Save directory required");
    if (!user.empty())
        scope_ = "user/" + Codec::userScope(user) + "/";
}
std::filesystem::path SaveStore::slotPath(const std::string& slot) const {
    validateText(slot);
    std::string name = scope_ + slot;
    auto hash = sha256(reinterpret_cast<const uint8_t*>(name.data()), name.size());
    return directory_ / (hex(hash.data(), hash.size()) + ".ogsave");
}
void SaveStore::save(const std::string& slot, const Bytes& payload) {
    auto path = slotPath(slot);
    auto envelope = codec_.encode(payload, "save/" + scope_ + slot);
    std::filesystem::create_directories(directory_);
    auto random = randomKey();
    auto temp = path;
    temp += L"." + std::filesystem::path(hex(random.data(), 16)).wstring() + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create temporary save");
    DWORD written = 0;
    bool ok = WriteFile(file, envelope.data(), DWORD(envelope.size()), &written, nullptr) &&
              written == envelope.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) {
        auto backup = path;
        backup += L".bak";
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            ok = ReplaceFileW(path.c_str(), temp.c_str(), backup.c_str(), 0, nullptr, nullptr) != 0;
        else
            ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
    }
    if (!ok) {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("Atomic save replacement failed");
    }
}
LoadStatus SaveStore::load(const std::string& slot, Bytes& payload, bool backup) const {
    payload.clear();
    auto path = slotPath(slot);
    if (backup)
        path += L".bak";
    try {
        if (!std::filesystem::exists(path))
            return LoadStatus::Missing;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return LoadStatus::IOError;
        auto length = file.tellg();
        if (length < 88 || length > std::streamoff(Codec::MaximumPayloadBytes + 88))
            return LoadStatus::Invalid;
        Bytes bytes(static_cast<size_t>(length));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), length);
        if (!file)
            return LoadStatus::IOError;
        if (!codec_.decode(bytes, "save/" + scope_ + slot, payload)) {
            Events::report({Signal::SavedDataTamper, Severity::High, slot,
                            "Saved data rejected before applying"});
            return LoadStatus::Invalid;
        }
        return LoadStatus::Success;
    } catch (const std::filesystem::filesystem_error&) {
        return LoadStatus::IOError;
    }
}
void SaveStore::saveVersioned(const std::string& slot, const Bytes& payload, int64_t revision) {
    if (scope_.empty() || revision < 0 || payload.size() > Codec::MaximumPayloadBytes - 12)
        throw std::invalid_argument("User scope and nonnegative trusted revision required");
    Bytes b = {'O', 'G', 'S', '2'};
    append(b, uint64_t(revision), 8);
    b.insert(b.end(), payload.begin(), payload.end());
    save(slot, b);
}
LoadStatus SaveStore::loadVersioned(const std::string& slot, int64_t minimum, Bytes& payload,
                                    int64_t& revision, bool backup) const {
    payload.clear();
    revision = -1;
    if (scope_.empty() || minimum < 0)
        throw std::invalid_argument("User scope and trusted revision floor required");
    Bytes record;
    auto status = load(slot, record, backup);
    if (status != LoadStatus::Success)
        return status;
    if (record.size() < 12 || std::memcmp(record.data(), "OGS2", 4))
        return LoadStatus::Invalid;
    auto raw = read(record, 4, 8);
    if (raw > INT64_MAX)
        return LoadStatus::Invalid;
    revision = int64_t(raw);
    if (revision < minimum)
        return LoadStatus::StaleRevision;
    payload.assign(record.begin() + 12, record.end());
    return LoadStatus::Success;
}
void SaveStore::erase(const std::string& slot) {
    auto p = slotPath(slot);
    std::filesystem::remove(p);
    p += L".bak";
    std::filesystem::remove(p);
}
Reply createReply(const Key& key, const Challenge& c, const Bytes& state) {
    if (c.sequence <= 0 || c.nonce.size() != 44 || !std::isfinite(c.expiresAt))
        throw std::invalid_argument("Malformed challenge");
    Reply r;
    r.sequence = c.sequence;
    r.nonce = c.nonce;
    r.digest = stateDigest(state);
    r.authentication = challengeMac(key, c, r.digest);
    return r;
}
ChallengeSession::ChallengeSession(Key key) : key_(key) {}
ChallengeSession::~ChallengeSession() {
    SecureZeroMemory(key_.data(), key_.size());
}
void ChallengeSession::checkTime(double t) {
    if (!std::isfinite(t) || (hasTime_ && t < last_))
        throw std::invalid_argument("Trusted monotonic time required");
    hasTime_ = true;
    last_ = t;
}
Challenge ChallengeSession::issue(double t, double lifetime) {
    std::lock_guard<std::mutex> lock(mutex_);
    checkTime(t);
    if (!std::isfinite(lifetime) || lifetime <= 0 || lifetime > 120 ||
        !std::isfinite(t + lifetime) || sequence_ == INT64_MAX)
        throw std::invalid_argument("Invalid challenge lifetime/sequence");
    auto nonce = randomKey();
    pending_ = {++sequence_, base64(nonce.data(), nonce.size()), t + lifetime};
    pendingValid_ = true;
    return pending_;
}
ReplyStatus ChallengeSession::verify(const Reply& r, const Bytes& state, double t) {
    std::lock_guard<std::mutex> lock(mutex_);
    checkTime(t);
    if (!pendingValid_ || r.sequence != pending_.sequence || r.nonce != pending_.nonce)
        return ReplyStatus::ReplayOrUnknown;
    if (t >= pending_.expiresAt) {
        pendingValid_ = false;
        return ReplyStatus::Expired;
    }
    auto expected = challengeMac(key_, pending_, r.digest);
    if (!equal(expected.data(), r.authentication.data(), 32))
        return ReplyStatus::BadAuthentication;
    pendingValid_ = false;
    auto digest = stateDigest(state);
    return equal(r.digest.data(), digest.data(), 32) ? ReplyStatus::Accepted
                                                     : ReplyStatus::StateMismatch;
}
DeltaBudget::DeltaBudget(double capacity, double rate)
    : capacity_(capacity), refill_(rate), tokens_(capacity) {
    if (!std::isfinite(capacity) || capacity <= 0 || !std::isfinite(rate) || rate < 0)
        throw std::invalid_argument("Invalid budget");
}
void DeltaBudget::refill(double t) {
    if (initialized_)
        tokens_ = std::min(capacity_, tokens_ + (t - last_) * refill_);
    initialized_ = true;
    last_ = t;
}
double DeltaBudget::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tokens_;
}
double DeltaBudget::refresh(double t) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!std::isfinite(t) || (initialized_ && t < last_))
        throw std::invalid_argument("Budget time reversed");
    refill(t);
    return tokens_;
}
bool DeltaBudget::spend(double cost, double t) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!std::isfinite(cost) || cost <= 0 || !std::isfinite(t) || (initialized_ && t < last_))
        return false;
    refill(t);
    if (cost > tokens_)
        return false;
    tokens_ -= cost;
    return true;
}
Bytes signatureRecord(const std::string& relative, const std::string& sha,
                      const std::string& release) {
    validateText(relative);
    validateText(release);
    if (sha.size() != 64)
        throw std::invalid_argument("SHA256 hex length");
    auto bytes = unhex(sha);
    Bytes record;
    string(record, "OUT.Guard/file/v1");
    string(record, release);
    string(record, relative);
    string(record, hex(bytes.data(), bytes.size()));
    return record;
}
std::string hashFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open integrity file");
    Hash hash;
    std::array<char, 65536> buffer{};
    while (file) {
        file.read(buffer.data(), buffer.size());
        hash.add(reinterpret_cast<const uint8_t*>(buffer.data()), size_t(file.gcount()));
    }
    if (!file.eof())
        throw std::runtime_error("Integrity read failed");
    auto digest = hash.finish();
    return hex(digest.data(), digest.size());
}
FileStatus verifyFile(const std::filesystem::path& root, const std::string& relative,
                      const std::string& expected) {
    try {
        if (root.empty() || expected.size() != 64)
            return FileStatus::InvalidPath;
        auto wanted = unhex(expected);
        auto rel = utf8Path(relative);
        if (rel.is_absolute() || rel.has_root_name() || relative.find(':') != std::string::npos)
            return FileStatus::InvalidPath;
        for (auto& part : rel)
            if (part == "..")
                return FileStatus::InvalidPath;
        auto base = std::filesystem::absolute(root).lexically_normal(),
             full = (base / rel).lexically_normal();
        auto bs = base.wstring(), fs = full.wstring();
        if (bs.back() != L'\\')
            bs += L'\\';
        if (fs.size() <= bs.size() || CompareStringOrdinal(fs.c_str(), int(bs.size()), bs.c_str(),
                                                           int(bs.size()), TRUE) != CSTR_EQUAL)
            return FileStatus::InvalidPath;
        for (auto current = full;; current = current.parent_path()) {
            DWORD attr = GetFileAttributesW(current.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES) {
                DWORD e = GetLastError();
                return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? FileStatus::Missing
                                                                              : FileStatus::IOError;
            }
            if (attr & FILE_ATTRIBUTE_REPARSE_POINT)
                return FileStatus::InvalidPath;
            if (current == base)
                break;
        }
        FileHandle rootHandle;
        rootHandle.h = CreateFileW(
            base.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (rootHandle.h == INVALID_HANDLE_VALUE)
            return FileStatus::IOError;
        FileHandle file;
        file.h = CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file.h == INVALID_HANDLE_VALUE)
            return FileStatus::IOError;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file.h, &info))
            return FileStatus::IOError;
        if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
            return FileStatus::InvalidPath;
        auto resolvedRoot = finalPath(rootHandle.h), resolvedFile = finalPath(file.h);
        if (resolvedRoot.back() != L'\\')
            resolvedRoot += L'\\';
        if (resolvedFile.size() <= resolvedRoot.size() ||
            CompareStringOrdinal(resolvedFile.c_str(), int(resolvedRoot.size()),
                                 resolvedRoot.c_str(), int(resolvedRoot.size()),
                                 TRUE) != CSTR_EQUAL)
            return FileStatus::InvalidPath;
        Hash hash;
        std::array<uint8_t, 65536> buffer{};
        DWORD readBytes = 0;
        for (;;) {
            if (!ReadFile(file.h, buffer.data(), DWORD(buffer.size()), &readBytes, nullptr))
                return FileStatus::IOError;
            if (!readBytes)
                break;
            hash.add(buffer.data(), readBytes);
        }
        auto actual = hash.finish();
        return equal(actual.data(), wanted.data(), 32) ? FileStatus::Valid : FileStatus::Modified;
    } catch (const std::invalid_argument&) {
        return FileStatus::InvalidPath;
    } catch (const std::exception&) {
        return FileStatus::IOError;
    }
}
FileStatus verifySignedFile(const std::filesystem::path& root, const std::string& relative,
                            const std::string& sha, const std::string& release,
                            const Bytes& signature, const Bytes& modulus, const Bytes& exponent) {
    if (signature.empty() || signature.size() > 1024 || modulus.size() < 256 ||
        modulus.size() > 1024 || exponent.empty() || exponent.size() > 8)
        return FileStatus::UntrustedManifest;
    size_t first = 0;
    while (first < modulus.size() && !modulus[first])
        first++;
    if (first == modulus.size())
        return FileStatus::UntrustedManifest;
    int high = 0;
    for (int n = modulus[first]; n; n >>= 1)
        high++;
    if ((modulus.size() - first - 1) * 8 + high < 2048)
        return FileStatus::UntrustedManifest;
    try {
        auto record = signatureRecord(relative, sha, release);
        auto digest = sha256(record.data(), record.size());
        Algorithm algorithm(BCRYPT_RSA_ALGORITHM);
        BCRYPT_RSAKEY_BLOB header{BCRYPT_RSAPUBLIC_MAGIC,
                                  ULONG(modulus.size() * 8),
                                  ULONG(exponent.size()),
                                  ULONG(modulus.size()),
                                  0,
                                  0};
        Bytes blob(sizeof(header));
        std::memcpy(blob.data(), &header, sizeof(header));
        blob.insert(blob.end(), exponent.begin(), exponent.end());
        blob.insert(blob.end(), modulus.begin(), modulus.end());
        KeyHandle key;
        check(BCryptImportKeyPair(algorithm.h, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key.h, blob.data(),
                                  ULONG(blob.size()), 0));
        BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
        if (BCryptVerifySignature(key.h, &padding, digest.data(), ULONG(digest.size()),
                                  const_cast<PUCHAR>(signature.data()), ULONG(signature.size()),
                                  BCRYPT_PAD_PKCS1) < 0)
            return FileStatus::UntrustedManifest;
        return verifyFile(root, relative, sha);
    } catch (const std::invalid_argument&) {
        return FileStatus::InvalidPath;
    } catch (const std::exception&) {
        return FileStatus::UntrustedManifest;
    }
}
} // namespace out::guard
