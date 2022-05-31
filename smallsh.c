/************************************************
 ** Author: hakirot 
 ** Date: 10/27/2020
 ** Description: A mini shell prompt program, 
    requirements outlined in Assignment 3: smallsh
************************************************/

// This progrom must now be compiled with --std=gnu99, as c99 does not recognize sigaction, idk why

#define _SVID_SOURCE    // POSIX EXTENSION for strdup() and others, kills warnings
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     // Hail to the string baby
#include <dirent.h>     // Directories and entries
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // wait, waitpid
#include <unistd.h>     // getpid, getppid, fork, NULL
#include <signal.h>     // signal handling API
#include <fcntl.h>

// Program should begin allowing background processes
int BACKGROUNDSET = 1;

// Save a few keystrokes
typedef struct Command Command;

void smallsh();
void exitProg(Command * userCommand);
Command * spliceCmd(Command * comStruct);
void Execute(Command * userCommand, struct sigaction SIGINT_action, struct sigaction SIGTSTP_action);

struct Command
{
    // Raw null-terminated user-entered string
    char rawCmd[2049];
    // Array of char array pointers to hold arguments for passing to exec
    char * newargv[512];
    // Holds input filename if specified
    char inputFile[128];
    // Holds output filename if specified
    char outputFile[128];
    // Tracks whether the user has entered '&'
    int backgroundBit;
    // Tracks exit status of child process
    int exitStatus;
    // Tracks the pid of the child process
    pid_t childPid;
};

/* Signal handler function for SIGTSTP, sends the signal number as an argument which is unused
    - This function simply flips the bit that allows background processes   */
void handle_SIGTSTP(int signo)
{
    // If 1 set to 0
    if(BACKGROUNDSET == 1)
    {
        char * message = "\nEntering foreground-only mode(& is now ignored)\n";
        write(STDOUT_FILENO, message, 49);
        fflush(stdout);
        BACKGROUNDSET = 0;
    }

    // else set to 1
    else
    {
        char * message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
        fflush(stdout);
        BACKGROUNDSET = 1;
    }
}

// main does not use arguments from the shell
int main(int argc, const char * argv[])
{
    // Launch miniShell
    smallsh();

    return 0;
}

