#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ALPHABET_LEN 26

/*
 * Counts the number of occurrences of each letter (case insensitive) in a text
 * file and stores the results in an array.
 * file_name: The name of the text file in which to count letter occurrences
 * counts: An array of integers storing the number of occurrences of each letter.
 *     counts[0] is the number of 'a' or 'A' characters, counts [1] is the number
 *     of 'b' or 'B' characters, and so on.
 * Returns 0 on success or -1 on error.
 */
int count_letters(const char *file_name, int *counts) {
    FILE *text_file = fopen(file_name, "r");
    if (text_file == NULL) {
        perror("fopen");
        return -1;
    }

    char letter_c[1]; // Character array of size one, which is where the next read character from file will be stored
    char curr_letter; // Current letter that was read from file, not stored in an array
    int index = 0; // Index of counts to increment based on curr_letter

    while (fread(letter_c, sizeof(char), 1, text_file) > 0) {
        curr_letter = letter_c[0];
        if (isalpha(curr_letter)) { // Checking if curr_letter is in alphabet
            if (isupper(curr_letter)) { // Checking if curr_letter is uppercase. If so, changes to lowercase
                curr_letter = tolower(curr_letter);
            }
            index = curr_letter - 97; // Since 'a' is 97, this will cause 'a' to be index 0, 'b' to be index 1, and so on
            *(counts + index) += 1;
        }
    }
    if (ferror(text_file)) {
        fclose(text_file);
        fprintf(stderr, "fread\n");
        return -1;
    }

    if (fclose(text_file) == EOF) {
        perror("fclose");
        return -1;
    }

    return 0;
}

/*
 * Processes a particular file(counting occurrences of each letter)
 *     and writes the results to a file descriptor.
 * This function should be called in child processes.
 * file_name: The name of the file to analyze.
 * out_fd: The file descriptor to which results are written
 * Returns 0 on success or -1 on error
 */
int process_file(const char *file_name, int out_fd) {
    int *counts = malloc(ALPHABET_LEN * sizeof(int));
    if (counts == NULL) {
        fprintf(stderr, "malloc\n");
        return -1;
    }

    for (int i = 0; i < ALPHABET_LEN; i++) {    // Initialize counts array to all zeros
        counts[i] = 0;
    }

    if (count_letters(file_name, counts) == -1) {
        free(counts);
        return -1;
    }
    if (write(out_fd, counts, ALPHABET_LEN * sizeof(int)) ==
            -1) {    // Writing letter counts to pipe
            perror("write");
            free(counts);
            return -1;
    }

    free(counts);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        // No files to consume, return immediately
        return 0;
    }

    // Create a pipe for child processes to write their results
    int fds[2];
    int pipe_result = pipe(fds);
    if (pipe_result < 0) {
        perror("pipe");
        return 1;
    }

    // Fork a child to analyze each specified file (names are argv[1], argv[2], ...)
    int num_files = argc - 1;
    for (int i = 0; i < num_files; i++) {
        pid_t child_pid = fork();
        if (child_pid == -1) {
            perror("fork");
            close(fds[0]);
            close(fds[1]);
            return 1;
        } else if (child_pid == 0) {
            // Close read end of pipe
            if (close(fds[0]) == -1) {
                perror("close");
                close(fds[1]);
                exit(1);
            }

            if (process_file(argv[i + 1], fds[1]) == -1) {
                close(fds[1]);
                exit(1);
            }

            // Close write end of pipe
            if (close(fds[1]) == -1) {
                perror("close");
                exit(1);
            }

            exit(0);
        }
    }

    if (close(fds[1]) == -1) { // Close write end of pipe
        perror("close");
        close(fds[0]);
        return 1;
    }

    // Aggregate all the results together by reading from the pipe in the parent
    int *counts = malloc(ALPHABET_LEN * sizeof(int));
    if (counts == NULL) {
        fprintf(stderr, "malloc\n");
        close(fds[0]);
        return 1;
    }

    memset(counts, 0, ALPHABET_LEN * sizeof(int));

    int *curr_counts = malloc(ALPHABET_LEN * sizeof(int));
    if (curr_counts == NULL) {
        fprintf(stderr, "malloc\n");
        close(fds[0]);
        free(counts);
        return 1;
    }

    memset(curr_counts, 0, ALPHABET_LEN * sizeof(int));

    int eof = 0;
    int num_bytes_read = 0;
    while (eof == 0) {
        num_bytes_read =
            read(fds[0], curr_counts, ALPHABET_LEN * sizeof(int));    // Read next counts array from pipe
        if (num_bytes_read < 0) {                               // Error occurred
            perror("read");
            close(fds[0]);
            free(counts);
            free(curr_counts);
            return 1;
        } else if (num_bytes_read == 0) {    // Reached end of file
            eof = 1;
        } else {
            for (int i = 0; i < ALPHABET_LEN; i++) {
                *(counts + i) += *(curr_counts + i);
            }
        }
    }

    if (close(fds[0]) == -1) { // Close read end of pipe
        perror("close");
        free(counts);
        free(curr_counts);
        return 1;
    }

    int status;
    int ret_val = 0;
    for (int i = 0; i < num_files; i++) {    // Check exit status of all children
        if (wait(&status) == -1) {
            perror("wait");
            return 1;
        }
        if (WEXITSTATUS(status) != 0) {
            ret_val = 1;
        }
    }

    // Prints out the total count of each letter (case insensitive)
    for (int i = 0; i < ALPHABET_LEN; i++) {
        printf("%c Count: %d\n", 'a' + i, counts[i]);
    }

    free(counts);
    free(curr_counts);
    return ret_val;
}
