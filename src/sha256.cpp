#include "sha256.h"

#include <bcrypt.h>
#include <stdio.h>

#pragma comment(lib, "bcrypt.lib")

namespace autosave {

bool sha256File(const wchar_t* path, char out[65]) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;
    static BYTE buf[1 << 16];
    UCHAR digest[32];
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) goto done;
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) != 0) goto done;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, buf, sizeof(buf), &got, nullptr)) goto done;
        if (got == 0) break;
        if (BCryptHashData(hash, buf, got, 0) != 0) goto done;
    }
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0) goto done;
    for (int i = 0; i < 32; i++) sprintf_s(out + 2 * i, 3, "%02x", digest[i]);
    ok = true;
done:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return ok;
}

} // namespace autosave
