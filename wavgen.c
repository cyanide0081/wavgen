#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <assert.h>

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
    WaveType waveType;
    double durationSecs;
    double amplitude;
    uint32_t sampleRate;
    uint32_t bitsPerSample;
    SampleFormat sampleFormat;
    bool applyDither;
    char *outputFile;
} Parameters;

typedef struct AudioBuffer {
    void *data;
    size_t sampleCount;
    size_t bytesPerSample;
} AudioBuffer;

typedef enum LogState {
    LOG_INIT,
    LOG_OK,
    ERR_READ,
    ERR_PARSE,
    ERR_ARG,
    ERR_FATAL,
    LOG_EXIT
} LogState;

void loggerInit(const char *file);
void loggerClose(int32_t code);
void loggerAppend(LogState state, const char *restrict fmt, ...);
WavHeader buildWavHeader(const Parameters *params);
Parameters parseParameters(const char *file);
void destroyParameters(Parameters* p);
double *generateWaves(const Parameters *p);
AudioBuffer buildAudioBuffer(const Parameters *p);
void destroyAudioBuffer(AudioBuffer* b);

#define LOG_FILE_NAME "log.txt"

/* TODO:
 * add output file name parameter */
int main(void) {
    assert(sizeof(WavHeader) == 44 &&
        "size of header is not 44 (consider using struct packing)");
    loggerInit(LOG_FILE_NAME);
    loggerAppend(LOG_INIT, "WAV generator initialized");

    const Parameters p = parseParameters("config.cfg");
    const WavHeader header = buildWavHeader(&p);
    AudioBuffer buf = buildAudioBuffer(&p);

    FILE *f = fopen(p.outputFile, "wb"); // write-binary mode
    if (!f) {
        loggerAppend(ERR_FATAL, "unable to open file '%s' for writing: %s",
            p.outputFile, strerror(errno));
        loggerClose(errno);
        exit(errno);
    }

    fwrite(&header, sizeof(header), 1, f);
    fwrite(buf.data, buf.bytesPerSample, buf.sampleCount, f);
    fclose(f);
    loggerClose(0);
    free(buf.data);

    return 0;
}

char *readFileContents(const char *restrict file, FILE *f);

static FILE *logFile = NULL;

void loggerInit(const char *file) {
    if (logFile) return;

    logFile = fopen(file, "w+");
    if (!logFile) {
        fprintf(stderr,
            "FATAL: unable to open logging file '%s' for writing: %s\n",
            file, strerror(errno));
        exit(errno);
    }
}

void loggerClose(int32_t code) {
    char *text = readFileContents(LOG_FILE_NAME, logFile);
    const char *status = code ? "abnormally" : "normally";
    loggerAppend(LOG_EXIT,
        "generator terminated %s with exit code %d\n", status, code);
    fclose(logFile);
    if (!text) {
        remove(LOG_FILE_NAME);
        return;
    }

    free(text);
}

void loggerAppend(LogState state, const char *restrict fmt, ...) {
    const char *logState;
    switch (state) {
    case LOG_INIT: {
        logState = "Logger Initialized";
    } break;
    case LOG_OK: {
        logState = "LOG";
    } break;
    case ERR_READ: {
        logState = "READ ERROR";
    } break;
    case ERR_PARSE: {
        logState = "PARSING ERROR";
    } break;
    case ERR_ARG: {
        logState = "Illegal Argument";
    } break;
    case ERR_FATAL: {
        logState = "FATAL ERROR";
    } break;
    case LOG_EXIT: {
        logState = "Logger Closing";
    } break;
    }

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char localTime[64] = {0};
    size_t ret = strftime(localTime, sizeof(localTime), "%F @ %T", tm);
    assert(ret && "unable to retrieve local time string");
    char format[2048] = {0};
    snprintf(format, sizeof(format), "[%s] %s: %s\n", localTime, logState, fmt);
    fmt = format;
    va_list args;
    va_start(args, fmt);
    ret = vfprintf(logFile, fmt, args);
    va_end(args);
    if (ret <= 0) {
        fprintf(stderr,
            "unable to write log message to file: %s\n", strerror(errno));
    }
}

