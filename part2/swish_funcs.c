#include "swish_funcs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>    //Added string.h for strcmp
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "string_vector.h"

#define MAX_ARGS 10

/*
 * Helper function to run a single command within a pipeline. You should make
 * make use of the provided 'run_command' function here.
 * tokens: String vector containing the tokens representing the command to be
 * executed, possible redirection, and the command's arguments.
 * pipes: An array of pipe file descriptors.
 * n_pipes: Length of the 'pipes' array
 * in_idx: Index of the file descriptor in the array from which the program
 *         should read its input, or -1 if input should not be read from a pipe.
 * out_idx: Index of the file descriptor in the array to which the program
 *          should write its output, or -1 if output should not be written to
 *          a pipe.
 * Returns 0 on success or -1 on error.
 */
int run_piped_command(strvec_t *tokens, int *pipes, int n_pipes, int in_idx, int out_idx) {
    // TODO Complete this function's implementation
    // TODO: error check

    // Close unused pipe ends
    for (int i = 0; i < 2 * n_pipes; i++) {
        if (i != in_idx && i != out_idx) {
            if (close(pipes[i]) == -1) {
                perror("close");
                for (int k = i + 1; k < 2 * n_pipes; k++) { // close pipe ends that have not been attempted to close yet
                    if (k != in_idx && i != out_idx) {
                        close(pipes[k]);
                    }
                }
                close(pipes[in_idx]);
                close(pipes[out_idx]);
                return -1;
            }
        }
    }

    if (in_idx != -1) {
        if (dup2(pipes[in_idx], STDIN_FILENO) == -1) { // redirect input
            perror("dup2");
            close(pipes[in_idx]);
            close(pipes[out_idx]);
            return -1;
        }
        if (close(pipes[in_idx]) == -1) {
            perror("close");
            close(pipes[out_idx]);
            return -1;
        }
    }

    if (out_idx != -1) {
        if (dup2(pipes[out_idx], STDOUT_FILENO) == -1) { // redirect output
            perror("dup2");
            close(pipes[out_idx]);
            return -1;
        }
        if (close(pipes[out_idx]) == -1) {
            perror("close");
            return -1;
        }
    }

    if (run_command(tokens) == -1) {
        return -1;
    }

    return 0;
}

int tokens_to_commands(strvec_t *tokens, strvec_t ***commands_list_out) { // TODO: add docstring
    int num_pipes = strvec_num_occurrences(tokens, "|");
    int num_cmds = num_pipes + 1;

    strvec_t **commands_list = malloc(num_cmds * sizeof(strvec_t *));
    if (commands_list == NULL) {
        fprintf(stderr, "malloc\n");
        return -1;
    }

    int start = 0;
    int j = 0;                                     // j indexes command string vectors
    for (int i = 0; i <= tokens->length; i++) {    // i indexes strvec
        if (i == tokens->length || strcmp(tokens->data[i], "|") == 0) {
            commands_list[j] = malloc(sizeof(strvec_t));
            if (commands_list[j] == NULL) {
                // Error handling
                fprintf(stderr, "malloc\n");
                for (int k = 0; k < j; k++) {
                    strvec_clear(commands_list[k]);
                    free(commands_list[k]);
                }
                free(commands_list);
                return -1;
            }
            if (strvec_slice(tokens, commands_list[j], start, i) == -1) {
                // Error handling
                fprintf(stderr, "strvec_slice\n");
                for (int k = 0; k < j; k++) {
                    strvec_clear(commands_list[k]);
                    free(commands_list[k]);
                }
                free(commands_list);
                return -1;
            }

            j++;
            start = i + 1;
        }
    }

    *commands_list_out = commands_list;

    return 0;
}

void free_commands_list(strvec_t **commands_list, int num_elements) { // TODO: add docstring
    for (int i = 0; i < num_elements; i++) {
        strvec_clear(commands_list[i]);
        free(commands_list[i]);
    }
    free(commands_list);
}

int run_pipelined_commands(strvec_t *tokens) { // TODO: add docstring
    // // TODO Complete this function's implementation

    // splice the command vectors into an array of strvec_t pointers
    strvec_t **commands_vector;
    if (tokens_to_commands(tokens, &commands_vector) == -1) {
        // error message already printed in tokens_to_commands
        return -1;
    }

    // Create an array of pipes
    int num_pipes = strvec_num_occurrences(tokens, "|");
    int *pipe_fds = malloc(2 * num_pipes * sizeof(int));
    if (pipe_fds == NULL) {
        fprintf(stderr, "malloc\n");
        return -1;
    }

    /* TESTING STRVEC_T slicing
    for (int i = 0; i <= num_pipes; i++) {
        for (int j = 0; j < commands_vector[i]->length; j++) {
            printf("%s ", commands_vector[i]->data[j]);
        }
        printf("\n");
    } */

    // Set up pipes
    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipe_fds + 2 * i) == -1) {
            perror("pipe");
            for (int k = 0; k < i; k++) {
                close(pipe_fds[2 * k]);
                close(pipe_fds[(2 * k) + 1]);
            }
            return -1;
        }
    }

    // Create child process for each file
    for (int i = 0; i <= num_pipes; i++) {
        pid_t childPid = fork();
        if (childPid == -1) { // fork error occurred
            // error handle
            perror("fork");
            for (int k = 0; k < num_pipes; k++) {
                close(pipe_fds[2 * k]);
                close(pipe_fds[(2 * k) + 1]);
            }
            return -1;
        } else if (childPid == 0) {
            if (i == 0) {
                // first command
                if (run_piped_command(commands_vector[i], pipe_fds, num_pipes, -1, (2 * i) + 1) == -1) {
                    // error message will print in run_piped_command and pipe ends will already be closed
                    exit(1);
                }
            } else if (i == num_pipes) {
                // last command
                if (run_piped_command(commands_vector[i], pipe_fds, num_pipes, 2 * (i - 1), -1) == -1) {
                    exit(1);
                }
            } else {
                // normal command such that input is i-1 and output is i+1
                if (run_piped_command(commands_vector[i], pipe_fds, num_pipes, 2 * (i - 1), (2 * i) + 1) == -1) {
                    exit(1);
                }
            }

            exit(0);
        }
    }

    // Close all pipes in the parent (output goes to terminal, not back to parent)
    for (int i = 0; i < 2 * num_pipes; i++) {
        if (close(pipe_fds[i]) == -1) {
            perror("close");
            for (int k = i + 1; k < 2 * num_pipes; k++) {
                close(pipe_fds[k]);
            }
            return -1;
        }
    }
    // Wait for all processes to terminate and then free the pipes
    /*for (int i = 0; i <= num_pipes; i++) {
        wait(NULL); // ned to get child exit statuses here, not just wait NULL
    }*/

    // Wait for all processes to terminate and then free the pipes
    int status;
    int ret_val = 0;
    for (int i = 0; i <= num_pipes; i++) {    // Check exit status of all children
        if (wait(&status) == -1) {
            fprintf(stderr, "wait\n");
            return 1;
        }
        if (WEXITSTATUS(status) != 0) {
            ret_val = 1;
        }
    }

    free(pipe_fds);
    free_commands_list(commands_vector, num_pipes + 1);

    // Tokenize --> find number of slices and save a vector of tokens
    //  Create pipes
    //  fork() a bunch
    //  run_piped_command in each thing
    //  if error --> clean up everything

    return ret_val;
}
