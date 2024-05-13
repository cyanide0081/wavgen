#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <time.h>
#include <errno.h>

typedef struct WavHeader {
    char chunkID[4];
    int32_t chunkSize;
    char format[4];
    char subChunk1ID[4];
    int32_t subChunk1Size;
    int16_t audioFormat;
    int16_t numChannels;
    int32_t sampleRate;
    int32_t byteRate;
    int16_t blockAlign;
    int16_t bitsPerSample;
    char subChunk2ID[4];
    int32_t subChunk2Size;
} WavHeader;

typedef enum WaveType {
    WAVE_SINE,
    WAVE_TRIANGLE,
    WAVE_SQUARE,
    WAVE_SAW,
    WAVE_EVEN
} WaveType;

typedef enum SampleFormat {
    FMT_INT_PCM = 1,
    FMT_FLOAT_PCM = 3
} SampleFormat;

typedef struct Parameters {
    double *freqs;
    size_t freqCount;
    double durationSecs;
    double amplitude;
    uint32_t sampleRate;
    uint32_t bitsPerSample;
    SampleFormat sampleFormat;
    WaveType waveType;
    bool applyDither;
    char *outputFile;
} Parameters;

typedef struct AudioBuffer {
    void *buf;
    size_t sampleCount;
    size_t bytesPerSample;
} AudioBuffer;

typedef struct WaveChunk {
    double *buf;
    size_t sampleCount;
} WaveChunk;

typedef enum LogState {
    LOG_INIT,
    LOG_INFO,
    ERR_READ,
    ERR_PARSE,
    ERR_ARG,
    ERR_FATAL,
    LOG_EXIT
} LogState;

void loggerInit(const char *file);
void loggerClose(int32_t code);
void loggerAppend(LogState state, const char *restrict fmt, ...);
WavHeader wavHeaderBuild(const Parameters *params);
Parameters parametersParse(const char *file);
void parametersDestroy(Parameters *p);
WaveChunk waveChunkGenerate(const Parameters *p);
AudioBuffer audioBufferBuild(const Parameters *p);
void audioBufferDestroy(AudioBuffer *b);

#define LOG_FILE_NAME "log.txt"
#define STATIC_ASSERT(condition) ((void)sizeof(char[1 - 2 * !(condition)]))

int main(void)
{
    STATIC_ASSERT(sizeof(WavHeader) == 44); // header must be 44 bytes long
    loggerInit(LOG_FILE_NAME);

    Parameters p = parametersParse("config.cfg");
    const WavHeader header = wavHeaderBuild(&p);
    AudioBuffer buf = audioBufferBuild(&p);

    fclose(fopen(p.outputFile, "w")); // clear file's contents if it exists
    FILE *f = fopen(p.outputFile, "ab"); // open it in append-binary mode
    if (f == NULL) {
        loggerAppend(ERR_FATAL, "unable to open file '%s' for writing: %s",
            p.outputFile, strerror(errno));
        loggerClose(errno);
        exit(EXIT_FAILURE);
    }

    loggerAppend(LOG_INFO, "writing wave to file on disk: '%s'", p.outputFile);
    fwrite(&header, sizeof(header), 1, f);
    double chunks = p.sampleRate * p.durationSecs / buf.sampleCount;
    for (size_t i = 0; i < (size_t)chunks; i++) {
        fwrite(buf.buf, buf.bytesPerSample, buf.sampleCount, f);
    }

    double trailingChunk = chunks - (size_t)chunks;
    if (trailingChunk > 0.0) {
        fwrite(buf.buf, buf.bytesPerSample, buf.sampleCount * trailingChunk, f);
    }

    fclose(f);
    loggerClose(0);
    audioBufferDestroy(&buf);
    parametersDestroy(&p);

    return 0;
}

#define KB 1024

char *readFileContents(const char *restrict file, FILE *f);

static FILE *logFile = NULL;

void loggerInit(const char *file)
{
    if (logFile != NULL) return;

    logFile = fopen(file, "w+");
    if (logFile == NULL) {
        fprintf(stderr,
            "FATAL: unable to open logging file '%s' for writing: %s\n",
            file, strerror(errno));
        exit(EXIT_FAILURE);
    }

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char localTime[KB];
    size_t ret = strftime(localTime, sizeof(localTime), "%F @ %T", tm);
    if (ret == 0) {
        strncpy(localTime, "unable to retrieve local date and time", KB);
    }

    loggerAppend(LOG_INIT, "WAVE generator initialized (%s)", localTime);
}

