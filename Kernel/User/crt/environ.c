/*
 * environ.c — environ / getenv / setenv / unsetenv（展览级命名）
 *
 * exec 可把 environ 指到栈上 envp；首次 setenv/unsetenv 时拷入本 CRT 表再改。
 */
#include <stdlib.h>
#include <string.h>

char **environ;

#define ENVIRONMENT_MAX_ENTRIES 64

static char *LocalEnvironmentSlots[ENVIRONMENT_MAX_ENTRIES + 1];
static int LocalEnvironmentReady;

static void EnsureEnvironReady(void) {
    if (LocalEnvironmentReady) {
        return;
    }
    LocalEnvironmentSlots[0] = 0;
    if (!environ) {
        environ = LocalEnvironmentSlots;
    }
    LocalEnvironmentReady = 1;
}

static int AdoptEnvironIntoLocalTable(void) {
    char **Source;
    int Count;
    size_t Length;
    char *Copy;

    EnsureEnvironReady();
    if (environ == LocalEnvironmentSlots) {
        return 0;
    }
    Source = environ;
    Count = 0;
    if (Source) {
        for (; *Source && Count < ENVIRONMENT_MAX_ENTRIES; Source++) {
            Length = strlen(*Source);
            Copy = (char *)malloc(Length + 1);
            if (!Copy) {
                while (Count > 0) {
                    Count--;
                    free(LocalEnvironmentSlots[Count]);
                    LocalEnvironmentSlots[Count] = 0;
                }
                return -1;
            }
            memcpy(Copy, *Source, Length + 1);
            LocalEnvironmentSlots[Count] = Copy;
            Count++;
        }
    }
    LocalEnvironmentSlots[Count] = 0;
    environ = LocalEnvironmentSlots;
    return 0;
}

static char *FindEnvironmentEntry(const char *Name, size_t NameLength) {
    char **Cursor;

    EnsureEnvironReady();
    if (!environ || !Name) {
        return 0;
    }
    for (Cursor = environ; *Cursor; Cursor++) {
        if (strncmp(*Cursor, Name, NameLength) == 0 &&
            (*Cursor)[NameLength] == '=') {
            return *Cursor;
        }
    }
    return 0;
}

char *getenv(const char *Name) {
    size_t NameLength;
    char *Entry;

    if (!Name || !Name[0]) {
        return 0;
    }
    NameLength = strlen(Name);
    Entry = FindEnvironmentEntry(Name, NameLength);
    if (!Entry) {
        return 0;
    }
    return Entry + NameLength + 1;
}

int setenv(const char *Name, const char *Value, int Overwrite) {
    size_t NameLength;
    size_t ValueLength;
    char *Entry;
    char *Built;
    char **Cursor;
    int Count;

    if (!Name || !Name[0] || strchr(Name, '=') != 0) {
        return -1;
    }
    if (!Value) {
        Value = "";
    }
    if (AdoptEnvironIntoLocalTable() != 0) {
        return -1;
    }
    NameLength = strlen(Name);
    ValueLength = strlen(Value);
    Entry = FindEnvironmentEntry(Name, NameLength);
    if (Entry && !Overwrite) {
        return 0;
    }
    Built = (char *)malloc(NameLength + 1 + ValueLength + 1);
    if (!Built) {
        return -1;
    }
    memcpy(Built, Name, NameLength);
    Built[NameLength] = '=';
    memcpy(Built + NameLength + 1, Value, ValueLength + 1);
    if (Entry) {
        for (Cursor = environ; *Cursor; Cursor++) {
            if (*Cursor == Entry) {
                free(Entry);
                *Cursor = Built;
                return 0;
            }
        }
    }
    Count = 0;
    for (Cursor = environ; *Cursor; Cursor++) {
        Count++;
    }
    if (Count >= ENVIRONMENT_MAX_ENTRIES) {
        free(Built);
        return -1;
    }
    LocalEnvironmentSlots[Count] = Built;
    LocalEnvironmentSlots[Count + 1] = 0;
    return 0;
}

int unsetenv(const char *Name) {
    size_t NameLength;
    char **Cursor;
    char **Shift;

    if (!Name || !Name[0] || strchr(Name, '=') != 0) {
        return -1;
    }
    if (AdoptEnvironIntoLocalTable() != 0) {
        return -1;
    }
    NameLength = strlen(Name);
    for (Cursor = LocalEnvironmentSlots; *Cursor; Cursor++) {
        if (strncmp(*Cursor, Name, NameLength) == 0 &&
            (*Cursor)[NameLength] == '=') {
            free(*Cursor);
            for (Shift = Cursor; *Shift; Shift++) {
                *Shift = *(Shift + 1);
            }
            return 0;
        }
    }
    return 0;
}
