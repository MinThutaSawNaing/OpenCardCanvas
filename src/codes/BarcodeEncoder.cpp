// ---------------------------------------------------------------------------
// BarcodeEncoder.cpp - linear (1D) barcode symbologies for ID cards.
//
//   * Code 128 (code sets A/B/C, minimal-length start/switch selection)
//   * Code 39  (full character set, '*' guards, optional modulo-43 check)
//   * EAN-13 / EAN-8 (L/G/R element tables, guards, modulo-10 check digit)
//   * ITF-14   (interleaved 2 of 5, 14-digit normalisation, modulo-10 check)
//
// The encoder produces module-level bars/spaces; rendering is the caller's job.
// ---------------------------------------------------------------------------
#include "codes/BarcodeEncoder.h"

#include <array>
#include <vector>

namespace occ {
namespace {

// ===========================================================================
// Code 128 - 107 symbol patterns, given as element widths in modules.
// The final entry (106, Stop) is the 7-element pattern 2331112 (13 modules).
// ===========================================================================
constexpr std::array<const char *, 107> kCode128 = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312",
    "132212", "221213", "221312", "231212", "112232", "122132", "122231", "113222",
    "123122", "123221", "223211", "221132", "221231", "213212", "223112", "312131",
    "311222", "321122", "321221", "312212", "322112", "322211", "212123", "212321",
    "232121", "111323", "131123", "131321", "112313", "132113", "132311", "211313",
    "231113", "231311", "112133", "112331", "132131", "113123", "113321", "133121",
    "313121", "211331", "231131", "213113", "213311", "213131", "311123", "311321",
    "331121", "312113", "312311", "332111", "314111", "221411", "431111", "111224",
    "111422", "121124", "121421", "141122", "141221", "112214", "112412", "122114",
    "122411", "142112", "142211", "241211", "221114", "413111", "241112", "134111",
    "111242", "121142", "121241", "114212", "124112", "124211", "411212", "421112",
    "421211", "212141", "214121", "412121", "111143", "111341", "131141", "114113",
    "114311", "411113", "411311", "113141", "114131", "311141", "411131", "211412",
    "211214", "211232", "2331112",
};

constexpr int kC128StartA = 103;
constexpr int kC128StartB = 104;
constexpr int kC128StartC = 105;
constexpr int kC128Stop = 106;
constexpr int kC128CodeA = 101;
constexpr int kC128CodeB = 100;
constexpr int kC128CodeC = 99;

// ===========================================================================
// Code 39 - 43 data characters plus the '*' guard; 9 elements, 3 of them wide.
// 'N' = narrow, 'W' = wide, alternating bar/space starting with a bar.
// ===========================================================================
constexpr const char *kCode39Alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%*";
constexpr std::array<const char *, 44> kCode39Patterns = {
    "NNNWWNWNN", "WNNWNNNNW", "NNWWNNNNW", "WNWWNNNNN", "NNNWWNNNW", "WNNWWNNNN",
    "NNWWWNNNN", "NNNWNNWNW", "WNNWNNWNN", "NNWWNNWNN", "WNNNNWNNW", "NNWNNWNNW",
    "WNWNNWNNN", "NNNNWWNNW", "WNNNWWNNN", "NNWNWWNNN", "NNNNNWWNW", "WNNNNWWNN",
    "NNWNNWWNN", "NNNNWWWNN", "WNNNNNNWW", "NNWNNNNWW", "WNWNNNNWN", "NNNNWNNWW",
    "WNNNWNNWN", "NNWNWNNWN", "NNNNNNWWW", "WNNNNNWWN", "NNWNNNWWN", "NNNNWNWWN",
    "WWNNNNNNW", "NWWNNNNNW", "WWWNNNNNN", "NWNNWNNNW", "WWNNWNNNN", "NWWNWNNNN",
    "NWNNNNWNW", "WWNNNNWNN", "NWWNNNWNN", "NWNWNWNNN", "NWNWNNNWN", "NWNNNWNWN",
    "NNNWNWNWN", "NWNNWNWNN",
};

// Position of a character in the Code 39 alphabet (0..42 data, 43 == '*'), or -1.
int code39Index(QChar c)
{
    for (int i = 0; i < 44; ++i) {
        if (kCode39Alphabet[i] == c)
            return i;
    }
    return -1;
}

// ===========================================================================
// EAN / UPC element tables and parity selector.
// ===========================================================================
constexpr std::array<const char *, 10> kEanL = {
    "0001101", "0011001", "0010011", "0111101", "0100011",
    "0110001", "0101111", "0111011", "0110111", "0001011",
};
constexpr std::array<const char *, 10> kEanG = {
    "0100111", "0110011", "0011011", "0100001", "0011101",
    "0111001", "0000101", "0010001", "0001001", "0010111",
};
constexpr std::array<const char *, 10> kEanR = {
    "1110010", "1100110", "1101100", "1000010", "1011100",
    "1001110", "1010000", "1000100", "1001000", "1110100",
};

// Which of the six left-hand digits of an EAN-13 use the G table, indexed by
// the (implicit) first digit.
constexpr std::array<const char *, 10> kEan13Parity = {
    "LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG",
    "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL",
};

// ===========================================================================
// Interleaved 2 of 5 - five elements per digit, exactly two of them wide.
// ===========================================================================
constexpr std::array<const char *, 10> kItf = {
    "NNWWN", "WNNNW", "NWNNW", "WWNNN", "NNWNW",
    "WNWNN", "NWWNN", "NNNWW", "WNNWN", "NWNWN",
};

// ===========================================================================
// Small helpers
// ===========================================================================
bool allDigits(const QString &s)
{
    if (s.isEmpty())
        return false;
    for (QChar c : s) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
    }
    return true;
}

