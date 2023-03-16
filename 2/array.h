#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"

typedef struct array {
    void **buf;
    ull len;
    ull capacity;
} array;

array *arr_new() {
    array *arr = malloc(sizeof(array));
    arr->buf = (void **) calloc(1, sizeof(void *));
    arr->len = 0;
    arr->capacity = 1;
    return arr;
}

void arr_free(array *arr) {
    /// NOTE: Assumes that the array doesn't have ownership over pointers
    free(arr->buf);
    free(arr);
}

void arr_resize(array *arr) {
    ull new_capacity = arr->capacity * 2;
    void **new_buf = realloc(arr->buf, sizeof(void *) * new_capacity);
    if (new_buf == NULL) {
        printf("Cannot allocate new buffer during array resizing");
        return;
    }
    arr->buf = new_buf;
    arr->capacity = new_capacity;
}

void arr_push_back(array *arr, void *c) {
    if (arr->len + 1 >= arr->capacity) {
        arr_resize(arr);
    }
    arr->buf[arr->len++] = c;
}

ull arr_len(array *arr) {
    return arr->len;
}

void *arr_pop(array *arr) {
    if (arr_len(arr) == 0) {
        return NULL;
    }
    void *result = arr->buf[--arr->len];
    arr->buf[arr->len] = NULL;
    return result;
}
