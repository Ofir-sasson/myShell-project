#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/resource.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#define MAX_SIZE 1025
#define MAX_ARGS 7
#define MAX_LIMIT 4

//dangerous command struct
typedef struct {
    int argc; //count how many args
    char **argv;
} DangerousCommand;

// Limit specification struct
typedef struct {
    int resource;         // RLIMIT_*
    rlim_t soft;
    rlim_t hard;
} LimitSpec;

typedef struct {
    int **matrix; //each matrix is stored in an array: [rows, cols, a1, a2, a3...]
    int count_matrix;
    int op; //1 for add, 2 for sub
} McalcMatrix;

typedef struct {
    int *matrix1;
    int *matrix2;
    int *result;
    int op; // 1 = ADD, 2 = SUB
} ThreadData;


void terminal(const char* file_name_dangerous, char* log_name);
void execute_single_command(char *input, FILE *fp, int *cmd_count, double *last_time, double *avg_time, double *min_time, double *max_time, DangerousCommand *dangerous_commands, int dangerous_count, int *dangerous_cmd_blocked, int *unblocked);
int find_len(const char *str);
void print_cmd(int cmd, int dangerous, double time, double avg_time, double min_time, double max_time);
double calc_elapsed_time(struct timespec start, struct timespec end);
void check_min_max(double *min_time, double *max_time, double cur);
int create_args(char ***arg, char *input);
void load_dangerous_commands(const char* filename, DangerousCommand **dangerous_commands, int* dangerous_count);
int is_dangerous(char **arg, const DangerousCommand *dangerous_commands, int dangerous_count, int count);
void free_dangerous_commands(DangerousCommand **dangerous_commands, int dangerous_count);
int check_pipe(const char *input);
bool is_command_blocked(char **args, DangerousCommand *dangerous_commands, int dangerous_count, int count, int *dangerous_cmd_blocked, int *unblocked) ;
void execute_pipe_command(char *input, FILE *fp, int *cmd_count, double *last_time, double *avg_time, double *min_time, double *max_time, DangerousCommand *dangerous_commands, int dangerous_count, int *dangerous_cmd_blocked, int *unblocked);
void trim(char *str) ;
void my_tee(int argc, char **argv);
long convert_with_units(const char *str);
void rlimit(char *input, LimitSpec *lr, int *num_lim);
void create_limit(int *shift, LimitSpec *rl, int *num_lim, char **args, int count);
void show_limit();
int process_rlimit(char *input, LimitSpec *limits, int *num_lim);
bool check_if_background(char *input);
int len(char *str);
void wait_status(int status, FILE *fp, int *cmd_count, double *last_time, double *avg_time, double *min_time, double *max_time, char *input, struct timespec start, struct timespec end);
void matrix_operation(char *input, FILE *fp, int *cmd_count, double *last_time, double *avg_time, double *min_time, double *max_time, DangerousCommand *dangerous_commands, int dangerous_count, int *dangerous_cmd_blocked, int *unblocked);
int count_args_matrix(char *str);
void split_matrix(char *input, McalcMatrix *matrix, int index);
void free_matrix(McalcMatrix *matrix);
void trim_quotes(char *str);
void* matrix_op_thread(void *arg);
void reduce_matrix_tree(McalcMatrix *matrix);


// handlers
void sigchld_handler(int signo) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) { }
    errno = saved_errno;
}

void handle_sigcpu(int signo)
{
    printf("CPU time limit exceeded!\n");
    exit(1);
}

void handle_sigfsz(int signo)
{
    printf("File size limit exceeded!\n");
    exit(1);
}

void handle_sigmem(int signo)
{
    printf("Memory allocation failed!\n");
    exit(1);
}

void handle_signof(int signo)
{
    printf("Too many open files!\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    signal(SIGXCPU  , handle_sigcpu); // cpu
    signal(SIGXFSZ , handle_sigfsz); // files
    signal(SIGSEGV, handle_sigmem); // memory
    signal(SIGUSR1, handle_signof); // open files - using SIGUSR1 as a custom signal

    terminal(argv[1], argv[2]);
    return 0;
}

void terminal(const char* file_name_dangerous, char* log_name) {
    //declarations
    char input[MAX_SIZE];
    char **arg;
    int create = 0;


    int cmd = 0; //count the legal commands
    int dangerous_cmd_blocked = 0; // count dangerous commands that the program blocked
    int unblocked = 0;
    double last_cmd_time = 0, min_time = 0, max_time = 0, avg_time = 0;

    //array of dangerous commands
    DangerousCommand *dangerous_commands = NULL;
    int dangerous_count = 0;
    load_dangerous_commands(file_name_dangerous, &dangerous_commands, &dangerous_count); //load from the file

    //open the log file in mode append
    FILE *fp = fopen(log_name, "a");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }

    //get the first command
    print_cmd(cmd,dangerous_cmd_blocked,last_cmd_time,avg_time,min_time,max_time);
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = '\0'; //deletes the \n from the end

    //while the user didnt enter 'done'
    while (strcmp(input, "done") != 0) {

        int isPipe = check_pipe(input);
        if (isPipe != -1) {
            execute_pipe_command(input, fp, &cmd, &last_cmd_time, &avg_time, &min_time, &max_time, dangerous_commands,dangerous_count, &dangerous_cmd_blocked, &unblocked);
        }
        else {
            //check if the command is a matrix operation
            if(strncmp(input, "mcalc", 5) == 0) {
                matrix_operation(input, fp, &cmd, &last_cmd_time, &avg_time, &min_time, &max_time, dangerous_commands,dangerous_count, &dangerous_cmd_blocked, &unblocked);
            }
            else {
                execute_single_command(input, fp, &cmd, &last_cmd_time, &avg_time, &min_time, &max_time, dangerous_commands,dangerous_count, &dangerous_cmd_blocked, &unblocked);
            }
            
        }
        print_cmd(cmd, dangerous_cmd_blocked, last_cmd_time, avg_time, min_time, max_time);
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0';
    }
    free_dangerous_commands(&dangerous_commands, dangerous_count);
    fclose(fp);
    printf("%d \n", dangerous_cmd_blocked);

}

