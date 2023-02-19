typedef long long ll;
typedef long double ld;


typedef struct file_to_sort {
    char *name;
    int acquired;
}file_to_sort;


typedef struct numbers_array {
    ll *numbers;
    ll len;
}numbers_array;


typedef struct coro_args {
    char *name;
    file_to_sort **files;
    ll files_len;
    numbers_array **arrays;
    // in case file1.txt will appear twice in cmd args
    // and should be processed twice
    // the number the same as `files_len`
    // otherwise it is equal or less than `files_len`
    ll *arrays_len;
    ld quantum;
} coro_args;