QString stripSeparators(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (c.isSpace() || c == QLatin1Char('-'))
            continue;
        out += c;
    }
    return out;
}

void pushRun(QVector<bool> &out, int count, bool dark)
{
    for (int i = 0; i < count; ++i)
        out.push_back(dark);
}

// Append an EAN bit pattern ('0' = light, '1' = dark).
void appendEanBits(QVector<bool> &out, const char *bits)
{
    for (const char *p = bits; *p != '\0'; ++p)
        out.push_back(*p == '1');
}

// Append a Code 128 symbol: the digit characters are literal module widths,
// alternating bar/space starting with a bar.
void appendCode128(QVector<bool> &out, int symbol)
{
    if (symbol < 0 || symbol >= static_cast<int>(kCode128.size()))
        return;
    bool dark = true;
    for (const char *p = kCode128[static_cast<std::size_t>(symbol)]; *p != '\0'; ++p) {
        pushRun(out, *p - '0', dark);
        dark = !dark;
    }
}

// Append a Code 39 character: 9 alternating elements, 'W' scaled by the ratio.
void appendCode39Char(QVector<bool> &out, const char *pattern, int narrow, int wide)
{
    bool dark = true;
    for (int i = 0; i < 9; ++i) {
        pushRun(out, pattern[i] == 'W' ? wide : narrow, dark);
        dark = !dark;
    }
}

// Append one interleaved 2 of 5 digit pair (first digit on the bars, second on
// the spaces).
void appendItfPair(QVector<bool> &out, const char *a, const char *b, int narrow, int wide)
{
    for (int k = 0; k < 5; ++k) {
        pushRun(out, a[k] == 'W' ? wide : narrow, true);
        pushRun(out, b[k] == 'W' ? wide : narrow, false);
    }
}

// ===========================================================================
// Code 128 minimal-length code-set planner (dynamic programming over A/B/C)
// ===========================================================================
constexpr int kInf = 1 << 28;
// Secondary cost weight: the planner minimises (symbols, then code-set
// switches), so the score is symbols * kSymbolWeight + switches.
constexpr int kSymbolWeight = 100000;