void execute_single_command(char *input, FILE *fp, int *cmd_count, double *last_time, double *avg_time, double *min_time, double *max_time, DangerousCommand *dangerous_commands, int dangerous_count, int *dangerous_cmd_blocked, int *unblocked) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    LimitSpec limits[MAX_LIMIT]; int num_lim=0;
    int l = process_rlimit(input, limits, &num_lim);
    if (l == 1) {
        // clock_gettime(CLOCK_MONOTONIC, &end);
        // (*cmd_count)++;
        // *last_time = calc_elapsed_time(start, end);
        // *avg_time = ((*avg_time) * ((*cmd_count) - 1) + *last_time) / (*cmd_count);
        // check_min_max(min_time, max_time, *last_time);
        // fprintf(fp, "%s : %.5f sec\n", input, *last_time);
        // fflush(fp);
        return;
    }

    bool is_background = check_if_background(input);

    char temp[MAX_SIZE];
    strcpy(temp, input);

    char *redir_ptr = strstr(input, "2>");
    char *error_file = NULL;

    if (redir_ptr != NULL && !is_background) {
        redir_ptr += 2; //  after "2>"
        while (*redir_ptr == ' ') redir_ptr++; // skip spaces
        error_file = strdup(redir_ptr); // save name file

        //delete the 2> part
        *(strstr(input, "2>")) = '\0';
    }
    char **arg;
    strcpy(temp, input);
    int count = create_args(&arg, temp);
    if (count > 0) {
        if (!is_command_blocked(arg, dangerous_commands, dangerous_count, count, dangerous_cmd_blocked, unblocked)) {
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                exit(1);
            }
            if (pid == 0) {
                for (int i = 0; i < num_lim; i++) {
                    struct rlimit rl = { limits[i].soft, limits[i].hard };
                    setrlimit(limits[i].resource, &rl);
                }
                //in case there is 2> in command
                if (error_file != NULL) {
                    int err_fd = open(error_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                    if (err_fd >= 0) {
                        dup2(err_fd, STDERR_FILENO);
                        close(err_fd);
                    } else {
                        perror("open error redirection file");
                    }
                }
                execvp(arg[0], arg);
                perror("execvp");
                exit(255);
            }

            int status;
            if (!is_background) {

                bool fsize_limit = false;
                for (int i = 0; i < num_lim; i++) {
                    if (limits[i].resource == RLIMIT_FSIZE) {
                        fsize_limit = true;
                        break;
                    }
                }

                waitpid(pid, &status, 0);
                clock_gettime(CLOCK_MONOTONIC, &end);

                wait_status(status, fp, cmd_count, last_time, avg_time, min_time, max_time, input, start, end);

                if (error_file != NULL) {
                    free(error_file);
                }
            }
        }

        // Free args
        for (int i = 0; i < count; i++) free(arg[i]);
        free(arg);
    }
}

int create_args(char ***arg, char *input) {
    int count = find_len(input); // Count arguments
    //if count == -1 there is an error
    if (count == -1) {
        return -1; // Invalid format (e.g. double space or too many args)
    }

    *arg = malloc((count + 1) * sizeof(char *)); // Allocate space for arguments + NULL
    if (*arg == NULL) {
        perror("malloc");
        return -1;
    }

    char *token = strtok(input, " ");
    int i = 0;
    while (token != NULL && i < count) {
        (*arg)[i] = malloc(strlen(token) + 1);
        if ((*arg)[i] == NULL) {
            perror("malloc");
            // free previously allocated strings
            for (int j = 0; j < i; j++) {
                free((*arg)[j]);
            }
            free(*arg);
            return -1;
        }

        strcpy((*arg)[i], token);
        token = strtok(NULL, " ");
        i++;
    }

    (*arg)[i] = NULL; // NULL termination for execvp
    return count;
}


//count how many args there are in the cmd the user puts
//if there is to many space or to many args returns -1 and print to the user the correct error
int find_len(const char *str) {
    int count = 0;
    int in_word = 0;
    int check = 0;

    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == ' ' && str[i + 1] == ' ') {
            printf("ERR_SPACE\n");
            check = 1;
        }
        if (str[i] != ' ' && in_word == 0) {
            in_word = 1;
            count++;
        } else if (str[i] == ' ') {
            in_word = 0;
        }
    }
    if (count > MAX_ARGS) {
        printf("ERR_ARGS\n");
        return -1;
    }
    if (check == 1) {
        return -1;
    }
    return count;
}

//print the prompt to the user
void print_cmd(int cmd, int dangerous, double time, double avg_time, double min_time, double max_time) {
    printf("#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>>",cmd,dangerous,time,avg_time,min_time,max_time);
}