void loggerClose(int32_t code)
{
    char *text = readFileContents(LOG_FILE_NAME, logFile);
    const char *status = code ? "abnormally" : "normally";
    loggerAppend(LOG_EXIT,
        "generator terminated %s with exit code %d", status, code);
    fclose(logFile);
    if (text == NULL) {
        remove(LOG_FILE_NAME);
        return;
    }

    free(text);
}

void loggerAppend(LogState state, const char *restrict fmt, ...)
{
    const char *logState = NULL;
    switch (state) {
    case LOG_INIT: {
        logState = "INIT";
    } break;
    case LOG_INFO: {
        logState = "INFO";
    } break;
    case ERR_READ: {
        logState = "READ";
    } break;
    case ERR_PARSE: {
        logState = "PARSE";
    } break;
    case ERR_ARG: {
        logState = "ARG";
    } break;
    case ERR_FATAL: {
        logState = "FATAL";
    } break;
    case LOG_EXIT: {
        logState = "EXIT";
    } break;
    }

    char format[2 * KB] = {0};
    snprintf(format, sizeof(format), "[%s]: %s\n", logState, fmt);
    fmt = format;
    va_list args;
    va_start(args, fmt);
    if (vfprintf(logFile, fmt, args) <= 0) {
        fprintf(stderr,
            "unable to write log message to file: %s\n", strerror(errno));
    } 
    
    va_end(args);
    va_start(args, fmt);
    if (vprintf(fmt, args) <= 0) {
        fprintf(stderr,
            "unable to write log message to stdout: %s\n", strerror(errno));
    }
    
    va_end(args);
}

WavHeader wavHeaderBuild(const Parameters *p)
{
    WavHeader h = {
        .chunkID = "RIFF",
        .format = "WAVE",
        .subChunk1ID = "fmt ",
        .subChunk1Size = 16,
        .audioFormat = p->sampleFormat,
        .numChannels = 1,
        .sampleRate = p->sampleRate,
        .bitsPerSample = p->bitsPerSample,
        .subChunk2ID = "data",
    };

    h.blockAlign = h.numChannels * h.bitsPerSample / 8;
    h.byteRate = h.sampleRate * h.blockAlign;
    h.subChunk2Size = p->sampleRate * p->durationSecs * h.blockAlign;
    h.chunkSize = 36 + h.subChunk2Size;

    return h;
}

#define PI 3.14159265358979323846
#define LINE_DELIMS "\r\n"
#define MAX_AMP_DB 6.0
#define OUT_FILE_NAME "file.wav"

#define ERR_OUT_OF_MEMORY() loggerAppend(ERR_FATAL, \
    "failed to allocate more memory: %s\n", strerror(errno))

#if defined _MSC_VER
#define strtok_r strtok_s
#define strdup _strdup
#define NAME_MAX FILENAME_MAX
#endif

typedef enum ConfigLine {
    LINE_TONE_FREQUENCIES,
    LINE_WAVE_TYPE,
    LINE_DURATION_SECONDS,
    LINE_AMPLITUDE,
    LINE_SAMPLE_RATE,
    LINE_BITS_PER_SAMPLE,
    LINE_SAMPLE_FORMAT,
    LINE_APPLY_DITHER,
    LINE_OUTPUT_FILE,
    LINE_COUNT
} ConfigLine;

double parseDouble(const char *line);
double *parseFreqList(char *line, size_t *listLen);
uint32_t parseUnsignedInt(const char *line);
WaveType parseWaveType(char *restrict line);
SampleFormat parseSampleFormat(char *restrict line);
bool parseBool(const char *line);
void stripWhiteSpace(char *restrict string);
void stripDoubleQuotes(char *restrict string);
const char *waveTypeToString(WaveType type);
const char *sampleFormatToString(SampleFormat fmt);
void logWaveProperties(Parameters *p);

