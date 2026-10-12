//required libraries
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

//max buffers
#define ARG_MAX 1024
#define INPUT_MAX 4096

//tokenization delimiters
#define DELIM " \t"

//background process linked list struct
typedef struct bg_process{
    pid_t pid;
    char *command;
    struct bg_process *next;
} bg_process;

//helper prototypes
void free_2d_array(char **array);
int count_tokenizations(char *input);
char *get_path(char *name);
void sig_handler(int signal);
void check_bg_process(bg_process **head);
char *bglist_args(char **args, char *path);
void change_directory(char **args);
char **get_args(char *input);
char *get_prompt(void);

//for redisplaying prompt from signal interrupt (like after pressing Ctrl+C)
char *sig_prompt = NULL;
size_t prompt_size = 0;

int main(void) {

    //signal handling
    struct sigaction sig;
    sig.sa_handler = sig_handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sig, NULL) == -1) {
        perror("sigaction");
        _exit(EXIT_FAILURE);
    }

    //initial bg_process linked list head
    bg_process *bg_head = NULL;

    //user typing "exit" or "bye" closes the shell/program
    int shell_running = 1;
    while(shell_running) {
        //get the prompt to display
        char *prompt = get_prompt();
        if (prompt == NULL) {
            break;
        }
        sig_prompt = prompt;
        prompt_size = strlen(prompt);

        //display prompt and get user input
        char *inputs = readline(prompt);
        sig_prompt = NULL;
        prompt_size = 0;
        free(prompt);
        if (!inputs) {
            break;
        }

        //checking invalid return values
        char **args = get_args(inputs);
        if (args == NULL) {
            free(inputs);
            break;
        }

        //checking background process
        check_bg_process(&bg_head);

        //if nothing is entered
        if (args[0] == NULL) {
            free_2d_array(args);
            free(inputs);
            continue;
        }

        //check if user wishes to close the shell
        if (!strcmp(args[0],"exit") || !strcmp(args[0], "bye")) {
            free_2d_array(args);
            free(inputs);
            break;
        }

        //changing directories
        if (!strcmp(args[0], "cd")) {
            change_directory(args);

            free_2d_array(args);
            free(inputs);
            continue;
        }

        //background process
        if (!strcmp(args[0], "bg")) {
            if (args[1] == NULL) {
                fprintf(stderr, "bg: commands required\n");
                free_2d_array(args);
                free(inputs);
                continue;
            }
            /*Put this above fork(), to deal with malloc() == NULL edge cases*/
            //create a new background process node in the list
            bg_process *new_bg = malloc(sizeof(bg_process));
            if (new_bg == NULL) {
                fprintf(stderr, "background process addition failed");
                free_2d_array(args);
                free(inputs);
                continue;
            }

            //path for background process
            char *path = get_path(args[1]);
            if (path == NULL) {
                free_2d_array(args);
                free(inputs);
                free(new_bg);
                continue;
            }

            //path+commands for output
            char *argument = bglist_args(&args[2], path);
            free(path);
            if (argument == NULL) {
                fprintf(stderr, "background process addition failed");
                free_2d_array(args);
                free(inputs);
                free(new_bg);
                continue;
            }

            //creating background process
            pid_t pid = fork();
            if (pid < 0) {
                free(argument);
                free(new_bg);
                perror("fork()");
            }
            //child background process
            else if (pid == 0) {
                //hiding background process outputs
                int fd = open("/dev/null", O_WRONLY);
                if (fd < 0) {
                    perror("open()");
                    _exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    perror("dup2");
                    close(fd);
                    _exit(EXIT_FAILURE);
                }
                close(fd);
                execvp(args[1], &args[1]);  //to not execute another bg
                perror("exec()");
                _exit(EXIT_FAILURE);
            }
            else {
                //new bg_process struct in the linked list
                new_bg->pid = pid;
                new_bg->command = argument;
                new_bg->next = bg_head;
                bg_head = new_bg;
            }

            free_2d_array(args);
            free(inputs);
            continue;
        }

        //bglist
        if (!strcmp(args[0], "bglist")) {

            //initialization for traversal
            bg_process *current_bg = bg_head;
            int count = 0; //number of running background processes

            pid_t pid;
            while (current_bg != NULL) {
                pid = current_bg->pid;
                char *arguments = current_bg->command;
                printf("%d: %s\n", pid, arguments);
                current_bg = current_bg->next;
                count++;
            }

            printf("Total Background jobs: %d\n", count);

            free_2d_array(args);
            free(inputs);
            continue;
        }

        //creating process for the shell
        pid_t pid;
        pid = fork();

        //check if process errors
        if (pid < 0) {
            perror("fork()");
            free_2d_array(args);
            free(inputs);
            break;
        }
        //child process executes
        else if(pid == 0) {

            //child signal handler
            struct sigaction child_sig;
            child_sig.sa_handler = SIG_DFL;
            sigemptyset(&child_sig.sa_mask);
            child_sig.sa_flags = SA_RESTART;
            if (sigaction(SIGINT, &child_sig, NULL) == -1) {
                perror("sigaction");
                _exit(EXIT_FAILURE);
            }
            //executes commands
            execvp(args[0], args);
            perror("execvp failed");
            _exit(EXIT_FAILURE);
        }
        //parent process waits for child
        else {
            if (waitpid(pid, NULL, 0) == -1){
                perror("waitpid()");
            }
        }

        //freeing dynamic arrays
        free_2d_array(args);
        free(inputs);
    }

    printf("exiting shell\n");
    return 0;
}


