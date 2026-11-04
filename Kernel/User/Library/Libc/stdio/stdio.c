/*
 * stdio.c — fopen / fflush / fclose / fseek / ungetc（全称下划线）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Blocks/File.h>
#include <Blocks/Syscall.h>
#include <errno.h>
#include <Host/DirectoryEntry.h>
#include "stdio_private.h"

int stdio_flush_write(FILE *stream) {
    size_t offset = 0;
    ssize_t written;

    if (!stream || stream->write_length == 0) {
        return 0;
    }
    while (offset < stream->write_length) {
        written = FileWrite(stream->file_descriptor, stream->write_buffer + offset,
                            stream->write_length - offset);
        if (written <= 0) {
            stream->error = 1;
            return -1;
        }
        offset += (size_t)written;
    }
    stream->write_length = 0;
    return 0;
}

static int parse_mode(const char *mode, unsigned *flags_out, int *append_out,
                     int *must_exist_out, int *truncate_out) {
    unsigned flags = 0;
    int append = 0;
    int plus = 0;
    char primary = 0;
    const char *cursor;

    if (!mode || mode[0] == 0 || !flags_out || !append_out || !must_exist_out ||
        !truncate_out) {
        return -1;
    }
    for (cursor = mode; *cursor != 0; cursor++) {
        if (*cursor == 'b') {
            continue;
        }
        if (*cursor == '+') {
            plus = 1;
            continue;
        }
        if (*cursor == 'r' || *cursor == 'w' || *cursor == 'a') {
            if (primary != 0) {
                return -1;
            }
            primary = *cursor;
            continue;
        }
        return -1;
    }
    if (primary == 0) {
        return -1;
    }
    if (primary == 'r') {
        flags = FILE_FLAG_READ;
    } else if (primary == 'w') {
        flags = FILE_FLAG_WRITE;
    } else {
        flags = FILE_FLAG_WRITE;
        append = 1;
    }
    if (plus) {
        flags = FILE_FLAG_READ | FILE_FLAG_WRITE;
    }
    *flags_out = flags;
    *append_out = append;
    *must_exist_out = (primary == 'r');
    /* FAT 无 truncate："w"/"w+" 先 unlink 再 open，缺文件仍可写创建 */
    *truncate_out = (primary == 'w');
    return 0;
}

FILE *fopen(const char *path, const char *mode) {
    unsigned flags;
    int append;
    int must_exist;
    int truncate;
    int file_descriptor;
    FILE *stream;
    FS_FILE_STAT status;

    if (parse_mode(mode, &flags, &append, &must_exist, &truncate) < 0 || !path ||
        path[0] == 0) {
        errno = EINVAL;
        return 0;
    }
    if (must_exist) {
        if (FileStat(path, &status) < 0) {
            errno = ENOENT;
            return 0;
        }
        if (status.Attr & FS_ATTR_DIR) {
            errno = EINVAL;
            return 0;
        }
    }
    if (truncate) {
        if (FileStat(path, &status) == 0) {
            if (status.Attr & FS_ATTR_DIR) {
                errno = EINVAL;
                return 0;
            }
            (void)blocks_unlink(path);
        }
    }
    file_descriptor = FileOpen(path);
    if (file_descriptor < 0) {
        return 0;
    }
    if (append) {
        if (FileSeek(file_descriptor, 0, SEEK_END) < 0) {
            FileClose(file_descriptor);
            return 0;
        }
    }
    stream = (FILE *)malloc(sizeof(*stream));
    if (!stream) {
        FileClose(file_descriptor);
        errno = ENOMEM;
        return 0;
    }
    memset(stream, 0, sizeof(*stream));
    stream->file_descriptor = file_descriptor;
    stream->flags = flags;
    return stream;
}

int fflush(FILE *stream) {
    if (!stream) {
        return 0;
    }
    if (stdio_flush_write(stream) < 0) {
        return EOF;
    }
    return 0;
}

int fclose(FILE *stream) {
    int close_result;

    if (!stream) {
        errno = EINVAL;
        return EOF;
    }
    if (stdio_flush_write(stream) < 0) {
        if ((stream->flags & FILE_FLAG_STATIC) == 0) {
            FileClose(stream->file_descriptor);
            free(stream);
        }
        return EOF;
    }
    if (stream->flags & FILE_FLAG_STATIC) {
        return 0;
    }
    close_result = FileClose(stream->file_descriptor);
    free(stream);
    return close_result;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    if (stdio_flush_write(stream) < 0) {
        return -1;
    }
    stream->has_ungotten = 0;
    if (FileSeek(stream->file_descriptor, (off_t)offset, whence) < 0) {
        stream->error = 1;
        return -1;
    }
    stream->end_of_file = 0;
    return 0;
}

long ftell(FILE *stream) {
    off_t position;

    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    if (stdio_flush_write(stream) < 0) {
        return -1;
    }
    position = FileSeek(stream->file_descriptor, 0, SEEK_CUR);
    if (position < 0) {
        stream->error = 1;
        return -1;
    }
    if (stream->has_ungotten && position > 0) {
        return (long)position - 1;
    }
    return (long)position;
}

int ungetc(int character, FILE *stream) {
    if (!stream || character == EOF || stream->has_ungotten) {
        return EOF;
    }
    stream->ungotten = (unsigned char)character;
    stream->has_ungotten = 1;
    stream->end_of_file = 0;
    return (int)stream->ungotten;
}

int feof(FILE *stream) {
    return stream ? stream->end_of_file : 0;
}

int ferror(FILE *stream) {
    return stream ? stream->error : 1;
}

void clearerr(FILE *stream) {
    if (!stream) {
        return;
    }
    stream->end_of_file = 0;
    stream->error = 0;
}

void rewind(FILE *stream) {
    if (!stream) {
        return;
    }
    (void)fseek(stream, 0, SEEK_SET);
    clearerr(stream);
}

int fileno(FILE *stream) {
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    return stream->file_descriptor;
}
