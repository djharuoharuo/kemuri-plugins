// kemuri_core 単体テスト — 依存ライブラリなしの最小ハーネス。
// M1 で JS 参照ベクトル (tests/reference/*.json) 一致テストを追加する。
#include <kemuri_core/Version.h>

#include <cstdio>
#include <cstring>

namespace
{
int failures = 0;

void expect (bool condition, const char* label)
{
    if (! condition)
    {
        std::printf ("FAIL: %s\n", label);
        ++failures;
    }
}
} // namespace

int main()
{
    expect (kemuri::core::versionMajor == 0, "versionMajor");
    expect (std::strcmp (kemuri::core::versionString, "0.1.0") == 0, "versionString");

    if (failures == 0)
        std::printf ("All tests passed.\n");

    return failures == 0 ? 0 : 1;
}
