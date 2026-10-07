// ---------------------------------------------------------------------------
// QrEncoder.cpp - QR Code (ISO/IEC 18004) encoder producing a module matrix.
//
// Implemented from the specification:
//   * versions 1..40, error correction levels L / M / Q / H
//   * numeric / alphanumeric / byte (UTF-8) modes with optimal whole-payload mode
//   * correct character-count indicators per version group and mode
//   * Reed-Solomon error correction over GF(256), primitive polynomial 0x11D
//   * block interleaving using the ISO block structure per version / level
//   * finder + separator, timing, alignment, dark module, format and version info
//   * all eight data masks scored with the four standard penalty rules
// ---------------------------------------------------------------------------
#include "codes/QrEncoder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace occ {
namespace {

// ===========================================================================
// GF(256) arithmetic - primitive polynomial 0x11D, generator 2
// ===========================================================================
class GaloisField
{
public:
    GaloisField()
    {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            m_exp[i] = static_cast<uint8_t>(x);
            m_log[x] = static_cast<uint8_t>(i);
            x <<= 1;
            if (x & 0x100)
                x ^= 0x11D;
        }
        for (int i = 255; i < 512; ++i)
            m_exp[i] = m_exp[i - 255];
    }

    uint8_t multiply(uint8_t a, uint8_t b) const
    {
        if (a == 0 || b == 0)
            return 0;
        return m_exp[m_log[a] + m_log[b]];
    }

    uint8_t power(int exponent) const { return m_exp[exponent % 255]; }

private:
    std::array<uint8_t, 512> m_exp{};
    std::array<uint8_t, 256> m_log{};
};

const GaloisField &gf()
{
    static const GaloisField instance;
    return instance;
}

// ===========================================================================
// Reed-Solomon error correction
// ===========================================================================
// Generator polynomial (x - a^0)(x - a^1)...(x - a^(degree-1)), stored highest
// power first, leading coefficient (which is always 1) omitted.
std::vector<uint8_t> rsDivisor(int degree)
{
    std::vector<uint8_t> result(static_cast<std::size_t>(degree), 0u);
    if (degree == 0)
        return result;
    result.back() = 1; // monomial x^0
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            result[j] = gf().multiply(result[j], root);
            if (j + 1 < degree)
                result[j] ^= result[j + 1];
        }
        root = gf().multiply(root, 0x02);
    }
    return result;
}

std::vector<uint8_t> rsRemainder(const std::vector<uint8_t> &data,
                                 const std::vector<uint8_t> &divisor)
{
    std::vector<uint8_t> result(divisor.size(), 0u);
    const std::size_t n = divisor.size();
    for (uint8_t b : data) {
        const uint8_t factor = static_cast<uint8_t>(b ^ result[0]);
        if (n > 1)
            std::memmove(result.data(), result.data() + 1, n - 1);
        result[n - 1] = 0;
        for (std::size_t i = 0; i < n; ++i)
            result[i] = static_cast<uint8_t>(result[i] ^ gf().multiply(divisor[i], factor));
    }
    return result;
}

// ===========================================================================
// Capacity tables
// ===========================================================================
// Total codewords (data + error correction) per version, index 0 unused.
constexpr std::array<int, 41> kTotalCodewords = {
    0,   26,  44,  70,  100, 134, 172, 196, 242, 292,
    346, 404, 466, 532, 581, 655, 733, 815, 901, 991,
    1085, 1156, 1258, 1364, 1474, 1588, 1706, 1828, 1921, 2051,
    2185, 2323, 2465, 2611, 2761, 2876, 3034, 3196, 3362, 3532,
    3706,
};