Parameters parametersParse(const char *file)
{
    Parameters params = { // default values
        .freqs = malloc(sizeof(*params.freqs)),
        .freqCount = 1,
        .waveType = WAVE_SINE,
        .durationSecs = 4.0,
        .amplitude = -1.0,
        .sampleRate = 48000,
        .bitsPerSample = 24,
        .sampleFormat = FMT_INT_PCM,
        .applyDither = true,
        .outputFile = strdup(OUT_FILE_NAME)
    };

    if (params.freqs == NULL) {
        ERR_OUT_OF_MEMORY();
        exit(EXIT_FAILURE);
    }

    *params.freqs = 440.0;

    FILE *f = fopen(file, "r");
    if (f == NULL) {
        loggerAppend(ERR_READ, "unable to read config file '%s': %s",
            file, strerror(errno));
        logWaveProperties(&params);
        return params;
    }

    char *fileBuf = readFileContents(file, f);
    fclose(f);
    if (fileBuf == NULL) return params;

    char *lines[LINE_COUNT] = {0};
    char *parserState = NULL;
    char *tok = strtok_r(fileBuf, LINE_DELIMS, &parserState);
    for (int i = 0; i < LINE_COUNT; i++) {
        lines[i] = tok;
        tok = strtok_r(NULL, LINE_DELIMS, &parserState);
    }

    for (size_t i = 0; i < LINE_COUNT; i++) {
        char *line = strtok(lines[i], ";");
        bool lineOk = false;
        while (*line != 0) {
            if (*line == '=') {
                lineOk = true;
                line += 1;
                break;
            }

            line += 1;
        }

        if (!lineOk) {
            loggerAppend(ERR_PARSE,
                "'%s': unable to parse line %zu: incorrect formatting",
                file, i + 1);
            continue;
        }

        stripWhiteSpace(line);
        switch (i) {
        case LINE_TONE_FREQUENCIES: {
            size_t listLen = 0;
            double *freqs = parseFreqList(line, &listLen);
            if (freqs != NULL)  {
                free(params.freqs);
                params.freqs = freqs, params.freqCount = listLen;
            }
        } break;
        case LINE_WAVE_TYPE: {
            int32_t waveType = parseWaveType(line);
            if (errno == 0) params.waveType = waveType;
        } break;
        case LINE_DURATION_SECONDS: {
            double durationSecs = parseDouble(line);
            if (errno == 0) params.durationSecs = durationSecs;
        } break;
        case LINE_AMPLITUDE: {
            double amplitude = parseDouble(line);
            if (errno == 0) {
                params.amplitude =
                    amplitude < MAX_AMP_DB ? amplitude : MAX_AMP_DB;
            }
        } break;
        case LINE_SAMPLE_RATE: {
            uint32_t sampleRate = parseUnsignedInt(line);
            if (errno == 0) {
                double highestFreq = 0.0;
                for (size_t i = 0; i < params.freqCount; i++) {
                    if (params.freqs[i] > highestFreq) {
                        highestFreq = params.freqs[i];
                    }
                }

                size_t nyquistLimit = (size_t)(highestFreq * 2);
                if (sampleRate <= nyquistLimit) {
                    loggerAppend(ERR_ARG,
                        "sample rate must be at least > %zuHz (ignoring)",
                        nyquistLimit);
                    break;
                }
                params.sampleRate = sampleRate;
            }
        } break;
        case LINE_BITS_PER_SAMPLE: {
            uint32_t bitsPerSample = parseUnsignedInt(line);
            if (errno == 0) params.bitsPerSample = bitsPerSample;
        } break;
        case LINE_SAMPLE_FORMAT: {
            int32_t sampleFormat = parseSampleFormat(line);
            if (errno == 0) {
                bool fmtOk = true;
                uint32_t b = params.bitsPerSample;
                switch (sampleFormat) {
                case FMT_INT_PCM: {
                    if (b != 8 && b != 16 && b != 24 && b != 32) {
                        loggerAppend(ERR_ARG,
                            "%u-bit integer PCM is invalid/unsupported"
                            " (using default value)", b);
                        params.bitsPerSample = 32;
                        fmtOk = false;
                    }
                } break;
                case FMT_FLOAT_PCM: {
                    if (b != 32 && b != 64) {
                        loggerAppend(ERR_ARG,
                            "%u-bit floating-point PCM is invalid/unsupported"
                            " (using default value)", b);
                        params.bitsPerSample = 32;
                        fmtOk = false;
                    }
                } break;
                default: {
                    loggerAppend(LOG_INFO,
                        "(defaulting sample format to 24-bit int)", line);
                    params.bitsPerSample = 24;
                    params.sampleFormat = FMT_INT_PCM;
                    fmtOk = false;
                } break;
                }

                if (fmtOk) params.sampleFormat = sampleFormat;
            }
        } break;
        case LINE_APPLY_DITHER: {
            bool applyDither = parseBool(line);
            if (errno == 0) params.applyDither = applyDither;
        } break;
        case LINE_OUTPUT_FILE: {
            stripDoubleQuotes(line);
            int len = snprintf(NULL, 0, "%s.wav", line);
            char *fileName = malloc(len * sizeof(*fileName));
            if (fileName == NULL) {
                ERR_OUT_OF_MEMORY();
                exit(EXIT_FAILURE);
            }

            sprintf(fileName, "%s.wav", line);
            if (strlen(fileName) >= NAME_MAX) {
                loggerAppend(ERR_ARG,
                    "filename is longer than %d bytes"
                    " (using default value)", NAME_MAX);
                free(fileName);
            } else {
                free(params.outputFile);
                params.outputFile = fileName;
            }
        } break;
        }
    }

    if (fileBuf != NULL) free(fileBuf);

    logWaveProperties(&params);
    return params;
}