// Returns the codeword sequence start + data (no check symbol, no stop).
QVector<int> planCode128(const QByteArray &data, QString *error)
{
    const int n = data.size();
    enum { A = 0, B = 1, C = 2 };
    std::vector<std::array<int, 3>> dp(static_cast<std::size_t>(n) + 1);
    std::vector<std::array<int, 3>> prevState(static_cast<std::size_t>(n) + 1);
    std::vector<std::array<int, 3>> prevPos(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        dp[static_cast<std::size_t>(i)] = {kInf, kInf, kInf};
        prevState[static_cast<std::size_t>(i)] = {-1, -1, -1};
        prevPos[static_cast<std::size_t>(i)] = {-1, -1, -1};
    }
    dp[0] = {kSymbolWeight, kSymbolWeight, kSymbolWeight}; // cost of the start code

    for (int i = 0; i < n; ++i) {
        for (int s = 0; s < 3; ++s) {
            if (dp[static_cast<std::size_t>(i)][static_cast<std::size_t>(s)] >= kInf)
                continue;
            for (int t = 0; t < 3; ++t) {
                const int sw = (t == s) ? 0 : 1; // 1 when the code set changes
                int j = 0;
                if (t == C) {
                    if (i + 2 > n)
                        continue;
                    const unsigned char c0 = static_cast<unsigned char>(data[i]);
                    const unsigned char c1 = static_cast<unsigned char>(data[i + 1]);
                    if (!(c0 >= '0' && c0 <= '9' && c1 >= '0' && c1 <= '9'))
                        continue;
                    j = i + 2;
                } else {
                    const unsigned char c = static_cast<unsigned char>(data[i]);
                    if (t == A) {
                        if (c > 95)
                            continue;
                    } else if (c < 32 || c > 127) {
                        continue;
                    }
                    j = i + 1;
                }
                // One data symbol, plus one more symbol for a code-set switch.
                const int nc = dp[static_cast<std::size_t>(i)][static_cast<std::size_t>(s)]
                               + (1 + sw) * kSymbolWeight + sw;
                if (nc < dp[static_cast<std::size_t>(j)][static_cast<std::size_t>(t)]) {
                    dp[static_cast<std::size_t>(j)][static_cast<std::size_t>(t)] = nc;
                    prevState[static_cast<std::size_t>(j)][static_cast<std::size_t>(t)] = s;
                    prevPos[static_cast<std::size_t>(j)][static_cast<std::size_t>(t)] = i;
                }
            }
        }
    }

    int best = kInf;
    int bs = -1;
    // See above: the score is symbols * kSymbolWeight + switches, so this picks
    // the shortest symbol and, on a tie, the variant with the fewest code-set
    // switches. Remaining ties prefer B, then C, then A (B is the natural set
    // for text; A exists only for control characters).
    static const int kPreference[3] = {B, C, A};
    for (int p = 0; p < 3; ++p) {
        const int s = kPreference[p];
        const int cost = dp[static_cast<std::size_t>(n)][static_cast<std::size_t>(s)];
        if (cost < best) {
            best = cost;
            bs = s;
        }
    }
    if (bs < 0) {
        if (error)
            *error = QStringLiteral("Code 128 could not encode the supplied data");
        return {};
    }

    QVector<int> reverseSeq;
    int i = n;
    int s = bs;
    while (i > 0) {
        const int ps = prevState[static_cast<std::size_t>(i)][static_cast<std::size_t>(s)];
        const int pi = prevPos[static_cast<std::size_t>(i)][static_cast<std::size_t>(s)];
        if (pi < 0) {
            if (error)
                *error = QStringLiteral("Code 128 could not encode the supplied data");
            return {};
        }
        if (s == C) {
            reverseSeq.push_back((data[pi] - '0') * 10 + (data[pi + 1] - '0'));
        } else if (s == A) {
            const unsigned char c = static_cast<unsigned char>(data[pi]);
            reverseSeq.push_back(c < 32 ? c + 64 : c - 32);
        } else {
            const unsigned char c = static_cast<unsigned char>(data[pi]);
            reverseSeq.push_back(c - 32);
        }
        if (s != ps)
            reverseSeq.push_back(s == A ? kC128CodeA : (s == B ? kC128CodeB : kC128CodeC));
        i = pi;
        s = ps;
    }
    reverseSeq.push_back(s == A ? kC128StartA : (s == B ? kC128StartB : kC128StartC));

    QVector<int> out;
    out.reserve(reverseSeq.size());
    for (int k = reverseSeq.size() - 1; k >= 0; --k)
        out.push_back(reverseSeq[k]);
    return out;
}

// ===========================================================================
// Normalisation helpers (canonical form per symbology)
// ===========================================================================
QString normalizeEan(const QString &data, int totalLen, int dataLen, QString *error)
{
    const QString s = stripSeparators(data);
    if (s.isEmpty()) {
        *error = QStringLiteral("Enter %1 digits for this barcode").arg(dataLen);
        return {};
    }
    if (!allDigits(s)) {
        *error = QStringLiteral("This barcode accepts digits only");
        return {};
    }
    if (s.size() == totalLen) {
        const int expected = BarcodeEncoder::eanCheckDigit(s.left(dataLen));
        if (expected != s.at(dataLen).digitValue()) {
            *error = QStringLiteral("Check digit is %1 but should be %2")
                         .arg(s.at(dataLen))
                         .arg(expected);
            return {};
        }
        return s;
    }
    if (s.size() == dataLen)
        return s + QChar(QLatin1Char('0').unicode() + BarcodeEncoder::eanCheckDigit(s));
    *error = QStringLiteral("This barcode needs %1 or %2 digits").arg(dataLen).arg(totalLen);
    return {};
}

