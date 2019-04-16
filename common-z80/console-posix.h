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

void consoleOut (char c) {
    static char lastc = 0;
    if (c == '\n' && lastc != '\r')
        write(0, "\r", 1);
    write(0, &c, 1);
    lastc = c;
}

void consoleOuts (const char* s) {
    while (*s)
        consoleOut(*s++);
}

int consoleHit (void) {
    if (batchMode)
        return 1;
    int i = 0;
    if (!done) {
        usleep(10);
        ioctl(0, FIONREAD, &i);
    }
    return i > 0;
}

int consoleWait (void) {
    int c = 0;
    if (batchMode)
        c = getchar();
    else if (read(0, &c, 1) < 0)
        c = -1;
    if (c < 0 || c == 0x04) {
        c = 0;
        done = 1;
    }
    if (c == '\n')
        c = '\r';
    return c;
}

int consoleIn (void) {
    return consoleHit() ? consoleWait() : 0;
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
