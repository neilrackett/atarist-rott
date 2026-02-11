#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int __argc = 0;
char **__argv = 0;
int _argc = 0;
char **_argv = 0;

char *strupr(char *s) {
    char *p = s;
    while (*p) {
        *p = (char)toupper((unsigned char)*p);
        ++p;
    }
    return s;
}

static void reverse(char *s, int len) {
    int i = 0;
    int j = len - 1;
    while (i < j) {
        char t = s[i];
        s[i] = s[j];
        s[j] = t;
        ++i;
        --j;
    }
}

char *itoa(int value, char *str, int base) {
    char *p = str;
    int sign = 0;
    unsigned int v;
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    if (value < 0) {
        sign = 1;
        v = (unsigned int)(-value);
    } else {
        v = (unsigned int)value;
    }
    do {
        unsigned int digit = v % (unsigned int)base;
        *p++ = (digit < 10) ? (char)('0' + digit) : (char)('a' + (digit - 10));
        v /= (unsigned int)base;
    } while (v);
    if (sign) *p++ = '-';
    *p = '\0';
    reverse(str, (int)(p - str));
    return str;
}

char *ltoa(long value, char *str, int base) {
    char *p = str;
    int sign = 0;
    unsigned long v;
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    if (value < 0) {
        sign = 1;
        v = (unsigned long)(-value);
    } else {
        v = (unsigned long)value;
    }
    do {
        unsigned long digit = v % (unsigned long)base;
        *p++ = (digit < 10) ? (char)('0' + digit) : (char)('a' + (digit - 10));
        v /= (unsigned long)base;
    } while (v);
    if (sign) *p++ = '-';
    *p = '\0';
    reverse(str, (int)(p - str));
    return str;
}

char *ultoa(unsigned long value, char *str, int base) {
    char *p = str;
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    do {
        unsigned long digit = value % (unsigned long)base;
        *p++ = (digit < 10) ? (char)('0' + digit) : (char)('a' + (digit - 10));
        value /= (unsigned long)base;
    } while (value);
    *p = '\0';
    reverse(str, (int)(p - str));
    return str;
}

char getch(void) {
    int c = getchar();
    if (c == EOF) return 0;
    return (char)c;
}

long filelength(int handle) {
    long cur = lseek(handle, 0, SEEK_CUR);
    long end = lseek(handle, 0, SEEK_END);
    lseek(handle, cur, SEEK_SET);
    return end;
}

int access(const char *path, int mode) {
    return access(path, mode);
}

int getpid(void) {
    return (int)getpid();
}

int setup_homedir(void) {
    return 0;
}

void crash_print(int sig) {
    (void)sig;
}
