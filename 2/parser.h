#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "array.h"
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "../utils/heap_help/heap_help.h"

typedef struct read_raw_result {
    char *value;
    int exit_code;
} read_raw_result;

struct cmd {
    char **argv;
    ll argc;

    char *redirect_to;
    int o_append;

    int in_background;
};

typedef struct pipeline {
    struct cmd **cmdv;
    ll cmdc;

    int in_background;

    /// 0 - do not wait
    /// 1 - if zero (and)
    /// 2 - if nonzero (or)
    int prev_wait;
} pipeline;

typedef struct pipeline_result {
    int exit_code;
    int should_exit;
} pipeline_result;

typedef struct pipeline_list {
    pipeline **pipelines;
    ll count;

    int in_background;
} pipeline_list;

char *arr_to_str(array *arr) {
    /// takes ownership on the arr
    char *result = calloc(arr_len(arr) + 1, sizeof(char));
    for(ll j = 0; j < arr_len(arr);j++) {
        char x = *((char *)arr->buf[j]);
        result[j] = x;
        free(arr->buf[j]);
    }
    arr_free(arr);
    return result;
}

void cleanup_cmd(struct cmd *cmd) {
    for (ll k = 0; k < cmd->argc; k++) {
        free(cmd->argv[k]);
    }
    free(cmd->argv);
    free(cmd->redirect_to);
    free(cmd);
}

void cleanup_pipeline(pipeline *pipeline) {
    for (ll j = 0; j < pipeline->cmdc; j++) {
        struct cmd *cmd = pipeline->cmdv[j];
        cleanup_cmd(cmd);
    }
    free(pipeline->cmdv);
    free(pipeline);
}

void cleanup_pipeline_list(pipeline_list *pl) {
    for (ll i = 0; i < pl->count;i++) {
        pipeline *pipeline = pl->pipelines[i];
        cleanup_pipeline(pipeline);
    }
    free(pl->pipelines);
    free(pl);
}

read_raw_result read_raw() {
    ll CWD_SIZE = 150;
    char *cwd = calloc(CWD_SIZE + 1, sizeof(char));
    getcwd(cwd, CWD_SIZE);
    read_raw_result r = {.value = NULL, .exit_code = 0};

    // printf("\033[0;32m%s\033[0m > ", cwd);
    free(cwd);
    array *chars = arr_new();
    int c;
    int should_exit = 1;
    int prev_slash = 0;
    char literal = '\0';
    int eol = 0;
    while ((c = fgetc(stdin)) != EOF) {
        char x = (char) c;
        should_exit = 0;
        switch (x) {
            case '"':
            case '\'':
                /// start of finish string token
                if (literal == '\0') {
                    literal = x;
                } else if (literal == x) {
                    /// "a'bc", 'a"bcd'
                    literal = '\0';
                }
                break;
            case '\n':
                if (!prev_slash && literal == '\0') {
                    eol = 1;
                    break;
                }
                prev_slash = 0;
                char *last_char_p = chars->buf[arr_len(chars) - 1];
                if (*last_char_p == '\\') {
                    free(arr_pop(chars));
                    continue;
                }
        }
        if (eol)
            break;
        prev_slash = 0;
        if (x == '\\') {
            prev_slash = 1;
        }
        char *x_p = malloc(sizeof(char));
        x_p[0] = x;
        arr_push_back(chars, x_p);
    }
    if (arr_len(chars) == 0) {
        arr_free(chars);
        if (eol)
            r.exit_code = -1;
        return r;
    }

    if (should_exit) {
        arr_free(chars);
        return r;
    }

    if (arr_len(chars) == 0) {
        arr_free(chars);
        r.exit_code = 2;
        return r;
    }

    r.value = calloc(arr_len(chars) + 1, sizeof(char));
    for (ll i = 0; i < arr_len(chars); i++) {
        char x = *((char *) chars->buf[i]);
        r.value[i] = x;
        free(chars->buf[i]);
    }
    arr_free(chars);

    // printf("read raw: %lld\n", heaph_get_alloc_count());
    return r;
}