/* Launches the minishell, redefines signal behavior, and performs specified commands */
void smallsh()
{
    // Signal handlers

    // Ignore CTRL-C, module 5-3
    // Init empty sigaction struct
    struct sigaction SIGINT_action = {0};
    // Register IGN as signal handler to ignore
    SIGINT_action.sa_handler = SIG_IGN;
    // Block all catchable signals while handle_SIGINT is running(likely not required)
    sigfillset(&SIGINT_action.sa_mask);
    // Set no Flags
    SIGINT_action.sa_flags = 0;
    // INSTALL, set default in child process and reinstall
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Catch CTRL-Z
    struct sigaction SIGTSTP_action = {0};
    // Register custom function for signal handler
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    // Ignore all catachable signals while custom function is running
    sigfillset(&SIGTSTP_action.sa_mask);
    // No flags
    SIGTSTP_action.sa_flags = 0;
    // INSTALL, set IGN and reinstall in child processes
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);


    char input[2049];
    // Initialize command struct to empty
    Command * userCommand = malloc(sizeof(Command));
    userCommand->backgroundBit = 0;
    int i;

    while(strcmp(input, "exit") != 0)
    {
        // Clear everything
        memset(input, '\0', 2049);
        memset(userCommand->rawCmd, '\0', 2049);
        memset(userCommand->inputFile, '\0', 128);
        memset(userCommand->outputFile, '\0', 128);
        userCommand->backgroundBit = 0;
        // keep exit status persistent for subsequent reading

        // Reset all argument pointers allocated with strdup()
        i = 0;
        while(userCommand->newargv[i] != NULL)
        {
            free(userCommand->newargv[i]);
            userCommand->newargv[i] = NULL;
            i++;
        }


        printf(": ");
        // Always flush, no exceptions
        fflush(stdout);

        // Shell must support command lines of max 2048 characters
        fgets(input, 2048, stdin);
        // Clear the last newline character that is read from fgets()
        input[strcspn(input, "\n")] = 0;
        // Copy raw command into command struct
        strcpy(userCommand->rawCmd, input);

        // First we will check for function comments or blanks
        if (input[0] == '#' || input[0] == '\0')
        {
            // Skip/Reiterate/Reset
            continue;
        }

        // Then we will splice the command and populate the argument struct
        userCommand = spliceCmd(userCommand);

        // Now we will handle the built-in functions
        
        // "cd" command
        if(strcmp(userCommand->newargv[0], "cd") == 0)
        {
            if(userCommand->newargv[1] != NULL)
            {
                // User has specified a specific directory
                int check = chdir(userCommand->newargv[1]);
                if(check == -1)
                {
                    printf("\nUnknown directory");
                    fflush(stdout);
                }
            }

            // User has not specified a specific directory
            else
            {
                // Direct user to HOME directory from environment variable
                char * home = getenv("HOME");
                chdir(home);
            }
        }

        // "exit" does nothing, breaks from the enclosed while loop
        else if(strcmp(userCommand->newargv[0], "exit") == 0)
        {
        }
        
        // "status" command
        else if(strcmp(input, "status") == 0 || strcmp(input, "status &") == 0)
        {
            // Program prints the current status after decoding signal specifier
            if(WIFEXITED(userCommand->exitStatus))
            {
                printf("exit value %d\n", WEXITSTATUS(userCommand->exitStatus));
                fflush(stdout);
            }
            else
            {
                printf("Terminated by signal %d\n", WTERMSIG(userCommand->exitStatus));
                fflush(stdout);
            }

            userCommand->exitStatus = 0;
        }

        // If it's not a built-in command, send it to execvp()
        else
        {
            Execute(userCommand, SIGINT_action, SIGTSTP_action);
        }

        // This will print the termination signal IF a child was terminated abnormally
        if(WIFSIGNALED(userCommand->exitStatus))
        {
            printf("Terminated by signal %d\n", WTERMSIG(userCommand->exitStatus));
            fflush(stdout);
        }
    }

    // The program is exiting, run exit(), killing any other jobs or processes before terminating
    exitProg(userCommand);
    //free allocated Command struct
    free(userCommand);
}

/*  Processes the user's non built-in command, while redefining signal behavior in child proceses,
 *  and searching PATH environment variable to execute functions built into linux */
