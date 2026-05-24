// MSXROMReader.c - MSX ROM Reader Program
// Purpose: Read ROM data from MSX cartridges via serial communication
// Supports: MegaROM mappers detection and ROM dump
// Copyright @v9938

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <tchar.h>
#include <winioctl.h>
#include <string.h>
#include <conio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwchar>

// ============================================================================
// Constants and Defines
// ============================================================================

#define BANK_SIZE           0x2000  // 8K bank size
#define SLOT_ADDR_BASE      0x4000  // Slot base address
#define HASH_SIZE           0x1000  // Hash calculation size
//#define HASH_SIZE           0x1f00  // Hash calculation size
#define MAX_RESPONSE_LEN    256
#define TIMEOUT_MS          5000
#define SRAM_THRESHOLD      8       // Number of identical banks to detect SRAM
//#define DISPLAY_HASH               // Display HASH


// MegaROM Mapper Types
typedef enum {
    MAPPER_UNKNOWN = 0,
    MAPPER_ASCII_16K,               // ASCII 16K
    MAPPER_ASCII_8K,                // ASCII 8K
    MAPPER_KONAMI_8K,               // Konami 8K
    MAPPER_KONAMI_SCC,              // Konami SCC
    MAPPER_GENERIC_16K,             // Generic 16K
    MAPPER_GENERIC_8K,              // Generic 8K
    MAPPER_RTYPE,                   // R-Type
    MAPPER_CROSSBLAM,               // CROSS BLAM
    MAPPER_HARRYFOX,                // HARRY FOX
    MAPPER_FMPAC,                   // FMPAC
    MAPPER_HALNOTE,                 // Hal Note
    MAPPER_NO_MAPPER_16K,           // 16KB no mapper
    MAPPER_NO_MAPPER_32K,           // 32KB no mapper
    MAPPER_NO_MAPPER_48K            // 48KB no mapper
} MAPPER_TYPE;

// ROM Information Structure
typedef struct {
    MAPPER_TYPE mapperType;
    const char* mapperName;
    DWORD romSize;                  // in bytes
    DWORD bankCount;                // number of banks
    BOOL hasSRAM;                   // has SRAM
    DWORD validDataStart;           // start of valid data
    DWORD validDataSize;            // size of valid data
    DWORD readBankSize;             // size per bank read
    DWORD readAreaStart;            // start address for reading
    DWORD readAreaSize;             // size of read area
} ROM_INFO;

typedef struct {
    std::string title;
    std::string system;
    std::string company;
    std::string year;
    std::string sha1;
    bool found;
} ROM_DB_INFO;

// ============================================================================
// Utility Functions
// ============================================================================

static std::wstring GetDirectoryFromPath(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return L".";
    return path.substr(0, pos);
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& file)
{
    if (dir.empty())
        return file;

    wchar_t last = dir[dir.size() - 1];
    if (last == L'\\' || last == L'/')
        return dir + file;

    return dir + L"\\" + file;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty())
        return L"";

    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    if (len <= 0)
        return L"";

    std::wstring out;
    out.resize(len - 1);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], len);
    return out;
}

static std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty())
        return "";

    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 0)
        return "";

    std::string out;
    out.resize(len - 1);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], len, NULL, NULL);
    return out;
}

static std::string Trim(const std::string& s)
{
    size_t start = 0;
    while (start < s.size() && isspace((unsigned char)s[start]))
        start++;

    size_t end = s.size();
    while (end > start && isspace((unsigned char)s[end - 1]))
        end--;

    return s.substr(start, end - start);
}

static std::string ReplaceAll(std::string s, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
    return s;
}

static std::string DecodeXmlEntities(std::string s)
{
    s = ReplaceAll(s, "&amp;", "&");
    s = ReplaceAll(s, "&lt;", "<");
    s = ReplaceAll(s, "&gt;", ">");
    s = ReplaceAll(s, "&quot;", "\"");
    s = ReplaceAll(s, "&apos;", "'");
    s = ReplaceAll(s, "&#39;", "'");
    s = ReplaceAll(s, "&#38;", "&");
    return s;
}

static std::wstring SanitizeFileName(const std::wstring& name)
{
    std::wstring out = name;
    const wchar_t* invalidChars = L"<>:\"/\\|?*";

    for (size_t i = 0; i < out.size(); i++)
    {
        if (wcschr(invalidChars, out[i]) != NULL || out[i] < 32)
            out[i] = L'_';
    }

    while (!out.empty() && (out.back() == L' ' || out.back() == L'.'))
        out.pop_back();

    if (out.empty())
        out = L"unknown";

    return out;
}

// ============================================================================
// SHA-1
// ============================================================================

typedef struct {
    unsigned int state[5];
    unsigned int count[2];
    unsigned char buffer[64];
} SHA1_CTX;

static void SHA1Transform(unsigned int state[5], const unsigned char buffer[64])
{
    unsigned int a, b, c, d, e;

    typedef union {
        unsigned char c[64];
        unsigned int l[16];
    } CHAR64LONG16;

    CHAR64LONG16 block;
    memcpy(&block, buffer, 64);

#define ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
#define BLK0(i) (block.l[i] = (ROL(block.l[i],24) & 0xFF00FF00) | (ROL(block.l[i],8) & 0x00FF00FF))
#define BLK(i) (block.l[i&15] = ROL(block.l[(i+13)&15] ^ block.l[(i+8)&15] ^ block.l[(i+2)&15] ^ block.l[i&15],1))
#define R0(v,w,x,y,z,i) z += ((w&(x^y))^y)     + BLK0(i) + 0x5A827999 + ROL(v,5); w = ROL(w,30);
#define R1(v,w,x,y,z,i) z += ((w&(x^y))^y)     + BLK(i)  + 0x5A827999 + ROL(v,5); w = ROL(w,30);
#define R2(v,w,x,y,z,i) z += (w^x^y)           + BLK(i)  + 0x6ED9EBA1 + ROL(v,5); w = ROL(w,30);
#define R3(v,w,x,y,z,i) z += (((w|x)&y)|(w&x)) + BLK(i)  + 0x8F1BBCDC + ROL(v,5); w = ROL(w,30);
#define R4(v,w,x,y,z,i) z += (w^x^y)           + BLK(i)  + 0xCA62C1D6 + ROL(v,5); w = ROL(w,30);

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    R0(a, b, c, d, e, 0); R0(e, a, b, c, d, 1); R0(d, e, a, b, c, 2); R0(c, d, e, a, b, 3);
    R0(b, c, d, e, a, 4); R0(a, b, c, d, e, 5); R0(e, a, b, c, d, 6); R0(d, e, a, b, c, 7);
    R0(c, d, e, a, b, 8); R0(b, c, d, e, a, 9); R0(a, b, c, d, e, 10); R0(e, a, b, c, d, 11);
    R0(d, e, a, b, c, 12); R0(c, d, e, a, b, 13); R0(b, c, d, e, a, 14); R0(a, b, c, d, e, 15);
    R1(e, a, b, c, d, 16); R1(d, e, a, b, c, 17); R1(c, d, e, a, b, 18); R1(b, c, d, e, a, 19);
    R2(a, b, c, d, e, 20); R2(e, a, b, c, d, 21); R2(d, e, a, b, c, 22); R2(c, d, e, a, b, 23);
    R2(b, c, d, e, a, 24); R2(a, b, c, d, e, 25); R2(e, a, b, c, d, 26); R2(d, e, a, b, c, 27);
    R2(c, d, e, a, b, 28); R2(b, c, d, e, a, 29); R2(a, b, c, d, e, 30); R2(e, a, b, c, d, 31);
    R2(d, e, a, b, c, 32); R2(c, d, e, a, b, 33); R2(b, c, d, e, a, 34); R2(a, b, c, d, e, 35);
    R2(e, a, b, c, d, 36); R2(d, e, a, b, c, 37); R2(c, d, e, a, b, 38); R2(b, c, d, e, a, 39);
    R3(a, b, c, d, e, 40); R3(e, a, b, c, d, 41); R3(d, e, a, b, c, 42); R3(c, d, e, a, b, 43);
    R3(b, c, d, e, a, 44); R3(a, b, c, d, e, 45); R3(e, a, b, c, d, 46); R3(d, e, a, b, c, 47);
    R3(c, d, e, a, b, 48); R3(b, c, d, e, a, 49); R3(a, b, c, d, e, 50); R3(e, a, b, c, d, 51);
    R3(d, e, a, b, c, 52); R3(c, d, e, a, b, 53); R3(b, c, d, e, a, 54); R3(a, b, c, d, e, 55);
    R3(e, a, b, c, d, 56); R3(d, e, a, b, c, 57); R3(c, d, e, a, b, 58); R3(b, c, d, e, a, 59);
    R4(a, b, c, d, e, 60); R4(e, a, b, c, d, 61); R4(d, e, a, b, c, 62); R4(c, d, e, a, b, 63);
    R4(b, c, d, e, a, 64); R4(a, b, c, d, e, 65); R4(e, a, b, c, d, 66); R4(d, e, a, b, c, 67);
    R4(c, d, e, a, b, 68); R4(b, c, d, e, a, 69); R4(a, b, c, d, e, 70); R4(e, a, b, c, d, 71);
    R4(d, e, a, b, c, 72); R4(c, d, e, a, b, 73); R4(b, c, d, e, a, 74); R4(a, b, c, d, e, 75);
    R4(e, a, b, c, d, 76); R4(d, e, a, b, c, 77); R4(c, d, e, a, b, 78); R4(b, c, d, e, a, 79);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;

#undef ROL
#undef BLK0
#undef BLK
#undef R0
#undef R1
#undef R2
#undef R3
#undef R4
}

static void SHA1Init(SHA1_CTX* context)
{
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = context->count[1] = 0;
}

static void SHA1Update(SHA1_CTX* context, const unsigned char* data, unsigned int len)
{
    unsigned int i, j;

    j = (context->count[0] >> 3) & 63;
    if ((context->count[0] += len << 3) < (len << 3))
        context->count[1]++;
    context->count[1] += (len >> 29);

    if ((j + len) > 63)
    {
        memcpy(&context->buffer[j], data, (i = 64 - j));
        SHA1Transform(context->state, context->buffer);
        for (; i + 63 < len; i += 64)
            SHA1Transform(context->state, &data[i]);
        j = 0;
    }
    else
    {
        i = 0;
    }

    memcpy(&context->buffer[j], &data[i], len - i);
}

