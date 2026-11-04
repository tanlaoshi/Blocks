/*
 * string_extra.c — strrchr / strncat / strtok / strerror / span 族（展览级命名）
 */
#include <errno.h>
#include <string.h>

void *memchr(const void *Block, int Character, size_t ByteCount) {
    const unsigned char *Cursor = (const unsigned char *)Block;
    unsigned char Wanted = (unsigned char)Character;
    size_t Index;

    if (!Block) {
        return 0;
    }
    for (Index = 0; Index < ByteCount; Index++) {
        if (Cursor[Index] == Wanted) {
            return (void *)(Cursor + Index);
        }
    }
    return 0;
}

static int CharacterInSet(char Character, const char *Set) {
    if (!Set) {
        return 0;
    }
    while (*Set != 0) {
        if (*Set == Character) {
            return 1;
        }
        Set++;
    }
    return 0;
}

size_t strspn(const char *String, const char *Accept) {
    size_t Length = 0;

    if (!String) {
        return 0;
    }
    while (String[Length] != 0 && CharacterInSet(String[Length], Accept)) {
        Length++;
    }
    return Length;
}

size_t strcspn(const char *String, const char *Reject) {
    size_t Length = 0;

    if (!String) {
        return 0;
    }
    while (String[Length] != 0 && !CharacterInSet(String[Length], Reject)) {
        Length++;
    }
    return Length;
}

char *strpbrk(const char *String, const char *Accept) {
    if (!String) {
        return 0;
    }
    while (*String != 0) {
        if (CharacterInSet(*String, Accept)) {
            return (char *)String;
        }
        String++;
    }
    return 0;
}

char *strrchr(const char *String, int Character) {
    const char *LastMatch = 0;
    unsigned char Wanted = (unsigned char)Character;

    if (!String) {
        return 0;
    }
    do {
        if ((unsigned char)*String == Wanted) {
            LastMatch = String;
        }
    } while (*String++ != 0);
    return (char *)LastMatch;
}

char *strncat(char *Destination, const char *Source, size_t MaxLength) {
    char *Cursor = Destination;

    if (!Destination) {
        return Destination;
    }
    if (!Source) {
        Source = "";
    }
    while (*Cursor != 0) {
        Cursor++;
    }
    while (MaxLength > 0 && *Source != 0) {
        *Cursor++ = *Source++;
        MaxLength--;
    }
    *Cursor = 0;
    return Destination;
}

char *strtok(char *String, const char *Delimiters) {
    static char *NextToken;
    char *TokenStart;
    const char *DelimiterCursor;
    int IsDelimiter;

    if (!Delimiters) {
        Delimiters = "";
    }
    if (!String) {
        String = NextToken;
    }
    if (!String) {
        return 0;
    }

    /* 跳过前导分隔符 */
    while (*String != 0) {
        IsDelimiter = 0;
        for (DelimiterCursor = Delimiters; *DelimiterCursor != 0;
             DelimiterCursor++) {
            if (*String == *DelimiterCursor) {
                IsDelimiter = 1;
                break;
            }
        }
        if (!IsDelimiter) {
            break;
        }
        String++;
    }
    if (*String == 0) {
        NextToken = 0;
        return 0;
    }

    TokenStart = String;
    while (*String != 0) {
        for (DelimiterCursor = Delimiters; *DelimiterCursor != 0;
             DelimiterCursor++) {
            if (*String == *DelimiterCursor) {
                *String = 0;
                NextToken = String + 1;
                return TokenStart;
            }
        }
        String++;
    }
    NextToken = 0;
    return TokenStart;
}

char *strerror(int ErrorNumber) {
    switch (ErrorNumber) {
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