WavHeader buildWavHeader(const Parameters *p) {
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
#define FILE_BUF_SIZE (32 * KB)
#define LINE_DELIMS "\r\n"
#define MAX_AMP_DB 6.0
#define KB 1024
#define OUT_FILE_NAME "file.wav"

#ifdef _MSC_VER
#define strtok_r strtok_s
#define strdup _strdup
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

double parseDouble(const char* line);
double* parseDoubleList(char* line, size_t* listLen);
uint32_t parseUnsignedInt(const char* line);
int32_t parseStringIntoWaveType(char* restrict line);
int32_t parseStringIntoSampleFormat(char* restrict line);
bool parseBool(const char* line);
void stripWhiteSpace(char* restrict string);
void stripDoubleQuotes(char* restrict string);
const char* getWaveTypeString(WaveType type);
const char* getSampleFormatString(SampleFormat fmt);

Parameters parseParameters(const char *file) {
    static const double DefaultFreq = 440.0;
    Parameters params = { // default values
        .freqs = (double*)&DefaultFreq,
        .freqCount = 1,
        .waveType = WAVE_SINE,
        .durationSecs = 4.0,
        .amplitude = 0.0,
        .sampleRate = 48000,
        .bitsPerSample = 32,
        .sampleFormat = FMT_INT_PCM,
        .applyDither = true
    };

    FILE *f = fopen(file, "rt"); // read-text mode
    if (!f) {
        loggerAppend(ERR_READ, "unable to read config file '%s': %s",
            file, strerror(errno));
        return params;
    }

    char *fileBuf = readFileContents(file, f);
    fclose(f);
    if (!fileBuf) return params;

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
        while (*line) {
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
            double *freqs = parseDoubleList(line, &listLen);
            if (!errno) {
                bool freqOk = true;
                for (size_t i = 0; i < listLen; i++) {
                    if (freqs[i] <= 0.0) {
                        loggerAppend(ERR_ARG,
                            "found illegal tone: every tone must be"
                            " a positive number > 0.0Hz (ignoring list)");
                        freqOk = false;
                        break;
                    }
                }

                if (freqOk) params.freqs = freqs, params.freqCount = listLen;
            }
        } break;
        case LINE_WAVE_TYPE: {
            int32_t waveType = parseStringIntoWaveType(line);
            if (!errno) params.waveType = waveType;
        } break;
        case LINE_DURATION_SECONDS: {
            double durationSecs = parseDouble(line);
            if (!errno) params.durationSecs = durationSecs;
        } break;
        case LINE_AMPLITUDE: {
            double amplitude = parseDouble(line);
            if (!errno) {
                params.amplitude =
                    amplitude < MAX_AMP_DB ? amplitude : MAX_AMP_DB;
            }
        } break;
        case LINE_SAMPLE_RATE: {
            uint32_t sampleRate = parseUnsignedInt(line);
            if (!errno) {
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
            if (!errno) params.bitsPerSample = bitsPerSample;
        } break;
        case LINE_SAMPLE_FORMAT: {
            int32_t sampleFormat = parseStringIntoSampleFormat(line);
            if (!errno) {
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
                }

                if (fmtOk) params.sampleFormat = sampleFormat;
            }
        } break;
        case LINE_APPLY_DITHER: {
            bool applyDither = parseBool(line);
            if (!errno) params.applyDither = applyDither;
        } break;
        case LINE_OUTPUT_FILE: {
            stripDoubleQuotes(line);
            int len = snprintf(NULL, 0, "%s.wav", line);
            char *fileName = malloc(len * sizeof(*fileName));
            sprintf(fileName, "%s.wav", line);
            if (strlen(fileName) >= FILENAME_MAX) {
                loggerAppend(ERR_ARG,
                    "filename is longer than %d bytes"
                    " (using default value)", FILENAME_MAX);
                params.outputFile = strdup(OUT_FILE_NAME);
                free(fileName);
            } else {
                params.outputFile = fileName;
            }
        } break;
        }
    }

    free(fileBuf);

    char toneList[4 * KB] = {0};
    for (size_t i = 0; i < params.freqCount && params.freqs; i++) {
        char num[32] = {0};
        snprintf(num, sizeof(num), "%.1lfHz, ", params.freqs[i]);
        strncat(toneList, num, sizeof(toneList) - 1);
    }

    toneList[strlen(toneList) - 2] = '\0';

    const char *type = getWaveTypeString(params.waveType);
    const char *sampleFmt = getSampleFormatString(params.sampleFormat);
    const char *dither = params.applyDither ? "Yes" : "No";
    if (params.sampleFormat == FMT_FLOAT_PCM) dither = "(ignored)";

    const double mb = (double)(params.sampleRate * params.durationSecs *
        (params.bitsPerSample / 8) + sizeof(WavHeader)) / KB;

    loggerAppend(LOG_OK,
        "Generating %zu %s wave(s)...", params.freqCount, type);
    loggerAppend(LOG_OK, "Frequencies:   %s", toneList);
    loggerAppend(LOG_OK, "Length:        %.2lfs (%.2lfKB)",
        params.durationSecs, mb);
    loggerAppend(LOG_OK, "Sample Peak:   %+.2lfdBFS", params.amplitude);
    loggerAppend(LOG_OK, "Sample Rate:   %uHz", params.sampleRate);
    loggerAppend(LOG_OK, "Sample Format: %s", sampleFmt);
    loggerAppend(LOG_OK, "Bit Depth:     %u-bit", params.bitsPerSample);
    loggerAppend(LOG_OK, "Dither:        %s", dither);
    loggerAppend(LOG_OK, "Output File:   '%s'", params.outputFile);

    return params;
}