static void SHA1Final(unsigned char digest[20], SHA1_CTX* context)
{
    unsigned int i;
    unsigned char finalcount[8];
    unsigned char c;

    for (i = 0; i < 8; i++)
    {
        finalcount[i] = (unsigned char)((context->count[(i >= 4 ? 0 : 1)]
            >> ((3 - (i & 3)) * 8)) & 255);
    }

    c = 0200;
    SHA1Update(context, &c, 1);
    while ((context->count[0] & 504) != 448)
    {
        c = 0000;
        SHA1Update(context, &c, 1);
    }

    SHA1Update(context, finalcount, 8);

    for (i = 0; i < 20; i++)
    {
        digest[i] = (unsigned char)
            ((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }

    memset(context, 0, sizeof(*context));
    memset(&finalcount, 0, sizeof(finalcount));
}

static std::string CalcSHA1Hex(const BYTE* data, DWORD size)
{
    unsigned char digest[20];
    SHA1_CTX ctx;
    SHA1Init(&ctx);
    SHA1Update(&ctx, data, size);
    SHA1Final(digest, &ctx);

    char hex[41];
    for (int i = 0; i < 20; i++)
        sprintf_s(hex + (i * 2), 3, "%02x", digest[i]);
    hex[40] = '\0';
    return std::string(hex);
}

// ============================================================================
// XML DB Search
// ============================================================================

static bool ReadTextFileUtf8(const std::wstring& path, std::string& outText)
{
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs)
        return false;

    std::ostringstream oss;
    oss << ifs.rdbuf();
    outText = oss.str();

    if (outText.size() >= 3 &&
        (unsigned char)outText[0] == 0xEF &&
        (unsigned char)outText[1] == 0xBB &&
        (unsigned char)outText[2] == 0xBF)
    {
        outText.erase(0, 3);
    }

    return true;
}

static bool ExtractFirstElement(const std::string& block, const std::string& tag, std::string& value)
{
    std::string openTag = "<" + tag;
    size_t openPos = block.find(openTag);
    if (openPos == std::string::npos)
        return false;

    size_t gtPos = block.find('>', openPos);
    if (gtPos == std::string::npos)
        return false;

    std::string closeTag = "</" + tag + ">";
    size_t closePos = block.find(closeTag, gtPos + 1);
    if (closePos == std::string::npos)
        return false;

    value = block.substr(gtPos + 1, closePos - (gtPos + 1));
    value = DecodeXmlEntities(Trim(value));
    return true;
}

static bool ExtractSha1FromDumpBlock(const std::string& dumpBlock, std::string& sha1)
{
    size_t hashPos = dumpBlock.find("<hash");
    while (hashPos != std::string::npos)
    {
        size_t gtPos = dumpBlock.find('>', hashPos);
        if (gtPos == std::string::npos)
            return false;

        size_t closePos = dumpBlock.find("</hash>", gtPos + 1);
        if (closePos == std::string::npos)
            return false;

        std::string hashValue = Trim(dumpBlock.substr(gtPos + 1, closePos - (gtPos + 1)));
        if (hashValue.length() == 40)
        {
            sha1 = hashValue;
            std::transform(sha1.begin(), sha1.end(), sha1.begin(), ::tolower);
            return true;
        }

        hashPos = dumpBlock.find("<hash", closePos + 7);
    }

    return false;
}

static bool FindROMInfoBySha1(const std::wstring& xmlPath, const std::string& targetSha1, ROM_DB_INFO* info)
{
    std::string xmlText;
    if (!ReadTextFileUtf8(xmlPath, xmlText))
        return false;

    std::string target = targetSha1;
    std::transform(target.begin(), target.end(), target.begin(), ::tolower);

    size_t pos = 0;
    while (true)
    {
        size_t start = xmlText.find("<software>", pos);
        if (start == std::string::npos)
            break;

        size_t end = xmlText.find("</software>", start);
        if (end == std::string::npos)
            break;

        std::string softwareBlock = xmlText.substr(start, end - start);

        std::string title, system, company, year;
        ExtractFirstElement(softwareBlock, "title", title);
        ExtractFirstElement(softwareBlock, "system", system);
        ExtractFirstElement(softwareBlock, "company", company);
        ExtractFirstElement(softwareBlock, "year", year);

        size_t dumpPos = 0;
        while (true)
        {
            size_t dumpStart = softwareBlock.find("<dump>", dumpPos);
            if (dumpStart == std::string::npos)
                break;

            size_t dumpEnd = softwareBlock.find("</dump>", dumpStart);
            if (dumpEnd == std::string::npos)
                break;

            std::string dumpBlock = softwareBlock.substr(dumpStart, dumpEnd - dumpStart);
            std::string sha1;
            if (ExtractSha1FromDumpBlock(dumpBlock, sha1))
            {
                if (sha1 == target)
                {
                    info->title = title;
                    info->system = system;
                    info->company = company;
                    info->year = year;
                    info->sha1 = sha1;
                    info->found = true;
                    return true;
                }
            }

            dumpPos = dumpEnd + 7;
        }

        pos = end + 11;
    }

    info->found = false;
    return true;
}

// ============================================================================
// Serial Communication Functions
// ============================================================================

static BOOL ConfigureSerialPort(HANDLE hSerial)
{
    if (!SetupComm(hSerial, 65536, 65536))
    {
        puts("SetupComm error");
        return FALSE;
    }

    DCB dcbSerialParam = { sizeof(DCB) };
    if (!GetCommState(hSerial, &dcbSerialParam))
    {
        puts("GetCommState error");
        return FALSE;
    }

    PurgeComm(hSerial, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

    dcbSerialParam.BaudRate = CBR_115200;
    dcbSerialParam.ByteSize = 8;
    dcbSerialParam.Parity = NOPARITY;
    dcbSerialParam.StopBits = ONESTOPBIT;
    dcbSerialParam.fBinary = TRUE;
    dcbSerialParam.fOutxCtsFlow = FALSE;
    dcbSerialParam.fOutxDsrFlow = FALSE;
    dcbSerialParam.fDtrControl = DTR_CONTROL_DISABLE;
    dcbSerialParam.fRtsControl = RTS_CONTROL_DISABLE;
    dcbSerialParam.fOutX = FALSE;
    dcbSerialParam.fInX = FALSE;
    dcbSerialParam.fNull = FALSE;
    dcbSerialParam.fErrorChar = FALSE;

    if (!SetCommState(hSerial, &dcbSerialParam))
    {
        puts("SetCommState error");
        return FALSE;
    }

    COMMTIMEOUTS timeout;
    timeout.ReadIntervalTimeout = 100;
    timeout.ReadTotalTimeoutMultiplier = 0;
    timeout.ReadTotalTimeoutConstant = 500;
    timeout.WriteTotalTimeoutMultiplier = 0;
    timeout.WriteTotalTimeoutConstant = 500;

    if (!SetCommTimeouts(hSerial, &timeout))
    {
        puts("SetCommTimeouts error");
        return FALSE;
    }

    return TRUE;
}

static BOOL SendCommand(HANDLE hSerial, const char* command)
{
    DWORD sendsize;
    char tmpstr[512];
    sprintf_s(tmpstr, sizeof(tmpstr), "%s\r\n", command);

    if (!WriteFile(hSerial, tmpstr, (DWORD)strlen(tmpstr), &sendsize, NULL))
        return FALSE;

    FlushFileBuffers(hSerial);
    return TRUE;
}

static BOOL RecvResponse(HANDLE hSerial, char* response, size_t maxlen, DWORD timeoutMs)
{
    DWORD bytesRead;
    size_t idx = 0;
    COMMTIMEOUTS timeout;
    DWORD startTime, currentTime;

    timeout.ReadIntervalTimeout = 50;
    timeout.ReadTotalTimeoutMultiplier = 0;
    timeout.ReadTotalTimeoutConstant = 50;
    timeout.WriteTotalTimeoutMultiplier = 0;
    timeout.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(hSerial, &timeout);

    memset(response, 0, maxlen);
    startTime = GetTickCount();

    while (1)
    {
        char ch;
        if (!ReadFile(hSerial, &ch, 1, &bytesRead, NULL))
            return FALSE;

        if (bytesRead == 0)
        {
            currentTime = GetTickCount();
            if (timeoutMs > 0 && currentTime - startTime >= timeoutMs)
            {
                printf("Response timeout\n");
                return FALSE;
            }
            continue;
        }

        startTime = GetTickCount();

        if (idx < maxlen - 1)
        {
            response[idx++] = ch;
            response[idx] = '\0';
        }
        else
        {
            for (size_t i = 0; i < maxlen - 2; i++)
            {
                response[i] = response[i + 1];
            }
            response[maxlen - 2] = ch;
            response[maxlen - 1] = '\0';
        }

        if (idx >= 3 && strstr(response, "OK\n") != NULL)
            return TRUE;

        if (idx >= 5 && strstr(response, "FAIL\n") != NULL)
            return FALSE;
    }

    return FALSE;
}

static BOOL RecvBinaryBlock(HANDLE hSerial, BYTE* recvbuf, DWORD recvsize)
{
    DWORD bytesRead = 0;
    DWORD totalRead = 0;
    COMMTIMEOUTS timeout;

    timeout.ReadIntervalTimeout = 50;
    timeout.ReadTotalTimeoutMultiplier = 0;
    timeout.ReadTotalTimeoutConstant = 1000;
    timeout.WriteTotalTimeoutMultiplier = 0;
    timeout.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(hSerial, &timeout);

    memset(recvbuf, 0, recvsize);

    while (totalRead < recvsize)
    {
        if (!ReadFile(hSerial, recvbuf + totalRead, recvsize - totalRead, &bytesRead, NULL))
        {
            if (bytesRead == 0)
                break;
            return FALSE;
        }
        if (bytesRead == 0)
            break;
        totalRead += bytesRead;
    }

    return (totalRead == recvsize);
}

static BOOL SendBinary(HANDLE hSerial, const BYTE* data, DWORD size)
{
    DWORD sentSize;
    if (!WriteFile(hSerial, data, size, &sentSize, NULL))
        return FALSE;
    FlushFileBuffers(hSerial);
    return (sentSize == size);
}

// ============================================================================
// Cartridge Control Functions
// ============================================================================

static BOOL SlotCheck(HANDLE hSerial)
{
    char response[MAX_RESPONSE_LEN];

    printf("Checking cartridge insertion...\n");

    if (!SendCommand(hSerial, "SCHK"))
    {
        printf("Failed to send SCHK command\n");
        return FALSE;
    }

    if (!RecvResponse(hSerial, response, sizeof(response), TIMEOUT_MS))
    {
        printf("Failed to receive SCHK response\n");
        return FALSE;
    }

    if (strstr(response, "0010") != NULL)
    {
        printf("Cartridge is properly inserted\n");
        return TRUE;
    }

    return FALSE;
}

static BOOL SlotPowerOn(HANDLE hSerial)
{
    char response[MAX_RESPONSE_LEN];

    printf("Turning on slot power...\n");

    if (!SendCommand(hSerial, "SPON"))
    {
        printf("Failed to send SPON command\n");
        return FALSE;
    }

    if (!RecvResponse(hSerial, response, sizeof(response), TIMEOUT_MS))
    {
        printf("Failed to receive SPON response\n");
        return FALSE;
    }

    if (strstr(response, "OK") != NULL)
    {
        printf("Slot power turned on successfully\n");
        return TRUE;
    }

    return FALSE;
}

static BOOL SlotPowerOff(HANDLE hSerial)
{
    char response[MAX_RESPONSE_LEN];

    printf("Turning off slot power...\n");

    if (!SendCommand(hSerial, "SPOFF"))
    {
        printf("Failed to send SPOFF command\n");
        return FALSE;
    }

    if (!RecvResponse(hSerial, response, sizeof(response), TIMEOUT_MS))
    {
        printf("Failed to receive SPOFF response\n");
        return FALSE;
    }

    if (strstr(response, "OK\n") != NULL)
    {
        printf("Slot power turned off successfully\n");
        return TRUE;
    }

    printf("ERROR: Failed to turn off slot power\n");
    return FALSE;
}

static BOOL SlotReset(HANDLE hSerial)
{
    char response[MAX_RESPONSE_LEN];

//    printf("Slot reset.\n");

    if (!SendCommand(hSerial, "SRST"))
    {
        printf("Failed to send SRST command\n");
        return FALSE;
    }

    if (!RecvResponse(hSerial, response, sizeof(response), TIMEOUT_MS))
    {
        printf("Failed to receive SRST response\n");
        return FALSE;
    }

    if (strstr(response, "OK\n") != NULL)
    {
        return TRUE;
    }

    printf("ERROR: Failed to turn off slot power\n");
    return FALSE;
}

// ============================================================================
// Slot Access Functions
// ============================================================================

static BOOL slotWrite(HANDLE hSerial, DWORD address, BYTE data)
{
    char command[128];
    char response[MAX_RESPONSE_LEN];

    sprintf_s(command, sizeof(command), "SMWR,%04lX,%02X", address, data);

    if (!SendCommand(hSerial, command))
        return FALSE;

    if (!RecvResponse(hSerial, response, sizeof(response), TIMEOUT_MS))
        return FALSE;

    return TRUE;
}

static BOOL slotRead(HANDLE hSerial, DWORD address, BYTE* data)
{
    char command[128];
    char response[MAX_RESPONSE_LEN];

    sprintf_s(command, sizeof(command), "SMRD,%04lX", address);

    if (!SendCommand(hSerial, command))
        return FALSE;

    if (!RecvResponse(hSerial, response, sizeof(response), TIMEOUT_MS))
        return FALSE;

    if (sscanf_s(response, "%*x : %02hhX", data) == 1)
        return TRUE;

    return FALSE;
}

static BOOL slotDump(HANDLE hSerial, DWORD address, DWORD length, BYTE* data)
{
    char command[128];
    char response[MAX_RESPONSE_LEN];

    sprintf_s(command, sizeof(command), "SMTR,%04lX,%04lX\r\nBSND,0,%04lX", address, length, length);

    if (!SendCommand(hSerial, command))
        return FALSE;

    if (!RecvResponse(hSerial, response, sizeof(response), TIMEOUT_MS))
        return FALSE;

    if (!RecvBinaryBlock(hSerial, data, length))
        return FALSE;

    if (!RecvResponse(hSerial, response, sizeof(response), TIMEOUT_MS))
        return FALSE;

    return TRUE;
}

static BOOL slotReadHash(HANDLE hSerial, DWORD address, DWORD length, DWORD* hash)
{
    char command[128];
    char response[MAX_RESPONSE_LEN];
    DWORD tmp;

    sprintf_s(command, sizeof(command), "SMTH,%04lX,%04lX", address, length);

    if (!SendCommand(hSerial, command))
        return FALSE;

    if (!RecvResponse(hSerial, response, sizeof(response), TIMEOUT_MS))
        return FALSE;

    if (sscanf_s(response, "%*x : %x", &tmp) == 1) {
        *hash = (int)tmp;
        return TRUE;
    }
    return FALSE;
}

static BOOL hardwareSetting(HANDLE hSerial, DWORD address, DWORD data)
{
    char command[128];
    char response[MAX_RESPONSE_LEN];

    sprintf_s(command, sizeof(command), "HSET,%04lX,%X", address, data);

    if (!SendCommand(hSerial, command))
        return FALSE;

    if (!RecvResponse(hSerial, response, sizeof(response), TIMEOUT_MS))
        return FALSE;

    return TRUE;
}


// ============================================================================
// Hash Calculation
// ============================================================================

static DWORD Hash7936(const BYTE* data, DWORD address)
{
    DWORD hash = 0x5381;
    DWORD i;

    if (address + HASH_SIZE > 0xC000)
        return 0;

    for (i = 0; i < HASH_SIZE; i++)
    {
        hash = ((hash << 5) + hash) ^ data[address + i];
    }

    return hash;
}

static DWORD HashFilledFF(DWORD length)
{
    DWORD hash = 0x5381;
    DWORD i;

    for (i = 0; i < length; i++)
    {
        hash = ((hash << 5) + hash) ^ 0xff;
    }

    return hash;
}

// ============================================================================
// ROM ACCESS Timing Setting
// ============================================================================

static BOOL ReadHash5Match(HANDLE hSerial)
{
    DWORD h[5];
    DWORD addr;
    int i;
    int retry;
    const DWORD filledFFHash = HashFilledFF(HASH_SIZE);

    for (retry = 0; retry < 2; retry++)
    {
        addr = (retry == 0) ? 0x4000 : 0x8000;

        for (i = 0; i < 5; i++)
        {
            if (!slotReadHash(hSerial, addr, HASH_SIZE, &h[i]))
            {
                printf("slotReadHash failed at try %d\n", i + 1);
                return FALSE;
        }
    }

#ifdef DISPLAY_HASH
        printf("Hash(addr=%04lX) = %08lX, %08lX, %08lX, %08lX, %08lX\n",
            addr, h[0], h[1], h[2], h[3], h[4]);
#endif

        // 5回すべて一致しているか
        if (!(h[0] == h[1] && h[0] == h[2] && h[0] == h[3] && h[0] == h[4]))
        {
//            printf("Hash mismatch\n");
            return FALSE;
        }

        // 0x4000 で全て filledFFHash の場合のみ、0x8000 で再試行
        if (h[0] == filledFFHash)
        {
            if (retry == 0)
            {
                continue;
            }

            // 0x8000 側でも無効値だった
            return FALSE;
        }

        return TRUE;
}

    return FALSE;
}

// address 0 を 100(1us) ずつ 1000(10us) まで変更しながら hash 一致を確認
static BOOL SweepAddress0AndCheck(HANDLE hSerial)
{
    DWORD d;

    for (d = 100; d <= 1000; d += 100)
    {
        printf("HardwareSetting (wait %d us)\n", d/100);

        if (!hardwareSetting(hSerial, 0, d))
        {
            return FALSE;
        }

        if (ReadHash5Match(hSerial))
        {
            printf("%dus PASS\n", d/100);
            return TRUE;
        }
    }

    return FALSE;
}

// hash の安定化確認
static BOOL CheckHashWithRetry(HANDLE hSerial)
{
    printf("Read Timing Check.\n");

    // default設定値
    if (!hardwareSetting(hSerial, 0, 0))  return FALSE;
    if (!hardwareSetting(hSerial, 0, 100))  return FALSE;

    printf("Checking default setting... ");
    if (ReadHash5Match(hSerial))
    {
        printf("PASS\n");
        return TRUE;
    }
    printf("FAILED\n");
    printf("Phase 1: Checking setting1 ... (/rd = 1us) \n");
    // Phase 1:
    // address 0 を 1us 刻みで 100us まで変更しながら確認
    if (SweepAddress0AndCheck(hSerial))
    return TRUE;

    printf("FAILED\n");
    // Phase 2:
    // address 0 を 0 に戻し、address 1 を 200(2us) に設定して再試行
    printf("Phase 2: Checking setting2 ... (/rd = 2us) \n");

    if (!hardwareSetting(hSerial, 0, 0))
    {
        printf("hardwareSetting failed: address=0, data=0\n");
        return FALSE;
    }

    if (!hardwareSetting(hSerial, 1, 200))
    {
        printf("hardwareSetting failed: address=1, data=200\n");
        return FALSE;
    }

    // address 1 設定直後の状態を一度確認
    printf("Checking after address 1 setting...\n");
    if (ReadHash5Match(hSerial))
    {
        return TRUE;
    }

    // その後、再度 address 0 を sweep
    printf("Phase 2 retry: sweep address 0 again\n");
    if (SweepAddress0AndCheck(hSerial))
        return TRUE;

    printf("FAILED\n");
    printf("Hash did not stabilize\n");
    return FALSE;
}




// ============================================================================
// Mapper Detection / Read Functions
// ============================================================================

static BOOL DetectASCII16K(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("--- Testing ASCII 16K ---\n");

    DWORD hashA[4], hashB[4];
    DWORD prevHashA[4];
    DWORD bankNum;
    DWORD maxBank = 0;
    int identicalCount = 0;
    DWORD sramStartBank = 0;
    BOOL foundPattern = FALSE;
    BYTE sramOrgData[4];
    BYTE sramData[4];
    const DWORD filledFFHash = HashFilledFF(HASH_SIZE);


    romInfo->hasSRAM = FALSE;

    if (!slotWrite(hSerial, 0x6000, 0)) return FALSE;
    if (!slotWrite(hSerial, 0x6800, 0)) return FALSE;
    if (!slotWrite(hSerial, 0x7000, 1)) return FALSE;
    if (!slotWrite(hSerial, 0x7800, 1)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashA[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashA[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashA[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashA[3])) return FALSE;

#ifdef DISPLAY_HASH
    printf("Bank 0: Hash[0x4000]=%08lX, Hash[0x6000]=%08lX, Hash[0x8000]=%08lX, Hash[0xA000]=%08lX\n",
        hashA[0], hashA[1], hashA[2], hashA[3]);
#endif

    memcpy(prevHashA, hashA, sizeof(hashA));
    identicalCount = 0;

    for (bankNum = 1; bankNum <= 0xff; bankNum++)
    {
        if (!slotWrite(hSerial, 0x6000, (BYTE)bankNum)) return FALSE;
        if (!slotWrite(hSerial, 0x6800, 0)) return FALSE;
        if (!slotWrite(hSerial, 0x7000, (BYTE)(bankNum + 1))) return FALSE;
        if (!slotWrite(hSerial, 0x7800, 1)) return FALSE;

        if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashB[0])) return FALSE;
        if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashB[1])) return FALSE;
        if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashB[2])) return FALSE;
        if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashB[3])) return FALSE;

        // BS6101対策(ROMANCIA/YoungSherlock etc)
        if ((bankNum == 1) && (hashB[0] == prevHashA[0]) && (hashB[1] == prevHashA[1])) break;

        if ((bankNum % 4 == 0) && (filledFFHash == hashB[0]) && (filledFFHash == hashB[1]) && (filledFFHash == hashB[2]) && (filledFFHash == hashB[3]))
        {
            break;
        }
        if ((prevHashA[0] == hashB[0]) && (prevHashA[1] == hashB[1]) && (prevHashA[2] == hashB[2]) && (prevHashA[3] == hashB[3]))
        {
            break;
        }

        if (hashA[2] == hashB[0] && hashA[3] == hashB[1])
        {
            maxBank = bankNum;
            foundPattern = TRUE;
        }

        //SRAM Check
        if (!slotRead(hSerial, 0x8000, &sramOrgData[0])) return FALSE;
        if (!slotRead(hSerial, 0xA000, &sramOrgData[2])) return FALSE;

        if (!slotWrite(hSerial, 0x8000, ~sramOrgData[0])) return FALSE;
        if (!slotWrite(hSerial, 0xA000, ~sramOrgData[2])) return FALSE;

        if (!slotRead(hSerial, 0x8000, &sramData[0])) return FALSE;
        if (!slotRead(hSerial, 0xA000, &sramData[2])) return FALSE;

        if ((sramData[0] == (sramOrgData[0] ^ (BYTE)0xff)) || (sramData[2] == (sramOrgData[2] ^ (BYTE)0xff))) {
            if (!slotWrite(hSerial, 0x8000, sramOrgData[0])) return FALSE;
            if (!slotWrite(hSerial, 0xA000, sramOrgData[2])) return FALSE;
            maxBank = bankNum;
            romInfo->hasSRAM = TRUE;
            foundPattern = TRUE;
            break;
        }


