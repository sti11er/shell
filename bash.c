#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

enum {
    KIND_SIMPLE,
    KIND_PIPELINE,
    KIND_SEQ1, 
    KIND_SEQ2, 
    KIND_REDIRECT, 
    OP_SEQ, 
    OP_CONJUNCT, 
    OP_BACKGROUND, 
    OP_DISJUNCT, 
    RD_OUTPUT, 
    RD_INPUT, 
    RD_APPEND,
};

typedef struct Command {
    int kind;
    union {
        struct {
            int argc;
            char ** argv;
        };
        struct {
            int pipeline_size;
            struct Command * pipeline_commands;
        };
        struct {
            int seq_size;
            struct Command * seq_commands;
            int * seq_operations;
        };
        struct {
            int rd_mode;
            char * rd_path;
            struct Command * rd_command;
        };
    };
} Command;

typedef struct state_t {
    int pid;
    int status;
} state_t;

state_t
run_command(Command *);

state_t 
seq1(Command *c) {
    state_t state; int status;
    for (int i = 0; i < c->seq_size; ++i) {
        switch (c->seq_operations[i]) {
            case OP_SEQ: {
                state = run_command(c->seq_commands + i);
                if (state.status == -1) {
                    waitpid(state.pid, &status, 0);
                    state.status = ((WEXITSTATUS(status) == 0) && WIFEXITED(status));
                }
                break;
            }
            case OP_BACKGROUND: {
                state = run_command(c->seq_commands + i);
                state.status = 1;
                break;
            }
            default:
                break;
            }
    }

    // while (wait(NULL) != -1);
    return state;
}

state_t 
seq2(Command *c) {
    state_t state; int status;
    state = run_command(c->seq_commands);
    // printf("-> %d\n", state.status);
    if (state.status == -1) {
        waitpid(state.pid, &status, 0);
        state.status = ((WEXITSTATUS(status) == 0) && WIFEXITED(status));
    }

    for (int i = 1; i < c->seq_size; ++i) {
        switch (c->seq_operations[i-1]) {
            case OP_CONJUNCT: {
                if (state.status == 1) {
                    state = run_command(c->seq_commands + i);
                    if (state.status == -1) {
                        waitpid(state.pid, &status, 0);
                        state.status = ((WEXITSTATUS(status) == 0) && WIFEXITED(status));
                    }
                }
                break;
            }
            case OP_DISJUNCT: {
                if (state.status == 0) {
                    state = run_command(c->seq_commands + i);
                    if (state.status == -1) {
                        waitpid(state.pid, &status, 0);
                        state.status = ((WEXITSTATUS(status) == 0) && WIFEXITED(status));
                    }
                }
                break;
            }
            default: {
                break;
            }
        }
    }
    // while (wait(NULL) != -1);
    return state;
}


state_t
pipeline(Command *);

int 
simple(Command *c) {
    int pid;
    if ((pid = fork()) == 0) {
        execvp(c->argv[0], c->argv);
        exit(1);
    }
    
    return pid;
}

