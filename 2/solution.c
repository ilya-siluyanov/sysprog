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
    pipeline_list pl = parse_pipeline_list(raw -> value);
    pipeline_result result = execute_pipeline_list(&pl);

    free(raw->value);
    free(raw);

    if (!result.should_exit) {
        return -1;
    }
    // printf("Leaks: %llu\n", heaph_get_alloc_count());
    return result.exit_code;
}

int main() {
    // heaph_init();
    int exit_code;
    while ((exit_code = iteration()) < 0);
    //FIXME: wait for zombies
    return exit_code;
}