#ifdef DISPLAY_HASH
        printf("Bank %lu: Hash[0x4000]=%08lX, Hash[0x6000]=%08lX, Hash[0x8000]=%08lX, Hash[0xA000]=%08lX\n",
            bankNum, hashB[0], hashB[1], hashB[2], hashB[3]);
#endif


        memcpy(hashA, hashB, sizeof(hashB));
        if (foundPattern == FALSE) break;
    }

    if (foundPattern && maxBank > 0)
    {
        printf("\n=== ASCII 16K Detected ===\n");
        romInfo->mapperType = MAPPER_ASCII_16K;

        if (romInfo->hasSRAM)  romInfo->mapperName = "ASCII 16K(+SRAM)";
        else romInfo->mapperName = "ASCII 16K";

        romInfo->mapperName = "ASCII 16K";
        romInfo->bankCount = maxBank + 1;
        romInfo->romSize = romInfo->bankCount * 0x4000;
        romInfo->readBankSize = 0x4000;
        romInfo->readAreaStart = 0x8000;
        romInfo->readAreaSize = 0x4000;
        printf("Bank count: %lu, ROM size: %lu (0x%lX)\n", romInfo->bankCount, romInfo->romSize, romInfo->romSize);
        return TRUE;
    }

    return FALSE;
}

static BOOL DetectASCII8K(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("--- Testing ASCII 8K ---\n");

    DWORD hashA[4], hashB[4];
    DWORD prevHashA[4];
    DWORD bankNum;
    DWORD maxBank = 0;
    int identicalCount = 0;
    DWORD sramStartBank = 0;
    BOOL foundSRAM = FALSE;
    BOOL foundPattern = FALSE;
    BYTE sramOrgData[4];
    BYTE sramData[4];
    const DWORD filledFFHash = HashFilledFF(HASH_SIZE);

    romInfo->hasSRAM = FALSE;

    if (!slotWrite(hSerial, 0x6000, 0)) return FALSE;
    if (!slotWrite(hSerial, 0x6800, 1)) return FALSE;
    if (!slotWrite(hSerial, 0x7000, 2)) return FALSE;
    if (!slotWrite(hSerial, 0x7800, 3)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashA[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashA[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashA[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashA[3])) return FALSE;

    //KONAMI8K check
    if (!slotWrite(hSerial, 0x8000, 0)) return FALSE;
    if (!slotWrite(hSerial, 0xA000, 0)) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashB[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashB[3])) return FALSE;

    if ((hashA[2] != hashB[2]) || (hashA[3] != hashB[3])) return FALSE;

    memcpy(prevHashA, hashA, sizeof(hashA));
    identicalCount = 0;

#ifdef DISPLAY_HASH
    printf("Bank 0: Hash[0x4000]=%08lX, Hash[0x6000]=%08lX, Hash[0x8000]=%08lX, Hash[0xA000]=%08lX\n",
        hashA[0], hashA[1], hashA[2], hashA[3]);
#endif

    for (bankNum = 1; bankNum <= 0xff; bankNum++)
    {
        if (!slotWrite(hSerial, 0x6000, (BYTE)bankNum)) return FALSE;
        if (!slotWrite(hSerial, 0x6800, (BYTE)(bankNum + 1))) return FALSE;
        if (!slotWrite(hSerial, 0x7000, (BYTE)(bankNum + 2))) return FALSE;
        if (!slotWrite(hSerial, 0x7800, (BYTE)(bankNum + 3))) return FALSE;

        if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashB[0])) return FALSE;
        if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashB[1])) return FALSE;
        if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashB[2])) return FALSE;
        if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashB[3])) return FALSE;

        // BS6101対策(ROMANCIA/YoungSherlock etc)
        if ((bankNum == 1) && (hashB[0] == prevHashA[0]) ) break;


        if ((bankNum % 8 == 0) && (filledFFHash == hashB[0]) && (filledFFHash == hashB[1]) && (filledFFHash == hashB[2]) && (filledFFHash == hashB[3]))
        {
            break;
        }

        if ((prevHashA[0] == hashB[0]) && (prevHashA[1] == hashB[1]) && (prevHashA[2] == hashB[2]) && (prevHashA[3] == hashB[3]))
        {
            break;
        }

        if (hashA[1] == hashB[0] && hashA[2] == hashB[1] && hashA[3] == hashB[2])
        {
            maxBank = bankNum;
            foundPattern = TRUE;
        }

        //SRAM Check
        if (!slotRead(hSerial, 0xA000, &sramOrgData[0])) return FALSE;
        if (!slotRead(hSerial, 0xB000, &sramOrgData[2])) return FALSE;

        if (!slotWrite(hSerial, 0xA000, ~sramOrgData[0])) return FALSE;
        if (!slotWrite(hSerial, 0xB000, ~sramOrgData[2])) return FALSE;

        if (!slotRead(hSerial, 0xA000, &sramData[0])) return FALSE;
        if (!slotRead(hSerial, 0xB000, &sramData[2])) return FALSE;


//        if ((sramData[0] == ~sramOrgData[0]) || (sramData[2] == ~sramOrgData[2])) {
        if ((sramData[0] == (sramOrgData[0] ^(BYTE) 0xff)) || (sramData[2] == (sramOrgData[2] ^ (BYTE) 0xff))) {

            if (!slotWrite(hSerial, 0xA000, sramOrgData[0])) return FALSE;
            if (!slotWrite(hSerial, 0xB000, sramOrgData[2])) return FALSE;
            maxBank = bankNum + 2;
            romInfo->hasSRAM = TRUE;
            foundPattern = TRUE;
            break;
        }

#ifdef DISPLAY_HASH
        printf("Bank %lu: Hash[0x4000]=%08lX, Hash[0x6000]=%08lX, Hash[0x8000]=%08lX, Hash[0xA000]=%08lX\n",
            bankNum, hashB[0], hashB[1], hashB[2], hashB[3]);