//calc the time that the command runs
double calc_elapsed_time(struct timespec start, struct timespec end) {
    double sec = end.tv_sec - start.tv_sec;
    double nsec = end.tv_nsec - start.tv_nsec;

    if (nsec < 0) {
        sec -= 1;
        nsec += 1e9;
    }

    return sec + nsec / 1e9;
}

//check if there is a need to change the min or max time for the command
void check_min_max(double *min_time, double *max_time, double cur) {
    if (*min_time == 0 && *max_time == 0) { //if it's the first command min and max are equals
        *min_time = cur;
        *max_time = cur;
    }
    else if (*min_time > cur ) {
        *min_time = cur;
    }
    else if (*max_time < cur) {
        *max_time = cur;
    }
}


//load the dangerous commands file to an array of the struct
void load_dangerous_commands(const char* filename, DangerousCommand **dangerous_commands, int* dangerous_count) {
    //open the file
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) { //failed
        perror("fopen");
        exit(1);
    }

    char line[MAX_SIZE];
    //while there are lines to read in the file
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0'; // Remove newline
        if (strlen(line) == 0) continue;  // Skip empty lines

        DangerousCommand cmd;
        cmd.argc = 0;

        // Allocate space for argv array
        cmd.argv = malloc(MAX_ARGS * sizeof(char *));
        if (cmd.argv == NULL) {
            perror("malloc");
            fclose(fp);
            exit(1);
        }

        // split the line by space
        char *token = strtok(line, " ");
        while (token != NULL && cmd.argc < MAX_ARGS) {
            cmd.argv[cmd.argc] = strdup(token);
            if (cmd.argv[cmd.argc] == NULL) {
                perror("strdup");

                // Free already allocated tokens
                for (int j = 0; j < cmd.argc; j++) {
                    free(cmd.argv[j]);
                }
                free(cmd.argv);
                fclose(fp);
                exit(1);
            }

            cmd.argc++;
            token = strtok(NULL, " ");
        }

        // Expand the dangerous_commands array
        DangerousCommand *tmp = realloc(*dangerous_commands, (*dangerous_count + 1) * sizeof(DangerousCommand));
        if (tmp == NULL) {
            perror("realloc");

            // Free command memory before exiting
            for (int j = 0; j < cmd.argc; j++) {
                free(cmd.argv[j]);
            }
            free(cmd.argv);
            fclose(fp);
            exit(1);
        }

        *dangerous_commands = tmp;
        (*dangerous_commands)[*dangerous_count] = cmd;
        (*dangerous_count)++; //count how many dangerous command there are in the file
    }

    fclose(fp); //close the file
}

//checks if the command the user try to run is a dangerous command
int is_dangerous(char **arg, const DangerousCommand *dangerous_commands, int dangerous_count, int count) {

    int match = 0;
    int best_match = 0;
    for (int i = 0; i < dangerous_count; i++) {
        if (strcmp(dangerous_commands[i].argv[0], arg[0]) == 0) {
            match++;
            if (count == dangerous_commands[i].argc) {
                for (int j = 1; j < count; j++) {
                    if (strcmp(dangerous_commands[i].argv[j], arg[j]) == 0) {
                        match++;
                    }
                }
            }
        }
        if (match > best_match) {
            best_match = match;
        }if (match == dangerous_commands[i].argc) {
            return -1; //complete match
        }
        match = 0;
    }
    return best_match;
}