void logWaveProperties(Parameters *p)
{
    char toneList[4 * KB] = {0};
    for (size_t i = 0; i < p->freqCount && p->freqs; i++) {
        char num[32] = {0};
        snprintf(num, sizeof(num), "%.1lfHz, ", p->freqs[i]);
        strncat(toneList, num, sizeof(toneList) - 1);
    }

    toneList[strlen(toneList) - 2] = '\0';

    const char *type = waveTypeToString(p->waveType);
    const char *sampleFmt = sampleFormatToString(p->sampleFormat);
    const char *dither = p->applyDither ? "Yes" : "No";
    if (p->sampleFormat == FMT_FLOAT_PCM) dither = "(ignored)";

    const double mb = (double)(p->sampleRate * p->durationSecs *
        (p->bitsPerSample / 8.0) + sizeof(WavHeader)) / KB;

    loggerAppend(LOG_INFO,
        "generating %zu %s wave(s):", p->freqCount, type);
    loggerAppend(LOG_INFO, "* Frequencies:   %s", toneList);
    loggerAppend(LOG_INFO, "* Length:        %.2lfs (%.2lfKB)",
        p->durationSecs, mb);
    loggerAppend(LOG_INFO, "* Sample Peak:   %+.2lfdBFS", p->amplitude);
    loggerAppend(LOG_INFO, "* Sample Rate:   %uHz", p->sampleRate);
    loggerAppend(LOG_INFO, "* Sample Format: %s", sampleFmt);
    loggerAppend(LOG_INFO, "* Bit Depth:     %u-bit", p->bitsPerSample);
    loggerAppend(LOG_INFO, "* Dither:        %s", dither);
    loggerAppend(LOG_INFO, "* Output File:   '%s'", p->outputFile);
}

double parseDouble(const char *line)
{
    errno = 0;
    double n = strtod(line, NULL);
    if (n == 0.0 && errno) {
        loggerAppend(ERR_PARSE,
            "unable to parse a floating-point number from '%s'", line);
    }

    return n;
}

#define INIT_DOUBLE_LIST_CAP 8
#define DOUBLE_LIST_DELIMS ", "

double *parseFreqList(char *line, size_t *listLen)
{
    size_t len = INIT_DOUBLE_LIST_CAP;
    double *list = calloc(len, sizeof(*list));
    if (list == NULL) {
        ERR_OUT_OF_MEMORY();
        exit(EXIT_FAILURE);
    }

    char *parserState = NULL;
    char *tok = strtok_r(line, DOUBLE_LIST_DELIMS, &parserState);
    size_t i;
    for (i = 0; tok; i++) {
        if (i >= len) {
            size_t newLen = len * 2;
            double *newList = realloc(list, newLen * sizeof(*newList));
            if (!newList) {
                ERR_OUT_OF_MEMORY();
                exit(EXIT_FAILURE);
            }

            list = newList;
            len = newLen;
        }

        double num = parseDouble(tok);
        if (num > 0.0) {
            list[i] = num;
        } else {
            loggerAppend(ERR_ARG, "found illegal tone: every tone"
                " must be a positive number > 0.0Hz (ignoring)");
            i -= 1;
        }

        tok = strtok_r(NULL, DOUBLE_LIST_DELIMS, &parserState);
    }

    len = i;
    if (len == 0) {
        free(list);
        return NULL;
    }

    double *trimmedList = realloc(list, len * sizeof(*list));
    if (trimmedList == NULL) {
        ERR_OUT_OF_MEMORY();
        exit(EXIT_FAILURE);
    }

    list = trimmedList;
    *listLen = len;
    return list;
}

