#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include "models.h"
#include "stack.h"
#include "timings.h"
#include "types.h"
#include <errno.h>


file_to_sort *find_file(file_to_sort **storage, ll size) {
    for (ll i = 0; i < size; i++) {
        if (!storage[i]->acquired) {
            return storage[i];
        }
    }
    return NULL;
}


ll numbers_count(char *filename) {
    FILE *f = fopen(filename, "r");
    if (f == 0) {
        printf("Cannot open file `%s` to count numbers: %s\n", filename, strerror(errno));
        return 0;
    }
    ll count = 0;
    while (1) {
        ll num = 0;
        int res = fscanf(f, "%lld", &num);
        if (res == -1)
            break;
        count++;
    }
    fclose(f);
    return count;
}

// Here I use not an optimal approach to detect the amount of numbers;
// Firstly I read the file to see how many numbers there
// then I allocate memory for them, read the file again and store the numbers
// in a buffer
numbers_array *read_array(char *filename) {
    ll count = numbers_count(filename);
    numbers_array *arr = (numbers_array *) calloc(1, sizeof(numbers_array));
    arr->numbers = calloc(count, sizeof(ll));

    FILE *f = fopen(filename, "r");
    if (f == 0) {
        printf("Cannot open file `%s`: %s\n", filename, strerror(errno));
        return arr;
    }
    while (1) {
        ll num = 0;
        int res = fscanf(f, "%lld", &num);
        if (res == -1)
            break;
        arr->numbers[arr->len++] = num;
    }

    fclose(f);
    return arr;
}


numbers_array *sort(numbers_array *arr1, numbers_array *arr2) {
    numbers_array *result = (numbers_array *) calloc(1, sizeof(numbers_array));
    result->numbers = calloc(arr1->len + arr2->len, sizeof(ll));
    result->len = (arr1->len + arr2->len);

    ll i = 0, j = 0, t = 0;
    while (i < arr1->len && j < arr2->len) {
        if (arr1->numbers[i] < arr2->numbers[j]) {
            result->numbers[t] = arr1->numbers[i];
            i++;
        } else {
            result->numbers[t] = arr2->numbers[j];
            j++;
        }
        t++;
    }
    while (i < arr1->len) {
        result->numbers[t] = arr1->numbers[i];
        i++;
        t++;
    }
    while (j < arr2->len) {
        result->numbers[t] = arr2->numbers[j];
        j++;
        t++;
    }

    return result;
}


void swap(ll *a, ll *b) {
    ll temp = *a;
    *a = *b;
    *b = temp;
}


ll sort_iteration(ll *numbers, ll l, ll r, ll n) {
    if (l + 1 >= r) {
        return -1;
    }

    if (l + 2 == r) {
        if (numbers[l] > numbers[l + 1]) {
            swap(&numbers[l], &numbers[l + 1]);
        }
        return -1;
    }

    ll p = (l + r) / 2;
    ll pivot = numbers[p];

    ll i = l;
    ll j = r - 1;

    while (i <= j) {
        while (numbers[i] < pivot) {
            i++;
        }
        while (numbers[j] > pivot) {
            j--;
        }
        if (i <= j) {
            swap(&numbers[i], &numbers[j]);
            i++;
            j--;
        }
    }
    return i;
}


void sort_array(ll *numbers, ll n, ld time_quantum) {
    stack *s = s_new();
    pair *p = (pair *) calloc(1, sizeof(pair));
    p->l = 0;
    p->r = n;
    s_push(s, p);
    ld ts = get_microsec();
    while ((p = s_pop(s)) != NULL) {
        ll pivot_index = sort_iteration(numbers, p->l, p->r, n);
        if (pivot_index == -1) {
            free(p);
            continue;
        }
        pair *p1 = calloc(1, sizeof(pair));
        p1->l = p->l;
        p1->r = pivot_index;
        pair *p2 = calloc(1, sizeof(pair));
        p2->l = pivot_index;
        p2->r = p->r;

        free(p);
        s_push(s, p2);
        s_push(s, p1);

        if (get_microsec() - ts > time_quantum) {
            ts = get_microsec();
            coro_yield();
        }
    }
    free(s->arr);
    free(s);
}


