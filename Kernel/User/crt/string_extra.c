/*
 * string_extra.c — strrchr / strncat / strtok / strerror（libc-string）
 */
#include <errno.h>
#include <string.h>

char *strrchr(const char *s, int c) {
    const char *Last = 0;
    unsigned char Ch = (unsigned char)c;

    if (!s) {
        return 0;
    }
    do {
        if ((unsigned char)*s == Ch) {
            Last = s;
        }
    } while (*s++);
    return (char *)Last;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *D = dst;

    if (!dst) {
        return dst;
    }
    if (!src) {
        src = "";
    }
    while (*D) {
        D++;
    }
    while (n > 0 && *src) {
        *D++ = *src++;
        n--;
    }
    *D = 0;
    return dst;
}

char *strtok(char *s, const char *delim) {
    static char *Save;
    char *Start;
    const char *D;

    if (!delim) {
        delim = "";
    }
    if (!s) {
        s = Save;
    }
    if (!s) {
        return 0;
    }
    while (*s) {
        for (D = delim; *D; D++) {
            if (*s == *D) {
                break;
            }
        }
        if (!*D) {
            break;
        }
        s++;
    }
    if (!*s) {
        Save = 0;
        return 0;
    }
    Start = s;
    while (*s) {
        for (D = delim; *D; D++) {
            if (*s == *D) {
                *s = 0;
                Save = s + 1;
                return Start;
            }
        }
        s++;
    }
    Save = 0;
    return Start;
}

char *strerror(int errnum) {
    switch (errnum) {
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
