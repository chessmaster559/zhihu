#include "eaf_asset.h"

static uint8_t asset[1252];
static void Put32(size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        asset[offset + i] = value >> (8 * i);
}
static void Checksum() {
    uint32_t sum = 0;
    for (size_t i = 16; i < sizeof(asset); ++i)
        sum += asset[i];
    Put32(8, sum);
}
static void ValidAsset() {
    for (auto& byte : asset)
        byte = 0;
    asset[0] = 0x89;
    asset[1] = 'E';
    asset[2] = 'A';
    asset[3] = 'F';
    Put32(4, 1);
    Put32(12, sizeof(asset) - 16);
    Put32(16, sizeof(asset) - 24);
    asset[24] = 'Z';
    asset[25] = 'Z';
    asset[26] = '_';
    asset[27] = 'S';
    asset[35] = 8;
    asset[36] = 128;
    asset[38] = 128;
    asset[40] = 8;
    asset[42] = 16;
    for (int block = 0; block < 8; ++block) {
        Put32(44 + block * 4, 19);
        const size_t start = 24 + 1076 + block * 19;
        for (int run = 0; run < 9; ++run)
            asset[start + 1 + run * 2] = run == 8 ? 8 : 255;
    }
    Checksum();
}
#define CHECK(condition)     \
    do {                     \
        if (!(condition))    \
            return __LINE__; \
    } while (0)
extern "C" int main() {
    ValidAsset();
    CHECK(ValidateEafAsset(asset, sizeof(asset)));
    CHECK(!ValidateEafAsset(nullptr, sizeof(asset)));
    CHECK(!ValidateEafAsset(asset, 15));
    CHECK(!ValidateEafAsset(asset, sizeof(asset) - 1));
    asset[8] ^= 1;
    CHECK(!ValidateEafAsset(asset, sizeof(asset)));
    ValidAsset();
    Put32(20, 0xffffffff);
    Checksum();
    CHECK(!ValidateEafAsset(asset, sizeof(asset)));
    ValidAsset();
    asset[37] = 1;
    Checksum();
    CHECK(!ValidateEafAsset(asset, sizeof(asset)));
    ValidAsset();
    asset[1100] = 5;
    Checksum();
    CHECK(!ValidateEafAsset(asset, sizeof(asset)));
    ValidAsset();
    Put32(44, 18);
    Checksum();
    CHECK(!ValidateEafAsset(asset, sizeof(asset)));
    ValidAsset();
    asset[1117] = 255;
    Checksum();
    CHECK(!ValidateEafAsset(asset, sizeof(asset)));
    ValidAsset();
    asset[1101] = 0;
    Checksum();
    CHECK(!ValidateEafAsset(asset, sizeof(asset)));
    return 0;
}
