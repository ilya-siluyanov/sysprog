#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "array.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef struct read_raw_result {
    char *value;
    int exit_code;
} read_raw_result;

read_raw_result *read_raw() {
    ll CWD_SIZE = 150;
    char *cwd = calloc(CWD_SIZE + 1, sizeof(char));
    getcwd(cwd, CWD_SIZE);
    read_raw_result *r = malloc(sizeof(read_raw_result));
    r->value = NULL;
    r->exit_code = 1;

    // printf("\033[0;32m%s\033[0m > ", cwd);
    free(cwd);
    array *chars = arr_new();
    int c;
    int should_exit = 1;
    int prev_slash = 0;
    char literal = '\0';
    int eof = 0;
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
                    eof = 1;
                    break;
                }
                prev_slash = 0;
                char *last_char_p = chars->buf[arr_len(chars) - 1];
                if (*last_char_p == '\\') {
                    free(arr_pop(chars));
                    continue;
                }
        }
        if (eof)
            break;
        prev_slash = 0;
        if (x == '\\') {
            prev_slash = 1;
        }
        char *x_p = malloc(sizeof(char));
        x_p[0] = x;
        arr_push_back(chars, x_p);
    }

    if (should_exit) {
        arr_free(chars);
        return r;
    }

    if (arr_len(chars) == 0) {
        arr_free(chars);
        r->exit_code = 2;
        return r;
    }

    r->value = calloc(arr_len(chars) + 1, sizeof(char));
    for (ll i = 0; i < arr_len(chars); i++) {
        char x = *((char *) chars->buf[i]);
        r->value[i] = x;
        free(chars->buf[i]);

    }
    arr_free(chars);

    return r;
}


struct cmd {
    char **argv;
    ll argc;

    char *redirect_to;
    int o_append;
};


typedef struct complete_cmd {
    struct cmd **cmdv;
    ll cmdc;
} complete_cmd;


struct cmd *parse_command(char const *cmd, ll cmd_len, ll *i) {
    char *token = calloc(cmd_len + 1, sizeof(char));
    ll token_len = 0;

    array *argv = arr_new();
    int o_append = 0;
    char *redirect_to = NULL;

    /// one of '\0', '\'', '"'
    char literal = '\0';
    int target_redirect = 0;
    int comment = 0;

