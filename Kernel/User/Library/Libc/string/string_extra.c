/*
 * string_extra.c — strrchr / strncat / strtok / strerror / span 族（全称下划线）
 */
#include <errno.h>
#include <string.h>

void *memchr(const void *block, int character, size_t byte_count) {
    const unsigned char *cursor = (const unsigned char *)block;
    unsigned char wanted = (unsigned char)character;
    size_t index;

    if (!block) {
        return 0;
    }
    for (index = 0; index < byte_count; index++) {
        if (cursor[index] == wanted) {
            return (void *)(cursor + index);
        }
    }
    return 0;
}

static int character_in_set(char character, const char *set) {
    if (!set) {
        return 0;
    }
    while (*set != 0) {
        if (*set == character) {
            return 1;
        }
        set++;
    }
    return 0;
}

size_t strspn(const char *string, const char *accept) {
    size_t length = 0;

    if (!string) {
        return 0;
    }
    while (string[length] != 0 && character_in_set(string[length], accept)) {
        length++;
    }
    return length;
}

size_t strcspn(const char *string, const char *reject) {
    size_t length = 0;

    if (!string) {
        return 0;
    }
    while (string[length] != 0 && !character_in_set(string[length], reject)) {
        length++;
    }
    return length;
}

char *strpbrk(const char *string, const char *accept) {
    if (!string) {
        return 0;
    }
    while (*string != 0) {
        if (character_in_set(*string, accept)) {
            return (char *)string;
        }
        string++;
    }
    return 0;
}

char *strrchr(const char *string, int character) {
    const char *last_match = 0;
    unsigned char wanted = (unsigned char)character;

    if (!string) {
        return 0;
    }
    do {
        if ((unsigned char)*string == wanted) {
            last_match = string;
        }
    } while (*string++ != 0);
    return (char *)last_match;
}

char *strncat(char *destination, const char *source, size_t max_length) {
    char *cursor = destination;

    if (!destination) {
        return destination;
    }
    if (!source) {
        source = "";
    }
    while (*cursor != 0) {
        cursor++;
    }
    while (max_length > 0 && *source != 0) {
        *cursor++ = *source++;
        max_length--;
    }
    *cursor = 0;
    return destination;
}

char *strtok(char *string, const char *delimiters) {
    static char *next_token;
    char *token_start;
    const char *delimiter_cursor;
    int is_delimiter;

    if (!delimiters) {
        delimiters = "";
    }
    if (!string) {
        string = next_token;
    }
    if (!string) {
        return 0;
    }

    /* 跳过前导分隔符 */
    while (*string != 0) {
        is_delimiter = 0;
        for (delimiter_cursor = delimiters; *delimiter_cursor != 0;
             delimiter_cursor++) {
            if (*string == *delimiter_cursor) {
                is_delimiter = 1;
                break;
            }
        }
        if (!is_delimiter) {
            break;
        }
        string++;
    }
    if (*string == 0) {
        next_token = 0;
        return 0;
    }

    token_start = string;
    while (*string != 0) {
        for (delimiter_cursor = delimiters; *delimiter_cursor != 0;
             delimiter_cursor++) {
            if (*string == *delimiter_cursor) {
                *string = 0;
                next_token = string + 1;
                return token_start;
            }
        }
        string++;
    }
    next_token = 0;
    return token_start;
}

char *strerror(int error_number) {
    switch (error_number) {
    case 0:
        return "Success";
    case EPERM:
        return "Operation not permitted";
    case ENOENT:
        return "No such file or directory";
    case ESRCH:
        return "No such process";
    case EIO:
        return "Input/output error";
    case EBADF:
        return "Bad file descriptor";
    case ECHILD:
        return "No child processes";
    case EAGAIN:
        return "Resource temporarily unavailable";
    case ENOMEM:
        return "Cannot allocate memory";
    case EACCES:
        return "Permission denied";
    case EEXIST:
        return "File exists";
    case ENOTDIR:
        return "Not a directory";
    case EINVAL:
        return "Invalid argument";
    case EMFILE:
        return "Too many open files";
    case ENOSPC:
        return "No space left on device";
    case ESPIPE:
        return "Illegal seek";
    case EPIPE:
        return "Broken pipe";
    case ENOSYS:
        return "Function not implemented";
    case ENETUNREACH:
        return "Network is unreachable";
    case ECONNRESET:
        return "Connection reset by peer";
    case ENOTCONN:
        return "Transport endpoint is not connected";
    case ETIMEDOUT:
        return "Connection timed out";
    case ECONNREFUSED:
        return "Connection refused";
    case EHOSTUNREACH:
        return "No route to host";
    case EINPROGRESS:
        return "Operation now in progress";
    default:
        return "Unknown error";
    }
}