int coroutine_func_f(void *context) {
    coro_args *args = context;
    printf("Started coroutine %s\n", args->name);
    while (1) {
        file_to_sort *file = find_file(args->files, args->files_len);
        if (file == NULL) {
            printf("Coro %s did not found a file to sort\n", args->name);
            break;
        }
        printf("Coro %s: take `%s` to sort\n", args->name, file->name);
        file->acquired = 1;
        numbers_array *arr = read_array(file->name);
        sort_array(arr->numbers, arr->len, args->quantum);
        args->arrays[*args->arrays_len] = arr;
        *args->arrays_len = *args->arrays_len + 1;
    }
    struct coro *coro = coro_this();
    printf(
            "Finished coro `%s`\nSwitch count: %lld, Busy time: %.3Lf microsec, Total time: %.3Lf microsec\n",
           args->name,
           coro->stats->switch_count,
           coro->stats->work_time,
           coro->stats->total_time
    );
    // clean up only coro-specific data, do not clean shared objects
    free(args->name);
    free(context);
    return 0;
}

int on_argparse_failed(char *message) {
    printf("%s\n", message);
    return 1;
}

int
main(int argc, char **argv) {
    if (argc < 2) {
        return on_argparse_failed("Not enough arguments. Check help for example launch");
    }
    if (strcmp(argv[1], "-h") == 0) {
        printf("Example: ./a.out -t 1 -n 2 -o result.txt file1.txt [file2.txt [...]]\n");
        printf("`-t [int]` - Target latency (microseconds)\n");
        printf("`-n [int]` - Number of coroutines\n");
        printf("`-o [str]` - Where to write sorted numbers\n");
        printf("Then a list of files are specified\n");
        printf("! Current version of the solution enforces to specify arguments in the order specified here\n");
        return 0;
    }

    if (argc < 3 || strcmp(argv[1], "-t") != 0) {
        return on_argparse_failed("Specify -t in the order specified in help");
    }
    if (argc < 5 || strcmp("-n", argv[3]) != 0) {
        return on_argparse_failed("Specify -n in the order specified in help");
    }
    if (argc < 7 || strcmp("-o", argv[5]) != 0) {
        return on_argparse_failed("Specify -o in the order specified in help");
    }

    if (argc < 8) {
        return on_argparse_failed("Did you provide a file to sort?");
    }

    ll T = atoll(argv[2]);
    ll N = atoll(argv[4]);
    char *outputname = argv[6];


    printf("Time quantum: %lld, Number of coroutines: %lld\n", T, N);

    ll offset = 7;
    ll files_count = argc - offset;
    printf("Files count: %lld\n", files_count);

    file_to_sort **files = (file_to_sort **) calloc(files_count, sizeof(file_to_sort *));

    for (int i = 0; i < files_count; i++) {
        file_to_sort *item = (file_to_sort *) calloc(1, sizeof(file_to_sort));
        item->name = strdup(argv[offset + i]);
        item->acquired = 0;
        files[i] = item;
    }

    // FIXME: If a file X will appear twice, there should be another logic for memory allocation
    numbers_array **all_arrays = (numbers_array **) calloc(files_count, sizeof(numbers_array *));
    ll arrays_len = 0;

    coro_sched_init();
    for (int i = 0; i < N; ++i) {
        //cleaned up in coro func
        coro_args *args = (coro_args *) calloc(1, sizeof(coro_args));
        args->files = files;
        args->quantum = T / N;
        args->name = calloc(10 * N, sizeof(char));
        args->arrays = all_arrays;
        args->files_len = files_count;
        args->arrays_len = &arrays_len;
        sprintf(args->name, "coro_%d", i);
        coro_new(coroutine_func_f, args);
    }

    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        coro_delete(c);
    }

    // cleanup files_to_sort
    for (ll i = 0; i < files_count; i++) {
        free(files[i]->name);
        free(files[i]);
    }
    free(files);

    numbers_array *result = calloc(1, sizeof(numbers_array));
    ull first_array_to_alloc = sizeof(ll) * all_arrays[0]->len;
    result->numbers = (ll *) calloc(1, first_array_to_alloc);
    memcpy(result->numbers, all_arrays[0]->numbers, first_array_to_alloc);
    result->len = all_arrays[0]->len;

    for (ll i = 1; i < files_count; i++) {
        numbers_array *new_result = sort(result, all_arrays[i]);
        free(result->numbers);
        free(result);
        result = new_result;
    }

    //cleanup all_arrays
    for (ll i = 0; i < files_count; i++) {
        free(all_arrays[i]->numbers);
        free(all_arrays[i]);
    }
    free(all_arrays);

    FILE *output = fopen(outputname, "w");
    if (output == 0) {
        printf("Cannot open file `%s` to write output", outputname);

    } else {
        for (ll i = 0; i < result->len; i++) {
            fprintf(output, "%lld ", result->numbers[i]);
        }
    }
    free(result->numbers);
    free(result);
    fclose(output);
    return 0;
}

