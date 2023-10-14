#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_COMMAND_LINE_LEN 1024
#define MAX_COMMAND_LINE_ARGS 128

char prompt[MAX_COMMAND_LINE_LEN];
char delimiters[] = " \t\r\n";

extern char **environ;

void update_prompt() {
    char current_directory[MAX_COMMAND_LINE_LEN];
    if (getcwd(current_directory, sizeof(current_directory)) != NULL) {
        snprintf(prompt, sizeof(prompt), "%s> ", current_directory);
    } else {
        perror("getcwd");
        exit(EXIT_FAILURE);
    }
}

void handle_cd(char *directory) {
    if (directory == NULL) {
        // Handle 'cd' without arguments (change to home directory)
        chdir(getenv("HOME"));
    } else if (chdir(directory) != 0) {
        perror("chdir");
    }
}

void handle_pwd() {
    char current_directory[MAX_COMMAND_LINE_LEN];
    if (getcwd(current_directory, sizeof(current_directory)) != NULL) {
        printf("%s\n", current_directory);
    } else {
        perror("getcwd");
    }
}

void handle_echo(char *arguments[]) {
    int i = 1; // Start from 1 to skip the "echo" command itself
    while (arguments[i] != NULL) {
        printf("%s ", arguments[i]);
        i++;
    }
    printf("\n");
}

void handle_env() {
    for (char **env = environ; *env != NULL; env++) {
        printf("%s\n", *env);
    }
}

void handle_setenv(char *name, char *value) {
    if (setenv(name, value, 1) != 0) {
        perror("setenv");
    }
}

// Signal handler for SIGINT (Ctrl-C)
void sigint_handler(int signum) {
    // Do nothing on SIGINT, return to the prompt
    printf("\n");
    update_prompt();
}

// Signal handler for SIGALRM
void sigalrm_handler(int signum) {
    // Terminate the process on alarm signal
    printf("\nProcess terminated due to timeout.\n");
    exit(EXIT_SUCCESS);
}

int main() {
    // Install the signal handlers
    if (signal(SIGINT, sigint_handler) == SIG_ERR || signal(SIGALRM, sigalrm_handler) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    char command_line[MAX_COMMAND_LINE_LEN];
    char *arguments[MAX_COMMAND_LINE_ARGS];

    while (true) {
        update_prompt();
        printf("%s", prompt);
        fflush(stdout);

        if (fgets(command_line, MAX_COMMAND_LINE_LEN, stdin) == NULL && ferror(stdin)) {
            fprintf(stderr, "fgets error");
            exit(EXIT_FAILURE);
        }

        // Tokenize the command line input
        char *token = strtok(command_line, delimiters);
        int arg_count = 0;

        while (token != NULL && arg_count < MAX_COMMAND_LINE_ARGS) {
            arguments[arg_count] = token;
            token = strtok(NULL, delimiters);
            arg_count++;
        }

        arguments[arg_count] = NULL; // Null-terminate the array

        // Check for background process
        int background = 0;
        if (arg_count > 0 && strcmp(arguments[arg_count - 1], "&") == 0) {
            background = 1;
            arguments[arg_count - 1] = NULL; // Remove the "&" from arguments
        }

        // Handle I/O Redirection and Piping
        int input_fd = STDIN_FILENO;
        int output_fd = STDOUT_FILENO;
        int pipe_fd[2] = {0};

        for (int i = 0; i < arg_count; ++i) {
            if (strcmp(arguments[i], "<") == 0) {
                // Input Redirection
                if (i < arg_count - 1) {
                    input_fd = open(arguments[i + 1], O_RDONLY);
                    if (input_fd == -1) {
                        perror("open");
                        exit(EXIT_FAILURE);
                    }
                }
            } else if (strcmp(arguments[i], ">") == 0) {
                // Output Redirection
                if (i < arg_count - 1) {
                    output_fd = open(arguments[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (output_fd == -1) {
                        perror("open");
                        exit(EXIT_FAILURE);
                    }
                }
            } else if (strcmp(arguments[i], "|") == 0) {
                // Pipe
                if (pipe(pipe_fd) == -1) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }

                // Replace standard output with the write end of the pipe
                output_fd = pipe_fd[1];
                // Redirect standard input to read from the pipe
                input_fd = pipe_fd[0];

                // Replace the pipe symbol with NULL in the arguments array
                arguments[i] = NULL;
            }
        }

        // Create a child process to execute the command
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) { // Child process
            // Set an alarm for 10 seconds
            alarm(10);

            // Redirect standard input and output if needed
            if (input_fd != STDIN_FILENO) {
                if (dup2(input_fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(input_fd);
            }

            if (output_fd != STDOUT_FILENO) {
                if (dup2(output_fd, STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(output_fd);
            }

            execvp(arguments[0], arguments);
            // If execvp returns, an error occurred
            perror("execvp");
            exit(EXIT_FAILURE);
        } else { // Parent process
            if (!background) {
                int status;
                if (waitpid(pid, &status, 0) == -1) {
                    perror("waitpid");
                    exit(EXIT_FAILURE);
                }

                // Cancel the alarm for foreground processes
                alarm(0);
            }

            // Close the pipe file descriptors in the parent
            if (pipe_fd[0] != 0) {
                close(pipe_fd[0]);
            }
            if (pipe_fd[1] != 0) {
                close(pipe_fd[1]);
            }
        }
    }

    return 0;
}