//free allocated memory from the file that we saved n dangerous command
void free_dangerous_commands(DangerousCommand **dangerous_commands, const int dangerous_count) {
    for (int i = 0; i < dangerous_count; i++) {
        for (int j = 0; j < (*dangerous_commands)[i].argc; j++) {
            free((*dangerous_commands)[i].argv[j]);
        }
        free((*dangerous_commands)[i].argv);
    }
    free(*dangerous_commands);
    *dangerous_commands = NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int check_pipe(const char *input) {
    for (int i = 0; input[i] != '\0'; i++) {
        if (input[i] == '|') {
            return i;
        }
    }
    return -1;
}

bool is_command_blocked(char **args, DangerousCommand *dangerous_commands, int dangerous_count, int count, int *dangerous_cmd_blocked, int *unblocked) {
    int match = is_dangerous(args, dangerous_commands, dangerous_count, count);
    if (match == -1) {
        (*dangerous_cmd_blocked)++;
        printf("ERR: Dangerous command detected. Execution prevented.\n");
        return true; // blocked
    } if (match != 0) {
        (*unblocked)++;
        printf("WARNING: Command similar to dangerous command. Proceed with caution.\n");
    }
    return false; // not blocked
}

void execute_pipe_command(char *input, FILE *fp, int *cmd_count, double *last_time, double *avg_time, double *min_time, double *max_time, DangerousCommand *dangerous_commands, int dangerous_count, int *dangerous_cmd_blocked, int *unblocked) {
    char *cmd1, *cmd2;
    char **argPipe1, **argPipe2;
    char temp[MAX_SIZE];

    struct timespec start1, end1, start2, end2;
    clock_gettime(CLOCK_MONOTONIC, &start1);
    clock_gettime(CLOCK_MONOTONIC, &start2);

    LimitSpec limits1[MAX_LIMIT]; int num_lim1=0;
    LimitSpec limits2[MAX_LIMIT]; int num_lim2=0;

    strcpy(temp,input);

    cmd1 = strtok(temp, "|");
    cmd2 = strtok(NULL, "|");
    if (!cmd2) {
        fprintf(stderr, "ERR: missing command after pipe\n");
    }
    else if (*cmd2 == '\0') {
        fprintf(stderr, "ERR: empty command after pipe\n");
    }
    else {
        char temp1[MAX_SIZE];
        trim(cmd1);
        trim(cmd2);

        int l1 = process_rlimit(cmd1, limits1, &num_lim1);
        int l2 = process_rlimit(cmd2, limits2, &num_lim2);

        if (l1 == 1) {
            clock_gettime(CLOCK_MONOTONIC, &end1);
            (*cmd_count)++;
            *last_time = calc_elapsed_time(start1, end1);
            *avg_time = ((*avg_time) * ((*cmd_count) - 1) + *last_time) / (*cmd_count);
            check_min_max(min_time, max_time, *last_time);
            fprintf(fp, "%s : %.5f sec\n", cmd1, *last_time);
            fflush(fp);
        }
        if (l2 == 1) {
            clock_gettime(CLOCK_MONOTONIC, &end2);
            (*cmd_count)++;
            *last_time = calc_elapsed_time(start2, end2);
            *avg_time = ((*avg_time) * ((*cmd_count) - 1) + *last_time) / (*cmd_count);
            check_min_max(min_time, max_time, *last_time);
            fprintf(fp, "%s : %.5f sec\n", cmd2, *last_time);
            fflush(fp);
        }


        char *redir_ptr = strstr(cmd1, "2>");
        char *error_file1 = NULL;
        char *error_file2 = NULL;

        if (redir_ptr != NULL) {
            redir_ptr += 2; //  after "2>"
            while (*redir_ptr == ' ')
                redir_ptr++; // skip spaces
            error_file1 = strdup(redir_ptr); // save name file

            //delete the 2> part
            *(strstr(input, "2>")) = '\0';
        }
        redir_ptr = strstr(cmd2, "2>");
        if (redir_ptr != NULL) {
            redir_ptr += 2; //  after "2>"
            while (*redir_ptr == ' ')
                redir_ptr++; // skip spaces
            error_file2 = strdup(redir_ptr); // save name file

            //delete the 2> part
            *(strstr(input, "2>")) = '\0';
        }

        strcpy(temp1, cmd1);               // copy cmd1 into temp1
        int check1 = create_args(&argPipe1, temp1);

        strcpy(temp1, cmd2);               // copy cmd2 into temp1
        int check2 = create_args(&argPipe2, temp1);


        if (check1 != -1 && check2 != -1) {
            bool danger1 = is_command_blocked(argPipe1, dangerous_commands, dangerous_count, check1, dangerous_cmd_blocked, unblocked) ;
            bool danger2 = is_command_blocked(argPipe2, dangerous_commands, dangerous_count, check2, dangerous_cmd_blocked, unblocked);
            if (!danger1 && !danger2) {
                //create pipe
                int pipe_fd[2];
                if (pipe(pipe_fd) == -1) {
                    perror("PIPE_ERR");
                }

                //create two sons to execute the pipe
                pid_t pid1 = fork();
                if (pid1 < 0) {
                    perror("fork");
                    exit(1);
                }
                if (pid1 == 0) { //first son - sends the first command
                    dup2(pipe_fd[1], STDOUT_FILENO); //dup to write
                    //close other fd
                    close(pipe_fd[0]);
                    close(pipe_fd[1]);
                    for (int i = 0; i < num_lim1; i++) {
                        struct rlimit rl = { limits1[i].soft, limits1[i].hard };
                        setrlimit(limits1[i].resource, &rl);
                    }
                    //in case there is 2> in command
                    if (error_file1 != NULL) {
                        int err_fd = open(error_file1, O_WRONLY | O_CREAT | O_APPEND, 0644);
                        if (err_fd >= 0) {
                            dup2(err_fd, STDERR_FILENO);
                            close(err_fd);
                        } else {
                            perror("open error redirection file");
                        }
                    }
                    execvp(argPipe1[0], argPipe1); //exec the first command
                    perror("execvp left");
                    exit(255);
                }

                pid_t pid2 = fork();
                if (pid2 < 0) {
                    perror("fork");
                    exit(1);
                }
                if (pid2 == 0) { //second son - read from the first
                    dup2(pipe_fd[0], STDIN_FILENO); //dup to the read
                    //close other fd
                    close(pipe_fd[1]);
                    close(pipe_fd[0]);
                    for (int i = 0; i < num_lim2; i++) {
                        struct rlimit rl = { limits2[i].soft, limits2[i].hard };
                        setrlimit(limits2[i].resource, &rl);
                    }
                    //in case there is 2> in command
                    if (error_file2 != NULL) {
                        int err_fd = open(error_file2, O_WRONLY | O_CREAT | O_APPEND, 0644);
                        if (err_fd >= 0) {
                            dup2(err_fd, STDERR_FILENO);
                            close(err_fd);
                        } else {
                            perror("open error redirection file");
                        }
                    }

                    //check if we need to use my_tee command
                    if (strcmp(argPipe2[0], "my_tee") == 0)  {
                        my_tee(check2, argPipe2);
                        exit(0);
                    }

                    execvp(argPipe2[0], argPipe2); //ecec the second command
                    perror("execvp right");
                    exit(255);
                }

                //father:
                //close fd
                close(pipe_fd[0]);
                close(pipe_fd[1]);

                int status1, status2;
                waitpid(pid1, &status1, 0);
                clock_gettime(CLOCK_MONOTONIC, &end1);
                waitpid(pid2, &status2, 0);
                clock_gettime(CLOCK_MONOTONIC, &end2);

                //check the time of the command
                wait_status(status1, fp, cmd_count, last_time, avg_time, min_time, max_time, cmd1, start1, end1);
                wait_status(status2, fp, cmd_count, last_time, avg_time, min_time, max_time, cmd2, start2, end2);
            }

            if (error_file1) free(error_file1);
            if (error_file2) free(error_file2);

            // Free args
            for (int i = 0; argPipe1[i] != NULL; i++) free(argPipe1[i]);
            free(argPipe1);
            for (int i = 0; argPipe2[i] != NULL; i++) free(argPipe2[i]);
            free(argPipe2);
        }
    }
}


void trim(char *str) {
    char *start = str;
    char *end;

    if (strlen(str) == 1 && str[0] == ' ') {
        str[0] = '\0';
        return;
    }
    // Step 1: move start pointer to the first non-space character
    while (isspace((unsigned char)*start)) {
        start++;
    }

    // Step 2: move end pointer to the last non-space character
    end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }

    // Step 3: write the null terminator after the last non-space character
    *(end + 1) = '\0';

    // Step 4: move the trimmed string to the beginning if needed
    if (start != str) {
        memmove(str, start, strlen(start) + 1); // +1 for '\0'
    }
}