uint32_t parseUnsignedInt(const char *line)
{
    errno = 0;
    unsigned long n = strtoul(line, NULL, 10);
    if ((n == 0 || n == ULONG_MAX)) {
        loggerAppend(ERR_PARSE,
            "unable to parse an unsigned number from '%s'", line);
    }

    return (uint32_t)n;
}

WaveType parseWaveType(char *restrict line)
{
    stripDoubleQuotes(line);
    if (strcmp(line, "sine") == 0) return WAVE_SINE;
    if (strcmp(line, "triangle") == 0) return WAVE_TRIANGLE;
    if (strcmp(line, "square") == 0) return WAVE_SQUARE;
    if (strcmp(line, "saw") == 0) return WAVE_SAW;
    if (strcmp(line, "even") == 0) return WAVE_EVEN;

    loggerAppend(ERR_PARSE, "unrecognized wave type: '%s'", line);
    return -1;
}

SampleFormat parseSampleFormat(char *restrict line)
{
    stripDoubleQuotes(line);
    if (strcmp(line, "int") == 0) return FMT_INT_PCM;
    if (strcmp(line, "float") == 0) return FMT_FLOAT_PCM;

    loggerAppend(ERR_PARSE, "unrecognized sample format: '%s'", line);
    return -1;
}

bool parseBool(const char *line)
{
    if (strcmp(line, "true") == 0) {
        return true;
    } else if (strcmp(line, "false") == 0) {
        return false;
    }

    errno = EINVAL;
    loggerAppend(ERR_PARSE,
        "unable to parse a boolean value from '%s'", line);
    return false;
}

void parametersDestroy(Parameters *p)
{
    if (p->freqs != NULL) free(p->freqs);
    if (p->outputFile != NULL) free(p->outputFile);
    memset(p, 0, sizeof(*p));
}

void stripWhiteSpace(char *restrict string)
{
    if (*string == '\0') return;

    size_t length = strlen(string);
    char *start = string, *end = string + length;
    while (isspace(*start)) start += 1;

    if (end > start) {
        while (isspace(*--end)) *end = '\0';
    }
    if (start != string) {
        memmove(string, start, end - start + 1);
        memset(end, '\0', start - string);
    }
}

void stripDoubleQuotes(char *restrict string)
{
    if (*string == '\0') return;

    size_t length = strlen(string);
    char *start = string, *end = string + length;
    while (*start == '"') start += 1;

    if (end > start) {
        while (*--end == '"') *end = '\0';
    }
    if (start != string) {
        memmove(string, start, end - start + 1);
        memset(end, '\0', start - string);
    }
}

char *readFileContents(const char *restrict file, FILE *f)
{
    int err = fseek(f, 0, SEEK_END);
    if (err != 0) {
        loggerAppend(ERR_READ, "unable to seek EOF for '%s': %s",
            file, strerror(errno));
        return NULL;
    }

    int len = (int)ftell(f) + 1; // for NUL terminator
    if (len == 0) {
        return NULL;
    } else if (len == -1) {
        loggerAppend(ERR_READ, "unable to get EOF position of '%s': %s",
            file, strerror(errno));
        return NULL;
    }

    rewind(f);
    char *fileBuf = calloc(len, sizeof(*fileBuf));
    if (fileBuf == NULL) {
        ERR_OUT_OF_MEMORY();
        exit(EXIT_FAILURE);
    }

    fread(fileBuf, sizeof(*fileBuf), len, f);

    return fileBuf;
}

const char *waveTypeToString(WaveType type)
{
    switch (type) {
    case WAVE_SINE:
        return "sine";
    case WAVE_TRIANGLE:
        return "triangle";
    case WAVE_SQUARE:
        return "square";
    case WAVE_SAW:
        return "saw";
    case WAVE_EVEN:
        return "even";
    }

    return NULL;
}