double parseDouble(const char *line) {
    errno = 0;
    double n = strtod(line, NULL);
    if (!n && errno) {
        loggerAppend(ERR_PARSE,
            "unable to parse a floating-point number from '%s'", line);
    }

    return n;
}

#define INIT_DOUBLE_LIST_CAP 8
#define DOUBLE_LIST_DELIMS ", "

double *parseDoubleList(char *line, size_t *listLen) {
    size_t len = INIT_DOUBLE_LIST_CAP;
    double *list = calloc(len, sizeof(*list));
    char *parserState = NULL;
    char *tok = strtok_r(line, DOUBLE_LIST_DELIMS, &parserState);
    size_t i;
    for (i = 0; tok; i++) {
        if (i >= len) {
            size_t newLen = len * 2;
            double *newList = realloc(list, newLen * sizeof(*newList));
            if (!newList) {
                loggerAppend(ERR_FATAL, "out of memory: %s", strerror(errno));
                loggerClose(errno);
                exit(errno);
            }

            list = newList;
            len = newLen;
        }

        list[i] = parseDouble(tok);
        tok = strtok_r(NULL, DOUBLE_LIST_DELIMS, &parserState);
    }

    realloc(list, i * sizeof(*list)); // trim list
    *listLen = i;
    return list;
}

uint32_t parseUnsignedInt(const char *line) {
    errno = 0;
    unsigned long n = strtoul(line, NULL, 10);
    if ((!n || n == ULONG_MAX)) {
        loggerAppend(ERR_PARSE,
            "unable to parse an unsigned number from '%s'", line);
    }

    return (uint32_t)n;
}

int32_t parseStringIntoWaveType(char *restrict line) {
    stripDoubleQuotes(line);

    if (strcmp(line, "sine") == 0) return WAVE_SINE;
    if (strcmp(line, "triangle") == 0) return WAVE_TRIANGLE;
    if (strcmp(line, "square") == 0) return WAVE_SQUARE;
    if (strcmp(line, "saw") == 0) return WAVE_SAW;
    if (strcmp(line, "even") == 0) return WAVE_EVEN;

    loggerAppend(ERR_PARSE, "unrecognized wave type: '%s'", line);
    return -1;
}

int32_t parseStringIntoSampleFormat(char *restrict line) {
    stripDoubleQuotes(line);

    if (strcmp(line, "integer") == 0) return FMT_INT_PCM;
    if (strcmp(line, "floating-point") == 0) return FMT_FLOAT_PCM;

    loggerAppend(ERR_PARSE, "unrecognized sample format: '%s'", line);
    return -1;
}