struct cmd parse_command(char const *raw_cmd, ll cmd_len, ll *i) {
    char *token = calloc(cmd_len + 1, sizeof(char));
    ll token_len = 0;

    array *argv = arr_new();
    int o_append = 0;
    char *redirect_to = NULL;

    struct cmd cmd = {.argv = NULL, .argc = 0};

    /// one of '\0', '\'', '"'
    char literal = '\0';
    int target_redirect = 0;
    int comment = 0;
    int in_background = 0;

    while (*i < cmd_len) {
        if (comment) {
            if (raw_cmd[*i] == '\n')
                comment = 0;
            (*i)++;
            continue;
        }
        switch (raw_cmd[*i]) {
            case '#':
                comment = 1;
                continue;
            case '\\':
                if (*i + 1 > cmd_len) {
                    printf("No char after \\\n: char %lld", *i + 1);
                    free(token);
                    for(ll i = 0; i < arr_len(argv);i++) {
                        free(argv->buf[i]);
                    }
                    arr_free(argv);
                    return cmd;
                }
                if (literal == '\'') {
                    token[token_len++] = raw_cmd[*i];
                } else if (literal == '\"') {
                    token[token_len++] = raw_cmd[(*i)++];
                    if (raw_cmd[*i] == '\\') {
                        (*i)++;
                    }
                } else {
                    token[token_len++] = raw_cmd[++(*i)];
                    (*i)++;
                }
                continue;

            case '"':
            case '\'':
                /// start of finish string token
                if (literal == '\0') {
                    literal = raw_cmd[(*i)++];
                    break;
                }
                /// "a'bc", 'a"bcd'
                if (literal != raw_cmd[*i]) {
                    token[token_len++] = raw_cmd[(*i)++];
                    break;
                }
                /// "abc", 'abc'
                literal = '\0';
                (*i)++;
                break;

            case '>':
                if (literal) {
                    token[token_len++] = raw_cmd[(*i)++];
                    break;
                }
                if (*i + 1 > cmd_len) {
                    printf("End of command found: char %lld\n", *i);
                    free(token);
                    for(ll j = 0; j < arr_len(argv);j++) {
                        free(argv->buf[j]);
                    }
                    arr_free(argv);
                    return cmd;
                }
                if (token_len > 0) {
                    char *tok = strdup(token);
                    arr_push_back(argv, tok);

                    free(token);
                    token = calloc(cmd_len + 1, sizeof(char));
                    token_len = 0;
                }
                target_redirect = 1;
                char next_tok = raw_cmd[*i + 1];
                if (next_tok == '>') {
                    /// append
                    o_append = 1;
                    (*i)++;
                }
                (*i)++;
                break;

            case ' ':
                if (literal) {
                    token[token_len++] = raw_cmd[(*i)++];
                    break;
                }
                if (token_len == 0) {
                    (*i)++;
                    break;
                }
                char *tok = strdup(token);
                free(token);
                token = calloc(cmd_len + 1, sizeof(char));
                token_len = 0;
                if (target_redirect) {
                    redirect_to = tok;
                    target_redirect = 0;
                } else {
                    arr_push_back(argv, tok);
                    break;
                }
                (*i)++;
                break;
            case '&':
                if (literal == '\0') {
                    in_background = 1;
                    (*i)++;
                    break;
                }
            default:
                token[token_len++] = raw_cmd[(*i)++];
        }
    }

    if (!target_redirect && token_len > 0) {
        arr_push_back(argv, strdup(token));
    }

    if (arr_len(argv) == 0) {
        free(token);
        arr_free(argv);
        free(redirect_to);
        // printf("parse command 0: %lld\n", heaph_get_alloc_count());
        return cmd;
    }
    arr_push_back(argv, NULL);
    cmd.argc = arr_len(argv) - 1;
    cmd.argv = (char **) argv->buf;
    cmd.redirect_to = redirect_to;
    cmd.in_background = in_background;
    if (target_redirect) {
        free(cmd.redirect_to);
        cmd.redirect_to = strdup(token);
        cmd.o_append = o_append;
    }
    free(argv);
    free(token);
    // printf("parse command 1: %lld\n", heaph_get_alloc_count());
    return cmd;
}