QString normalizeItf14(const QString &data, QString *error)
{
    const QString s = stripSeparators(data);
    if (s.isEmpty()) {
        *error = QStringLiteral("Enter up to 14 digits for ITF-14");
        return {};
    }
    if (!allDigits(s)) {
        *error = QStringLiteral("ITF-14 accepts digits only");
        return {};
    }
    const int n = s.size();
    if (n > 14) {
        *error = QStringLiteral("ITF-14 accepts at most 14 digits");
        return {};
    }
    if (n == 14) {
        const int expected = BarcodeEncoder::eanCheckDigit(s.left(13));
        if (expected != s.at(13).digitValue()) {
            *error = QStringLiteral("Check digit is %1 but should be %2")
                         .arg(s.at(13))
                         .arg(expected);
            return {};
        }
        return s;
    }
    if (n == 13)
        return s + QChar(QLatin1Char('0').unicode() + BarcodeEncoder::eanCheckDigit(s));
    if (n % 2 == 0) {
        QString padded = s;
        while (padded.size() < 13)
            padded.prepend(QLatin1Char('0'));
        return padded + QChar(QLatin1Char('0').unicode() + BarcodeEncoder::eanCheckDigit(padded));
    }
    *error = QStringLiteral("ITF requires an even number of digits (or 13 digits "
                            "without the check digit), but got %1")
                 .arg(n);
    return {};
}

// ===========================================================================
// Symbology builders (input already normalised)
// ===========================================================================
void fail(BarcodeEncoder::Result &r, const QString &message)
{
    r.ok = false;
    r.error = message;
    r.modules.clear();
}

BarcodeEncoder::Result buildCode128(const QString &data)
{
    BarcodeEncoder::Result r;
    r.symbology = BarcodeSymbology::Code128;
    QByteArray bytes;
    bytes.reserve(data.size());
    for (QChar c : data) {
        if (c.unicode() > 127) {
            fail(r, QStringLiteral("Code 128 supports ASCII characters only"));
            return r;
        }
        bytes.append(static_cast<char>(c.unicode()));
    }

    QString err;
    const QVector<int> plan = planCode128(bytes, &err);
    if (plan.isEmpty()) {
        fail(r, err.isEmpty() ? QStringLiteral("Code 128 could not encode the data") : err);
        return r;
    }

    int check = plan.at(0);
    for (int k = 1; k < plan.size(); ++k)
        check += plan.at(k) * k;
    check %= 103;

    for (int symbol : plan)
        appendCode128(r.modules, symbol);
    appendCode128(r.modules, check);
    appendCode128(r.modules, kC128Stop);

    r.ok = true;
    r.humanText = data;
    r.quietZoneModules = 10;
    return r;
}

BarcodeEncoder::Result buildCode39(const QString &data, bool showCheckDigit)
{
    BarcodeEncoder::Result r;
    r.symbology = BarcodeSymbology::Code39;
    const int narrow = 1;
    const int wide = 3;
    const int star = code39Index(QLatin1Char('*'));

    QVector<int> chars;
    chars.reserve(data.size() + 1);
    for (QChar c : data) {
        const int idx = code39Index(c);
        if (idx < 0 || idx == star) {
            fail(r, QStringLiteral("Code 39 cannot encode '%1'").arg(QString(c)));
            return r;
        }
        chars.push_back(idx);
    }
    if (showCheckDigit)
        chars.push_back(code39Index(BarcodeEncoder::code39CheckCharacter(data)));

    appendCode39Char(r.modules, kCode39Patterns[static_cast<std::size_t>(star)], narrow, wide);
    for (int idx : chars) {
        pushRun(r.modules, narrow, false); // inter-character gap
        appendCode39Char(r.modules, kCode39Patterns[static_cast<std::size_t>(idx)], narrow, wide);
    }
    pushRun(r.modules, narrow, false); // gap before the stop guard
    appendCode39Char(r.modules, kCode39Patterns[static_cast<std::size_t>(star)], narrow, wide);

    r.ok = true;
    r.humanText = data;
    r.quietZoneModules = 10;
    return r;
}