void Execute(Command * userCommand, struct sigaction SIGINT_action, struct sigaction SIGTSTP_action)
{
    pid_t spawnPid = -5;

    // Alright let's try not to fork this up
    spawnPid = fork();

    switch(spawnPid)
    {
        case -1:
            perror("fork() failure\n");
            fflush(stdout);
            break;

        case 0:
            // Child Process

            // REINSTALL SIGINT_action for CTRL-C after resetting default action
            SIGINT_action.sa_handler = SIG_DFL;
            sigaction(SIGINT, &SIGINT_action, NULL);

            // REINSTALL SIGTSTP_aciton for CTRL-Z after setting IGNORE for child process
            SIGTSTP_action.sa_handler = SIG_IGN;
            sigaction(SIGTSTP, &SIGTSTP_action, NULL);

            // Handle input/output redirection for this process
            if(strlen(userCommand->inputFile) > 0)
            {
                // Attempt to open user-specified file
                int sourceFD = open(userCommand->inputFile, O_RDONLY);

                // Check for error opening file
                if(sourceFD == -1)
                {
                    printf("\nCannot open %s for input\n", userCommand->inputFile);
                    fflush(stdout);
                    // Quit the process if failure
                    exit(1);
                }
                else
                {
                    // Otherwise assign source file as stdin
                    int result = dup2(sourceFD, 0);
                }
            }

            // Repeat for output file
            if(strlen(userCommand->outputFile) > 0)
            {
                // Attempt to open user-specified file
                int targetFD = open(userCommand->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                // Check for error opening file
                if(targetFD == -1)
                {
                    printf("\nCannot open %s for output\n", userCommand->outputFile);
                    fflush(stdout);
                    // Quit the process if failure
                    exit(1);
                }
                else
                {
                    // Otherwise assign source file as stdin
                    int result = dup2(targetFD, 1);
                }
            }

            // Execute with inherited file descriptors
            execvp(userCommand->newargv[0], userCommand->newargv);

            // This will only execute if execvp fails
            printf("error in execvp, bad command\n");
            fflush(stdout);
            exit(1);

        default:
            // Only allow this branch if not in foreground-only mode and user has specified with '&'
            // Background process
            if(BACKGROUNDSET == 1 && userCommand->backgroundBit == 1)
            {
                // Notify user of background process pid
                printf("background pid is %d\n", spawnPid);
                fflush(stdout);
                pid_t waitRetVal = waitpid(spawnPid, &userCommand->exitStatus, WNOHANG);
                // Store child pid on struct for access later, will be used to check status
                userCommand->childPid = spawnPid;
            }

            // Foreground process, does not need to store pid for later use
            else
            {
                pid_t waitRetVal = waitpid(spawnPid, &userCommand->exitStatus, 0);
            }

        // Check for terminated child process, RetVal will be nonzero if state has changed
        pid_t RetVal = waitpid(userCommand->childPid, &userCommand->exitStatus, WNOHANG);
        if (RetVal > 0)
        {
            // Print message to user notifying of changes state
            printf("background pid %d is done: ", userCommand->childPid);
            fflush(stdout);

            // Check status of terminated child process
            if(WIFEXITED(userCommand->exitStatus))
            {
                // Print if exited normally
                printf("exit value %d\n", WEXITSTATUS(userCommand->exitStatus));
                fflush(stdout);
            }
            else
            {
                // Print message if terminated by signal
                printf("Terminated by signal %d\n", WTERMSIG(userCommand->exitStatus));
                fflush(stdout);
            }

            // Reset exitStatus after notifying user
            userCommand->exitStatus = 0;
            fflush(stdout);
        }
    }
}

/* spliceCmd takes the full command from the shell and splices necessary information
 * to a struct which holds the data needed to execute that command properly. 
 *
 * Returns a pointer to the same struct after it is populated with data from the shell
 */
Command * spliceCmd(Command * comStruct)
{
    char * token = strtok(comStruct->rawCmd, " ");

    // Cycle through every word in the command until token finds NULL
    for(int i = 0; token != NULL; i++)
    {

        // Get the word after input indicator and set as the input file
        if(strcmp(token, "<") == 0)
        {
            // Advance!
            token = strtok(NULL, " ");
            // Copy input filename to struct storage
            strcpy(comStruct->inputFile, token);
        }
        
        // Set the output file
        else if(strcmp(token, ">") == 0)
        {
            token = strtok(NULL, " ");
            strcpy(comStruct->outputFile, token);
        }
        // Set background variable
        else if(strcmp(token, "&") == 0)
        {
            comStruct->backgroundBit = 1;
        }

        // Append main command and arguments to newarg array within struct
        else
        {
            comStruct->newargv[i] = strdup(token);

            // Variable expansion for '$$' performed here
            if(!strstr(comStruct->newargv[i], "$$") == 0)
            {
                // Clear the argument
                free(comStruct->newargv[i]);
                int pid = getpid();
                char newStr[64];
                char writeStr[64];
                memset(newStr, '\0', 64);
                memset(writeStr, '\0', 64);
                strcpy(newStr, token);
                int strLength = strlen(newStr);
                for(int j = 0; j < strLength; j++)
                {
                    if(newStr[j] == '$' && newStr[j + 1] == '$')
                    {
                        newStr[j] = '\0';
                        newStr[j + 1] = '\0';
                        sprintf(writeStr, "%s%d", newStr, pid);
                        j++;
                    }
                }
                // duplicate variable-expanded string to argument placeholder
                comStruct->newargv[i] = strdup(writeStr);
            }
        }

        // Advance the token
        token = strtok(NULL, " ");
    }

    // The struct is now populated, give it back
    return comStruct;
}

// exitProg executes on exit and frees allocated data from the command struct
void exitProg(Command * userCommand)
{
    // free allocated args from previous command
    int i = 0;
    while(userCommand->newargv[i] != NULL)
    {
        free(userCommand->newargv[i]);
        userCommand->newargv[i] = NULL;
        i++;
    }
}