pipeline parse_pipeline(char *raw_cmd) {
    char *raw_part = strtok(raw_cmd, "|");
    array *cmdv = arr_new();
    pipeline pipeline = {.cmdv = NULL, .cmdc = 0, .in_background = 0};
    while (raw_part != NULL) {
        ll i = 0;
        struct cmd cmd = parse_command(raw_part, strlen(raw_part), &i);
        if (cmd.argc > 0) {
            struct cmd *cmd_p = calloc(1, sizeof(struct cmd));
            memcpy(cmd_p, &cmd, sizeof(struct cmd));
            arr_push_back(cmdv, cmd_p);
        }
        raw_part = strtok(NULL, "|");
    }

    if (arr_len(cmdv) == 0) {
        free(raw_part);
        arr_free(cmdv);
        return pipeline;
    }

    pipeline.cmdv = (struct cmd **) cmdv->buf;
    pipeline.cmdc = arr_len(cmdv);
    if (pipeline.cmdc > 0) {
        pipeline.in_background = pipeline.cmdv[pipeline.cmdc - 1]->in_background;
    }
    free(cmdv);
    //printf("parse complete: %lld\n", heaph_get_alloc_count());
    return pipeline;
}

pipeline_list parse_pipeline_list(char *raw_cmd) {
    array *pipelines = arr_new();
    array *chars = arr_new();
    ll i = 0;
    ll n = strlen(raw_cmd);
    int prev_flag = 0;
    pipeline_list pl;
    while(i < n) {
        char curr = raw_cmd[i];
        if (i + 1 >= n) {
            char *x_p = calloc(1, sizeof(char));
            x_p[0] = curr;
            arr_push_back(chars, x_p);
            i++;
            continue;
        }
        char next = raw_cmd[i + 1];
        int and_found = curr == '&' && next == '&';
        int or_found = curr == '|' && next == '|';

        if (!and_found && !or_found) {
            char *x_p = calloc(1, sizeof(char));
            x_p[0] = curr;
            arr_push_back(chars, x_p);
            i++;
            continue;
        }
        i += 2;

        char *raw_pipeline = arr_to_str(chars);
        chars = arr_new();

        pipeline pipeline = parse_pipeline(raw_pipeline);
        free(raw_pipeline);
        pipeline.prev_wait = prev_flag;

        if (pipeline.cmdc == 0) {
            break;
        }
        if (and_found) {
            prev_flag = 1;
        } else {
            prev_flag = 2;
        }
        struct pipeline *pipeline_p = calloc(1, sizeof(pipeline));
        memcpy(pipeline_p, &pipeline, sizeof(pipeline));
        arr_push_back(pipelines, pipeline_p);
    }

    if (arr_len(chars) > 0) {
        char *raw_pipeline = arr_to_str(chars);
        pipeline pipeline = parse_pipeline(raw_pipeline);
        pipeline.prev_wait = prev_flag;
        free(raw_pipeline);
        if (pipeline.cmdc > 0) {
            struct pipeline *pipeline_p = calloc(1, sizeof(pipeline));
            memcpy(pipeline_p, &pipeline, sizeof(pipeline));
            arr_push_back(pipelines, pipeline_p);
        }
    } else {
        arr_free(chars);
    }

    pl.count = arr_len(pipelines);
    pl.pipelines = (pipeline **)pipelines->buf;
    if(pl.count > 0) {
        pl.in_background = pl.pipelines[pl.count - 1]->in_background;
    }
    free(pipelines);
    return pl;
}