state_t
redirect(Command * c) {
    state_t state;
    int buffer = 0, fd, mode = 0;
    switch (c->rd_mode) {
        case RD_INPUT: {
            buffer = dup(0);
            fd = open(c->rd_path, O_RDONLY);
            dup2(fd, 0);
            mode = 0;
            close(fd);
            break;
        }
        case RD_OUTPUT: {
            buffer = dup(1);
            fd = open(c->rd_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            dup2(fd, 1);
            mode = 1;
            close(fd);
            break;
        }
        case RD_APPEND: {
            buffer = dup(1);
            fd = open(c->rd_path, O_WRONLY | O_CREAT | O_APPEND, 0666);
            dup2(fd, 1);
            mode = 1;
            close(fd);
            break;
        }
        default: {
            break;
        }
    }
    state = run_command(c->rd_command);
    dup2(buffer, mode);
    return state;
}

state_t
run_command(Command *c) {
    state_t state;
    switch (c->kind) {
        case KIND_SIMPLE: {
            state.pid = simple(c);
            state.status = -1;
            break;
        }
        case KIND_SEQ1: {
            state = seq1(c);
            break;
        }
        case KIND_SEQ2: {
            state = seq2(c);
            break;
        }
        case KIND_PIPELINE: {
            state = pipeline(c);
            break;
        }
        case KIND_REDIRECT: {
            state = redirect(c);
            break;
        }         
        default: {
            break;
        }   
    }
    return state;
}

state_t
pipeline(Command * c) {
    int fd[2], prev = 0,
    init_in = dup(0), init_out = dup(1);
    state_t state;

    for (int i = 0; i < c->pipeline_size; ++i) {
        if (i != 0) {
            dup2(prev, 0);
            close(prev);
        }

        if (i == c->pipeline_size-1) {
            dup2(init_out, 1);
        } else {
            pipe(fd);
            dup2(fd[1], 1);
            close(fd[1]);
        }

        state = run_command(c->pipeline_commands + i);
        prev = fd[0];
    }
    dup2(init_in, 0);
    if (prev == 0) {
        close(prev);
    }
    return state;
}

int
main(void)
{
    // command "uname"
    Command c1_1_1 = {
        .kind = KIND_SIMPLE,
        .argc = 1,
        .argv = (char *[]){"uname", 0}
    };
    Command c1_1 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 1,
        .pipeline_commands = &c1_1_1,
    };
    run_command(&c1_1);

    // command "echo 1 2 3 > file && wc < file &"

    Command c2_1_1_1 = {
        .kind = KIND_SIMPLE,
        .argc = 4,
        .argv = (char *[]) {"echo", "1", "2", "3", 0}
    };
    Command c2_1_1 = {
        .kind = KIND_REDIRECT,
        .rd_mode = RD_OUTPUT,
        .rd_path = "file",
        .rd_command = &c2_1_1_1
    };
    Command c2_1 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 1,
        .pipeline_commands = &c2_1_1,
    };
    Command c2_2_1_1 = {
        .kind = KIND_SIMPLE,
        .argc = 1,
        .argv = (char *[]) {"wc", 0},
    };
    Command c2_2_1 = {
        .kind = KIND_REDIRECT,
        .rd_mode = RD_INPUT,
        .rd_path = "file",
        .rd_command = &c2_2_1_1,
    };
    Command c2_2 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 1,
        .pipeline_commands = &c2_2_1,
    };
    Command c2 = {
        .kind = KIND_SEQ2,
        .seq_size = 2,
        .seq_commands = (Command []){c2_1, c2_2},
        .seq_operations = (int []){OP_CONJUNCT},
    };
    Command c2_0 = {
        .kind = KIND_SEQ1,
        .seq_size = 1,
        .seq_commands = &c2,
        .seq_operations = (int []){OP_BACKGROUND},
    };
    run_command(&c2_0);

    // command "echo 1 2 3 | wc"

    Command c3 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 2,
        .pipeline_commands = (Command []) {c2_1_1_1, c2_2_1_1}
    };
    run_command(&c3);

    // command "echo 1 >> file || echo 2 >> file && cat file"
    Command c4_1_1_1 = {
        .kind = KIND_SIMPLE,
        .argc = 2,
        .argv = (char *[]){"echo", "1", 0},
    };
    Command c4_1_1 = {
        .kind = KIND_REDIRECT,
        .rd_mode = RD_APPEND,
        .rd_path = "file",
        .rd_command = &c4_1_1_1,
    };
    Command c4_1 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 1,
        .pipeline_commands = &c4_1_1,
    };
    Command c4_2_1_1 = {
        .kind = KIND_SIMPLE,
        .argc = 2,
        .argv = (char *[]){"echo", "2", 0},
    };
    Command c4_2_1 = {
        .kind = KIND_REDIRECT,
        .rd_mode = RD_APPEND,
        .rd_path = "file",
        .rd_command = &c4_2_1_1,
    };
    Command c4_2 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 1,
        .pipeline_commands = &c4_2_1,
    };
    Command c4_3_1 = {
        .kind = KIND_SIMPLE,
        .argc = 2,
        .argv = (char *[]){"cat", "file", 0},
    };
    Command c4_3 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 1,
        .pipeline_commands = &c4_3_1,
    };
    Command c4 = {
        .kind = KIND_SEQ2,
        .seq_size = 3,
        .seq_commands = (Command []) {c4_1, c4_2, c4_3},
        .seq_operations = (int []){OP_DISJUNCT, OP_CONJUNCT},
    };
    run_command(&c4);

    // command "echo 1 2 3 | wc > file; cat file"
    Command c5_1_1 = {
        .kind = KIND_REDIRECT,
        .rd_mode = RD_OUTPUT,
        .rd_path = "file",
        .rd_command = &c2_2_1_1,
    };
    Command c5_1 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 2,
        .pipeline_commands = (Command []) {c2_1_1_1, c5_1_1},
    };
    Command c5 = {
        .kind = KIND_SEQ1,
        .seq_size = 2,
        .seq_commands = (Command []) {c5_1, c4_3},
        .seq_operations = (int []){OP_SEQ, OP_SEQ},
    };
    run_command(&c5);

    // command "echo 1 || (echo 2 && echo 3)"
    Command c6_1 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 1,
        .pipeline_commands = &c4_1_1_1,
    };
    Command c6_2_1_1 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 1,
        .pipeline_commands = &c4_2_1_1,
    };
    Command c6_2_1_2_1 = {
        .kind = KIND_SIMPLE,
        .argc = 2,
        .argv = (char *[]) {"echo", "3", 0},
    };
    Command c6_2_1_2 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 1,
        .pipeline_commands = &c6_2_1_2_1,
    };
    Command c6_2_1 = {
        .kind = KIND_SEQ2,
        .seq_size = 2,
        .seq_commands = (Command []) {c6_2_1_1, c6_2_1_2},
        .seq_operations = (int []) {OP_CONJUNCT},
    };
    Command c6_2 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 1,
        .pipeline_commands = &c6_2_1,   
    };
    Command c6 = {
        .kind = KIND_SEQ2,
        .seq_size = 2,
        .seq_commands = (Command []) {c6_1, c6_2},
        .seq_operations = (int []) {OP_DISJUNCT},
    };
    run_command(&c6);

    // command "yes | head"
    Command c7_1 = {
        .kind = KIND_SIMPLE,
        .argc = 1,
        .argv = (char *[]) {"yes", 0},
    };
    Command c7_2 = {
        .kind = KIND_SIMPLE,
        .argc = 1,
        .argv = (char *[]) {"head", 0},
    };
    Command c7 = {
        .kind = KIND_PIPELINE,
        .pipeline_size = 2,
        .pipeline_commands = (Command []) {c7_1, c7_2},
    };
    run_command(&c7);
}