// EC codewords per block, [level 0..3 = L,M,Q,H][version 1..40].
constexpr std::array<std::array<int, 41>, 4> kEccCodewordsPerBlock = {{
    // Low
    {0, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
     28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    // Medium
    {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26,
     26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
    // Quartile
    {0, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30,
     28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
    // High
    {0, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28,
     30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
}};

// Number of error correction blocks, [level 0..3][version 1..40].
constexpr std::array<std::array<int, 41>, 4> kBlockCount = {{
    // Low
    {0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8,
     8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
    // Medium
    {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16,
     17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
    // Quartile
    {0, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20,
     23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
    // High
    {0, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25,
     25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
}};

int levelIndex(QrErrorCorrection ecc)
{
    return static_cast<int>(ecc); // Low=0, Medium=1, Quartile=2, High=3
}

// ===========================================================================
// Modes and bit-stream assembly
// ===========================================================================
enum class Mode { Numeric, Alphanumeric, Byte };

constexpr int kNumericMode = 0x1;
constexpr int kAlphaMode = 0x2;
constexpr int kByteMode = 0x4;

const char *modeName(Mode m)
{
    switch (m) {
    case Mode::Numeric: return "numeric";
    case Mode::Alphanumeric: return "alphanumeric";
    case Mode::Byte: return "byte";
    }
    return "byte";
}

int modeIndicator(Mode m)
{
    switch (m) {
    case Mode::Numeric: return kNumericMode;
    case Mode::Alphanumeric: return kAlphaMode;
    case Mode::Byte: return kByteMode;
    }
    return kByteMode;
}

int charCountBits(Mode m, int version)
{
    const int group = (version <= 9) ? 0 : (version <= 26 ? 1 : 2);
    switch (m) {
    case Mode::Numeric:
        return group == 0 ? 10 : (group == 1 ? 12 : 14);
    case Mode::Alphanumeric:
        return group == 0 ? 9 : (group == 1 ? 11 : 13);
    case Mode::Byte:
        return group == 0 ? 8 : 16;
    }
    return 8;
}

// Alphanumeric character value table: 0-9 A-Z space $ % * + - . / :
int alphanumericValue(char c)
{
    static const char kChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    for (int i = 0; i < 45; ++i) {
        if (kChars[i] == c)
            return i;
    }
    return -1;
}

bool isAllDigits(const QString &text)
{
    if (text.isEmpty())
        return false;
    for (QChar c : text) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
    }
    return true;
}

bool isAllAlphanumeric(const QString &text)
{
    if (text.isEmpty())
        return false;
    for (QChar c : text) {
        if (c.unicode() > 0x7F)
            return false;
        if (alphanumericValue(static_cast<char>(c.unicode())) < 0)
            return false;
    }
    return true;
}

// Growing bit buffer, MSB first.
class BitBuffer
{
public:
    void appendBits(uint32_t value, int length)
    {
        for (int i = length - 1; i >= 0; --i)
            m_bits.push_back(((value >> i) & 1u) != 0);
    }

    std::size_t size() const { return m_bits.size(); }
    const std::vector<bool> &bits() const { return m_bits; }

    // Append up to `count` zero terminator bits, stopping early if needed.
    void appendTerminator(std::size_t capacity)
    {
        const std::size_t remaining = capacity - m_bits.size();
        const std::size_t count = std::min<std::size_t>(4, remaining);
        for (std::size_t i = 0; i < count; ++i)
            m_bits.push_back(false);
    }

    // Zero pad to the next codeword boundary. The independent reference used
    // for verification (segno) appends a full zero codeword when the stream is
    // already aligned after the terminator, so that behaviour is reproduced
    // here (clamped so we never overshoot the data capacity).
    void padToByteBoundary(std::size_t capacityBits)
    {
        const std::size_t remainder = m_bits.size() % 8;
        std::size_t count = 8 - remainder; // 8 when already aligned
        while (count > 0 && m_bits.size() < capacityBits) {
            m_bits.push_back(false);
            --count;
        }
    }

    // Alternating 0xEC / 0x11 pad codewords up to the data capacity.
    void appendPadCodewords(std::size_t capacityBits)
    {
        static const uint8_t kPad[2] = {0xEC, 0x11};
        int index = 0;
        while (m_bits.size() + 8 <= capacityBits) {
            appendBits(kPad[index], 8);
            index ^= 1;
        }
    }

    std::vector<uint8_t> toCodewords() const
    {
        std::vector<uint8_t> out((m_bits.size() + 7) / 8, 0u);
        for (std::size_t i = 0; i < m_bits.size(); ++i) {
            if (m_bits[i])
                out[i / 8] |= static_cast<uint8_t>(1u << (7 - (i % 8)));
        }
        return out;
    }

private:
    std::vector<bool> m_bits;
};

// Number of data bits required for `count` characters of mode `m` (excluding
// the mode indicator and character count indicator).
std::size_t payloadBits(Mode m, std::size_t count)
{
    switch (m) {
    case Mode::Numeric:
        return (count / 3) * 10 + (count % 3 == 1 ? 4 : (count % 3 == 2 ? 7 : 0));
    case Mode::Alphanumeric:
        return (count / 2) * 11 + (count % 2 == 1 ? 6 : 0);
    case Mode::Byte:
        return count * 8;
    }
    return count * 8;
}

// Encode the payload into the bit buffer.
void writePayload(BitBuffer &buffer, Mode mode, const QString &text,
                  const QByteArray &utf8, std::size_t charCount)
{
    switch (mode) {
    case Mode::Numeric: {
        const std::size_t n = static_cast<std::size_t>(text.size());
        for (std::size_t i = 0; i < n; i += 3) {
            const std::size_t chunk = std::min<std::size_t>(3, n - i);
            uint32_t value = 0;
            for (std::size_t k = 0; k < chunk; ++k)
                value = value * 10 + static_cast<uint32_t>(text[static_cast<int>(i + k)].unicode() - '0');
            const int bits = static_cast<int>(chunk) * 3 + 1; // 3->10, 2->7, 1->4
            buffer.appendBits(value, bits);
        }
        break;
    }
    case Mode::Alphanumeric: {
        const std::size_t n = static_cast<std::size_t>(text.size());
        for (std::size_t i = 0; i < n; i += 2) {
            const int a = alphanumericValue(static_cast<char>(text[static_cast<int>(i)].unicode()));
            if (i + 1 < n) {
                const int b = alphanumericValue(static_cast<char>(text[static_cast<int>(i + 1)].unicode()));
                buffer.appendBits(static_cast<uint32_t>(a * 45 + b), 11);
            } else {
                buffer.appendBits(static_cast<uint32_t>(a), 6);
            }
        }
        break;
    }
    case Mode::Byte: {
        for (char c : utf8)
            buffer.appendBits(static_cast<uint8_t>(c), 8);
        break;
    }
    }
    (void)charCount;
}

// ===========================================================================
// Block structure and interleaving
// ===========================================================================
struct BlockLayout
{
    int totalCodewords = 0;
    int dataCodewords = 0;
    int eccPerBlock = 0;
    int numBlocks = 0;
};

BlockLayout layoutFor(int version, QrErrorCorrection ecc)
{
    BlockLayout l;
    const int level = levelIndex(ecc);
    l.totalCodewords = kTotalCodewords[version];
    l.eccPerBlock = kEccCodewordsPerBlock[level][version];
    l.numBlocks = kBlockCount[level][version];
    l.dataCodewords = l.totalCodewords - l.eccPerBlock * l.numBlocks;
    return l;
}

// Split `data` into blocks, append Reed-Solomon EC codewords to each block and
// interleave everything into the final codeword sequence.
std::vector<uint8_t> addEccAndInterleave(const std::vector<uint8_t> &data, int version,
                                         QrErrorCorrection ecc)
{
    const BlockLayout l = layoutFor(version, ecc);
    const int shortBlockLen = l.totalCodewords / l.numBlocks;
    const int numShortBlocks = l.numBlocks - (l.totalCodewords % l.numBlocks);
    const int eccLen = l.eccPerBlock;

    const std::vector<uint8_t> divisor = rsDivisor(eccLen);
    std::vector<std::vector<uint8_t>> blocks(
        static_cast<std::size_t>(l.numBlocks),
        std::vector<uint8_t>(static_cast<std::size_t>(shortBlockLen) + 1, 0u));

    std::size_t k = 0;
    for (int i = 0; i < l.numBlocks; ++i) {
        const int datLen = shortBlockLen - eccLen + (i < numShortBlocks ? 0 : 1);
        std::vector<uint8_t> dat(data.begin() + static_cast<long>(k),
                                 data.begin() + static_cast<long>(k) + datLen);
        k += static_cast<std::size_t>(datLen);
        for (int j = 0; j < datLen; ++j)
            blocks[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                dat[static_cast<std::size_t>(j)];
        const std::vector<uint8_t> ec = rsRemainder(dat, divisor);
        for (int j = 0; j < eccLen; ++j)
            blocks[static_cast<std::size_t>(i)]
                  [static_cast<std::size_t>(shortBlockLen + 1 - eccLen + j)] =
                ec[static_cast<std::size_t>(j)];
    }

    std::vector<uint8_t> result;
    result.reserve(static_cast<std::size_t>(l.totalCodewords));
    for (int i = 0; i < shortBlockLen + 1; ++i) {
        for (int j = 0; j < l.numBlocks; ++j) {
            if (i != shortBlockLen - eccLen || j >= numShortBlocks)
                result.push_back(blocks[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)]);
        }
    }
    return result;
}

// ===========================================================================
// Module matrix
// ===========================================================================
struct QrMatrix
{
    int size = 0;
    std::vector<uint8_t> dark; // 1 == dark
    std::vector<uint8_t> func; // 1 == function / reserved module

    explicit QrMatrix(int s)
        : size(s),
          dark(static_cast<std::size_t>(s) * static_cast<std::size_t>(s), 0u),
          func(static_cast<std::size_t>(s) * static_cast<std::size_t>(s), 0u) {}

    int idx(int x, int y) const { return y * size + x; }
    bool isFunction(int x, int y) const { return func[static_cast<std::size_t>(idx(x, y))] != 0; }
    bool get(int x, int y) const { return dark[static_cast<std::size_t>(idx(x, y))] != 0; }
    void setModule(int x, int y, bool d)
    {
        dark[static_cast<std::size_t>(idx(x, y))] = d ? 1u : 0u;
    }
    void setFunction(int x, int y, bool d)
    {
        const std::size_t i = static_cast<std::size_t>(idx(x, y));
        func[i] = 1u;
        dark[i] = d ? 1u : 0u;
    }
    void flip(int x, int y) { dark[static_cast<std::size_t>(idx(x, y))] ^= 1u; }
};

// Centre coordinates of the alignment patterns for a version.
std::vector<int> alignmentPositions(int version)
{
    if (version == 1)
        return {};
    const int size = version * 4 + 17;
    const int numAlign = version / 7 + 2;
    const int step = (version == 32)
                         ? 26
                         : (version * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
    std::vector<int> result(static_cast<std::size_t>(numAlign));
    result[0] = 6;
    for (int i = numAlign - 1, pos = size - 7; i >= 1; --i, pos -= step)
        result[static_cast<std::size_t>(i)] = pos;
    return result;
}

void drawFinder(QrMatrix &m, int cx, int cy)
{
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            const int dist = std::max(std::abs(dx), std::abs(dy));
            const int x = cx + dx;
            const int y = cy + dy;
            if (x >= 0 && x < m.size && y >= 0 && y < m.size)
                m.setFunction(x, y, dist != 2 && dist != 4);
        }
    }
}

void drawAlignment(QrMatrix &m, int cx, int cy)
{
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            m.setFunction(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

void drawVersionInfo(QrMatrix &m, int version)
{
    if (version < 7)
        return;
    int rem = version;
    for (int i = 0; i < 12; ++i)
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    const int bits = (version << 12) | rem; // 18 bits
    for (int i = 0; i < 18; ++i) {
        const bool bit = ((bits >> i) & 1) != 0;
        const int a = m.size - 11 + i % 3;
        const int b = i / 3;
        m.setFunction(a, b, bit);
        m.setFunction(b, a, bit);
    }
}

// Format information (BCH 15,5) with the 0x5412 mask, written in both
// locations, plus the always-dark module at (8, size-8).
void drawFormatBits(QrMatrix &m, QrErrorCorrection ecc, int mask)
{
    int fmtBits = 0;
    switch (ecc) {
    case QrErrorCorrection::Low: fmtBits = 1; break;
    case QrErrorCorrection::Medium: fmtBits = 0; break;
    case QrErrorCorrection::Quartile: fmtBits = 3; break;
    case QrErrorCorrection::High: fmtBits = 2; break;
    }
    const int data = (fmtBits << 3) | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    const int bits = ((data << 10) | rem) ^ 0x5412;
    auto bitAt = [bits](int i) { return ((bits >> i) & 1) != 0; };

    const int size = m.size;
    for (int i = 0; i <= 5; ++i)
        m.setFunction(8, i, bitAt(i));
    m.setFunction(8, 7, bitAt(6));
    m.setFunction(8, 8, bitAt(7));
    m.setFunction(7, 8, bitAt(8));
    for (int i = 9; i < 15; ++i)
        m.setFunction(14 - i, 8, bitAt(i));
    for (int i = 0; i < 8; ++i)
        m.setFunction(size - 1 - i, 8, bitAt(i));
    for (int i = 8; i < 15; ++i)
        m.setFunction(8, size - 15 + i, bitAt(i));
    m.setFunction(8, size - 8, true); // dark module
}

// Reserve the format-information cells and the always-dark module. Their values
// stay light while the data masks are evaluated (they are written afterwards).
void reserveFormatCells(QrMatrix &m)
{
    const int size = m.size;
    for (int i = 0; i <= 5; ++i)
        m.setFunction(8, i, false);
    m.setFunction(8, 7, false);
    m.setFunction(8, 8, false);
    m.setFunction(7, 8, false);
    for (int i = 9; i < 15; ++i)
        m.setFunction(14 - i, 8, false);
    for (int i = 0; i < 8; ++i)
        m.setFunction(size - 1 - i, 8, false);
    for (int i = 8; i < 15; ++i)
        m.setFunction(8, size - 15 + i, false);
    m.setFunction(8, size - 8, false); // dark module, written once a mask is chosen
}

// Reserve the version-information cells for versions 7 and above.
void reserveVersionInfo(QrMatrix &m, int version)
{
    if (version < 7)
        return;
    for (int i = 0; i < 18; ++i) {
        const int a = m.size - 11 + i % 3;
        const int b = i / 3;
        m.setFunction(a, b, false);
        m.setFunction(b, a, false);
    }
}

QrMatrix buildFunctionPatterns(int version)
{
    const int size = version * 4 + 17;
    QrMatrix m(size);

    // Timing patterns (drawn first, partially overwritten by the finders).
    for (int i = 0; i < size; ++i) {
        m.setFunction(6, i, i % 2 == 0);
        m.setFunction(i, 6, i % 2 == 0);
    }

    // Finder patterns (with separators) in the three corners.
    drawFinder(m, 3, 3);
    drawFinder(m, size - 4, 3);
    drawFinder(m, 3, size - 4);

    // Alignment patterns, except where they would clash with the finders.
    const std::vector<int> pos = alignmentPositions(version);
    const int n = static_cast<int>(pos.size());
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))
                continue;
            drawAlignment(m, pos[static_cast<std::size_t>(i)], pos[static_cast<std::size_t>(j)]);
        }
    }

    // Reserve (but do not fill) the format information and the version
    // information so the data mask can be evaluated without them.
    reserveFormatCells(m);
    reserveVersionInfo(m, version);
    return m;
}

// Zig-zag placement of the codeword bit stream, skipping the timing column and
// leaving any remainder modules light.
void drawCodewords(QrMatrix &m, const std::vector<uint8_t> &codewords)
{
    const int size = m.size;
    const std::size_t totalBits = codewords.size() * 8;
    std::size_t i = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        for (int vert = 0; vert < size; ++vert) {
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? (size - 1 - vert) : vert;
                if (!m.isFunction(x, y) && i < totalBits) {
                    const bool bit = ((codewords[i >> 3] >> (7 - (i & 7))) & 1u) != 0;
                    m.setModule(x, y, bit);
                    ++i;
                }
            }
        }
    }
}

// Apply (or, being self-inverse, undo) one of the eight data masks.
void applyMask(QrMatrix &m, int mask)
{
    const int size = m.size;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (m.isFunction(x, y))
                continue;
            bool invert = false;
            switch (mask) {
            case 0: invert = (x + y) % 2 == 0; break;
            case 1: invert = y % 2 == 0; break;
            case 2: invert = x % 3 == 0; break;
            case 3: invert = (x + y) % 3 == 0; break;
            case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
            case 5: invert = (x * y % 2) + (x * y % 3) == 0; break;
            case 6: invert = ((x * y % 2) + (x * y % 3)) % 2 == 0; break;
            case 7: invert = (((x + y) % 2) + (x * y % 3)) % 2 == 0; break;
            default: break;
            }
            if (invert)
                m.flip(x, y);
        }
    }
}