const char *sampleFormatToString(SampleFormat fmt)
{
    return fmt == FMT_INT_PCM ? "Integer" : "Floating-point";
}

void addWave(double *buf, size_t len, int32_t type, double freq, int32_t rate);
double gainToDecibels(double gain);
double decibelsToGain(double decibels);
bool machineIsBigEndian(void);
void convertToLittleEndian(void *buf, size_t len, size_t bits);

WaveChunk waveChunkGenerate(const Parameters *p)
{
    double lowestFreq = p->freqs[0];
    for (size_t i = 1; i < p->freqCount; i++) {
        if (p->freqs[i] < lowestFreq) lowestFreq = p->freqs[i];        
    }

    double maxSamples = p->durationSecs * p->sampleRate;
    double baseSampleCount = 1.0 / lowestFreq * p->sampleRate;
    double sampleCount = baseSampleCount;
    /* making sure we don't get a chunk with an fractional amount of samples */
    while (sampleCount != (size_t)sampleCount && sampleCount < maxSamples) {
        sampleCount += baseSampleCount;
    }

    /* making sure we get at least one second worth of dithered samples */
    if (p->applyDither) {
        while (sampleCount < p->sampleRate) sampleCount += baseSampleCount;
    }

#ifndef NDEBUG
    printf("sampleCount: %lf (%.2lfKB)\n", sampleCount, sampleCount / KB);
    printf("minfreq: %lf, secs: %lf\n", lowestFreq, 1.0 / lowestFreq);
#endif    

    double *buf = calloc(sampleCount, sizeof(*buf));
    if (buf == NULL) {
        ERR_OUT_OF_MEMORY();
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < p->freqCount; i++) {
        addWave(buf, sampleCount, p->waveType, p->freqs[i], p->sampleRate);
    }

    double posPeak = buf[0], negPeak = posPeak;
    for (size_t i = 1; i < sampleCount; i++) {
        if (buf[i] > posPeak) posPeak = buf[i];
        else if (buf[i] < negPeak) negPeak = buf[i];
    }

    double absPeak = posPeak > -negPeak ? posPeak : -negPeak;
    absPeak /= decibelsToGain(p->amplitude);
    if (absPeak != 1.0) {
        for (size_t i = 0; i < sampleCount; i++) {
            buf[i] /= absPeak;
        }
    }

    // NOTE: moved this to buffer building step
    // if (machineIsBigEndian()) convertToLittleEndian(w.buf, w.sampleCount);

    return (WaveChunk){
        .buf = buf,
        .sampleCount = sampleCount,    
    };
}

#define BELOW_NYQUIST(freq, rate) (freq < rate / 2.0)
#define SINE_WAVE(freq, factor, rate, i) \
    (double)(sin((2.0 * PI * freq * factor) / rate * i))

void addWave(double *buf, size_t len, int32_t type, double freq, int32_t rate)
{
    double factor = 1.0, amp = 1.0;
    switch (type) {
    case WAVE_SINE: {
        for (size_t i = 0; i < len; i++) {
            buf[i] = SINE_WAVE(freq, factor, rate, i);
        }
    } break;
    case WAVE_TRIANGLE: {
        double phase = -1.0;
        while (BELOW_NYQUIST(freq * factor, rate)) {
            phase *= -1.0;
            amp = 1.0 / (factor * factor);
            for (size_t i = 0; i < len; i++) {
                buf[i] += SINE_WAVE(freq, factor, rate, i) * amp * phase;
            }

            factor += 2.0;
        }
    } break;
    case WAVE_SQUARE: {
        while (BELOW_NYQUIST(freq * factor, rate)) {
            amp = 4.0 / (factor * PI);
            for (size_t i = 0; i < len; i++) {
                buf[i] += SINE_WAVE(freq, factor, rate, i) * amp;
            }

            factor += 2.0;
        }
    } break;
    case WAVE_SAW: {
        while (BELOW_NYQUIST(freq * factor, rate)) {
            amp = 1.0 / factor;
            for (size_t i = 0; i < len; i++) {
                buf[i] += SINE_WAVE(freq, factor, rate, i) * amp;
            }

            factor += 1.0;
        }
    } break;
    case WAVE_EVEN: {
        while (BELOW_NYQUIST(freq * factor, rate)) {
            amp = 1.0 / factor;
            for (size_t i = 0; i < len; i++) {
                buf[i] += SINE_WAVE(freq, factor, rate, i) * amp;
            }

            if (factor == 1.0) factor = 0.0;

            factor += 2.0;
        }
    } break;
    }
}