BarcodeEncoder::Result buildEan13(const QString &digits)
{
    BarcodeEncoder::Result r;
    r.symbology = BarcodeSymbology::Ean13;
    appendEanBits(r.modules, "101");
    const int first = digits.at(0).digitValue();
    const char *parity = kEan13Parity[static_cast<std::size_t>(first)];
    for (int i = 0; i < 6; ++i) {
        const int d = digits.at(i + 1).digitValue();
        appendEanBits(r.modules, parity[i] == 'G' ? kEanG[static_cast<std::size_t>(d)]
                                                  : kEanL[static_cast<std::size_t>(d)]);
    }
    appendEanBits(r.modules, "01010");
    for (int i = 0; i < 6; ++i) {
        const int d = digits.at(i + 7).digitValue();
        appendEanBits(r.modules, kEanR[static_cast<std::size_t>(d)]);
    }
    appendEanBits(r.modules, "101");
    r.ok = true;
    r.quietZoneModules = 11;
    return r;
}

BarcodeEncoder::Result buildEan8(const QString &digits)
{
    BarcodeEncoder::Result r;
    r.symbology = BarcodeSymbology::Ean8;
    appendEanBits(r.modules, "101");
    for (int i = 0; i < 4; ++i)
        appendEanBits(r.modules, kEanL[static_cast<std::size_t>(digits.at(i).digitValue())]);
    appendEanBits(r.modules, "01010");
    for (int i = 0; i < 4; ++i)
        appendEanBits(r.modules, kEanR[static_cast<std::size_t>(digits.at(i + 4).digitValue())]);
    appendEanBits(r.modules, "101");
    r.ok = true;
    r.quietZoneModules = 7;
    return r;
}

BarcodeEncoder::Result buildItf14(const QString &digits)
{
    BarcodeEncoder::Result r;
    r.symbology = BarcodeSymbology::Itf14;
    const int narrow = 1;
    const int wide = 3;

    // Start pattern n n n n (bar/space/bar/space).
    pushRun(r.modules, narrow, true);
    pushRun(r.modules, narrow, false);
    pushRun(r.modules, narrow, true);
    pushRun(r.modules, narrow, false);

    for (int i = 0; i + 1 < digits.size(); i += 2) {
        const int a = digits.at(i).digitValue();
        const int b = digits.at(i + 1).digitValue();
        appendItfPair(r.modules, kItf[static_cast<std::size_t>(a)],
                      kItf[static_cast<std::size_t>(b)], narrow, wide);
    }

    // Stop pattern w n n (wide bar, narrow space, narrow bar).
    pushRun(r.modules, wide, true);
    pushRun(r.modules, narrow, false);
    pushRun(r.modules, narrow, true);

    r.ok = true;
    r.quietZoneModules = 10;
    return r;
}

} // namespace

// ===========================================================================
// BarcodeEncoder public interface
// ===========================================================================
BarcodeEncoder::Result BarcodeEncoder::encode(const QString &data, BarcodeSymbology symbology,
                                              bool showCheckDigit)
{
    Result r;
    r.symbology = symbology;

    QString err;
    const QString norm = normalize(data, symbology, &err);
    if (norm.isEmpty()) {
        r.error = err.isEmpty() ? QStringLiteral("Invalid barcode data") : err;
        return r;
    }

    switch (symbology) {
    case BarcodeSymbology::Code128: r = buildCode128(norm); break;
    case BarcodeSymbology::Code39: r = buildCode39(norm, showCheckDigit); break;
    case BarcodeSymbology::Ean13: r = buildEan13(norm); break;
    case BarcodeSymbology::Ean8: r = buildEan8(norm); break;
    case BarcodeSymbology::Itf14: r = buildItf14(norm); break;
    }
    if (r.ok)
        r.humanText = humanReadableText(norm, symbology);
    return r;
}

bool BarcodeEncoder::validate(const QString &data, BarcodeSymbology symbology, QString *error)
{
    QString err;
    const QString norm = normalize(data, symbology, &err);
    if (norm.isEmpty()) {
        if (error)
            *error = err.isEmpty() ? QStringLiteral("Invalid barcode data") : err;
        return false;
    }
    if (error)
        error->clear();
    return true;
}

