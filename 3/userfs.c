#include "userfs.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>

typedef size_t offset_t;
enum {
    BLOCK_SIZE = 512,
    MAX_FILE_SIZE = 1024 * 1024 * 100,
};

enum {
    FILL_BLOCK,
    END_OF_REWRITE,
    END_OF_DATA
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
    /** Block memory. */
    char *memory;
    /** How many bytes are occupied. */
    size_t occupied;
    /** Next block in the file. */
    struct block *next;
    /** Previous block in the file. */
    struct block *prev;

    /* PUT HERE OTHER MEMBERS */
};

struct file {
    /** Double-linked list of file blocks. */
    struct block *block_list;
    /**
     * Last block in the list above for fast access to the end
     * of file.
     */
    struct block *last_block;

    /** How many file descriptors are opened on the file. */
    int refs;
    /** File name. */
    char *name;
    /** Files are stored in a double-linked list. */
    struct file *next;
    struct file *prev;

    size_t size;

    int mark_as_deleted;
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
    struct file *file;
    offset_t offset;
    int flags;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno() {
    return ufs_error_code;
}

size_t min(size_t a, size_t b) {
    return a < b ? a : b;
}


void free_block(struct block *block) {
    if (block->next != NULL)
        block->next->prev = block->prev;
    if (block->prev != NULL)
        block->prev->next = block->next;
    free(block->memory);
    free(block);
}

void free_file_blocks(struct file *f) {
    while (f->last_block != NULL) {
        struct block *prev = f->last_block->prev;
        free_block(f->last_block);
        f->last_block = prev;
    }
}

void free_file(struct file *f) {
    free_file_blocks(f);
    if (f->next != NULL)
        f->next->prev = f->prev;
    if (f->prev != NULL)
        f->prev->next = f->next;
    free(f->name);
    free(f);
}
int ufs_descriptors_resize() {
    int new_capacity = file_descriptor_capacity * 2 + 1;
    struct filedesc **new_storage = realloc(file_descriptors, new_capacity * sizeof(struct filedesc));
    if (new_storage == NULL) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }
    for (int i = file_descriptor_capacity; i < new_capacity; i++) {
        new_storage[i] = NULL;
    }
    file_descriptor_capacity = new_capacity;
    file_descriptors = new_storage;
    return 0;
}

struct file *find_file(const char *filename) {
    struct file *file = file_list;

    while (file != NULL) {
        if (!file->mark_as_deleted && strcmp(file->name, filename) == 0) {
            break;
        }
        file = file->next;
    }

    return file;
}

void delete_file(struct file *file) {
    struct file *prev = file->prev;
    struct file *next = file->next;

    if (prev != NULL) {
        prev->next = next;
    } else {
        file_list = next;
    }

    if (next != NULL) {
        next->prev = prev;
    }

    free_file(file);
}

void append_file(struct file *f) {
    if (file_list == NULL) {
        file_list = f;
        return;
    }
    struct file *last_file = file_list;
    while (last_file->next != NULL) {
        last_file = last_file->next;
    }
    last_file->next = f;
    f->prev = last_file;
}


void append_block(struct file *f) {
    struct block *block = calloc(1, sizeof(struct block));
    block->memory = calloc(BLOCK_SIZE, sizeof(char));

    if (f->block_list == NULL) {
        f->block_list = block;
    }

    if (f->last_block != NULL) {
        f->last_block->next = block;
        block->prev = f->last_block;
    }

    f->last_block = block;
}

int write_to(struct filedesc *desc, const char *buf, size_t size) {
    if (desc->offset + size > MAX_FILE_SIZE) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }
    struct file *f = desc->file;
    desc->offset = min(desc->offset, f->size);
    size_t bytes_written = 0;

    if (f->block_list == NULL) {
        append_block(f);
    }
    struct block *block = f->block_list;

    int pages_to_skip = (int) desc -> offset / BLOCK_SIZE;
    for(int i = 0; i < pages_to_skip;i++) {
        if (block == NULL) {
            printf("Bug! Block cannot be null: Line %d", __LINE__);
        }
        block = block->next;
    }
    if (block == NULL) {
        append_block(f);
        block = f->last_block;
    }
    offset_t block_offset = desc->offset % BLOCK_SIZE;
    int write_new_data = 0;

    int bytes_remaining = size;
    while (bytes_remaining > 0) {
        size_t max_write_to_block = BLOCK_SIZE - block_offset;
        size_t max_write_with_rewrite = block->occupied - block_offset;

        size_t bytes_to_write = max_write_to_block;
        int decision = FILL_BLOCK;
        if (!write_new_data && max_write_with_rewrite < bytes_to_write) {
            bytes_to_write = max_write_with_rewrite;
            decision = END_OF_REWRITE;
        }
        if (bytes_remaining < bytes_to_write) {
            bytes_to_write = bytes_remaining;
            decision = END_OF_DATA;
        }

        memcpy(block->memory + block_offset, buf + bytes_written, bytes_to_write);
        bytes_remaining -= bytes_to_write;
        bytes_written += bytes_to_write;
        desc->offset += bytes_to_write;

        if (decision == END_OF_REWRITE) {
            block_offset += bytes_to_write;
            write_new_data = 1;
            continue;
        }

        if (bytes_remaining > 0 && block->next == NULL) {
            append_block(f);
        }
        if (write_new_data)
            block->occupied += bytes_to_write;

        block_offset = 0;
        block = block->next;
    }

    f->size += bytes_written;
    return bytes_written;
}