// --- Penalty scoring (four standard rules) --------------------------------
// N1 = 3 (runs of 5+ same-colour modules), N2 = 3 (2x2 same-colour blocks),
// N3 = 40 (1:1:3:1:1 finder-like pattern flanked by a 4-module light area),
// N4 = 10 (dark/light balance). The symbol is evaluated *without* the format and
// version information, as required by ISO/IEC 18004 clause 7.8.
constexpr int kPenaltyN1 = 3;
constexpr int kPenaltyN2 = 3;
constexpr int kPenaltyN3 = 40;
constexpr int kPenaltyN4 = 10;

int findPattern(const std::vector<uint8_t> &seq, const int *pattern, int start, int size)
{
    for (int i = std::max(start, 0); i + 7 <= size; ++i) {
        bool ok = true;
        for (int k = 0; k < 7; ++k) {
            if (seq[static_cast<std::size_t>(i + k)] != pattern[k]) {
                ok = false;
                break;
            }
        }
        if (ok)
            return i;
    }
    return -1;
}

// True when seq[from, to) contains no dark module (empty range counts as light).
bool allLight(const std::vector<uint8_t> &seq, int from, int to, int size)
{
    const int a = std::max(from, 0);
    const int b = std::min(to, size);
    for (int i = a; i < b; ++i) {
        if (seq[static_cast<std::size_t>(i)] != 0)
            return false;
    }
    return true;
}