QString BarcodeEncoder::normalize(const QString &data, BarcodeSymbology symbology, QString *error)
{
    if (error)
        error->clear();

    QString err;
    QString result;

    switch (symbology) {
    case BarcodeSymbology::Code128: {
        if (data.isEmpty()) {
            err = QStringLiteral("Enter the text to encode for Code 128");
            break;
        }
        for (QChar c : data) {
            if (c.unicode() > 127) {
                err = QStringLiteral("Code 128 supports ASCII characters only");
                break;
            }
        }
        if (err.isEmpty())
            result = data;
        break;
    }
    case BarcodeSymbology::Code39: {
        if (data.isEmpty()) {
            err = QStringLiteral("Enter the text to encode for Code 39");
            break;
        }
        const QString up = data.toUpper();
        const int star = code39Index(QLatin1Char('*'));
        for (QChar c : up) {
            const int idx = code39Index(c);
            if (idx < 0 || idx == star) {
                err = QStringLiteral("Code 39 cannot encode '%1'").arg(QString(c));
                break;
            }
        }
        if (err.isEmpty())
            result = up;
        break;
    }
    case BarcodeSymbology::Ean13:
        result = normalizeEan(data, 13, 12, &err);
        break;
    case BarcodeSymbology::Ean8:
        result = normalizeEan(data, 8, 7, &err);
        break;
    case BarcodeSymbology::Itf14:
        result = normalizeItf14(data, &err);
        break;
    }

    if (result.isEmpty()) {
        if (error)
            *error = err.isEmpty() ? QStringLiteral("Invalid barcode data") : err;
        return {};
    }
    return result;
}

bool BarcodeEncoder::supportsHumanText(BarcodeSymbology symbology)
{
    switch (symbology) {
    case BarcodeSymbology::Code128:
    case BarcodeSymbology::Code39:
    case BarcodeSymbology::Ean13:
    case BarcodeSymbology::Ean8:
    case BarcodeSymbology::Itf14:
        return true;
    }
    return false;
}

QString BarcodeEncoder::humanReadableText(const QString &data, BarcodeSymbology symbology)
{
    QString err;
    QString norm = normalize(data, symbology, &err);
    if (norm.isEmpty())
        norm = data;

    switch (symbology) {
    case BarcodeSymbology::Code128:
    case BarcodeSymbology::Code39:
        return norm;
    case BarcodeSymbology::Ean13:
        if (norm.size() == 13)
            return norm.left(1) + QLatin1Char(' ') + norm.mid(1, 6) + QLatin1Char(' ')
                   + norm.mid(7, 6);
        return norm;
    case BarcodeSymbology::Ean8:
        if (norm.size() == 8)
            return norm.left(4) + QLatin1Char(' ') + norm.mid(4, 4);
        return norm;
    case BarcodeSymbology::Itf14:
        if (norm.size() == 14) {
            QString out;
            for (int i = 0; i < 14; i += 2) {
                if (i > 0)
                    out += QLatin1Char(' ');
                out += norm.mid(i, 2);
            }
            return out;
        }
        return norm;
    }
    return norm;
}

QString BarcodeEncoder::displayName(BarcodeSymbology symbology)
{
    switch (symbology) {
    case BarcodeSymbology::Code128: return QStringLiteral("Code 128");
    case BarcodeSymbology::Code39: return QStringLiteral("Code 39");
    case BarcodeSymbology::Ean13: return QStringLiteral("EAN-13");
    case BarcodeSymbology::Ean8: return QStringLiteral("EAN-8");
    case BarcodeSymbology::Itf14: return QStringLiteral("ITF-14");
    }
    return QStringLiteral("Code 128");
}

int BarcodeEncoder::eanCheckDigit(const QString &digitsWithoutCheckDigit)
{
    const int n = digitsWithoutCheckDigit.size();
    if (n == 0)
        return -1;
    int sum = 0;
    for (int i = 0; i < n; ++i) {
        const int d = digitsWithoutCheckDigit.at(i).digitValue();
        if (d < 0)
            return -1;
        // Rightmost data digit gets weight 3 (standard EAN/UPC/GTIN weighting).
        const int weight = ((n - i - 1) % 2 == 0) ? 3 : 1;
        sum += d * weight;
    }
    return (10 - (sum % 10)) % 10;
}

QChar BarcodeEncoder::code39CheckCharacter(const QString &data)
{
    int sum = 0;
    for (QChar c : data) {
        const int idx = code39Index(c);
        if (idx < 0 || idx == 43) // ignore the '*' guard and unknown characters
            continue;
        sum += idx;
    }
    return QLatin1Char(kCode39Alphabet[sum % 43]);
}

} // namespace occ