void my_tee(int argc, char **argv) {
    int append = 0;
    int start_index = 1;

    if (argc > 1 && strcmp(argv[1], "-a") == 0) { //check if the user want to open in append
        append = 1;
        start_index = 2;
    }

    int file_count = argc - start_index;
    int fds[file_count];

    // open the files
    for (int i = 0; i < file_count; i++) {
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC); //check if need append flag
        fds[i] = open(argv[start_index + i], flags, 0644);
        if (fds[i] < 0) {
            perror("open");
            // close the files that already opened
            for (int j = 0; j < i; j++) close(fds[j]);
            exit(1);
        }
    }

    //read from stdin and write to stdout an the files
    char buffer[MAX_SIZE];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buffer, MAX_ARGS)) > 0) {
        write(STDOUT_FILENO, buffer, n);
        for (int i = 0; i < file_count; i++) {
            write(fds[i], buffer, n);
        }
    }

    // close files
    for (int i = 0; i < file_count; i++) {
        close(fds[i]);
    }
}

void rlimit(char *input, LimitSpec *rl, int *num_lim) {

    char temp[MAX_SIZE];
    strcpy(temp, input);
    int in_word = 0, count =0;
    char **arg;
    int shift = 1;


    /*  CREATE ARGS TO CHECK WHAT NEEDS TO LIMIT   */
    for (int i = 0; temp[i] != '\0'; i++) {
        if (temp[i] != ' ' && in_word == 0) {
            in_word = 1;
            count++;
        } else if (temp[i] == ' ') {
            in_word = 0;
        }
    }
    arg = malloc((count + 1) * sizeof(char *)); // Allocate space for arguments + NULL
    if (*arg == NULL) {
        perror("malloc");
    }

    char *token = strtok(input, " ");
    int i = 0;
    while (token != NULL && i < count) {
        arg[i] = malloc(strlen(token) + 1);
        if (arg[i] == NULL) {
            perror("malloc");
            // free previously allocated strings
            for (int j = 0; j < i; j++) {
                free(arg[j]);
            }
            free(arg);
        }

        strcpy(arg[i], token);
        token = strtok(NULL, " ");
        i++;
    }

    /*  CHECK IF SHOW OR SET  */
    if (strcmp(arg[1], "show") == 0) {
        shift++;
        show_limit();
    }else if (strcmp(arg[1], "set") == 0) {
        shift++;
        create_limit(&shift, rl, num_lim, arg, count);
    }else {
        printf("ERR");
    }

    /*  FREE ARGS   */
    for (int i = 0; i < count; i++) {
        free(arg[i]);
    }
    free(arg);
}

void show_limit() {
    struct rlimit rl;
    getrlimit(RLIMIT_CPU, &rl);
    printf("CPU time: soft=%llds, hard=%llds\n",
           (long long)rl.rlim_cur, (long long)rl.rlim_max);
    getrlimit(RLIMIT_AS, &rl);
    printf("Memory: soft=%lldB, hard=%lldB\n",
           (long long)rl.rlim_cur, (long long)rl.rlim_max);
    getrlimit(RLIMIT_FSIZE, &rl);
    printf("File size: soft=%lldB, hard=%lldB\n",
           (long long)rl.rlim_cur, (long long)rl.rlim_max);
    getrlimit(RLIMIT_NOFILE, &rl);
    printf("Open files: soft=%lld, hard=%lld\n",
           (long long)rl.rlim_cur, (long long)rl.rlim_max);
}