#endif


        memcpy(hashA, hashB, sizeof(hashB));
        if (foundPattern == FALSE) break;
    }

    if (foundPattern && maxBank > 0)
    {
        printf("\n=== ASCII 8K Detected ===\n");

        romInfo->mapperType = MAPPER_ASCII_8K;
        if (romInfo->hasSRAM)  romInfo->mapperName = "ASCII 8K(+SRAM)";
        else romInfo->mapperName = "ASCII 8K";
        romInfo->bankCount = maxBank + 1;
        romInfo->romSize = romInfo->bankCount * 0x2000;
        romInfo->readBankSize = 0x2000;
        romInfo->readAreaStart = 0x8000;
        romInfo->readAreaSize = 0x2000;
        printf("Bank count: %lu, ROM size: %lu (0x%lX)\n", romInfo->bankCount, romInfo->romSize, romInfo->romSize);
        return TRUE;
    }

    return FALSE;
}


static BOOL DetectKONAMI8K(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("--- Testing KONAMI 8K ---\n");

    DWORD hashA[4], hashB[4], hashC[4];
    DWORD prevHashA[4];
    DWORD bankNum;
    DWORD maxBank = 0;
    int identicalCount = 0;
    BOOL foundPattern = FALSE;
    const DWORD filledFFHash = HashFilledFF(HASH_SIZE);

    romInfo->hasSRAM = false;


    if (!slotWrite(hSerial, 0x4000, 3)) return FALSE;
    if (!slotWrite(hSerial, 0x6000, 0)) return FALSE;
    if (!slotWrite(hSerial, 0x8000, 1)) return FALSE;
    if (!slotWrite(hSerial, 0xA000, 2)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashA[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashA[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashA[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashA[3])) return FALSE;

    memcpy(prevHashA, hashA, sizeof(hashA));
    identicalCount = 0;

    for (bankNum = 1; bankNum <= 0x1F; bankNum++)
    {
        if (!slotWrite(hSerial, 0x4000, (BYTE)(bankNum + 2))) return FALSE;
        if (!slotWrite(hSerial, 0x6000, (BYTE)(bankNum + 0))) return FALSE;
        if (!slotWrite(hSerial, 0x8000, (BYTE)(bankNum + 1))) return FALSE;
        if (!slotWrite(hSerial, 0xA000, (BYTE)(bankNum + 2))) return FALSE;

        if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashB[0])) return FALSE;
        if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashB[1])) return FALSE;
        if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashB[2])) return FALSE;
        if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashB[3])) return FALSE;

        if ((bankNum % 8 == 0) && (filledFFHash == hashB[1]) && (filledFFHash == hashB[2]) && (filledFFHash == hashB[3]))
        {
            break;
        }

        //新10倍カートリッジのチェック SRAMは4KBx2pageなので同じデータが連続する
        if ((bankNum + 1) == 0x10) {
            if (!slotReadHash(hSerial, 0x8000, 0x1000, &hashC[0])) return FALSE;
            if (!slotReadHash(hSerial, 0x9000, 0x1000, &hashC[1])) return FALSE;
            if (hashC[0] == hashC[1]) {
                maxBank = bankNum;
                romInfo->hasSRAM = TRUE;
                foundPattern = TRUE;
                break;
            }
        }

        if ((prevHashA[0] == hashB[0]) && (prevHashA[1] == hashB[1]) && (prevHashA[2] == hashB[2]) && (prevHashA[3] == hashB[3]))
        {
            break;
        }

        if (hashA[0] == hashB[0] && hashA[2] == hashB[1] && hashA[3] == hashB[2])
        {
            maxBank = bankNum;
            foundPattern = TRUE;
        }

        memcpy(hashA, hashB, sizeof(hashB));
        if (foundPattern == FALSE) break;
    }

    if (foundPattern && maxBank > 0)
    {
        printf("\n=== KONAMI 8K Detected ===\n");
        romInfo->mapperType = MAPPER_KONAMI_8K;
        romInfo->mapperName = romInfo->hasSRAM ? "KONAMI 8K (+SRAM)" : "KONAMI 8K";
        romInfo->bankCount = maxBank + 1;
        romInfo->romSize = romInfo->bankCount * 0x2000;
        romInfo->readBankSize = 0x2000;
        romInfo->readAreaStart = 0x8000;
        romInfo->readAreaSize = 0x2000;
        printf("Bank count: %lu, ROM size: %lu (0x%lX)\n", romInfo->bankCount, romInfo->romSize, romInfo->romSize);
        return TRUE;
    }

    return FALSE;
}

static BOOL DetectKONAMI_SCC(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("--- Testing KONAMI SCC ---\n");

    DWORD hashA[4], hashB[4];
    DWORD prevHashA[4];
    DWORD bankNum;
    DWORD maxBank = 0;
    BOOL foundPattern = FALSE;
    const DWORD filledFFHash = HashFilledFF(HASH_SIZE);


    if (!slotWrite(hSerial, 0x5000, 0)) return FALSE;
    if (!slotWrite(hSerial, 0x7000, 1)) return FALSE;
    if (!slotWrite(hSerial, 0x9000, 2)) return FALSE;
    if (!slotWrite(hSerial, 0xB000, 3)) return FALSE;
    if (!slotWrite(hSerial, 0x5800, 0)) return FALSE;
    if (!slotWrite(hSerial, 0x7800, 1)) return FALSE;
    if (!slotWrite(hSerial, 0x9800, 2)) return FALSE;
    if (!slotWrite(hSerial, 0xB800, 3)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashA[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashA[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashA[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashA[3])) return FALSE;

    memcpy(prevHashA, hashA, sizeof(hashA));

    for (bankNum = 1; bankNum <= 0x3F; bankNum++)
    {
        if (!slotWrite(hSerial, 0x5000, (BYTE)bankNum)) return FALSE;
        if (!slotWrite(hSerial, 0x7000, (BYTE)(bankNum + 1))) return FALSE;
        if (!slotWrite(hSerial, 0x9000, (BYTE)(bankNum + 2))) return FALSE;
        if (!slotWrite(hSerial, 0xB000, (BYTE)(bankNum + 3))) return FALSE;
        if (!slotWrite(hSerial, 0x5800, 0)) return FALSE;
        if (!slotWrite(hSerial, 0x7800, 1)) return FALSE;
        if (!slotWrite(hSerial, 0x9800, 2)) return FALSE;
        if (!slotWrite(hSerial, 0xB800, 3)) return FALSE;

        if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashB[0])) return FALSE;
        if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashB[1])) return FALSE;
        if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashB[2])) return FALSE;
        if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashB[3])) return FALSE;

        if ((bankNum % 8 == 0) && (filledFFHash == hashB[1]) && (filledFFHash == hashB[2]) && (filledFFHash == hashB[3]))
        {
            break;
        }

        if ((prevHashA[0] == hashB[0]) && (prevHashA[1] == hashB[1]) && (prevHashA[2] == hashB[2]) && (prevHashA[3] == hashB[3]))
        {
            break;
        }

        if (hashA[1] == hashB[0] && hashA[2] == hashB[1] && hashA[3] == hashB[2])
        {
            maxBank = bankNum;
            foundPattern = TRUE;
        }

        memcpy(hashA, hashB, sizeof(hashB));
        if (foundPattern == FALSE) break;
    }

    if (foundPattern && maxBank > 0)
    {
        printf("\n=== KONAMI SCC Detected ===\n");
        romInfo->mapperType = MAPPER_KONAMI_SCC;
        romInfo->mapperName = "KONAMI SCC";
        romInfo->bankCount = maxBank + 1;
        romInfo->romSize = romInfo->bankCount * 0x2000;
        romInfo->readBankSize = 0x2000;
        romInfo->readAreaStart = 0x8000;
        romInfo->readAreaSize = 0x2000;
        printf("Bank count: %lu, ROM size: %lu (0x%lX)\n", romInfo->bankCount, romInfo->romSize, romInfo->romSize);
        return TRUE;
    }

    return FALSE;
}

static BOOL DetectGeneric16K(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("--- Testing Generic 16K ---\n");

    DWORD hashA[4], hashB[4];
    DWORD prevHashA[4];
    DWORD bankNum;
    DWORD maxBank = 0;
    int identicalCount = 0;
    DWORD sramStartBank = 0;
    BOOL foundPattern = FALSE;
    const DWORD filledFFHash = HashFilledFF(HASH_SIZE);


    if (!slotWrite(hSerial, 0x4000, 0)) return FALSE;
    if (!slotWrite(hSerial, 0x6000, 0)) return FALSE;
    if (!slotWrite(hSerial, 0x8000, 1)) return FALSE;
    if (!slotWrite(hSerial, 0xA000, 1)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashA[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashA[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashA[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashA[3])) return FALSE;

    memcpy(prevHashA, hashA, sizeof(hashA));
    memcpy(prevHashA, hashA, sizeof(hashA));
    identicalCount = 0;

    for (bankNum = 1; bankNum <= 0xff; bankNum++)
    {
        if (!slotWrite(hSerial, 0x4000, (BYTE)bankNum)) return FALSE;
        if (!slotWrite(hSerial, 0x6000, 0)) return FALSE;
        if (!slotWrite(hSerial, 0x8000, (BYTE)(bankNum + 1))) return FALSE;
        if (!slotWrite(hSerial, 0x7800, 1)) return FALSE;

        if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashB[0])) return FALSE;
        if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashB[1])) return FALSE;
        if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashB[2])) return FALSE;
        if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashB[3])) return FALSE;

        if ((bankNum % 4 == 0) && (filledFFHash == hashB[0]) && (filledFFHash == hashB[1]) && (filledFFHash == hashB[2]) && (filledFFHash == hashB[3]))
        {
            break;
        }

        if ((prevHashA[0] == hashB[0]) && (prevHashA[1] == hashB[1]) && (prevHashA[2] == hashB[2]) && (prevHashA[3] == hashB[3]))
        {
            break;
        }

        if (hashA[2] == hashB[0] && hashA[3] == hashB[1])
        {
            maxBank = bankNum;
            foundPattern = TRUE;
        }

        memcpy(hashA, hashB, sizeof(hashB));
        if (foundPattern == FALSE) break;
    }

    if (foundPattern && maxBank > 0)
    {
        printf("\n=== Generic 16K Detected ===\n");
        romInfo->mapperType = MAPPER_GENERIC_16K;
        romInfo->mapperName = "Generic 16K";
        romInfo->bankCount = maxBank + 1;
        romInfo->romSize = romInfo->bankCount * 0x4000;
        romInfo->readBankSize = 0x4000;
        romInfo->readAreaStart = 0x8000;
        romInfo->readAreaSize = 0x4000;
        printf("Bank count: %lu, ROM size: %lu (0x%lX)\n", romInfo->bankCount, romInfo->romSize, romInfo->romSize);
        return TRUE;
    }

    return FALSE;
}

static BOOL DetectGeneric8K(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("--- Testing Generic 8K ---\n");
    DWORD hashA[4], hashB[4];
    DWORD prevHashA[4];
    DWORD bankNum;
    DWORD maxBank = 0;
    int identicalCount = 0;
    DWORD sramStartBank = 0;
    BOOL foundPattern = FALSE;
    const DWORD filledFFHash = HashFilledFF(HASH_SIZE);


    if (!slotWrite(hSerial, 0x4000, 0)) return FALSE;
    if (!slotWrite(hSerial, 0x6000, 1)) return FALSE;
    if (!slotWrite(hSerial, 0x8000, 2)) return FALSE;
    if (!slotWrite(hSerial, 0xA000, 3)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashA[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashA[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashA[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashA[3])) return FALSE;

    memcpy(prevHashA, hashA, sizeof(hashA));
    memcpy(prevHashA, hashA, sizeof(hashA));
    identicalCount = 0;

    for (bankNum = 1; bankNum <= 0xff; bankNum++)
    {
        if (!slotWrite(hSerial, 0x4000, (BYTE)bankNum)) return FALSE;
        if (!slotWrite(hSerial, 0x6000, (BYTE)(bankNum + 1))) return FALSE;
        if (!slotWrite(hSerial, 0x8000, (BYTE)(bankNum + 2))) return FALSE;
        if (!slotWrite(hSerial, 0xa000, (BYTE)(bankNum + 3))) return FALSE;

        if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashB[0])) return FALSE;
        if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashB[1])) return FALSE;
        if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashB[2])) return FALSE;
        if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashB[3])) return FALSE;

        if ((bankNum % 8 == 0) && (filledFFHash == hashB[0]) && (filledFFHash == hashB[1]) && (filledFFHash == hashB[2]) && (filledFFHash == hashB[3]))
        {
            break;
        }

        if ((prevHashA[0] == hashB[0]) && (prevHashA[1] == hashB[1]) && (prevHashA[2] == hashB[2]) && (prevHashA[3] == hashB[3]))
        {
            break;
        }

        if (hashA[2] == hashB[0] && hashA[3] == hashB[1])
        {
            maxBank = bankNum;
            foundPattern = TRUE;
        }

        memcpy(hashA, hashB, sizeof(hashB));
        if (foundPattern == FALSE) break;
    }

    if (foundPattern && maxBank > 0)
    {
        printf("\n=== Generic 8K Detected ===\n");
        romInfo->mapperType = MAPPER_GENERIC_8K;
        romInfo->mapperName = "Generic 8K";
        romInfo->bankCount = maxBank + 1;
        romInfo->romSize = romInfo->bankCount * 0x2000;
        romInfo->readBankSize = 0x2000;
        romInfo->readAreaStart = 0x8000;
        romInfo->readAreaSize = 0x2000;
        printf("Bank count: %lu, ROM size: %lu (0x%lX)\n", romInfo->bankCount, romInfo->romSize, romInfo->romSize);
        return TRUE;
    }

    return FALSE;
}