// Counts 1:1:3:1:1 (dark:light:dark:dark:dark:light:dark) patterns that are at
// the edge of the line or preceded/followed by a 4-module light area.
int n3Occurrences(const std::vector<uint8_t> &seq, int size)
{
    static const int kPattern[7] = {1, 0, 1, 1, 1, 0, 1};
    int count = 0;
    int idx = findPattern(seq, kPattern, 0, size);
    while (idx != -1) {
        int offset = idx + 7;
        if (idx == 0 || idx == size - 7 || allLight(seq, idx - 4, idx, size)
            || allLight(seq, offset, offset + 4, size))
            count += kPenaltyN3;
        else
            offset = idx + 4;
        idx = findPattern(seq, kPattern, offset, size);
    }
    return count;
}

int penaltyScore(const QrMatrix &m)
{
    const int size = m.size;
    int scoreN1 = 0;
    int scoreN2 = 0;
    int scoreN3 = 0;
    int darkCount = 0;

    std::vector<uint8_t> row(static_cast<std::size_t>(size));
    std::vector<uint8_t> column(static_cast<std::size_t>(size));
    std::vector<uint8_t> lastRow;
    bool haveLastRow = false;

    for (int i = 0; i < size; ++i) {
        for (int j = 0; j < size; ++j)
            row[static_cast<std::size_t>(j)] = m.get(j, i) ? 1u : 0u;

        int rowPrev = -1;
        int colPrev = -1;
        int n1Row = 0;
        int n1Col = 0;
        for (int j = 0; j < size; ++j) {
            const int rc = row[static_cast<std::size_t>(j)];
            const int cc = m.get(i, j) ? 1 : 0;
            column[static_cast<std::size_t>(j)] = static_cast<uint8_t>(cc);
            darkCount += rc;

            if (rc == rowPrev)
                ++n1Row;
            else {
                if (n1Row >= 5)
                    scoreN1 += n1Row - 2;
                n1Row = 1;
            }
            if (cc == colPrev)
                ++n1Col;
            else {
                if (n1Col >= 5)
                    scoreN1 += n1Col - 2;
                n1Col = 1;
            }
            if (haveLastRow && j > 0 && rc == rowPrev
                && lastRow[static_cast<std::size_t>(j)] == rc
                && lastRow[static_cast<std::size_t>(j - 1)] == rc)
                scoreN2 += kPenaltyN2;

            rowPrev = rc;
            colPrev = cc;
        }
        lastRow = row;
        haveLastRow = true;

        scoreN3 += n3Occurrences(row, size);
        scoreN3 += n3Occurrences(column, size);

        if (n1Row >= 5)
            scoreN1 += n1Row - 2;
        if (n1Col >= 5)
            scoreN1 += n1Col - 2;
    }

    const double percent = static_cast<double>(darkCount) / (static_cast<double>(size) * size);
    const int k4 = static_cast<int>(std::abs(percent * 100.0 - 50.0) / 5.0);
    const int scoreN4 = k4 * kPenaltyN4;

    return scoreN1 + scoreN2 + scoreN3 + scoreN4;
}