    while (*i < cmd_len) {
        if (comment) {
            if (cmd[*i] == '\n')
                comment = 0;
            (*i)++;
            continue;
        }
        switch (cmd[*i]) {
            case '#':
                comment = 1;
                continue;

            case '\\':
                if (*i + 1 > cmd_len) {
                    printf("No char after \\\n: char %lld", *i + 1);
                    return NULL;
                }
                char char_to_add = cmd[++(*i)];
                if (cmd[*i] == 'n') {
                    /// \n found
                    char_to_add = '\n';
                }
                if (cmd[*i] == 't') {
                    /// \t found
                    char_to_add = '\t';
                }
                (*i)++;
                token[token_len++] = char_to_add;
                continue;

            case '"':
            case '\'':
                /// start of finish string token
                if (literal == '\0') {
                    literal = cmd[(*i)++];
                    break;
                }
                /// "a'bc", 'a"bcd'
                if (literal != cmd[*i]) {
                    token[token_len++] = cmd[(*i)++];
                    break;
                }
                /// "abc", 'abc'
                literal = '\0';
                (*i)++;
                break;

            case '>':
                if (literal) {
                    token[token_len++] = cmd[(*i)++];
                    break;
                }
                if (*i + 1 > cmd_len) {
                    printf("End of command found: char %lld\n", *i);
                    return NULL;
                }
                if (token_len > 0) {
                    char *tok = strdup(token);
                    arr_push_back(argv, tok);
                    free(token);
                    token = calloc(cmd_len + 1, sizeof(char));
                    token_len = 0;
                }
                target_redirect = 1;
                char next_tok = cmd[*i + 1];
                if (next_tok == '>') {
                    /// append
                    o_append = 1;
                    (*i)++;
                }
                (*i)++;
                break;

            case ' ':
                if (literal) {
                    token[token_len++] = cmd[(*i)++];
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
            default:
                token[token_len++] = cmd[*i];
                (*i)++;
        }
    }

    if (!target_redirect && token_len > 0) {
        arr_push_back(argv, strdup(token));
    }

    if (arr_len(argv) == 0) {
        return NULL;
    }
    arr_push_back(argv, NULL);
    struct cmd *result = malloc(sizeof(struct cmd));
    result->argc = arr_len(argv) - 1;
    result->argv = (char **) argv->buf;
    result->redirect_to = redirect_to;
    if (target_redirect) {
        result->redirect_to = strdup(token);
        result->o_append = o_append;
    }
    free(argv);
    free(token);
    return result;
}


complete_cmd *parse_complete(char *raw_cmd) {
    char *raw_part = strtok(raw_cmd, "|");

    array *cmdv = arr_new();
    while (raw_part != NULL) {
        ll i = 0;
        struct cmd *cmd = parse_command(raw_part, strlen(raw_part), &i);
        if (cmd != NULL)
            arr_push_back(cmdv, cmd);
        raw_part = strtok(NULL, "|");
    }

    if (arr_len(cmdv) == 0) {
        return NULL;
    }

    complete_cmd *complete_cmd = malloc(sizeof(complete_cmd));
    complete_cmd->cmdv = (struct cmd **) cmdv->buf;
    complete_cmd->cmdc = arr_len(cmdv);
    free(cmdv);

    return complete_cmd;
}

pid_t run_process(struct cmd *cmd, int fd[][2], ll i, ll n) {
    if (strcmp(cmd->argv[0], "cd") == 0) {
        chdir(cmd->argv[1]);
        return 0;
    }
    int child_pid = fork();
    if (child_pid == 0) {
        dup2(fd[i - 1][0], STDIN_FILENO);
        dup2(fd[i][1], STDOUT_FILENO);
        for(ll j = 0; j < n + 1;j++) {
            close(fd[j][0]);
            close(fd[j][1]);
        }
        if (cmd->redirect_to != NULL) {
            int flags = O_CREAT | O_WRONLY;
            if (cmd->o_append)
                flags |= O_APPEND;
            else
                flags |= O_TRUNC;
            int fd = open(cmd->redirect_to, flags, S_IRWXU);
            dup2(fd, STDOUT_FILENO);
        }
        execvp(strdup(cmd->argv[0]), cmd->argv);
    }
    return child_pid;
}

int execute_complete(complete_cmd *complete_cmd) {
    ll n = complete_cmd->cmdc;
    int fds[n + 1][2];
    pid_t pids[n];

    for (ll i = 0; i < n + 1; i++) {
        pipe(fds[i]);
    }
    dup2(STDIN_FILENO, fds[0][0]);
    dup2(STDOUT_FILENO, fds[n][1]);


    for (ll i = 0; i < complete_cmd->cmdc; i++) {
        struct cmd *cmd = complete_cmd->cmdv[i];
        pids[i] = run_process(cmd, fds, i + 1, n);
    }
    for(ll i = 0; i < n + 1;i++) {
        close(fds[i][0]);
        close(fds[i][1]);
    }

    for(ll i = 0; i < n - 1;i++) {
        waitpid(pids[i], NULL, 0);
    }

    int exit_code = -1;
    int status;
    waitpid(pids[n - 1], &status, 0);
    struct cmd *last_cmd = complete_cmd->cmdv[n - 1];
    if (last_cmd->argc == 1 && strcmp(last_cmd->argv[0], "exit") == 0) {
        exit_code = 0;
        if (last_cmd->argc > 1) {
            char *_;
            exit_code = strtol(last_cmd->argv[1], &_, 10);
        }
    }

    for(ll i = 0; i < complete_cmd->cmdc;i++) {
        struct cmd *cmd = complete_cmd->cmdv[i];
        for(ll j = 0; j < cmd->argc;j++) {
            free(cmd->argv[j]);
        }
        free(cmd->argv);
        if (cmd->redirect_to) {
            free(cmd->redirect_to);
        }
        free(cmd);
    }
    free(complete_cmd->cmdv);
    free(complete_cmd);

    return exit_code;
}