static BOOL DetectRType(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("--- Testing R-Type ---\n");

    DWORD hashA[4], hashB[4];
    DWORD prevHashA[4];
    DWORD maxBank = 0;
    DWORD sramStartBank = 0;
    BOOL foundPattern = FALSE;


    if (!slotWrite(hSerial, 0x7000, 0x0f)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashA[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashA[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashA[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashA[3])) return FALSE;

    // ページ2バンク0x0Fがページ1と同じ内容であることを確認
    if ((hashA[0] != hashA[2]) || (hashA[1] != hashA[3])) return FALSE;

    memcpy(prevHashA, hashA, sizeof(hashA));

    if (!slotWrite(hSerial, 0x6000, 0x01)) return FALSE;
    if (!slotWrite(hSerial, 0x7800, 0x1f)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashB[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashB[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashB[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashB[3])) return FALSE;

    // ページ1は固定で、ページ2の0x0Fと0x1Fと同じ内容であることを確認
    if ((hashA[0] != hashB[0]) || (hashA[1] != hashB[1]) || (hashA[2] != hashB[2]) || (hashA[3] != hashB[3])) return FALSE;

    if (!slotWrite(hSerial, 0x6800, 0x00)) return FALSE;
    if (!slotWrite(hSerial, 0x7800, 0x00)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashB[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashB[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashB[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hashB[3])) return FALSE;

    if ((hashA[0] == hashB[0]) && (hashA[1] == hashB[1]) && (hashA[2] != hashB[2]) && (hashA[3] != hashB[3]))
    {
        printf("\n=== R-Type Detected ===\n");
        romInfo->mapperType = MAPPER_RTYPE;
        romInfo->mapperName = "R-TYPE";
        romInfo->bankCount = 0x18;
        romInfo->romSize = romInfo->bankCount * 0x4000;
        romInfo->readBankSize = 0x4000;
        romInfo->readAreaStart = 0x8000;
        romInfo->readAreaSize = 0x4000;
        printf("Bank count: %lu, ROM size: %lu (0x%lX)\n", romInfo->bankCount, romInfo->romSize, romInfo->romSize);
        return TRUE;
    }

    return FALSE;
}

static BOOL DetectCrossBlam(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("--- Testing Cross Blam ---\n");

    DWORD hashA[4], hashB[04], hashC[4];

    if (!slotWrite(hSerial, 0x6000, 0x00)) return FALSE;
    if (!slotWrite(hSerial, 0x7000, 0x01)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashA[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashA[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashA[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xA000, HASH_SIZE, &hashA[3])) return FALSE;

    if (!slotWrite(hSerial, 0x6000, 0x02)) return FALSE;
    if (!slotWrite(hSerial, 0x7000, 0x02)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashB[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashB[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashB[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xA000, HASH_SIZE, &hashB[3])) return FALSE;

    if ((hashA[0] != hashB[0]) || (hashA[1] != hashB[1]) || (hashA[2] == hashB[2]) || (hashA[3] == hashB[3]))
        return FALSE;

    if (!slotWrite(hSerial, 0x6000, 0x01)) return FALSE;
    if (!slotWrite(hSerial, 0x7000, 0x03)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hashC[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hashC[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hashC[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xA000, HASH_SIZE, &hashC[3])) return FALSE;

    if ((hashA[0] != hashC[0]) || (hashA[1] != hashC[1]) || (hashB[2] == hashC[2]) || (hashB[3] == hashC[3]))
        return FALSE;

    DWORD expectedHash[4][2] = {
        { hashA[2], hashA[3] },
        { hashA[2], hashA[3] },
        { hashB[2], hashB[3] },
        { hashC[2], hashC[3] }
    };

    for (DWORD bank = 0; bank < 8; bank++)
    {
        if (!slotWrite(hSerial, 0x7FFF, (BYTE)bank)) return FALSE;

        DWORD h2, h3;
        if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &h2)) return FALSE;
        if (!slotReadHash(hSerial, 0xA000, HASH_SIZE, &h3)) return FALSE;

        DWORD idx = bank % 4;
        if ((h2 != expectedHash[idx][0]) || (h3 != expectedHash[idx][1]))
            return FALSE;
    }

    printf("\n=== Cross Blam ROM Detected ===\n");
    romInfo->mapperType = MAPPER_CROSSBLAM;
    romInfo->mapperName = "CROSS BLAM (page1:fix + page2:bank)";
    romInfo->bankCount = 0x4;
    romInfo->romSize = romInfo->bankCount * 0x4000;
    romInfo->readBankSize = 0x4000;
    romInfo->readAreaStart = 0x4000;
    romInfo->readAreaSize = 0x8000;
    printf("Bank count: %lu, ROM size: %lu (0x%lX)\n", romInfo->bankCount, romInfo->romSize, romInfo->romSize);
    return TRUE;
}

static BOOL DetectHarryFox(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("--- Testing HarryFox ---\n");

    DWORD hash[4];
    DWORD prevHashA[4];
    DWORD prevHashB[4];
    BYTE i;

    /* 基準パターンA取得 */
    if (!slotWrite(hSerial, 0x6000, 0x00)) return FALSE;
    if (!slotWrite(hSerial, 0x7000, 0x00)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &prevHashA[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &prevHashA[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &prevHashA[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &prevHashA[3])) return FALSE;

    /* 基準パターンB取得 */
    if (!slotWrite(hSerial, 0x6000, 0x01)) return FALSE;
    if (!slotWrite(hSerial, 0x7000, 0x01)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &prevHashB[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &prevHashB[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &prevHashB[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &prevHashB[3])) return FALSE;

    if ((prevHashA[0] == prevHashB[0]) || (prevHashA[1] == prevHashB[1]) || (prevHashA[2] == prevHashB[2]) || (prevHashA[3] == prevHashB[3])) return FALSE;
 
    /* i = 0～8 を検査 */
    for (i = 0; i <= 7; i++)
    {
        DWORD* expectedHash = (i % 2 == 0) ? prevHashA : prevHashB;

        if (!slotWrite(hSerial, 0x6fff, i)) return FALSE;
        if (!slotWrite(hSerial, 0x7fff, i)) return FALSE;

        if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &hash[0])) return FALSE;
        if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &hash[1])) return FALSE;
        if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &hash[2])) return FALSE;
        if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &hash[3])) return FALSE;

        if ((hash[0] != expectedHash[0]) ||
            (hash[1] != expectedHash[1]) ||
            (hash[2] != expectedHash[2]) ||
            (hash[3] != expectedHash[3]))
        {
            return FALSE;
        }
    }

    printf("\n=== Harry Fox ROM Detected ===\n");
    romInfo->mapperType = MAPPER_HARRYFOX;
    romInfo->mapperName = "HARRY FOX";
    romInfo->bankCount = 0x2;
    romInfo->romSize = romInfo->bankCount * 0x8000;
    romInfo->readBankSize = 0x8000;
    romInfo->readAreaStart = 0x4000;
    romInfo->readAreaSize = 0x8000;
    printf("Bank count: %lu, ROM size: %lu (0x%lX)\n", romInfo->bankCount, romInfo->romSize, romInfo->romSize);


    return TRUE;
}

static BOOL DetectHalnote(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("--- Testing HALNOTE ---\n");

    DWORD prevHashA[4];
    DWORD prevHashB[4];

    /* 基準パターンA取得 */
    if (!slotWrite(hSerial, 0x4FFF, 0x00)) return FALSE;
    if (!slotWrite(hSerial, 0x6FFF, 0x00)) return FALSE;
    if (!slotWrite(hSerial, 0x8FFF, 0x00)) return FALSE;
    if (!slotWrite(hSerial, 0xAFFF, 0x00)) return FALSE;

    // Main Mapper
    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &prevHashA[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &prevHashA[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &prevHashA[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &prevHashA[3])) return FALSE;

    if ((prevHashA[0] != prevHashA[1]) || (prevHashA[1] != prevHashA[2]) || (prevHashA[2] != prevHashA[3])) return FALSE;

    if (!slotReadHash(hSerial, 0x6C00, 0x0800, &prevHashA[1])) return FALSE;

    if (!slotWrite(hSerial, 0x4FFF, 0x00)) return FALSE;
    if (!slotWrite(hSerial, 0x6FFF, 0x01)) return FALSE;
    if (!slotWrite(hSerial, 0x8FFF, 0x02)) return FALSE;
    if (!slotWrite(hSerial, 0xAFFF, 0x00)) return FALSE;

    // Main Mapper
    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &prevHashB[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6000, HASH_SIZE, &prevHashB[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x8000, HASH_SIZE, &prevHashB[2])) return FALSE;
    if (!slotReadHash(hSerial, 0xa000, HASH_SIZE, &prevHashB[3])) return FALSE;

    if ((prevHashA[0] == prevHashB[1]) || (prevHashA[0] == prevHashB[2]) || (prevHashB[0] != prevHashB[3])) return FALSE;

    // Sub Mapper
    if (!slotWrite(hSerial, 0x4FFF, 0x40)) return FALSE;
    if (!slotWrite(hSerial, 0x6FFF, 0x80)) return FALSE;

    if (!slotReadHash(hSerial, 0x4000, HASH_SIZE, &prevHashB[0])) return FALSE;
    if (!slotReadHash(hSerial, 0x6C00, 0x0800, &prevHashB[1])) return FALSE;
    if (!slotReadHash(hSerial, 0x4000, 0x0800, &prevHashB[2])) return FALSE;
    if (!slotReadHash(hSerial, 0x7000, 0x0800, &prevHashB[3])) return FALSE;

    if ((prevHashA[0] == prevHashB[1]) || (prevHashA[0] == prevHashB[1]) || (prevHashB[2] != prevHashB[3])) return FALSE;


    printf("\n=== HALNOTE ROM Detected ===\n");
    romInfo->mapperType = MAPPER_HALNOTE;
    romInfo->mapperName = "HALNOTE";
    romInfo->bankCount = 0x80;
    romInfo->romSize = romInfo->bankCount * 0x2000;
    romInfo->readBankSize = 0x2000;
    romInfo->readAreaStart = 0x6000;
    romInfo->readAreaSize = 0x2000;
    printf("Bank count: %lu, ROM size: %lu (0x%lX)\n", romInfo->bankCount, romInfo->romSize, romInfo->romSize);


    return TRUE;
}


static BOOL DetectStandardROM(HANDLE hSerial, ROM_INFO* romInfo)
{
    BYTE fullDataBuffer[0xC000];
    DWORD filledFFHash;

    filledFFHash = HashFilledFF(HASH_SIZE);

    printf("=== Detecting Standard ROM Type ===\n");

    printf("Reading 0x0000-0xBFFF...\n");
    if (!slotDump(hSerial, 0x0000, 0xC000, fullDataBuffer))
    {
        printf("Failed to read full ROM\n");
        return FALSE;
    }

    DWORD hash0 = Hash7936(fullDataBuffer, 0x0000);
    DWORD hash2 = Hash7936(fullDataBuffer, 0x2000);
    DWORD hash4 = Hash7936(fullDataBuffer, 0x4000);
    DWORD hash6 = Hash7936(fullDataBuffer, 0x6000);
    DWORD hash8 = Hash7936(fullDataBuffer, 0x8000);
    DWORD hashA = Hash7936(fullDataBuffer, 0xA000);

    if ((hash0 == filledFFHash) && (hash2 == filledFFHash) && (hash4 != filledFFHash) && (hash4 == hash6) && (hash8 == filledFFHash) && (hashA == filledFFHash))
    {
        printf("\n=== 8KB Mirrored ROM Detected ===\n");
        romInfo->mapperType = MAPPER_NO_MAPPER_16K;
        romInfo->mapperName = "8KB ROM (Mirrored)";
        romInfo->romSize = 0x2000;
        romInfo->validDataStart = 0x4000;
        romInfo->validDataSize = 0x2000;
        return TRUE;
    }

    if ((hash0 == filledFFHash) && (hash2 == filledFFHash) && (hash4 == filledFFHash) && (hash6 == filledFFHash) && (hash8 != filledFFHash) && (hash8 == hashA))
    {
        printf("\n=== 8KB Mirrored ROM Detected ===\n");
        romInfo->mapperType = MAPPER_NO_MAPPER_16K;
        romInfo->mapperName = "8KB ROM (Mirrored)";
        romInfo->romSize = 0x2000;
        romInfo->validDataStart = 0x8000;
        romInfo->validDataSize = 0x2000;
        return TRUE;
    }

    if ((hash0 == filledFFHash) && (hash2 == filledFFHash) && (hash4 != filledFFHash) && (hash6 == filledFFHash) && (hash8 == filledFFHash) && (hashA == filledFFHash))
    {
        printf("\n=== 16KB Standard ROM Detected ===\n");
        romInfo->mapperType = MAPPER_NO_MAPPER_16K;
        romInfo->mapperName = "8KB ROM";
        romInfo->romSize = 0x2000;
        romInfo->validDataStart = 0x4000;
        romInfo->validDataSize = 0x2000;
        return TRUE;
    }

    if ((hash0 == filledFFHash) && (hash2 == filledFFHash) && (hash4 != filledFFHash) && (hash6 != filledFFHash) && (hash8 == filledFFHash) && (hashA == filledFFHash))
    {
        printf("\n=== 16KB Standard ROM Detected ===\n");
        romInfo->mapperType = MAPPER_NO_MAPPER_16K;
        romInfo->mapperName = "16KB ROM";
        romInfo->romSize = 0x4000;
        romInfo->validDataStart = 0x4000;
        romInfo->validDataSize = 0x4000;
        return TRUE;
    }

    if ((hash0 == filledFFHash) && (hash2 == filledFFHash) && (hash4 == filledFFHash) && (hash6 == filledFFHash) && (hash8 != filledFFHash) && (hashA != filledFFHash))
    {
        printf("\n=== 16KB Standard ROM Detected ===\n");
        romInfo->mapperType = MAPPER_NO_MAPPER_16K;
        romInfo->mapperName = "16KB ROM";
        romInfo->romSize = 0x4000;
        romInfo->validDataStart = 0x8000;
        romInfo->validDataSize = 0x4000;
        return TRUE;
    }

    if ((hash0 == hash4) && (hash2 == hash6) && (hash4 == hash8) && (hash6 == hashA))
    {
        printf("\n=== 16KB Standard ROM Detected ===\n");
        romInfo->mapperType = MAPPER_NO_MAPPER_16K;
        romInfo->mapperName = "16KB ROM";
        romInfo->romSize = 0x4000;
        romInfo->validDataStart = 0x4000;
        romInfo->validDataSize = 0x4000;
        return TRUE;
    }

    if ((hash0 == filledFFHash) && (hash2 == filledFFHash) && (hash4 == hash8) && (hash6 == hashA))
    {
        printf("\n=== 16KB Standard ROM Detected ===\n");
        romInfo->mapperType = MAPPER_NO_MAPPER_16K;
        romInfo->mapperName = "16KB ROM";
        romInfo->romSize = 0x4000;
        romInfo->validDataStart = 0x4000;
        romInfo->validDataSize = 0x4000;
        return TRUE;
    }

    if ((hash0 == filledFFHash) && (hash2 == filledFFHash) && (hash4 != hash8) && (hash6 != hashA))
    {
        printf("\n=== 32KB Standard ROM Detected ===\n");
        romInfo->mapperType = MAPPER_NO_MAPPER_16K;
        romInfo->mapperName = "32KB ROM";
        romInfo->romSize = 0x8000;
        romInfo->validDataStart = 0x4000;
        romInfo->validDataSize = 0x8000;
        return TRUE;
    }

    if ((hash0 == hash4) && (hash2 == hash6) && (hash4 != hash8) && (hash6 != hashA))
    {
        printf("\n=== 32KB Standard ROM Detected ===\n");
        romInfo->mapperType = MAPPER_NO_MAPPER_16K;
        romInfo->mapperName = "32KB ROM";
        romInfo->romSize = 0x8000;
        romInfo->validDataStart = 0x4000;
        romInfo->validDataSize = 0x8000;
        return TRUE;
    }

    if ((hash0 == hash8) && (hash2 == hashA) && (hash4 != hash8) && (hash6 != hashA))
    {
        printf("\n=== 32KB Standard ROM Detected ===\n");
        romInfo->mapperType = MAPPER_NO_MAPPER_16K;
        romInfo->mapperName = "32KB ROM";
        romInfo->romSize = 0x8000;
        romInfo->validDataStart = 0x4000;
        romInfo->validDataSize = 0x8000;
        return TRUE;
    }

    printf("\n=== 48KB Standard ROM Detected ===\n");
    romInfo->mapperType = MAPPER_NO_MAPPER_48K;
    romInfo->mapperName = "48KB ROM";
    romInfo->romSize = 0xC000;
    romInfo->validDataStart = 0x0000;
    romInfo->validDataSize = 0xC000;
    return TRUE;
}

static BOOL ReadASCII16K(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading ASCII 16K ROM ===\n\n");

    BYTE buffer[0x4000];
    DWORD bytesWritten = 0;

    for (DWORD bank = 0; bank < romInfo->bankCount; bank++)
    {
        if (!slotWrite(hSerial, 0x7000, (BYTE)bank)) return FALSE;

        if (!slotDump(hSerial, romInfo->readAreaStart, romInfo->readAreaSize, buffer))
        {
            printf("Failed to read bank %lu\n", bank);
            return FALSE;
        }

        memcpy(outData + bytesWritten, buffer, romInfo->readBankSize);
        bytesWritten += romInfo->readBankSize;

        printf("Saved bank %lu (0x%04lX - 0x%04lX)\n", bank, bytesWritten - romInfo->readBankSize, bytesWritten - 1);
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", bytesWritten, bytesWritten);
    return TRUE;
}

static BOOL ReadASCII8K(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading ASCII 8K ROM ===\n\n");

    BYTE buffer[0x2000];
    DWORD bytesWritten = 0;

    for (DWORD bank = 0; bank < romInfo->bankCount; bank++)
    {
        if (!slotWrite(hSerial, 0x7000, (BYTE)bank)) return FALSE;

        if (!slotDump(hSerial, romInfo->readAreaStart, romInfo->readAreaSize, buffer))
        {
            printf("Failed to read bank %lu\n", bank);
            return FALSE;
        }

        memcpy(outData + bytesWritten, buffer, romInfo->readBankSize);
        bytesWritten += romInfo->readBankSize;

        printf("Saved bank %lu (0x%04lX - 0x%04lX)\n", bank, bytesWritten - romInfo->readBankSize, bytesWritten - 1);
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", bytesWritten, bytesWritten);
    return TRUE;
}

static BOOL ReadKONAMI8K(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading KONAMI 8K ROM ===\n\n");

    BYTE buffer[0x2000];
    DWORD bytesWritten = 0;

    for (DWORD bank = 0; bank < romInfo->bankCount; bank++)
    {
        if (!slotWrite(hSerial, 0x8000, (BYTE)bank)) return FALSE;

        if (!slotDump(hSerial, romInfo->readAreaStart, romInfo->readAreaSize, buffer))
        {
            printf("Failed to read bank %lu\n", bank);
            return FALSE;
        }

        memcpy(outData + bytesWritten, buffer, romInfo->readBankSize);
        bytesWritten += romInfo->readBankSize;

        printf("Saved bank %lu (0x%04lX - 0x%04lX)\n", bank, bytesWritten - romInfo->readBankSize, bytesWritten - 1);
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", bytesWritten, bytesWritten);
    return TRUE;
}

static BOOL ReadKONAMI_SCC(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading KONAMI SCC ROM ===\n\n");

    BYTE buffer[0x2000];
    DWORD bytesWritten = 0;

    for (DWORD bank = 0; bank < romInfo->bankCount; bank++)
    {
        if (!slotWrite(hSerial, 0x9000, (BYTE)bank)) return FALSE;

        if (!slotDump(hSerial, romInfo->readAreaStart, romInfo->readAreaSize, buffer))
        {
            printf("Failed to read bank %lu\n", bank);
            return FALSE;
        }

        memcpy(outData + bytesWritten, buffer, romInfo->readBankSize);
        bytesWritten += romInfo->readBankSize;

        printf("Saved bank %lu (0x%04lX - 0x%04lX)\n", bank, bytesWritten - romInfo->readBankSize, bytesWritten - 1);
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", bytesWritten, bytesWritten);
    return TRUE;
}

static BOOL ReadGeneric16K(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading Generic 16K ROM ===\n\n");

    BYTE buffer[0x4000];
    DWORD bytesWritten = 0;

    for (DWORD bank = 0; bank < romInfo->bankCount; bank++)
    {
        if (!slotWrite(hSerial, 0x4000, (BYTE)bank)) return FALSE;
        if (!slotWrite(hSerial, 0x8000, (BYTE)(bank + 1))) return FALSE;

        if (!slotDump(hSerial, romInfo->readAreaStart, romInfo->readAreaSize, buffer))
        {
            printf("Failed to read bank %lu\n", bank);
            return FALSE;
        }

        memcpy(outData + bytesWritten, buffer, romInfo->readBankSize);
        bytesWritten += romInfo->readBankSize;

        printf("Saved bank %lu (0x%04lX - 0x%04lX)\n", bank, bytesWritten - romInfo->readBankSize, bytesWritten - 1);
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", bytesWritten, bytesWritten);
    return TRUE;
}

static BOOL ReadGeneric8K(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading Generic 8K ROM ===\n\n");

    BYTE buffer[0x2000];
    DWORD bytesWritten = 0;

    for (DWORD bank = 0; bank < romInfo->bankCount; bank++)
    {
        if (!slotWrite(hSerial, 0x4000, (BYTE)bank)) return FALSE;
        if (!slotWrite(hSerial, 0x6000, (BYTE)(bank + 1))) return FALSE;
        if (!slotWrite(hSerial, 0x8000, (BYTE)(bank + 2))) return FALSE;
        if (!slotWrite(hSerial, 0xA000, (BYTE)(bank + 3))) return FALSE;

        if (!slotDump(hSerial, romInfo->readAreaStart, romInfo->readAreaSize, buffer))
        {
            printf("Failed to read bank %lu\n", bank);
            return FALSE;
        }

        memcpy(outData + bytesWritten, buffer, romInfo->readBankSize);
        bytesWritten += romInfo->readBankSize;

        printf("Saved bank %lu (0x%04lX - 0x%04lX)\n", bank, bytesWritten - romInfo->readBankSize, bytesWritten - 1);
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", bytesWritten, bytesWritten);
    return TRUE;
}

static BOOL ReadRType(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading R-TYPE ROM ===\n\n");

    BYTE buffer[0x4000];
    DWORD bytesWritten = 0;

    for (DWORD bank = 0; bank < romInfo->bankCount; bank++)
    {
        if (!slotWrite(hSerial, 0x7000, (BYTE)bank)) return FALSE;

        if (!slotDump(hSerial, romInfo->readAreaStart, romInfo->readAreaSize, buffer))
        {
            printf("Failed to read bank %lu\n", bank);
            return FALSE;
        }

        memcpy(outData + bytesWritten, buffer, romInfo->readBankSize);
        bytesWritten += romInfo->readBankSize;

        printf("Saved bank %lu (0x%04lX - 0x%04lX)\n", bank, bytesWritten - romInfo->readBankSize, bytesWritten - 1);
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", bytesWritten, bytesWritten);
    return TRUE;
}

static BOOL ReadCrossBlam(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading Cross Blam ROM ===\n\n");

    BYTE buffer[0x4000];
    DWORD bytesWritten = 0;
    DWORD fixedSize = romInfo->readAreaSize - romInfo->readBankSize;

    _ASSERT(sizeof(buffer) >= fixedSize);
    _ASSERT(sizeof(buffer) >= romInfo->readBankSize);

    for (DWORD bank = 0; bank < romInfo->bankCount; bank++)
    {
        if (!slotWrite(hSerial, 0x7000, (BYTE)bank)) return FALSE;

        DWORD readAreaStart = romInfo->readAreaStart;
        DWORD readAreaSize = fixedSize;

        if (bank != 0)
        {
            readAreaStart += fixedSize;
            readAreaSize = romInfo->readBankSize;
        }

        if (!slotDump(hSerial, readAreaStart, readAreaSize, buffer))
        {
            printf("Failed to read bank %lu\n", bank);
            return FALSE;
        }

        memcpy(outData + bytesWritten, buffer, readAreaSize);
        bytesWritten += readAreaSize;

        printf("Saved bank %lu (0x%04lX - 0x%04lX)\n", bank, bytesWritten - readAreaSize, bytesWritten - 1);
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", bytesWritten, bytesWritten);
    return TRUE;
}

static BOOL ReadHarryFox(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading Harry Fox -Yuki no Maou- ROM ===\n\n");

    BYTE buffer[0x8000];
    DWORD bytesWritten = 0;

    for (DWORD bank = 0; bank < romInfo->bankCount; bank++)
    {
        if (!slotWrite(hSerial, 0x6000, (BYTE)bank)) return FALSE;
        if (!slotWrite(hSerial, 0x7000, (BYTE)bank)) return FALSE;

        if (!slotDump(hSerial, romInfo->readAreaStart, romInfo->readAreaSize, buffer))
        {
            printf("Failed to read bank %lu\n", bank);
            return FALSE;
        }

        memcpy(outData + bytesWritten, buffer, romInfo->readBankSize);
        bytesWritten += romInfo->readBankSize;

        printf("Saved bank %lu (0x%04lX - 0x%04lX)\n", bank, bytesWritten - romInfo->readBankSize, bytesWritten - 1);
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", bytesWritten, bytesWritten);
    return TRUE;
}


static BOOL ReadHalNote(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading HALNOTE ROM ===\n\n");

    BYTE buffer[0x2000];
    DWORD bytesWritten = 0;

    if (!slotWrite(hSerial, 0xC000, 0x03)) return FALSE;

    for (DWORD bank = 0; bank < romInfo->bankCount; bank++)
    {
        if (!slotWrite(hSerial, 0x6FFF, (BYTE)bank)) return FALSE;

        if (!slotDump(hSerial, romInfo->readAreaStart, romInfo->readAreaSize, buffer))
        {
            printf("Failed to read bank %lu\n", bank);
            return FALSE;
        }

        memcpy(outData + bytesWritten, buffer, romInfo->readBankSize);
        bytesWritten += romInfo->readBankSize;

        printf("Saved bank %lu (0x%04lX - 0x%04lX)\n", bank, bytesWritten - romInfo->readBankSize, bytesWritten - 1);
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", bytesWritten, bytesWritten);
    return TRUE;
}


static BOOL ReadStandardROM(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("=== Reading Standard ROM ===\n");

    printf("Reading standard ROM: 0x%04lX - 0x%04lX (%lu bytes)\n",
        romInfo->validDataStart, romInfo->validDataStart + romInfo->validDataSize - 1,
        romInfo->validDataSize);

    if (!slotDump(hSerial, romInfo->validDataStart, romInfo->validDataSize, outData))
    {
        printf("Failed to read ROM\n");
        return FALSE;
    }

    printf("\nTotal bytes read: %lu (0x%lX)\n", romInfo->validDataSize, romInfo->validDataSize);
    return TRUE;
}

static BOOL ReadCompleteROM(HANDLE hSerial, ROM_INFO* romInfo, BYTE* outData)
{
    printf("\n========== READING ROM DATA ==========\n");

    switch (romInfo->mapperType)
    {
    case MAPPER_ASCII_16K:
        return ReadASCII16K(hSerial, romInfo, outData);
    case MAPPER_ASCII_8K:
        return ReadASCII8K(hSerial, romInfo, outData);
    case MAPPER_KONAMI_8K:
        return ReadKONAMI8K(hSerial, romInfo, outData);
    case MAPPER_KONAMI_SCC:
        return ReadKONAMI_SCC(hSerial, romInfo, outData);
    case MAPPER_GENERIC_16K:
        return ReadGeneric16K(hSerial, romInfo, outData);
    case MAPPER_GENERIC_8K:
        return ReadGeneric8K(hSerial, romInfo, outData);
    case MAPPER_RTYPE:
        return ReadRType(hSerial, romInfo, outData);
    case MAPPER_CROSSBLAM:
        return ReadCrossBlam(hSerial, romInfo, outData);
    case MAPPER_HARRYFOX:
        return ReadHarryFox(hSerial, romInfo, outData);
    case MAPPER_HALNOTE:
        return ReadHalNote(hSerial, romInfo, outData);


    case MAPPER_NO_MAPPER_16K:
    case MAPPER_NO_MAPPER_32K:
    case MAPPER_NO_MAPPER_48K:
        return ReadStandardROM(hSerial, romInfo, outData);
    default:
        printf("Unknown mapper type\n");
        return FALSE;
    }
}

static BOOL SaveROMToFile(const wchar_t* filename, const BYTE* data, DWORD size)
{
    printf("\n=== Saving ROM to File ===\n\n");

    HANDLE hFile = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        wprintf(L"Failed to create output file: %s\n", filename);
        return FALSE;
    }

    DWORD bytesWritten;
    if (!WriteFile(hFile, data, size, &bytesWritten, NULL))
    {
        printf("Failed to write file\n");
        CloseHandle(hFile);
        return FALSE;
    }

    CloseHandle(hFile);

    wprintf(L"ROM saved successfully: %s (%lu bytes)\n", filename, bytesWritten);
    return TRUE;
}

// ============================================================================
// Main Mapper Detection
// ============================================================================

static BOOL DetectMapper(HANDLE hSerial, ROM_INFO* romInfo)
{
    printf("\n========== ROM DETECTION START ==========\n");

    printf("\n=== Testing MegaROM Mappers ===\n");

    if (DetectASCII16K(hSerial, romInfo))
        return TRUE;
    if (DetectASCII8K(hSerial, romInfo))
        return TRUE;
    if (DetectKONAMI8K(hSerial, romInfo))
        return TRUE;
    if (DetectKONAMI_SCC(hSerial, romInfo))
        return TRUE;
    if (DetectGeneric16K(hSerial, romInfo))
        return TRUE;
    if (DetectGeneric8K(hSerial, romInfo))
        return TRUE;
    if (DetectRType(hSerial, romInfo))
        return TRUE;
    if (DetectCrossBlam(hSerial, romInfo))
        return TRUE;
    if (DetectHarryFox(hSerial, romInfo))
        return TRUE;
    if (DetectHalnote(hSerial, romInfo))
        return TRUE;
    if (DetectStandardROM(hSerial, romInfo))
        return TRUE;

    printf("\nROM detection failed\n");
    return FALSE;
}

// ============================================================================
// Main Processing
// ============================================================================
static std::wstring GetFileNameFromPath(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return path;
    return path.substr(pos + 1);
}

static std::wstring SanitizeMapperNameForFileName(const char* mapperName)
{
    if (!mapperName)
        return L"UnknownMapper";

    std::wstring src = Utf8ToWide(std::string(mapperName));
    std::wstring result;
    result.reserve(src.size());

    for (wchar_t ch : src)
    {
        switch (ch)
        {
        case L'\\':
        case L'/':
        case L':':
        case L'*':
        case L'?':
        case L'"':
        case L'<':
        case L'>':
        case L'|':
            result += L'_';
            break;
        default:
            if (iswspace(ch))
                result += L'_';
            else
                result += ch;
            break;
        }
    }

    if (result.empty())
        result = L"UnknownMapper";

    return result;
}

static bool ContainsABorCDAtOffset(const BYTE* romData, DWORD romSize, DWORD offset)
{
    if (!romData)
        return false;

    if (offset + 1 >= romSize)
        return false;

    return ((romData[offset] == 'A' && romData[offset + 1] == 'B') ||
        (romData[offset] == 'C' && romData[offset + 1] == 'D'));
}

static bool IsSuccessfulROMImage(const BYTE* romData, DWORD romSize)
{
    return ContainsABorCDAtOffset(romData, romSize, 0x0000) ||
        ContainsABorCDAtOffset(romData, romSize, 0x4000) ||
        ContainsABorCDAtOffset(romData, romSize, 0x8000) ||
        ContainsABorCDAtOffset(romData, romSize, 0x3C000);
}

static bool FileExists(const wchar_t* path)
{
    if (!path || !path[0])
        return false;

    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool CalcFileSHA1Hex(const wchar_t* filePath, std::string& outSha1)
{
    outSha1.clear();

    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        CloseHandle(hFile);
        return false;
    }

    if (fileSize.QuadPart <= 0 || fileSize.QuadPart > 0xFFFFFFFF)
    {
        CloseHandle(hFile);
        return false;
    }

    DWORD size = (DWORD)fileSize.QuadPart;
    BYTE* buffer = (BYTE*)malloc(size);
    if (!buffer)
    {
        CloseHandle(hFile);
        return false;
    }

    DWORD totalRead = 0;
    bool ok = true;

    while (totalRead < size)
    {
        DWORD bytesRead = 0;
        DWORD toRead = size - totalRead;
        if (!ReadFile(hFile, buffer + totalRead, toRead, &bytesRead, NULL))
        {
            ok = false;
            break;
        }
        if (bytesRead == 0)
        {
            ok = false;
            break;
        }
        totalRead += bytesRead;
    }

    if (ok && totalRead == size)
    {
        outSha1 = CalcSHA1Hex(buffer, size);
    }
    else
    {
        ok = false;
    }

    free(buffer);
    CloseHandle(hFile);
    return ok;
}

struct ROM_DB_INFO_EX
{
    bool found;
    std::string title;
    std::string system;
    std::string company;
    std::string year;
    std::string status;
    std::string remark;
};

bool FindXMLAttributeValue(const std::string& text, const std::string& key, std::string& value)
{
    std::string pattern = key + "=\"";
    size_t pos = text.find(pattern);
    if (pos == std::string::npos)
        return false;

    pos += pattern.length();
    size_t end = text.find("\"", pos);
    if (end == std::string::npos)
        return false;

    value = DecodeXmlEntities(text.substr(pos, end - pos));
    return true;
}

bool LoadTextFileUTF8(const std::wstring& filePath, std::string& outText)
{
    outText.clear();

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        CloseHandle(hFile);
        return false;
    }

    if (fileSize.QuadPart <= 0 || fileSize.QuadPart > 0x7fffffff)
    {
        CloseHandle(hFile);
        return false;
    }

    DWORD size = (DWORD)fileSize.QuadPart;
    char* buffer = (char*)malloc(size + 1);
    if (!buffer)
    {
        CloseHandle(hFile);
        return false;
    }

    DWORD totalRead = 0;
    bool ok = true;

    while (totalRead < size)
    {
        DWORD bytesRead = 0;
        DWORD toRead = size - totalRead;
        if (!ReadFile(hFile, buffer + totalRead, toRead, &bytesRead, NULL))
        {
            ok = false;
            break;
        }
        if (bytesRead == 0)
        {
            ok = false;
            break;
        }
        totalRead += bytesRead;
    }

    CloseHandle(hFile);

    if (!ok || totalRead != size)
    {
        free(buffer);
        return false;
    }

    buffer[size] = '\0';
    outText.assign(buffer, size);
    free(buffer);
    return true;
}

bool FindROMInfoBySha1FromSoftwareDB(const std::wstring& xmlPath, const std::string& sha1, ROM_DB_INFO_EX* dbInfo)
{
    if (!dbInfo)
        return false;

    dbInfo->found = false;
    dbInfo->title.clear();
    dbInfo->system.clear();
    dbInfo->company.clear();
    dbInfo->year.clear();
    dbInfo->status.clear();
    dbInfo->remark.clear();

    std::string xml;
    if (!LoadTextFileUTF8(xmlPath, xml))
        return false;

    size_t searchPos = 0;
    while (true)
    {
        size_t softwareStart = xml.find("<software ", searchPos);
        if (softwareStart == std::string::npos)
            break;

        size_t softwareTagEnd = xml.find(">", softwareStart);
        if (softwareTagEnd == std::string::npos)
            break;

        size_t softwareEnd = xml.find("</software>", softwareTagEnd);
        if (softwareEnd == std::string::npos)
            break;

        std::string softwareTag = xml.substr(softwareStart, softwareTagEnd - softwareStart + 1);
        std::string softwareBody = xml.substr(softwareTagEnd + 1, softwareEnd - softwareTagEnd - 1);

        size_t romSearchPos = 0;
        while (true)
        {
            size_t romStart = softwareBody.find("<rom ", romSearchPos);
            if (romStart == std::string::npos)
                break;

            size_t romEnd = softwareBody.find("/>", romStart);
            if (romEnd == std::string::npos)
                break;

            std::string romTag = softwareBody.substr(romStart, romEnd - romStart + 2);

            std::string romSha1;
            if (FindXMLAttributeValue(romTag, "sha1", romSha1))
            {
                if (_stricmp(romSha1.c_str(), sha1.c_str()) == 0)
                {
                    dbInfo->found = true;
                    FindXMLAttributeValue(softwareTag, "title", dbInfo->title);
                    FindXMLAttributeValue(softwareTag, "system", dbInfo->system);
                    FindXMLAttributeValue(softwareTag, "company", dbInfo->company);
                    FindXMLAttributeValue(softwareTag, "year", dbInfo->year);
                    FindXMLAttributeValue(romTag, "status", dbInfo->status);
                    FindXMLAttributeValue(romTag, "remark", dbInfo->remark);
                    return true;
                }
            }

            romSearchPos = romEnd + 2;
        }

        searchPos = softwareEnd + 11;
    }

    return true;
}

bool FindROMInfoWithPriority(const std::string& sha1, ROM_DB_INFO_EX* dbInfo, std::wstring& usedXmlPath)
{
    if (!dbInfo)
        return false;

    usedXmlPath.clear();

    std::wstring softwareDbPath = L"softwaredb.xml";
    DWORD softwareDbAttr = GetFileAttributesW(softwareDbPath.c_str());
    bool softwareDbExists = (softwareDbAttr != INVALID_FILE_ATTRIBUTES) && !(softwareDbAttr & FILE_ATTRIBUTE_DIRECTORY);

    std::wstring msxRomDbPath = L"msxromdb.xml";
    DWORD msxRomDbAttr = GetFileAttributesW(msxRomDbPath.c_str());
    bool msxRomDbExists = (msxRomDbAttr != INVALID_FILE_ATTRIBUTES) && !(msxRomDbAttr & FILE_ATTRIBUTE_DIRECTORY);

    if (softwareDbExists)
    {
        usedXmlPath = softwareDbPath;
        return FindROMInfoBySha1FromSoftwareDB(softwareDbPath, sha1, dbInfo);
    }

    if (msxRomDbExists)
    {
        usedXmlPath = msxRomDbPath;

        ROM_DB_INFO oldInfo = {};
        if (!FindROMInfoBySha1(msxRomDbPath, sha1, &oldInfo))
            return false;

        dbInfo->found = oldInfo.found;
        dbInfo->title = oldInfo.title;
        dbInfo->system = oldInfo.system;
        dbInfo->company = oldInfo.company;
        dbInfo->year = oldInfo.year;
        dbInfo->status.clear();
        dbInfo->remark.clear();
        return true;
    }

    return true;
}

bool IsIgnorableTagValue(const std::wstring& value)
{
    if (value.empty())
        return true;
    if (!_wcsicmp(value.c_str(), L"unknown"))
        return true;
    if (!_wcsicmp(value.c_str(), L"n/a"))
        return true;
    if (!_wcsicmp(value.c_str(), L"none"))
        return true;
    if (value == L"-")
        return true;
    return false;
}

std::wstring BuildAutoFileName(const ROM_DB_INFO_EX& dbInfo)
{
    std::wstring titleW = SanitizeFileName(Utf8ToWide(dbInfo.title));
    std::wstring systemW = SanitizeFileName(Utf8ToWide(dbInfo.system));
    std::wstring companyW = SanitizeFileName(Utf8ToWide(dbInfo.company));
    std::wstring yearW = SanitizeFileName(Utf8ToWide(dbInfo.year));
    std::wstring statusW = SanitizeFileName(Utf8ToWide(dbInfo.status));
    std::wstring remarkW = SanitizeFileName(Utf8ToWide(dbInfo.remark));

    std::wstring renamedFile = titleW + L"(" + systemW + L")-" + companyW + L"(" + yearW + L")";

    if (!IsIgnorableTagValue(statusW))
        renamedFile += L"[" + statusW + L"]";

    if (!IsIgnorableTagValue(remarkW))
        renamedFile += L"[" + remarkW + L"]";

    renamedFile += L".rom";
    return renamedFile;
}

int ProcessROMRead(const wchar_t* outputFileArg, bool autoFileNameMode)
{
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_COMPORT, 0, 0,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (!hDevInfo)
    {
        printf("Failed to get device info\n");
        return 1;
    }

    // outputFileArg == NULL の場合は、自動ファイル名モードと同じ動作にする
    if (outputFileArg == NULL)
        autoFileNameMode = true;

    std::wstring requestedOutputPath = outputFileArg ? outputFileArg : L"";
    std::wstring outputDir;

    if (autoFileNameMode)
    {
        // 自動ファイル名モード時は outputFileArg を出力先ディレクトリとして扱う
        if (!requestedOutputPath.empty())
            outputDir = requestedOutputPath;
        else
            outputDir = L".";
    }
    else
    {
        // 通常モード時は outputFileArg を出力ファイル名として扱う
        if (!requestedOutputPath.empty())
            outputDir = GetDirectoryFromPath(requestedOutputPath);
        else
            outputDir = L".";
    }

    SP_DEVINFO_DATA data = { sizeof(data) };
    BOOL foundDevice = FALSE;

    for (int count = 0; SetupDiEnumDeviceInfo(hDevInfo, count, &data); count++)
    {
        HKEY key = SetupDiOpenDevRegKey(hDevInfo, &data, DICS_FLAG_GLOBAL, 0,
            DIREG_DEV, KEY_QUERY_VALUE);
        if (!key)
            continue;

        WCHAR devdesc[256], portname[256];
        DWORD size = sizeof(portname);

        if (!SetupDiGetDeviceRegistryProperty(hDevInfo, &data, SPDRP_HARDWAREID, NULL,
            (LPBYTE)devdesc, sizeof(devdesc), NULL))
        {
            RegCloseKey(key);
            continue;
        }

        if (wcsncmp(L"USB\\", devdesc, 4) != 0)
        {
            RegCloseKey(key);
            continue;
        }

        if (RegQueryValueExW(key, L"PortName", NULL, NULL, (LPBYTE)portname, &size) != ERROR_SUCCESS)
        {
            RegCloseKey(key);
            continue;
        }

        RegCloseKey(key);

        wprintf(L"Found USB COM port: %s\n\n", portname);

        wchar_t fullPortName[32];
        swprintf_s(fullPortName, sizeof(fullPortName) / sizeof(wchar_t), L"\\\\.\\%s", portname);

        HANDLE hSerial = CreateFileW(fullPortName, GENERIC_READ | GENERIC_WRITE, 0, 0,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        if (hSerial == INVALID_HANDLE_VALUE)
        {
            printf("Failed to open COM port\n");
            continue;
        }

        if (!ConfigureSerialPort(hSerial))
        {
            CloseHandle(hSerial);
            continue;
        }

        wprintf(L"Connected to %s\n\n", portname);

        if (!SlotCheck(hSerial))
        {
            printf("ERROR: Cartridge is not properly inserted\n\n");
            CloseHandle(hSerial);
            continue;
        }

        if (!SlotPowerOn(hSerial))
        {
            printf("ERROR: Failed to power on slot\n\n");
            CloseHandle(hSerial);
            continue;
        }
        if (!SlotReset(hSerial))
        {
            printf("ERROR: Failed to reset on slot\n\n");
            SlotPowerOff(hSerial);
            CloseHandle(hSerial);
            continue;
        }

        if (!CheckHashWithRetry(hSerial))
        {
            printf("ERROR: Failed to ROM Read :-( on slot\n\n");
            SlotPowerOff(hSerial);
            CloseHandle(hSerial);
            continue;
        }

        ROM_INFO romInfo = { MAPPER_UNKNOWN };
        if (!DetectMapper(hSerial, &romInfo))
        {
            printf("Mapper detection failed\n\n");
            SlotPowerOff(hSerial);
            CloseHandle(hSerial);
            continue;
        }

        if (!SlotReset(hSerial))
        {
            printf("ERROR: Failed to reset on slot\n\n");
            SlotPowerOff(hSerial);
            CloseHandle(hSerial);
            continue;
        }

        printf("\n========== DETECTION RESULT ==========\n");
        printf("Detected: %s\n", romInfo.mapperName);
        printf("Bank Count: %lu\n", romInfo.bankCount);
        printf("ROM Size: %lu bytes (0x%lX)\n", romInfo.romSize, romInfo.romSize);

        BYTE* romData = (BYTE*)malloc(romInfo.romSize);
        if (!romData)
        {
            printf("Memory allocation failed\n");
            SlotPowerOff(hSerial);
            CloseHandle(hSerial);
            continue;
        }

        if (!ReadCompleteROM(hSerial, &romInfo, romData))
        {
            printf("ROM reading failed\n");
            free(romData);
            SlotPowerOff(hSerial);
            CloseHandle(hSerial);
            continue;
        }

        std::string sha1 = CalcSHA1Hex(romData, romInfo.romSize);
        printf("\n========== SHA1 ==========\n");
        printf("%s\n", sha1.c_str());

        std::wstring savePath;
        bool decidedSavePath = false;

        ROM_DB_INFO_EX dbInfo = {};
        std::wstring usedXmlPath;
        bool xmlLoadOk = FindROMInfoWithPriority(sha1, &dbInfo, usedXmlPath);

        if (!usedXmlPath.empty())
        {
            if (xmlLoadOk)
            {
                if (dbInfo.found)
                {
                    printf("\n========== DB MATCH ==========\n");
                    printf("Title  : %s\n", dbInfo.title.c_str());
                    printf("System : %s\n", dbInfo.system.c_str());
                    printf("Company: %s\n", dbInfo.company.c_str());
                    printf("Year   : %s\n", dbInfo.year.c_str());

                    if (!dbInfo.status.empty())
                        printf("Status : %s\n", dbInfo.status.c_str());
                    if (!dbInfo.remark.empty())
                        printf("Remark : %s\n", dbInfo.remark.c_str());

                    std::wstring renamedFile = BuildAutoFileName(dbInfo);
                    savePath = JoinPath(outputDir, renamedFile);
                    decidedSavePath = true;
                }
                else
                {
                    wprintf(L"\n========== DB MATCH ==========\n");
                    wprintf(L"No match found in %s\n", usedXmlPath.c_str());

                    if (!autoFileNameMode && !requestedOutputPath.empty())
                    {
                        printf("Saving with specified output file name.\n");

                        savePath = requestedOutputPath;
                        decidedSavePath = true;
                    }
                    else
                    {
                        printf("Saving with auto-generated file name.\n");

                        std::wstring mapperW = SanitizeMapperNameForFileName(romInfo.mapperName);
                        std::wstring sha1W = Utf8ToWide(sha1);
                        std::wstring renamedFile = L"Unknown_" + sha1W + L"[" + mapperW + L"].rom";
                        savePath = JoinPath(outputDir, renamedFile);
                        decidedSavePath = true;
                    }
                }
            }
            else
            {
                wprintf(L"\nFailed to load XML database: %s\n", usedXmlPath.c_str());

                if (!autoFileNameMode && !requestedOutputPath.empty())
                {
                    printf("Saving with specified output file name.\n");

                    savePath = requestedOutputPath;
                    decidedSavePath = true;
                }
                else
                {
                    printf("Saving with auto-generated file name.\n");

                    std::wstring mapperW = SanitizeMapperNameForFileName(romInfo.mapperName);
                    std::wstring sha1W = Utf8ToWide(sha1);
                    std::wstring renamedFile = L"Unknown_" + sha1W + L"[" + mapperW + L"].rom";
                    savePath = JoinPath(outputDir, renamedFile);
                    decidedSavePath = true;
                }
            }
        }
        else
        {
            wprintf(L"\nXML database not found: softwaredb.xml / msxromdb.xml\n");

            if (!autoFileNameMode && !requestedOutputPath.empty())
            {
                printf("Saving with specified output file name.\n");

                savePath = requestedOutputPath;
                decidedSavePath = true;
            }
            else
            {
                printf("Saving with auto-generated file name.\n");

                std::wstring mapperW = SanitizeMapperNameForFileName(romInfo.mapperName);
                std::wstring sha1W = Utf8ToWide(sha1);
                std::wstring renamedFile = L"Unknown_" + sha1W + L"[" + mapperW + L"].rom";
                savePath = JoinPath(outputDir, renamedFile);
                decidedSavePath = true;
            }
        }

        if (!decidedSavePath || savePath.empty())
        {
            printf("Failed to determine output file name.\n");
            free(romData);
            SlotPowerOff(hSerial);
            CloseHandle(hSerial);
            continue;
        }

        std::wstring finalDir = GetDirectoryFromPath(savePath);
        std::wstring finalName = GetFileNameFromPath(savePath);

        if (!IsSuccessfulROMImage(romData, romInfo.romSize))
        {
            finalName = L"[unsuccessful]" + finalName;
        }

        std::wstring finalOutputPath = JoinPath(finalDir, finalName);

        if (FileExists(finalOutputPath.c_str()))
        {
            std::string existingSha1;
            if (CalcFileSHA1Hex(finalOutputPath.c_str(), existingSha1))
            {
                printf("\n========== EXISTING FILE SHA1 ==========\n");
                printf("%s\n", existingSha1.c_str());

                if (existingSha1 == sha1)
                {
                    finalName = L"[same]" + finalName;
                }
                else
                {
                    std::wstring sha1W = Utf8ToWide(sha1);
                    finalName = L"[other_" + sha1W + L"]" + finalName;
                }
            }
            else
            {
                printf("\n========== EXISTING FILE SHA1 ==========\n");
                printf("Failed to calculate SHA1 of existing file\n");

                finalName = L"[same]" + finalName;
            }

            finalOutputPath = JoinPath(finalDir, finalName);
        }

        if (!SaveROMToFile(finalOutputPath.c_str(), romData, romInfo.romSize))
        {
            printf("[ERROR] File save failed\n");
            free(romData);
            SlotPowerOff(hSerial);
            CloseHandle(hSerial);
            continue;
        }

        wprintf(L"\nSaved output: %s\n", finalOutputPath.c_str());
        printf("\nROM read and save completed successfully!\n\n");

        SlotPowerOff(hSerial);

        free(romData);
        CloseHandle(hSerial);
        foundDevice = TRUE;
        break;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    if (!foundDevice)
    {
        printf("No suitable device found\n");
        return 1;
    }

    return 0;
}
// ============================================================================
// Entry Point
// ============================================================================

int wmain(int argc, wchar_t* argv[])
{
    printf("MSX Game Adapter ROM Dumper\n");
    printf("Copyright @v9938\n");
    printf("Build: %s %s\n", __DATE__, __TIME__);
    printf("\n");

    bool autoFileNameMode = false;
    const wchar_t* outputFileArg = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (_wcsicmp(argv[i], L"/auto") == 0)
        {
            autoFileNameMode = true;
        }
        else
        {
            outputFileArg = argv[i];
        }
    }

    // 通常モード時は出力ファイル名必須
    // /auto 時は引数省略ならカレントディレクトリを使う
    if (!autoFileNameMode && outputFileArg == NULL)
    {
        wprintf(L"Usage: %s <output_file_path> [/auto]\n", argv[0]);
        wprintf(L"\n");
        wprintf(L"Normal mode:\n");
        wprintf(L"  %s <output_file_path>\n", argv[0]);
        wprintf(L"    Save ROM using the specified output file path.\n");
        wprintf(L"\n");
        wprintf(L"Auto file name mode:\n");
        wprintf(L"  %s /auto [output_directory]\n", argv[0]);
        wprintf(L"    Save ROM using an automatically generated file name.\n");
        wprintf(L"    If [output_directory] is omitted, the current directory is used.\n");
        wprintf(L"\n");
        wprintf(L"Notes:\n");
        wprintf(L"  softwaredb.xml is used if present.\n");
        wprintf(L"  If softwaredb.xml is not present, msxromdb.xml is used.\n");
        return 1;
    }

    int result = ProcessROMRead(outputFileArg, autoFileNameMode);

    printf("Done.\n");
    return result;
}