int read_from(struct filedesc *desc, char *buf, size_t size) {
    struct file *f = desc->file;
    desc->offset = min(desc->offset, f->size);

    size_t bytes_copied = 0;
    struct block *block = f->block_list;
    int pages_to_skip = (int) desc -> offset / BLOCK_SIZE;
    for(;pages_to_skip > 0;pages_to_skip--) {
        if (block == NULL) {
            printf("Bug! Block cannot be null: Line %d", __LINE__);
        }
        block = block->next;
    }
    offset_t block_offset = desc->offset % BLOCK_SIZE;

    while (bytes_copied < size && block != NULL) {
        size_t max_bytes_read_in_block = BLOCK_SIZE - block_offset;
        size_t bytes_remaining = size - bytes_copied;
        size_t bytes_available = block->occupied - block_offset;

        size_t bytes_to_copy = max_bytes_read_in_block;
        if (bytes_remaining < bytes_to_copy)
            bytes_to_copy = bytes_remaining;
        if (bytes_available < bytes_to_copy)
            bytes_to_copy = bytes_available;
        memcpy(buf + bytes_copied, block->memory + block_offset, bytes_to_copy);
        bytes_copied += bytes_to_copy;
        block_offset = 0;
        desc->offset += bytes_to_copy;
        block = block->next;
    }
    return bytes_copied;
}

void truncate(struct file *f) {
    free_file_blocks(f);
}

int allowed_to_write(struct filedesc *desc) {
    if (!(desc->flags & (UFS_READ_WRITE | UFS_WRITE_ONLY))) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return 0;
    }
    return 1;
}

int allowed_to_read(struct filedesc *desc) {
    if (!(desc->flags & (UFS_READ_WRITE | UFS_READ_ONLY))) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return 0;
    }
    return 1;
}

int ufs_find_first_free_fd() {
    int fd = 0;
    for (; fd < file_descriptor_count; fd++) {
        if (file_descriptors[fd] == NULL) {
            return fd;
        }
    }
    return fd;
}

int no_such_fd(int fd) {
    return file_descriptors == NULL || fd > file_descriptor_capacity || fd < 0 || file_descriptors[fd] == NULL;
}

int
ufs_open(const char *filename, int flags) {
    if (file_descriptors == NULL) {
        if (ufs_descriptors_resize() != 0) {
            return -1;
        }
    }

    struct file *file = find_file(filename);
    if (file == NULL) {
        if ((flags & UFS_CREATE) == 0) {
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }
        struct file *f = calloc(1, sizeof(struct file));
        f->name = strdup(filename);
        file = f;
        append_file(f);
    }

    int fd = ufs_find_first_free_fd();
    if (fd >= file_descriptor_capacity) {
        if (ufs_descriptors_resize() != 0) {
            return -1;
        }
    }

    file->refs++;
    struct filedesc *desc = calloc(1, sizeof(struct filedesc));
    desc->file = file;
    file_descriptors[fd] = desc;
    file_descriptor_count++;

    if ((flags & (UFS_READ_ONLY | UFS_WRITE_ONLY | UFS_READ_WRITE)) == 0) {
        /// provide default here
        flags = UFS_READ_WRITE;
    }
    desc->flags = flags;
    return fd;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size) {
    if (no_such_fd(fd)) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *desc = file_descriptors[fd];

    if (allowed_to_write(desc) == 1) {
        return write_to(desc, buf, size);
    }
    return -1;
}

void resize_file(struct filedesc *desc, size_t new_size) {
    size_t size = 0;
    struct file *f = desc->file;
    struct block *block = f->block_list;
    while (size < new_size && block != NULL) {
        if (size + block->occupied < new_size) {
            size += block->occupied;
            block = block->next;
            continue;
        }
        size_t bytes_to_preserve = new_size - size;
        memset(block->memory + bytes_to_preserve, 0, BLOCK_SIZE - bytes_to_preserve);
        size += bytes_to_preserve;
        block->occupied = bytes_to_preserve;
    }
    struct block *last_block = f->last_block;
    while (last_block != block) {
        last_block = last_block->prev;
        free_block(last_block->next);
        f->last_block = last_block;
    }
    if (size == 0) {
        free_block(block);
    }
    desc->offset = min(desc->offset, size);
    f->size = min(f->size, size);
}

int ufs_resize(int fd, size_t new_size) {
    if (no_such_fd(fd)) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    struct filedesc *desc = file_descriptors[fd];
    resize_file(desc, new_size);
    return 0;
}

ssize_t
ufs_read(int fd, char *buf, size_t size) {
    if (no_such_fd(fd)) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *desc = file_descriptors[fd];

    if (allowed_to_read(desc) == 1) {
        return read_from(desc, buf, size);
    }
    return -1;
}

int
ufs_close(int fd) {
    if (no_such_fd(fd)) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *desc = file_descriptors[fd];
    if (desc == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct file *f = desc->file;
    f->refs--;
    free(desc);
    if (f->refs == 0 && f->mark_as_deleted) {
        delete_file(f);
    }
    file_descriptors[fd] = NULL;
    file_descriptor_count--;
    return 0;
}

int
ufs_delete(const char *filename) {
    struct file *f = find_file(filename);
    if (f == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }
    f->mark_as_deleted = 1;
    if (f->refs == 0) {
        delete_file(f);
    }
    if(file_descriptor_count == 0) {
        free(file_descriptors);
        file_descriptors = NULL;
    }
    return 0;
}
