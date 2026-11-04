/*
 * environ.c — environ / getenv / setenv / unsetenv（hosted-getenv；C99）
 *
 * exec 可把 environ 指到栈上 envp；首次 setenv/unsetenv 时拷入本 CRT 表再改。
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

static int EnvironAdoptLocal(void) {
    char **Source;
    int Count;
    size_t Length;
    char *Copy;

    EnvironEnsure();
    if (environ == gSlots) {
        return 0;
    }
    Source = environ;
    Count = 0;
    if (Source) {
        for (; *Source && Count < ENV_MAX; Source++) {
            Length = strlen(*Source);
            Copy = (char *)malloc(Length + 1);
            if (!Copy) {
                while (Count > 0) {
                    Count--;
                    free(gSlots[Count]);
                    gSlots[Count] = 0;
                }
                return -1;
            }
            memcpy(Copy, *Source, Length + 1);
            gSlots[Count] = Copy;
            Count++;
        }
    }
    gSlots[Count] = 0;
    environ = gSlots;
    return 0;
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
    if (EnvironAdoptLocal() != 0) {
        return -1;
    }
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
    if (EnvironAdoptLocal() != 0) {
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