void create_limit(int *shift, LimitSpec *rl, int *num_lim, char **args, int count) {
    for (int i = 2; i < count; i++) {
        char *resourse_def = args[i];
        char *equal = strchr(resourse_def, '='); //check where the "=" symbol and return a pointer
        if (!equal) {
            *shift = i;
            break;
        }

        *equal = '\0'; //place \0 instead of = in the string -> create two following strings
        char *resourse_name = resourse_def; //place the first substring as the name of the resource
        char *limit_str = equal + 1; //the second substring. contains the limit

        //split the hard limit and soft limit
        char *point = strchr(limit_str, ':');
        long soft , hard;
        if (point) {
            *point = '\0';
            soft = convert_with_units(limit_str);
            hard = convert_with_units(point + 1);
        }else {
            soft = hard = convert_with_units(limit_str); //if there is no ':' the soft and hard limits are equals
        }

        //check the resource and create the limit.
        if (strcmp(resourse_name, "cpu") == 0) {
            rl[*num_lim].resource = RLIMIT_CPU;
        }else if (strcmp(resourse_name, "mem") == 0) {
            rl[*num_lim].resource = RLIMIT_AS;
        }else if (strcmp(resourse_name, "fsize") == 0) {
            rl[*num_lim].resource = RLIMIT_FSIZE;
        }else if (strcmp(resourse_name, "nofile") == 0) {
            rl[*num_lim].resource = RLIMIT_NOFILE;
        }else {
            fprintf(stderr, "ERR: Unsupported resource type: %s\n", resourse_name);
            break;
        }
        rl[*num_lim].soft = soft;
        rl[*num_lim].hard = hard;
        (*num_lim)++;
    }
}

// Convert with units
long convert_with_units(const char *str) {
    char *end;
    long v = strtol(str, &end, 10);
    if (strcasecmp(end, "K")==0)   v *= 1024L;
    else if (strcasecmp(end, "KB")==0)  v *= 1024L;
    else if (strcasecmp(end, "M")==0)   v *= 1024L*1024L;
    else if (strcasecmp(end, "MB")==0)  v *= 1024L*1024L;
    else if (strcasecmp(end, "G")==0)   v *= 1024L*1024L*1024L;
    else if (strcasecmp(end, "GB")==0)  v *= 1024L*1024L*1024L;
    else if (*end != '\0') {
        fprintf(stderr,"ERR: invalid unit '%s'\n", end);
        return -1;
    }
    return v;
}

int process_rlimit(char *input, LimitSpec *limits, int *num_lim) {
    if (strncmp(input, "rlimit ", 7) != 0) {
        return 0;
    }

    char temp[MAX_SIZE];
    strcpy(temp, input);
    rlimit(temp, limits, num_lim);

    char *p = input + 7;
    char *mode_end = strchr(p, ' ');
    if (!mode_end) {
        input[0] = '\0';
        return 1;
    }
    *mode_end = '\0';
    char *mode = p;
    p = mode_end + 1;

    if (strcmp(mode, "show") == 0) {
        input[0] = '\0';
    }
    else if (strcmp(mode, "set") == 0) {
        while (*p) {
            while (*p == ' ') p++;
            char *tok_end = strchr(p, ' ');
            if (!tok_end) {
                input[0] = '\0';
                break;
            }
            if (!memchr(p, '=', tok_end - p)) {
                memmove(input, p, strlen(p) + 1);
                break;
            }
            p = tok_end + 1;
        }
    }
    else {
        input[0] = '\0';
    }
    return 0;
}

bool check_if_background(char *input) {
    int i = strlen(input) - 1;
    while (i >= 0 && isspace((unsigned char)input[i])) {
        i--;
    }
    if (i >= 0 && input[i] == '&') {
        i--;
        while (i >= 0 && isspace((unsigned char)input[i])) {
            i--;
        }
        input[i + 1] = '\0';
        return true;
    }
    return false;
}

int len(char *str) {
    int in_word = 0;
    int count = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] != ' ' && in_word == 0) {
            in_word = 1;
            count++;
        } else if (str[i] == ' ') {
            in_word = 0;
        }
    }
    return count;
}

