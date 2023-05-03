#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"

typedef struct pair {
    ll l;
    ll r;
} pair;

typedef struct stack {
    pair **arr;
    ll cap;
    ll len;
} stack;


stack *s_new() {
    stack *s = (stack *) malloc(sizeof(stack));
    return s;
}


void s_resize(stack *s) {
    ll cap = s->cap * 2 + 1;
    s->arr = realloc(s->arr, sizeof(pair *) * cap);
    s->cap = cap;
}

void s_push(stack *s, pair *v) {
    if (s->len == s->cap) {
        s_resize(s);
    }
    s->arr[s->len++] = v;
}

pair *s_pop(stack *s) {
    if (s->len <= 0) {
        return NULL;
    }
    return s->arr[--s->len];
}

void s_print(stack *s) {
    for(int i = 0; i < s->len;i++) {
        printf("%lld %lld | ", s->arr[i]->l, s->arr[i]->r);
    }
    printf("\nLength: %lld\n", s->len);
}