bool parseBool(const char *line) {
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

void destroyParameters(Parameters* p) {
    free(p->freqs);
    free(p->outputFile);
    memset(p, 0, sizeof(*p));
}

void stripWhiteSpace(char *restrict string) {
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

void stripDoubleQuotes(char *restrict string) {
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

char *readFileContents(const char *restrict file, FILE *f) {
    int err = fseek(f, 0, SEEK_END);
    if (err) {
        loggerAppend(ERR_READ, "unable to seek EOF for '%s': %s",
            file, strerror(errno));
        return NULL;
    }

    int len = ftell(f);
    if (!len) {
        return NULL;
    } else if (len == -1) {
        loggerAppend(ERR_READ, "unable to get EOF position of '%s': %s",
            file, strerror(errno));
        return NULL;
    }

    rewind(f);
    char *fileBuf = malloc(len * sizeof(*fileBuf));
    fread(fileBuf, sizeof(*fileBuf), len, f);

    return fileBuf;
}

const char* getWaveTypeString(WaveType type) {
    switch (type) {
    case WAVE_SINE: {
        return "sine";
    } break;
    case WAVE_TRIANGLE: {
        return "triangle";
    } break;
    case WAVE_SQUARE: {
        return "square";
    } break;
    case WAVE_SAW: {
        return "saw";
    } break;
    case WAVE_EVEN: {
        return "even";
    } break;
    }
}

const char* getSampleFormatString(SampleFormat fmt) {
    return fmt == FMT_INT_PCM ? "integer" : "floating-point";
}

void addWave(double *buf, size_t len, int32_t type, double freq, int32_t rate);
double gainToDecibels(double gain);
double decibelsToGain(double decibels);

double *generateWaves(const Parameters *p) {
    size_t len = (size_t)(p->sampleRate * p->durationSecs);
    double *buf = calloc(len, sizeof(*buf));
    for (size_t i = 0; i < p->freqCount; i++) {
        addWave(buf, len, p->waveType, p->freqs[i], p->sampleRate);
    }

    double posPeak = *buf, negPeak = *buf;
    for (size_t i = 1; i < len; i++) {
        if (buf[i] > posPeak) posPeak = buf[i];
        if (buf[i] < negPeak) negPeak = buf[i];
    }

    double peak = posPeak > -negPeak ? posPeak : -negPeak;
    double maxVal = decibelsToGain(p->amplitude);
    peak /= maxVal;
    if (peak != 1.0) {
        for (size_t i = 0; i < len; i++) {
            buf[i] /= peak;
        }
    }

    return buf;
}

#define BELOW_NYQUIST(freq, rate) (freq < rate / 2)
#define SINE_WAVE(freq, factor, rate, i) \
    (double)(sin((2.0 * PI * freq * factor) / rate * i))

void addWave(double *buf, size_t len, int32_t type, double freq, int32_t rate) {
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
            double amp = 4.0 / (factor * PI);
            for (size_t i = 0; i < len; i++) {
                buf[i] += SINE_WAVE(freq, factor, rate, i) * amp;
            }

            factor += 2.0;
        }
    } break;
    case WAVE_SAW: {
        while (BELOW_NYQUIST(freq * factor, rate)) {
            double amp = 1.0 / factor;
            for (size_t i = 0; i < len; i++) {
                buf[i] += SINE_WAVE(freq, factor, rate, i) * amp;
            }

            factor += 1.0;
        }
    } break;
    case WAVE_EVEN: {
        while (BELOW_NYQUIST(freq * factor, rate)) {
            double amp = 1.0 / factor;
            for (size_t i = 0; i < len; i++) {
                buf[i] += SINE_WAVE(freq, factor, rate, i) * amp;
            }

            if (factor == 1.0) factor = 0.0;

            factor += 2.0;
        }
    } break;
    }
}

void applyDither(double* buf, size_t len, uint32_t bits);

AudioBuffer buildAudioBuffer(const Parameters *p) {
    double *src = generateWaves(p);
    size_t len = (size_t)((p->sampleRate) * (p->durationSecs));
    uint32_t bits = p->bitsPerSample;

    AudioBuffer b = {
        .sampleCount = len,
        .bytesPerSample = bits / 8,
        .data = malloc(len * bits / 8)
    };

    void *buf = b.data;
    switch (p->sampleFormat) {
    case FMT_INT_PCM: {
        if (p->applyDither) applyDither(src, len, bits);

        size_t maxInt = (size_t)((pow(2.0, bits - 1.0) - 1.0));
        switch (bits) {
        case 8: {
            const uint8_t offset = INT8_MAX + 1;
            for (size_t i = 0; i < len; i++) {
                ((uint8_t*)buf)[i] = (uint8_t)round(src[i] * maxInt + offset);
            }
        } break;
        case 16: {
            for (size_t i = 0; i < len; i++) {
                ((int16_t*)buf)[i] = (int16_t)round(src[i] * maxInt);
            }
        } break;
        case 24: {
            /* NOTE: this bit shifting will probably work very
             * badly if the machine has the wrong endianness */
            for (size_t i = 0, j = 0; i < len; i++, j += 3) {
                int32_t val = (int32_t)round(src[i] * maxInt);
                ((int8_t*)buf)[j] = (int8_t)(val);
                ((int8_t*)buf)[j + 1] = (int8_t)(val >> 8);
                ((int8_t*)buf)[j + 2] = (int8_t)(val >> 16);
            }
        } break;
        case 32: {
            for (size_t i = 0; i < len; i++) {
                ((int32_t*)buf)[i] = (int32_t)round(src[i] * maxInt);
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
        case 64: {
            memcpy(buf, src, len * sizeof(*src));
        } break;
        }
    } break;
    }

    free(src);

    return b;
}

void destroyAudioBuffer(AudioBuffer* b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}

/* TODO: 8-bit dithering is adding dc offset (cause unsigned) */
void applyDither(double* buf, size_t len, uint32_t bits) {
    const double amp = 1.0 / pow(2.0, bits - 1) / RAND_MAX;
    for (size_t i = 0; i < len; i++) {
        buf[i] += (double)(rand() - rand()) * amp;
    }
}

#define MINUS_INF_DB -150.0
#define MAX(a, b) (a > b ? a : b)

double gainToDecibels(double gain) {
    return gain > 0.0 ? MAX(MINUS_INF_DB, log10(gain) * 20.0) : MINUS_INF_DB;
}

double decibelsToGain(double decibels) {
    return decibels > MINUS_INF_DB ? pow(10.0, decibels * 0.05) : 0.0;
}
