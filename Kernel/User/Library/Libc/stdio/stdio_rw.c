/*
 * stdio_rw.c — fread / fwrite（全称下划线；写缓冲 + ungetc）
 */
#include <stdio.h>
#include <errno.h>
#include <Blocks/File.h>
#include "stdio_private.h"

size_t fread(void *buffer, size_t element_size, size_t element_count, FILE *stream) {
    unsigned char *output;
    size_t byte_count;
    size_t got = 0;
    ssize_t read_count;

    if (!stream || !buffer || element_size == 0 || element_count == 0) {
        return 0;
    }
    if ((stream->flags & FILE_FLAG_READ) == 0) {
        stream->error = 1;
        errno = EINVAL;
        return 0;
    }
    if (stdio_flush_write(stream) < 0) {
        return 0;
    }
    if (element_size != 0 && element_count > ((size_t)-1) / element_size) {
        stream->error = 1;
        errno = EINVAL;
        return 0;
    }
    byte_count = element_size * element_count;
    output = (unsigned char *)buffer;
    if (stream->has_ungotten && byte_count > 0) {
        output[0] = stream->ungotten;
        stream->has_ungotten = 0;
        got = 1;
        output++;
        byte_count--;
    }
    if (byte_count > 0) {
        read_count = FileRead(stream->file_descriptor, output, byte_count);
        if (read_count < 0) {
            stream->error = 1;
            return got / element_size;
        }
        if ((size_t)read_count < byte_count) {
            stream->end_of_file = 1;
        }
        got += (size_t)read_count;
    }
    return got / element_size;
}

size_t fwrite(const void *buffer, size_t element_size, size_t element_count,
              FILE *stream) {
    const unsigned char *input;
    size_t byte_count;
    size_t done = 0;

    if (!stream || !buffer || element_size == 0 || element_count == 0) {
        return 0;
    }
    if ((stream->flags & FILE_FLAG_WRITE) == 0) {
        stream->error = 1;
        errno = EINVAL;
        return 0;
    }
    if (element_size != 0 && element_count > ((size_t)-1) / element_size) {
        stream->error = 1;
        errno = EINVAL;
        return 0;
    }
    byte_count = element_size * element_count;
    input = (const unsigned char *)buffer;
    while (done < byte_count) {
        size_t room = STDIO_BUFFER_SIZE - stream->write_length;
        size_t chunk = byte_count - done;
        size_t index;

        if (room == 0) {
            if (stdio_flush_write(stream) < 0) {
                return done / element_size;
            }
            room = STDIO_BUFFER_SIZE;
        }
        if (chunk > room) {
            chunk = room;
        }
        for (index = 0; index < chunk; index++) {
            unsigned char byte = input[done + index];

            stream->write_buffer[stream->write_length++] = byte;
            if (byte == '\n') {
                if (stdio_flush_write(stream) < 0) {
                    return (done + index + 1) / element_size;
                }
            }
        }
        done += chunk;
    }
    return done / element_size;
}
