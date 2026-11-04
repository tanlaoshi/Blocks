/*
 * environ.c — environ / getenv / setenv / unsetenv（全称下划线）
 *
 * exec 可把 environ 指到栈上 envp；首次 setenv/unsetenv 时拷入本 CRT 表再改。
 */
#include <stdlib.h>
#include <string.h>

char **environ;

#define ENVIRONMENT_MAX_ENTRIES 64

static char *local_environment_slots[ENVIRONMENT_MAX_ENTRIES + 1];
static int local_environment_ready;

static void ensure_environ_ready(void) {
    if (local_environment_ready) {
        return;
    }
    local_environment_slots[0] = 0;
    if (!environ) {
        environ = local_environment_slots;
    }
    local_environment_ready = 1;
}

static int adopt_environ_into_local_table(void) {
    char **source;
    int count;
    size_t length;
    char *copy;

    ensure_environ_ready();
    if (environ == local_environment_slots) {
        return 0;
    }
    source = environ;
    count = 0;
    if (source) {
        for (; *source && count < ENVIRONMENT_MAX_ENTRIES; source++) {
            length = strlen(*source);
            copy = (char *)malloc(length + 1);
            if (!copy) {
                while (count > 0) {
                    count--;
                    free(local_environment_slots[count]);
                    local_environment_slots[count] = 0;
                }
                return -1;
            }
            memcpy(copy, *source, length + 1);
            local_environment_slots[count] = copy;
            count++;
        }
    }
    local_environment_slots[count] = 0;
    environ = local_environment_slots;
    return 0;
}

static char *find_environment_entry(const char *name, size_t name_length) {
    char **cursor;

    ensure_environ_ready();
    if (!environ || !name) {
        return 0;
    }
    for (cursor = environ; *cursor; cursor++) {
        if (strncmp(*cursor, name, name_length) == 0 &&
            (*cursor)[name_length] == '=') {
            return *cursor;
        }
    }
    return 0;
}

char *getenv(const char *name) {
    size_t name_length;
    char *entry;

    if (!name || !name[0]) {
        return 0;
    }
    name_length = strlen(name);
    entry = find_environment_entry(name, name_length);
    if (!entry) {
        return 0;
    }
    return entry + name_length + 1;
}

int setenv(const char *name, const char *value, int overwrite) {
    size_t name_length;
    size_t value_length;
    char *entry;
    char *built;
    char **cursor;
    int count;

    if (!name || !name[0] || strchr(name, '=') != 0) {
        return -1;
    }
    if (!value) {
        value = "";
    }
    if (adopt_environ_into_local_table() != 0) {
        return -1;
    }
    name_length = strlen(name);
    value_length = strlen(value);
    entry = find_environment_entry(name, name_length);
    if (entry && !overwrite) {
        return 0;
    }
    built = (char *)malloc(name_length + 1 + value_length + 1);
    if (!built) {
        return -1;
    }
    memcpy(built, name, name_length);
    built[name_length] = '=';
    memcpy(built + name_length + 1, value, value_length + 1);
    if (entry) {
        for (cursor = environ; *cursor; cursor++) {
            if (*cursor == entry) {
                free(entry);
                *cursor = built;
                return 0;
            }
        }
    }
    count = 0;
    for (cursor = environ; *cursor; cursor++) {
        count++;
    }
    if (count >= ENVIRONMENT_MAX_ENTRIES) {
        free(built);
        return -1;
    }
    local_environment_slots[count] = built;
    local_environment_slots[count + 1] = 0;
    return 0;
}

int unsetenv(const char *name) {
    size_t name_length;
    char **cursor;
    char **shift;

    if (!name || !name[0] || strchr(name, '=') != 0) {
        return -1;
    }
    if (adopt_environ_into_local_table() != 0) {
        return -1;
    }
    name_length = strlen(name);
    for (cursor = local_environment_slots; *cursor; cursor++) {
        if (strncmp(*cursor, name, name_length) == 0 &&
            (*cursor)[name_length] == '=') {
            free(*cursor);
            for (shift = cursor; *shift; shift++) {
                *shift = *(shift + 1);
            }
            return 0;
        }
    }
    return 0;
}