void wait_status(int status, FILE *fp, int *cmd_count, double *last_time, double *avg_time, double *min_time, double *max_time, char *input, struct timespec start, struct timespec end) {
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) { //succses
            (*cmd_count)++;
            *last_time = calc_elapsed_time(start, end);
            *avg_time = ((*avg_time) * ((*cmd_count) - 1) + *last_time) / (*cmd_count);
            check_min_max(min_time, max_time, *last_time);
            fprintf(fp, "%s : %.5f sec\n", input, *last_time);
            fflush(fp);
        }
        else {
            fprintf(stderr,"Process exited with error code %d\n", WEXITSTATUS(status));
        }
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        switch (sig) {
            case SIGSEGV: fprintf(stderr, "Memory allocation failed!\n"); break;
            case SIGXCPU: fprintf(stderr,"CPU time limit exceeded!\n"); break;
            case SIGXFSZ: fprintf(stderr,"File size limit exceeded!\n"); break;
            case SIGUSR1: fprintf(stderr,"Too many open files!\n"); break;
            default: fprintf(stderr,"Terminated by signal: %s\n", strsignal(sig)); break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void matrix_operation(char *input, FILE *fp, int *cmd_count, double *last_time, double *avg_time, double *min_time, double *max_time, DangerousCommand *dangerous_commands, int dangerous_count, int *dangerous_cmd_blocked, int *unblocked) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int countArgs = count_args_matrix(input); // Check how many args there are
    if (countArgs == -1) { // Double space
        return;
    }

    char **args = malloc(countArgs * sizeof(char *)); // Allocate space for arguments
    if (args == NULL) {
        perror("malloc");
        return;
    }

    //split the input into args
    char *token = strtok(input, " ");
    int i = 0;
    while (token != NULL && i < countArgs) {
        args[i] = malloc(strlen(token) + 1);
        if (args[i] == NULL) {
            perror("malloc");
            for (int j = 0; j < i; j++) {
                free(args[j]);
            }
            free(args);
            return;
        }
        strcpy(args[i], token);
        token = strtok(NULL, " ");
        i++;
    }

    for (int j = 1; j < countArgs; j++) {
        trim_quotes(args[j]); // Remove the quotes from the string
    }

    int count_matrix = countArgs - 2; // Number of matrices
    McalcMatrix *matrix = malloc(sizeof(McalcMatrix)); // Allocate space for matrix struct
    if (matrix == NULL) {
        perror("malloc");
        for (int j = 0; j < countArgs; j++) {
            free(args[j]);
        }
        free(args);
        return;
    }

    if (strcmp(args[countArgs - 1], "ADD") == 0) {
        matrix->op = 1;
    } else if (strcmp(args[countArgs - 1], "SUB") == 0) {
        matrix->op = 2;
    } else {
        fprintf(stderr, "ERR_MAT_INPUT\n");
        for (int j = 0; j < countArgs; j++) {
            free(args[j]);
        }
        free(args);
        free(matrix);
        return;
    }

    matrix->count_matrix = count_matrix;
    matrix->matrix = malloc(count_matrix * sizeof(int *)); // Allocate space for matrices
    if (matrix->matrix == NULL) {
        perror("malloc");
        for (int j = 0; j < countArgs; j++) {
            free(args[j]);
        }
        free(args);
        free(matrix);
        return;
    }

    for (int j = 0; j < count_matrix; j++) {
        split_matrix(args[j + 1], matrix, j);
        if (matrix->matrix[j] == NULL) { // Check if split_matrix failed
            for (int k = 0; k < j; k++) {
                free(matrix->matrix[k]);
            }
            free(matrix->matrix);
            free(matrix);
            for (int k = 0; k < countArgs; k++) {
                free(args[k]);
            }
            free(args);
            return;
        }
    }

    reduce_matrix_tree(matrix); // Reduce the matrix tree
    clock_gettime(CLOCK_MONOTONIC, &end);

    //count successful commands
    (*cmd_count)++;
    *last_time = calc_elapsed_time(start, end);
    *avg_time = ((*avg_time) * ((*cmd_count) - 1) + *last_time) / (*cmd_count);
    check_min_max(min_time, max_time, *last_time);
    fprintf(fp, "%s : %.5f sec\n", input, *last_time);
    fflush(fp);

    // Print the result and free memory
    int *result = matrix->matrix[0];
    int rows = result[0];
    int cols = result[1];
    printf("# Output: (%d,%d:", rows, cols);
    for (int j = 0; j < rows * cols; j++) {
        printf("%d", result[j + 2]);
        if (j < rows * cols - 1) {
            printf(",");
        }
    }
    printf(")\n");

    free_matrix(matrix);
    free(matrix);
    for (int j = 0; j < countArgs; j++) {
        free(args[j]);
    }
    free(args);
}

int count_args_matrix(char *str) {
    int count = 0;
    int in_word = 0;
    int check = 0;

    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == ' ' && str[i + 1] == ' ') {
            fprintf(stderr, "ERR_MAT_INPUT\n");
            return -1;
        }
        if (str[i] != ' ' && in_word == 0) {
            in_word = 1;
            count++;
        } else if (str[i] == ' ') {
            in_word = 0;
        }
    }
    return count;
}

void split_matrix(char *input, McalcMatrix *matrix, int index) {
    int len = strlen(input);
    if (input[0] != '(' || input[len - 1] != ')') {
        fprintf(stderr, "ERR_MAT_INPUT\n");
        return;
    }

    input[len - 1] = '\0'; // Replace the last char with \0
    char *start = input + 1; // Skip the first char

    char *dims = strtok(start, ":");
    char *matrix_data = strtok(NULL, ":");
    if (dims == NULL || matrix_data == NULL) {
        fprintf(stderr, "ERR_MAT_INPUT\n");
        return;
    }

    char *rows_str = strtok(dims, ",");
    char *cols_str = strtok(NULL, ",");
    if (rows_str == NULL || cols_str == NULL) {
        fprintf(stderr, "ERR_MAT_INPUT\n");
        return;
    }

    int rows = atoi(rows_str);
    int cols = atoi(cols_str);
    if (rows <= 0 || cols <= 0) {
        fprintf(stderr, "ERR_MAT_INPUT\n");
        return;
    }

    int size = rows * cols + 2;
    matrix->matrix[index] = malloc(size * sizeof(int));
    if (matrix->matrix[index] == NULL) {
        perror("malloc");
        return;
    }

    matrix->matrix[index][0] = rows;
    matrix->matrix[index][1] = cols;

    char *token = strtok(matrix_data, ",");
    int i = 0;
    while (token != NULL && i < rows * cols) {
        matrix->matrix[index][i + 2] = atoi(token);
        token = strtok(NULL, ",");
        i++;
    }

    if (i != rows * cols) {
        fprintf(stderr, "ERR_MAT_INPUT\n");
        free(matrix->matrix[index]);
        matrix->matrix[index] = NULL; // Avoid dangling pointer
        return;
    }
}

void free_matrix(McalcMatrix *matrix) {
    for (int i = 0; i < matrix->count_matrix; i++) {
        free(matrix->matrix[i]);
    }
    free(matrix->matrix);
}


void trim_quotes(char *str) {
    int len = strlen(str);
    if (len >= 2 && str[0] == '"' && str[len - 1] == '"') {
        // Shift content to the left to remove the first quote
        memmove(str, str + 1, len - 2);  // move everything left 1 place
        str[len - 2] = '\0';             // new null terminator
    }
}