/*Helper functions below*/

//Frees given dynamically allocated 2d array
//Assumes the final index is NULL
void free_2d_array(char **array) {

    //check for valid array
    if(array == NULL) {
        return;
    }
    int i = 0;
    while (array[i] != NULL) {
        free(array[i]);
        i++;
    }
    free(array);
}

//Counts number of tokenization 
//Returns the count
int count_tokenizations(char *input) {
    
    //allocate for the copy array
    char *input_copy = malloc(sizeof(char) * (strlen(input) + 1));
    if (input_copy == NULL) {
        return -1;
    }

    //copy given array
    strcpy(input_copy, input);

    //counting tokenizations
    int count = 0;
    char *token = strtok(input_copy, DELIM);
    while (token != NULL) {
        token = strtok(NULL, DELIM);
        count++;
    }
    free(input_copy);
    return count;
}

//Get path of background process
//Return dynamically allocated path
char *get_path(char *name) {
    int command_length = strlen("which ") + strlen(name) + 1;
    char *which_command = malloc(command_length);
    if (which_command == NULL) {
        return NULL;
    }
    snprintf(which_command, command_length, "which %s", name);

    FILE *fp = popen(which_command, "r");
    free(which_command);
    if (fp == NULL) {
        perror("popen()");
        return NULL;
    } 
    size_t size = 0;
    char *path = NULL;
    if (getline(&path, &size, fp) == -1) {
        free(path);
        pclose(fp);
        return NULL;
    }
    //strip ending '\n'
    path[strcspn(path, "\n")] = '\0';

    pclose(fp);
    return path;
}

//Signal handler
void sig_handler(int signal) {
    (void)signal;
    write(STDOUT_FILENO, "\n", 1);

    //redisplay prompt after interrupt
    if (sig_prompt != NULL) {
        write(STDOUT_FILENO, sig_prompt, prompt_size);
    }
}

//Checking any ongoing background processes
void check_bg_process(bg_process **head) {

    //initialize linked list to head for traversal
    bg_process *current = *head;
    bg_process *previous = NULL;
    while (current != NULL) {
        int status;
        pid_t result = waitpid(current->pid, &status, WNOHANG);
        if (result < 0) {
            perror("waitpid()");
            previous = current;
            current = current->next;
        }
        else if (result == 0) {
            previous = current;
            current = current->next;
        }else {
            printf("%d: %s has terminated.\n", current->pid, current->command);

            bg_process *remove = current;
            current = current->next;

            if (previous == NULL) {
                *head = current;
            }else {
                previous->next = current;
            }

            free(remove->command);
            free(remove);
        }

    }
}

//Returns string of ongoing background process outputs
char *bglist_args(char **args, char *path) {

    int size = sizeof(char) * (strlen(path) + 1);
    int i = 0;
    while (args[i] != NULL) {
        size += strlen(args[i]);
        if (args[i + 1] != NULL) {
            size += sizeof(char);
        }
        i++;
    }
    char *input = malloc(size + 1);
    if (input == NULL) {
        return NULL;
    }

    //initialize input first
    input[0] = '\0';

    strcat(input, path);
    int n = 0;
    while (args[n] != NULL) {
        strcat(input, " ");
        strcat(input, args[n]);
        n++;
    }

    return input;
}

//Changes directory for cd command
void change_directory(char **args) {
    char *directory;
    if (args[1] == NULL  || !strcmp(args[1], "~")) {
        //for home directory edge cases
        directory = getenv("HOME");
        if (directory == NULL) {
            fprintf(stderr, "cd: Home not found");
            return;
        }
    }else {
        directory = args[1];
    }
    //changing the directory
    if (chdir(directory) == -1) {
        perror("cd");
    }
}

//Get the user commands in the shell via tokenization
//Returns returns 2d char array that contains the user arguments
char **get_args(char *input) {
    int num_arg = count_tokenizations(input);
    if (num_arg < 0) {
        return NULL;
    }

    //dynamically allocate array of pointers to strings +1 for final null terminator
    char **args = malloc(sizeof(char *) * (num_arg + 1));
    if (args == NULL) {
        return NULL;
    }

    char *token = strtok(input, DELIM);
    int i = 0;
    while(token != NULL) {
        args[i] = malloc(sizeof(char) * (strlen(token) + 1));
        //check each index allocation
        if (args[i] == NULL) {
            free_2d_array(args);
            return NULL;
        }
        strcpy(args[i], token);
        token = strtok(NULL, DELIM);
        i++;
    }

    //make final index NULL
    //important for later when freeing the array
    args[i] = NULL;

    return args;
}

//Get prompt to print to the shell
//Returns dynamic string representing the entire prompt to display
char *get_prompt(void) {

    char hostname[ARG_MAX];
    char cwd[INPUT_MAX];

    char *username = getlogin();

    //checking invalid return values 
    if (username == NULL) {
        return NULL;
    }if (gethostname(hostname, sizeof(hostname)) == -1) {
        return NULL;
    }if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return NULL;
    }

    //make sure to give extra room for fillers such as "@", "$", etc
    char *output = malloc((sizeof(char)) * (strlen(username) + strlen(hostname) + strlen(cwd) + 8));

    if (output == NULL) {
        return NULL;
    }

    //formatted to the expect output
    sprintf(output, "%s@%s: %s$ ", username, hostname, cwd);

    return output;
}