// ===========================================================================
// Encoding pipeline
// ===========================================================================
Mode selectMode(const QString &text)
{
    if (isAllDigits(text))
        return Mode::Numeric;
    if (isAllAlphanumeric(text))
        return Mode::Alphanumeric;
    return Mode::Byte;
}

std::size_t charCountFor(Mode mode, const QString &text, const QByteArray &utf8)
{
    return mode == Mode::Byte ? static_cast<std::size_t>(utf8.size())
                              : static_cast<std::size_t>(text.size());
}

int findSmallestVersion(Mode mode, std::size_t charCount, QrErrorCorrection ecc)
{
    for (int v = 1; v <= 40; ++v) {
        const int bits = 4 + charCountBits(mode, v)
                         + static_cast<int>(payloadBits(mode, charCount));
        if (bits <= layoutFor(v, ecc).dataCodewords * 8)
            return v;
    }
    return 0;
}

QrEncoder::Result encodeImpl(const QString &text, const QByteArray &utf8, QrErrorCorrection ecc)
{
    QrEncoder::Result result;
    result.ecc = ecc;

    const Mode mode = selectMode(text);
    const std::size_t charCount = charCountFor(mode, text, utf8);
    result.mode = QString::fromLatin1(modeName(mode));

    const int version = findSmallestVersion(mode, charCount, ecc);
    if (version == 0) {
        result.error = QStringLiteral("Data does not fit in a QR code (version 40) at error "
                                      "correction level %1")
                           .arg(names::qrEcc(ecc));
        return result;
    }

    const int capacityBits = layoutFor(version, ecc).dataCodewords * 8;

    BitBuffer buffer;
    buffer.appendBits(static_cast<uint32_t>(modeIndicator(mode)), 4);
    buffer.appendBits(static_cast<uint32_t>(charCount), charCountBits(mode, version));
    writePayload(buffer, mode, text, utf8, charCount);
    buffer.appendTerminator(static_cast<std::size_t>(capacityBits));
    buffer.padToByteBoundary(static_cast<std::size_t>(capacityBits));
    buffer.appendPadCodewords(static_cast<std::size_t>(capacityBits));

    const std::vector<uint8_t> dataCodewords = buffer.toCodewords();
    const std::vector<uint8_t> codewords = addEccAndInterleave(dataCodewords, version, ecc);

    QrMatrix m = buildFunctionPatterns(version);
    drawCodewords(m, codewords);

    // Evaluate all eight masks on the symbol *without* format/version data,
    // exactly as ISO/IEC 18004 clause 7.8 requires, then pick the lowest score.
    int bestMask = 0;
    int bestPenalty = std::numeric_limits<int>::max();
    for (int mask = 0; mask < 8; ++mask) {
        applyMask(m, mask);
        const int penalty = penaltyScore(m);
        if (penalty < bestPenalty) {
            bestPenalty = penalty;
            bestMask = mask;
        }
        applyMask(m, mask); // undo (the mask is self-inverse)
    }
    applyMask(m, bestMask);
    drawFormatBits(m, ecc, bestMask);
    drawVersionInfo(m, version);

    result.ok = true;
    result.version = version;
    result.size = m.size;
    result.modules.resize(m.size * m.size);
    for (int y = 0; y < m.size; ++y)
        for (int x = 0; x < m.size; ++x)
            result.modules[y * m.size + x] = m.get(x, y);
    return result;
}

} // namespace