//the thread function that will do the operation on the two matrices
void* matrix_op_thread(void *arg){
    ThreadData *data = (ThreadData *)arg;
    
    int *matrix1 = data->matrix1;
    int *matrix2 = data->matrix2;
    int *result = data->result;
    int op = data->op;

    int row1 = matrix1[0], col1 = matrix1[1];
    int row2 = matrix2[0], col2 = matrix2[1];

    if(row1 != row2 || col1 != col2) {
        fprintf(stderr, "ERR_MAT_INPUT\n");
        pthread_exit(NULL);
    }

    int total_elements = row1 * col1;
    result[0] = row1; // Save the number of rows
    result[1] = col1; // Save the number of columns

    for(int i = 0; i < total_elements; i++) {
        if (op == 1) { // ADD
            result[i + 2] = matrix1[i + 2] + matrix2[i + 2];
        } else if (op == 2) { // SUB
            result[i + 2] = matrix1[i + 2] - matrix2[i + 2];
        }
    }
    pthread_exit(NULL); 

}

// Function to reduce the matrix tree
// This function will create threads to perform the operations on the matrices
// and reduce the number of matrices until only one matrix remains
// The final result will be stored in the first matrix of the McalcMatrix struct
// The function will also free the memory of the old matrices
// and update the McalcMatrix struct with the new matrix
void reduce_matrix_tree(McalcMatrix *matrix) {
    int active = matrix->count_matrix;

    while (active > 1) {
        int new_count = (active + 1) / 2;

        int **new_matrix = malloc(new_count * sizeof(int *)); // Allocate space for new matrices
        if (new_matrix == NULL) {
            perror("malloc");
            return;
        }

        pthread_t *threads = malloc((active / 2) * sizeof(pthread_t)); // Allocate space for threads
        if (threads == NULL) {
            perror("malloc");
            free(new_matrix);
            return;
        }

        ThreadData **thread_data = malloc((active / 2) * sizeof(ThreadData *)); // Allocate space for thread data
        if (thread_data == NULL) {
            perror("malloc");
            free(new_matrix);
            free(threads);
            return;
        }

        int thread_count = 0;
        // Create threads to perform the operations on the matrices
        for (int i = 0; i < active; i += 2) {
            if (i + 1 < active) {
                int rows = matrix->matrix[i][0];
                int cols = matrix->matrix[i][1];
                new_matrix[thread_count] = malloc((rows * cols + 2) * sizeof(int)); // Allocate space for the result matrix
                if (new_matrix[thread_count] == NULL) {
                    perror("malloc");
                    for (int j = 0; j < thread_count; j++) {
                        free(new_matrix[j]);
                        free(thread_data[j]);
                    }
                    free(new_matrix);
                    free(threads);
                    free(thread_data);
                    return;
                }

                thread_data[thread_count] = malloc(sizeof(ThreadData)); // Allocate space for thread data
                if (thread_data[thread_count] == NULL) {
                    perror("malloc");
                    free(new_matrix[thread_count]);
                    for (int j = 0; j < thread_count; j++) {
                        free(new_matrix[j]);
                        free(thread_data[j]);
                    }
                    free(new_matrix);
                    free(threads);
                    free(thread_data);
                    return;
                }

                // Initialize thread data
                thread_data[thread_count]->matrix1 = matrix->matrix[i];
                thread_data[thread_count]->matrix2 = matrix->matrix[i + 1];
                thread_data[thread_count]->result = new_matrix[thread_count];
                thread_data[thread_count]->op = matrix->op;

                // Create the thread
                if (pthread_create(&threads[thread_count], NULL, matrix_op_thread, thread_data[thread_count]) != 0) {
                    perror("pthread_create");
                    free(new_matrix[thread_count]);
                    free(thread_data[thread_count]);
                    for (int j = 0; j < thread_count; j++) {
                        free(new_matrix[j]);
                        free(thread_data[j]);
                    }
                    free(new_matrix);
                    free(threads);
                    free(thread_data);
                    return;
                }
                thread_count++;
            } else {
                // If there is an odd matrix, just copy it to the new matrix
                // and increment the thread count
                // This matrix will not be processed by a thread
                int rows = matrix->matrix[i][0];
                int cols = matrix->matrix[i][1];
                new_matrix[thread_count] = malloc((rows * cols + 2) * sizeof(int));
                if (new_matrix[thread_count] == NULL) {
                    perror("malloc");
                    for (int j = 0; j < thread_count; j++) {
                        free(new_matrix[j]);
                        free(thread_data[j]);
                    }
                    free(new_matrix);
                    free(threads);
                    free(thread_data);
                    return;
                }
                memcpy(new_matrix[thread_count], matrix->matrix[i], (rows * cols + 2) * sizeof(int)); // Copy the matrix§
            }
        }

        // Wait for all threads to finish
        // and free the thread data
        for (int i = 0; i < thread_count; i++) {
            if (pthread_join(threads[i], NULL) != 0) {
                perror("pthread_join");
                exit(1);
            }
        }

        for (int i = 0; i < thread_count; i++) {
            free(thread_data[i]);
        }
        free(thread_data);
        free(threads);

        // Free the old matrix
        for (int i = 0; i < active; i++) {
            free(matrix->matrix[i]);
        }
        free(matrix->matrix);

        // Update the McalcMatrix struct with the new matrix
        matrix->matrix = new_matrix;
        matrix->count_matrix = new_count;
        active = new_count;
    }
}