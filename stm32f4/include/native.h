// Raw keyboard I/O under Linux and MacOS, from embello's 1638-pdp8/p8.c code.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#ifdef macosx
#include <sys/filio.h>
#endif

struct termios tiosSaved;
static int done, batchMode;

static void cleanup (void) {
    tcsetattr(0, TCSANOW, &tiosSaved);
}

static int argCnt;
static const char* const* argVec;

extern void loadMem (void* ptr, size_t len) {
    FILE* fp = fopen(argVec[1], "rb");
    if (fp == 0 || fread(ptr, 1, len, fp) == 0) {
        perror(argVec[1]);
        exit(1);
    }
    fclose(fp);
}

extern void saveMem (const void* ptr, size_t len) {
    if (argCnt < 3)
        return;

    FILE* fp = fopen(argVec[2], "wb");
    if (fp == 0) {
        perror(argVec[2]);
        exit(1);
    }
    fwrite(ptr, 1, len, fp);
    fclose(fp);
}

#if 0
int main (int argc, const char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s loadbin ?savebin?\n", argv[0]);
        exit(1);
    }

    if (isatty(0)) {
        tcgetattr(0, &tiosSaved);
        atexit(cleanup);

        struct termios tios = tiosSaved;
        cfmakeraw(&tios);
        tcsetattr(0, TCSANOW, &tios);
    } else
        batchMode = 1;

    argCnt = argc;
    argVec = argv;
    
    run();

    return 0;
}
#endif

struct Console {
    void init () {
        if (isatty(0)) {
            tcgetattr(0, &tiosSaved);
            atexit(cleanup);

            struct termios tios = tiosSaved;
            cfmakeraw(&tios);
            tcsetattr(0, TCSANOW, &tios);
        } else
            batchMode = 1;
    }

    bool writable () {
        return true;
    }
    void putc (int c) {
        static char lastc = 0;
        if (c == '\n' && lastc != '\r')
            write(0, "\r", 1);
        write(0, &c, 1);
        lastc = c;
    }
    bool readable () {
        if (batchMode)
            return 1;
        int i = 0;
        if (!done) {
            usleep(10);
            ioctl(0, FIONREAD, &i);
        }
        return i > 0;
    }
    int getc () {
        int c = 0;
        if (batchMode)
            c = getchar();
        else if (read(0, &c, 1) < 0)
            c = -1;
        if (c < 0) {
            c = 0;
            done = 1;
        }
        if (c == '\n')
            c = '\r';
        // not same as interrupt, only works while input is being polled
        if (c == 0x1C) // ctrl-backslash
            exit(1);
        return c;
    }
};

template< int N >
class DiskImage {
    const char* filename;
    FILE* fp;

public:
    DiskImage (const char* fn) : filename (fn), fp (0) {}

    bool valid () {
        FILE* f = fopen(filename, "r+");
        if (f != 0)
            fclose(f);
        return f != 0;
    }
    int init (bool erase =false) {
        if (erase) {
            printf("initialising internal flash\r\n");
            FILE* f = fopen(filename, "w+");
            constexpr int size = N == 128 ? 77*26*128 : 1440*1024;
            fseek(f, size-1, SEEK_SET);
            fputc(0, f);
            fclose(f);
        }
        fp = fopen(filename, "r+");
        return 0;
    }
    void readSector (int pos, void* buf) {
        fseek(fp, N*pos, SEEK_SET);
        memset(buf, 0xE5, N); // in case read is past the current file end
        fread(buf, N, 1, fp);
    }
    void writeSector (int pos, void const* buf) {
        fseek(fp, N*pos, SEEK_SET);
        fwrite(buf, N, 1, fp);
        fflush(fp);
    }
};

struct DummyGPIO {
    void toggle () {}
};