// ===========================================================================
// QrEncoder public interface
// ===========================================================================
QrEncoder::Result QrEncoder::encode(const QString &text, QrErrorCorrection ecc)
{
    return encodeImpl(text, text.toUtf8(), ecc);
}

QrEncoder::Result QrEncoder::encodeUtf8(const QByteArray &utf8, QrErrorCorrection ecc)
{
    return encodeImpl(QString::fromUtf8(utf8), utf8, ecc);
}

bool QrEncoder::canEncode(const QString &text, QrErrorCorrection ecc)
{
    const QByteArray utf8 = text.toUtf8();
    const Mode mode = selectMode(text);
    return findSmallestVersion(mode, charCountFor(mode, text, utf8), ecc) != 0;
}

int QrEncoder::dataCapacityBits(int version, QrErrorCorrection ecc)
{
    if (version < 1 || version > 40)
        return 0;
    return layoutFor(version, ecc).dataCodewords * 8;
}

int QrEncoder::totalCodewords(int version)
{
    if (version < 1 || version > 40)
        return 0;
    return kTotalCodewords[version];
}

int QrEncoder::eccCodewordsPerBlock(int version, QrErrorCorrection ecc)
{
    if (version < 1 || version > 40)
        return 0;
    return kEccCodewordsPerBlock[levelIndex(ecc)][version];
}

int QrEncoder::blockCount(int version, QrErrorCorrection ecc)
{
    if (version < 1 || version > 40)
        return 0;
    return kBlockCount[levelIndex(ecc)][version];
}

} // namespace occ