pid_t run_process(pipeline_list *pl, ll pipeline_n, ll cmd_n, int fd[][2]) {
    pipeline *pipeline = pl->pipelines[pipeline_n];
    struct cmd *cmd = pipeline->cmdv[cmd_n - 1];
    if (strcmp(cmd->argv[0], "cd") == 0) {
        if (chdir(cmd->argv[1]) == -1) {
            printf("\033[0;31mcd: %s\n\033[0m", strerror(errno));
        }
        return 0;
    }
    int child_pid = fork();
    if (child_pid == 0) {
        dup2(fd[cmd_n - 1][0], STDIN_FILENO);
        dup2(fd[cmd_n][1], STDOUT_FILENO);

        for(ll j = 0; j < pipeline->cmdc + 1;j++) {
            close(fd[j][0]);
            close(fd[j][1]);
        }
        if (cmd->redirect_to != NULL) {
            int flags = O_CREAT | O_WRONLY;
            if (cmd->o_append)
                flags |= O_APPEND;
            else
                flags |= O_TRUNC;
            int fds = open(cmd->redirect_to, flags, S_IRWXU);
            dup2(fds, STDOUT_FILENO);
        }
        if (cmd->argc > 0 && strcmp(cmd->argv[0], "exit") == 0) {
            int exit_code = 0;
            if (cmd->argc > 1) {
                char *_;
                exit_code = strtol(cmd->argv[1], &_, 10);
            }
            cleanup_pipeline_list(pl);
            exit(exit_code);
        }

        char *cmd_name = strdup(cmd->argv[0]);
        execvp(cmd_name, cmd->argv);
        free(cmd_name);

        for(ll j = 0; j < cmd->argc - 1;j++) {
            printf("\033[0;31m%s ", cmd->argv[j]);
        }
        printf("%s", cmd->argv[cmd->argc - 1]);
        printf(": %s\033[0m\n", strerror(errno));

        cleanup_pipeline_list(pl);
        exit(errno);
    }
    return child_pid;
}

pipeline_result execute_pipeline(pipeline_list *pl, ll pipeline_n) {
    pipeline *pipeline = pl->pipelines[pipeline_n];
    ll n = pipeline->cmdc;
    int fds[n + 1][2];
    pid_t pids[n];

    for (ll i = 0; i < n + 1; i++) {
        pipe(fds[i]);
    }
    dup2(STDIN_FILENO, fds[0][0]);
    dup2(STDOUT_FILENO, fds[n][1]);


    for (ll i = 0; i < pipeline->cmdc; i++) {
        pids[i] = run_process(pl, pipeline_n, i + 1, fds);
    }
    for(ll i = 0; i < n + 1;i++) {
        close(fds[i][0]);
        close(fds[i][1]);
    }

    pipeline_result result = {.should_exit = 0, .exit_code = 0};
    if (pipeline->in_background){
        return result;
    }
    int status;
    for(ll i = 0; i < pipeline->cmdc;i++) {
        waitpid(pids[i], &status, 0);
    }

    // FIXME: cover cases other than exit
    result.exit_code = 0;
    if (WIFEXITED(status))
        result.exit_code = WEXITSTATUS(status);

    struct cmd *last_cmd = pipeline->cmdv[n - 1];
    if (pipeline->cmdc == 1 && last_cmd->argc > 0 && strcmp(last_cmd->argv[0], "exit") == 0) {
        result.should_exit = 1;
    }

    // printf("exec complete: %lld\n", heaph_get_alloc_count());
    return result;
}

pipeline_result execute_pipeline_list(pipeline_list *pl) {
    pipeline_result result;
    result.exit_code = 0;
    result.should_exit = 0;

    if (pl->count == 0) {
        return result;
    }
    // if (pl->pipelines[pl->count - 1]->in_background) {
    //     /// run in background
    //     return result;
    // }
    for(ll i = 0; i < pl->count; i++) {
        if (pl->pipelines[i]->prev_wait == 1 && result.exit_code != 0) {
            /// wait zero, but received nonzero
            continue;
        }
        if (pl->pipelines[i]->prev_wait == 2 && result.exit_code == 0) {
            /// wait nonzero, but received zero
            continue;
        }
        result = execute_pipeline(pl, i);
    }
    return result;
}
