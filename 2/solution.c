#include "stdio.h"
#include "parser.h"
#include "../utils/heap_help/heap_help.h"


int iteration() {
    read_raw_result *raw = read_raw();
    if (raw->value == NULL) {
        int to_return = 0;
        if (raw->exit_code == 1)
            to_return = 1;
        free(raw);
        return to_return;
    }
    complete_cmd *complete = parse_complete(raw -> value);

    free(raw->value);
    free(raw);

    if (complete == NULL) {
        free(complete);
        return -1;
    }
    int exit_code = execute_complete(complete);
    free(complete->cmdv);
    free(complete);
    // printf("Leaks: %llu\n", heaph_get_alloc_count());
    return exit_code;
}

int main() {
    // heaph_init();
    int exit_code;
    while ((exit_code = iteration()) < 0);
    return exit_code;
}