void applyDither(double *buf, size_t len, size_t bits);

AudioBuffer audioBufferBuild(const Parameters *p)
{
    loggerAppend(LOG_INFO, "generating base wave(s)...");
    WaveChunk w = waveChunkGenerate(p);
    double *src = w.buf;
    size_t len = w.sampleCount;
    size_t bits = p->bitsPerSample;
    size_t bytes = bits / 8;

    if (p->sampleFormat == FMT_INT_PCM && p->applyDither) {
        loggerAppend(LOG_INFO, "applying %zu-bit dither...", bits);
        applyDither(src, len, bits);
    }

    AudioBuffer b = {
        .sampleCount = len,
        .bytesPerSample = bytes,
        .buf = (bits == 64) ? src : malloc(len * bytes),
    };

    if (b.buf == NULL) {
        ERR_OUT_OF_MEMORY();
        exit(EXIT_FAILURE);
    }
    
    void *buf = b.buf;
    switch (p->sampleFormat) {
    case FMT_INT_PCM: {
        loggerAppend(LOG_INFO, "truncating to %zu-bit integer", bits);
        size_t maxInt = (size_t)((pow(2.0, bits - 1.0) - 1.0));
        switch (bits) {
        case 8: {
            const uint8_t offset = INT8_MAX + 1;
            for (size_t i = 0; i < len; i++) {
                ((uint8_t*)buf)[i] = (uint8_t)lround(src[i] * maxInt + offset);
            }
        } break;
        case 16: {
            for (size_t i = 0; i < len; i++) {
                ((int16_t*)buf)[i] = (int16_t)lround(src[i] * maxInt);
            }
        } break;
        case 24: {
            enum { word = 3 * sizeof(int8_t) };
            for (size_t i = 0; i < len; i++) {
                int32_t val = (int32_t)lround(src[i] * maxInt);
                for (size_t j = 0, offset = 0; j < word; j++, offset += 8) {
                    ((int8_t*)buf)[i * word + j] = (int8_t)(val >> offset);
                }
            }
        } break;
        case 32: {
            for (size_t i = 0; i < len; i++) {
                ((int32_t*)buf)[i] = (int32_t)lround(src[i] * maxInt);
            }
        } break;
        }
    } break;
    case FMT_FLOAT_PCM: {
        switch (bits) {
        case 32: {
            for (size_t i = 0; i < len; i++) {
                ((float*)buf)[i] = (float)(src[i]);
            }
        } break;
        }
    } break;
    }

    if (bits != 64) free(src);
    if (machineIsBigEndian()) convertToLittleEndian(buf, len, bits);

    return b;
}

void audioBufferDestroy(AudioBuffer *b)
{
    free(b->buf);
    memset(b, 0, sizeof(*b));
}

void applyDither(double *buf, size_t len, size_t bits)
{
    const double amp = 1.0 / pow(2.0, bits - 1.0) / RAND_MAX;
    for (size_t i = 0; i < len; i++) {
        buf[i] += (double)(rand() - rand()) * amp;
    }
}

#define MINUS_INF_DB -150.0
#define MAX(a, b) (a > b ? a : b)

double gainToDecibels(double gain)
{
    return gain > 0.0 ? MAX(MINUS_INF_DB, log10(gain) * 20.0) : MINUS_INF_DB;
}

double decibelsToGain(double decibels)
{
    return decibels > MINUS_INF_DB ? pow(10.0, decibels * 0.05) : 0.0;
}

bool machineIsBigEndian(void)
{
    static const uint32_t n = 0x1;
    return *((char*)&n) == 0;
}

void convertToLittleEndian(void *buf, size_t len, size_t bits)
{
    size_t size = bits / 8;
    union {
        double n;
        uint8_t *b;
    } src, res;
    res.n = 0.0;

    for (size_t i = 0; i < len; i += size) {
        src.n = ((uint8_t*)buf)[i];
        for (size_t j = 0; j < size; j++) {
            res.b[j] = src.b[size - 1 - j];
        }

        ((uint8_t*)buf)[i] = (uint8_t)res.n;
    }
}
