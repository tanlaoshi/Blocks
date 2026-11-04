/*
 * environ.c — environ / getenv / setenv / unsetenv（hosted-getenv；C99）
 */
#include <stdlib.h>
#include <string.h>

char **environ;

#define ENV_MAX 64

static char *gSlots[ENV_MAX + 1];
static int gReady;

static void EnvironEnsure(void) {
    if (gReady) {
        return;
    }
    gSlots[0] = 0;
    if (!environ) {
        environ = gSlots;
    }
    gReady = 1;
}

static char *FindVar(const char *Name, size_t NameLen) {
    char **P;

    EnvironEnsure();
    if (!environ || !Name) {
        return 0;
    }
    for (P = environ; *P; P++) {
        if (strncmp(*P, Name, NameLen) == 0 && (*P)[NameLen] == '=') {
            return *P;
        }
    }
    return 0;
}

char *getenv(const char *name) {
    size_t N;
    char *Entry;

    if (!name || !name[0]) {
        return 0;
    }
    N = strlen(name);
    Entry = FindVar(name, N);
    if (!Entry) {
        return 0;
    }
    return Entry + N + 1;
}

int setenv(const char *name, const char *value, int overwrite) {
    size_t NameLen;
    size_t ValueLen;
    char *Entry;
    char *Built;
    char **P;
    int Count;

    if (!name || !name[0] || strchr(name, '=') != 0) {
        return -1;
    }
    if (!value) {
        value = "";
    }
    EnvironEnsure();
    NameLen = strlen(name);
    ValueLen = strlen(value);
    Entry = FindVar(name, NameLen);
    if (Entry && !overwrite) {
        return 0;
    }
    Built = (char *)malloc(NameLen + 1 + ValueLen + 1);
    if (!Built) {
        return -1;
    }
    memcpy(Built, name, NameLen);
    Built[NameLen] = '=';
    memcpy(Built + NameLen + 1, value, ValueLen + 1);
    if (Entry) {
        for (P = environ; *P; P++) {
            if (*P == Entry) {
                free(Entry);
                *P = Built;
                return 0;
            }
        }
    }
    Count = 0;
    for (P = environ; *P; P++) {
        Count++;
    }
    if (Count >= ENV_MAX) {
        free(Built);
        return -1;
    }
    if (environ != gSlots) {
        /* 外部 environ：教学简化，只支持本 CRT 表 */
        free(Built);
        return -1;
    }
    gSlots[Count] = Built;
    gSlots[Count + 1] = 0;
    return 0;
}

int unsetenv(const char *name) {
    size_t NameLen;
    char **P;
    char **Q;

    if (!name || !name[0] || strchr(name, '=') != 0) {
        return -1;
    }
    EnvironEnsure();
    if (environ != gSlots) {
        return -1;
    }
    NameLen = strlen(name);
    for (P = gSlots; *P; P++) {
        if (strncmp(*P, name, NameLen) == 0 && (*P)[NameLen] == '=') {
            free(*P);
            for (Q = P; *Q; Q++) {
                *Q = *(Q + 1);
            }
            return 0;
        }
    }
    return 0;
}
