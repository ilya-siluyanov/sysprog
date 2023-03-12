#include "stdio.h"
#include "parser.h"
// #include "../utils/heap_help/heap_help.h"


int iteration() {
    read_raw_result *raw = read_raw();
    if (raw->value == NULL) {
        if (raw->exit_code == 1)
            return 1;
        return -1;
    }
    complete_cmd *complete = parse_complete(raw -> value);
    free(raw->value);
    free(raw);

    if (complete == NULL) {
        return -1;
    }
    int exit_code = execute_complete(complete);

    // printf("Leaks: %llu\n", heaph_get_alloc_count());
    return exit_code;
}

int main() {
    // heaph_init();
    int exit_code;
    while ((exit_code = iteration()) < 0);
    return exit_code;
}
