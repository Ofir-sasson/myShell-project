#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>

#define MAX_SIZE 1025
#define MAX_ARGS 7

//dangerous command struct
typedef struct {
    int argc; //count how many args
    char **argv;
} DangerousCommand;

void terminal(const char* file_name_dangerous, char* log_name);
int find_len(const char *str);
void print_cmd(int cmd, int dangerous, double time, double avg_time, double min_time, double max_time);
double calc_elapsed_time(struct timespec start, struct timespec end);
void check_min_max(double *min_time, double *max_time, double cur);
int create_args(char ***arg, char *input);
void load_dangerous_commands(const char* filename, DangerousCommand **dangerous_commands, int* dangerous_count);
int is_dangerous(char **arg, const DangerousCommand *dangerous_commands, int dangerous_count, int count);
void free_dangerous_commands(DangerousCommand **dangerous_commands, int dangerous_count);

int main(int argc, char *argv[]) {
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
        char temp[MAX_SIZE];

        strcpy(temp, input);
        //create the arg array for the command
        ///if returns -1 the command cant run
        ///else returns the num of args so we coulf free it from the memory after the command runs
        create = create_args(&arg, input);

        if (create != -1) {

            struct timespec start, end;
            clock_gettime(CLOCK_MONOTONIC, &start);

            int match = is_dangerous(arg, dangerous_commands, dangerous_count, create); //checks for dangerous commands
            if (match == -1) { //perfect match
                dangerous_cmd_blocked++;
                printf("ERR: Dangerous command detected (\"%s\"). Execution prevented.\n", temp);
            }
            else {
                if (match != 0) { //similar to dangerous command
                    unblocked++;
                    printf("WARNING: Command similar to dangerous command (\"%s\"). Proceed with caution.\n", temp);
                }

                pid_t pid = fork(); //create a son

                //if the fork failed
                if (pid < 0) {
                    perror("fork");
                    exit(1);
                }
                //child process
                if (pid == 0) {
                    execvp(arg[0], arg); //do the command that the user enter to the terminal
                    perror("execvp"); //if the execvp failed
                    exit(255);
                }
                //father process
                //waits for the son to finish and checks if the execvp was successful
                int status;
                wait(&status);
                clock_gettime(CLOCK_MONOTONIC, &end);
                if (WIFEXITED(status) && WEXITSTATUS(status) != 255) { //if the command success
                    cmd++;
                    last_cmd_time = calc_elapsed_time(start, end);

                    avg_time = ((avg_time * (cmd - 1)) + last_cmd_time) / cmd;
                    check_min_max(&min_time, &max_time, last_cmd_time);

                    fprintf(fp, "%s : %.5f sec\n", temp, last_cmd_time); //writes every successful command
                    fflush(fp); // save the file
                }
                //free the allocated memory
                for (int i = 0; i < create; i++) {
                    free(arg[i]);
                }
                free(arg);
            }
        }

        //gets a new command from the user
        print_cmd(cmd, dangerous_cmd_blocked, last_cmd_time, avg_time, min_time, max_time);
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0';
    }
    free_dangerous_commands(&dangerous_commands, dangerous_count);
    fclose(fp);
    printf("Blocked: %d \n", dangerous_cmd_blocked);
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
        printf("ERR_ARG\n");
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
