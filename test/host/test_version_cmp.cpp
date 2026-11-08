/**
 * host 侧单测：version_cmp
 * CI 里 g++ 直接编译，不依赖 ESP-IDF：
 *   g++ -std=c++17 -Wall -Wextra -Werror -I components/memoria_package/include \
 *       test/host/test_version_cmp.cpp -o /tmp/t && /tmp/t
 */
#include <cstdio>
#include "version_util.hpp"

using memoria::package::version_cmp;

static int fails = 0;

static void expect(const char* a, const char* b, int want) {
    int got = version_cmp(a, b);
    if (got != want) {
        std::printf("FAIL  cmp(\"%s\", \"%s\") = %d, want %d\n",
                    a ? a : "(null)", b ? b : "(null)", got, want);
        fails++;
    }
}

int main() {
    /* 相等 */
    expect("1.6.7", "1.6.7", 0);
    expect("1.0.0", "1.0.0", 0);

    /* 第三位 */
    expect("1.6.7", "1.6.6", 1);
    expect("1.6.6", "1.6.7", -1);

    /* 数值比较不是字典序：7 < 70，10 > 9 */
    expect("1.6.7", "1.6.70", -1);
    expect("1.6.10", "1.6.9", 1);

    /* 第二位 */
    expect("1.7.0", "1.6.99", 1);
    expect("1.6.99", "1.7.0", -1);

    /* 第一位 */
    expect("2.0.0", "1.99.99", 1);

    /* 缺段按 0 */
    expect("1.6", "1.6.0", 0);
    expect("1.6", "1.6.1", -1);
    expect("1", "1.0.0", 0);

    /* 非数字尾巴 */
    expect("1.6.7-beta", "1.6.7", 0);
    expect("1.6.7", "1.6.7-beta", 0);

    /* 空指针 / 空串 */
    expect("", "", 0);
    expect(nullptr, "1.0.0", -1);

    /* 身份校验用到的判定：新镜像 1.6.7 vs 运行中 1.6.9 → 旧，应被拒 */
    expect("1.6.7", "1.6.9", -1);
    /* 同版本允许（重刷同一固件不该被拒） */
    expect("1.6.9", "1.6.9", 0);

    if (fails) {
        std::printf("%d case(s) failed\n", fails);
        return 1;
    }
    std::printf("version_cmp: all cases ok\n");
    return 0;
}
