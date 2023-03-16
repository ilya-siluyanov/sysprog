#include "stdio.h"
#include "parser.h"
#include "../utils/heap_help/heap_help.h"


int iteration() {
    read_raw_result raw = read_raw();
    if (raw.value == NULL) {
        free(raw.value);
        return raw.exit_code;
    }
    pipeline_list pl = parse_pipeline_list(raw.value);
    free(raw.value);
    pipeline_list *pl_p = calloc(1, sizeof(pipeline_list));
    memcpy(pl_p, &pl, sizeof(pipeline_list));
    pipeline_result result = {.should_exit = 0, .exit_code = 0};
    if (pl_p->in_background) {
        if (fork() == 0) {
            dup2(open("/dev/null", O_RDONLY), STDIN_FILENO);
            execute_pipeline_list(pl_p);
            result.should_exit = 1;
        }
    } else {
        result = execute_pipeline_list(pl_p);
    }
    cleanup_pipeline_list(pl_p);

    // printf("Leaks: %llu\n", heaph_get_alloc_count());

    if (!result.should_exit) {
        return -1;
    }
    return result.exit_code;
}

int main() {
    // heaph_init();
    int exit_code;
    while ((exit_code = iteration()) < 0);
    //FIXME: wait for zombies
    return exit_code;
}
