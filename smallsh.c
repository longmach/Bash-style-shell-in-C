/* 
* Long Mach
* CS 344-400
* Assignment 3: smallsh 
*/

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

//set max length and max arguments in the command
#define MAX_LENGTH 2048
#define MAX_ARG 512

// to allow enter and exit background only mode when receiving signal Control-Z
int allowBackground = 1;

/* struct for commands */
struct command
{
    char *cmd;          //command
    char *arg;          //argument
    char *inputFile;    //input file name
    char *outputFile;   //output file name
    int background;     //does this run in the background
    int argNum;         //numer of argument
    int exit;           //to avoid special character in the echo command
    int argSize;        //length of arg in bytes
};

struct command *parseCmd();                             //to parse command input into struct
void printCmd (struct command *);                       //print for debugging
void runCmd(struct command *, int*, struct sigaction);  //run the command function
void printExitStatus(int);                              //build-in status function
void handle_SIGTSTP(int);                               //function to handle SIGTSTP

int main()
{
    //Signal handlers adapted from : https://canvas.oregonstate.edu/courses/1830250/pages/exploration-signal-handling-api?module_item_id=21468881
    //SIGINT handler- set main to ignore it
    struct sigaction SIGINT_action = {{0}};     // Fill out the SIGINT_action struct
    SIGINT_action.sa_handler = SIG_IGN;         // Register SIG_IGN as the signal handler to ignore in main
	sigfillset(&SIGINT_action.sa_mask);         // Block all catchable signals while handle_SIGINT is running
	SIGINT_action.sa_flags = 0;                 // No flags set
	sigaction(SIGINT, &SIGINT_action, NULL);    // Register SIGINT signal
	
	//SIGTSTP handler
    struct sigaction SIGTSTP_action = {{0}};    // Fill out the SIGTSTP_action struct
	SIGTSTP_action.sa_handler = handle_SIGTSTP; // Register SIG_IGN as the signal handler
	sigfillset(&SIGTSTP_action.sa_mask);        // Block all catchable signals while handle_SIGINT is running
	SIGTSTP_action.sa_flags = SA_RESTART;       // Set Restart flag
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);  // Register SIGTSTP signal

	//set variables, set childStatus as 0 so if run status first then it will also return 0
    int childStatus = 0;
    struct command *newCmd;
    
    //run terminal until users input "exit"
    while (1)
    {    
        //get user command and parse it into struct
        newCmd = parseCmd();
        
        //if command is exit
        if (strcmp(newCmd->cmd, "exit") == 0)
        {
            //Free all allocated memory before exit to avoid memory leak
            free(newCmd->cmd);
            if(newCmd->argNum > 0)
            {
                free(newCmd->arg);
            }
            
            free(newCmd->inputFile);
            free(newCmd->outputFile);
            free(newCmd);
            
            //exit main
            return 0;

        } 

        //if command is cd
        else if (strncmp(newCmd->cmd, "cd", 2) == 0)
        {
            //check if there is a specific directory users want to go to
            if (newCmd->argNum > 0)
            {
                //if the directory cannot be open, print error
                if (chdir(newCmd->arg) == -1)
                {
                    printf("Directory not found.\n");
					fflush(stdout);
                }
            }
            //if users want to go to home directory
            else
            {
                chdir(getenv("HOME"));
            }
        }
        
        //print out status
        else if (strcmp(newCmd->cmd, "status") == 0)
        {
            printExitStatus(childStatus);
        }
        
        //run other commands
        else 
        {
            runCmd(newCmd, &childStatus, SIGINT_action);
        }

        // free all allocated memory after finish running a command to avoid memory leak
        free(newCmd->cmd);
        if(newCmd->argNum > 0)
        {
            free(newCmd->arg);
        }
        free(newCmd->inputFile);
        free(newCmd->outputFile);
        free(newCmd);
    }

    return 0;
}

// function to get command and parse 
struct command *parseCmd()
{
    //get main Pid
    int mainPid = 0;
    mainPid = getpid();

    //reprompt if input is blank or comment
    reprompt:
    if(1)
    {
        //allocate struct command and set variables
        struct command *newCmd = malloc(sizeof(struct command));
        newCmd->exit = 0;
        newCmd->inputFile = NULL;
        newCmd->outputFile = NULL;
        newCmd->background = 0;
        
        char currLine[MAX_LENGTH];
        char tempCopy[MAX_LENGTH] = "";

        //print out the prompt
        printf(": ");
        fflush(stdout);
        
        //get input
        fgets(currLine, MAX_LENGTH, stdin);

        //if commnent or blank
        if ((strcmp(currLine, "\n") == 0) || (strncmp(currLine, "#", 1) == 0)) 
        {
            goto reprompt;
        }
        
        //remove the next line character and replace it with \0 to make char array or string so it will be easier to parse later
        currLine[strcspn(currLine, "\r\n")] = 0;

        //check for expansion
        char *expansion = "$$";

        //if expansion existed in the input, copy each character from currLine to tempCopy
        if(strstr(currLine, expansion) != NULL)
        {
            int j = 0;
            for (int i = 0; currLine[i]; i++)
            {
                //replace $$ with mainPid
                if (currLine[i] == '$' && currLine[i + 1] == '$')
                {
                    char num [100];
                    sprintf(num, "%d", mainPid);        //convert mainPid to string
                    int len = strlen(num);             
                    strcat(tempCopy, num);              //then add it to tempCopy
                    i++;
                    j = j + len;                        //jump the index for tempCopy
                }
                else
                {
                    tempCopy[j] = currLine[i];          //otherwise copy each character
                    j++;
                }
            }
            
            memset(currLine, 0, sizeof currLine);       //reset currLine
            strcpy(currLine, tempCopy);                 //copy from tempCopy back to currLine
            memset(tempCopy, 0, sizeof tempCopy);       //reset tempCopy
        }

        // For use with strtok_r
        char *saveptr;

        // The first token is the cmd
        char *token = strtok_r(currLine, " ", &saveptr);
        newCmd->cmd = calloc(strlen(token) + 1, sizeof(char));
        strcpy(newCmd->cmd, token);

        int i = 0;
        
        //next token
        token = strtok_r(NULL, " ", &saveptr);

        //if next token is valid
        while(token)
        {
            // set background if contain & and cmd is NOT echo
            if((strncmp(token, "&", 1) == 0) && (strcmp(newCmd->cmd, "echo") != 0))
            {
                //set background process if main allow background process to run
                if(allowBackground == 1)
                {
                    newCmd->background = 1;
                }
                else
                {
                    newCmd->background = 0;
                }
                break;
            }

            // set file input if contain < or (-f and command is NOT echo)
            else if ((strcmp(token, "<") == 0) || ((strcmp(token, "-f") == 0) && (strcmp(newCmd->cmd, "echo") != 0)))
            {
                //if there is -f in token, set it to file input
                  if(strcmp(token, "-f") == 0)
                {
                    newCmd->exit = 1;
                }
                token = strtok_r(NULL, " ", &saveptr);                          //find the next token
                newCmd->inputFile = calloc(strlen(token) + 1, sizeof(char));    //allocate inputFile name
                strcpy(newCmd->inputFile, token);                               //copy to inputFile name
            }

            // set file output if contain >
            else if (strcmp(token, ">") == 0)
            {
                token = strtok_r(NULL, " ", &saveptr);                          //find the next token
                newCmd->outputFile = calloc(strlen(token) + 1, sizeof(char));   //allocate outputFile name
                strcpy(newCmd->outputFile, token);                              //copy to inputFile name
            }

            //else set argument
            else
            {
                //if no previous argument, set the first one
                if (i == 0)
                {
                    newCmd->argSize = strlen(token) + 1;                        //set argument size in bytes
                    newCmd->arg = calloc(strlen(token) + 1, sizeof(char));      //allocate outputFile name
                    strcpy(newCmd->arg, token);                                 //copy to inputFile name
                    
                }
                //if there was previsou argument
                else 
                {
                    char *temp = calloc(newCmd->argSize, sizeof(char));         //allocate
                    strcpy(temp, newCmd->arg);                                  //copy arg to temp 

                    free(newCmd->arg);                                          //free arg
                    newCmd->argSize = newCmd->argSize + strlen(token) + 2;      //set argument size in bytes
                    
                    newCmd->arg = calloc(newCmd->argSize, sizeof(char));        //allocate arg again
                    strcat(newCmd->arg, temp);                                  //add temp then blank then new token to arg
                    strcat(newCmd->arg, " ");
                    strcat(newCmd->arg, token);

                    free(temp);                                                 //free arg
                }
                i++;
            }
            //next token
            token = strtok_r(NULL, " ", &saveptr);
        }
        
        //set number of arg
        newCmd->argNum = i;

        return newCmd;
    }
}

/* 
* function to print out the struct command after parsing for debugging
*/
void printCmd (struct command *aCmd)
{
    printf("%s \n", aCmd->cmd);
    printf("%s \n", aCmd->arg);
    printf("%s \n", aCmd->inputFile);
    printf("%s \n", aCmd->outputFile);
    printf("%d \n", aCmd->background);
    printf("%d \n", aCmd->argNum);
    printf("%d \n", aCmd->exit);
}

/* 
* function to run command
# Citation for the following function:
# Date: 10/30/2021
# Copied from /OR/ Adapted from /OR/ Based on:
* https://canvas.oregonstate.edu/courses/1830250/pages/exploration-process-api-executing-a-new-program?module_item_id=21468874
*/
void runCmd (struct command *newCmd, int *childStatus, struct sigaction SIGINT_action)
{
	// Fork a new process
    pid_t spawnPid = -5;
    if (newCmd->argNum > 0)
    {
        //if there is SIGTSTP signal in the command, do not folk a child, just set spawdPid as 0 to run the command to send SIGTSTP signal
        if (strncmp(newCmd->arg, "-SIGTSTP", 8) == 0)
        {
            spawnPid = 0;
        }
        else
        {
            spawnPid = fork();
        }
    }
    else
    {
            spawnPid = fork();
    }
    
    //set variables to default at beginning of every command to avoid memory remained from previous command
    int stdin_copy = dup(0);
    int stdout_copy = dup(1);
    int inFile = 0, 
        outFile = 0, 
        inDevNull = 0, 
        outDevNull = 0;

    switch(spawnPid)
    {
        //if folk fails, print erro
        case -1:
            perror("fork()\n");
            fflush(stdout);
            exit(1);
            break;

        //if folk succeeds
        case 0:

            //SIGINT signal, set child to default action so child will not ignore it
            SIGINT_action.sa_handler = SIG_DFL;
            sigaction(SIGINT, &SIGINT_action, NULL);

            //redirect
            //Adapted from: https://stackoverflow.com/questions/9084099/re-opening-stdout-and-stdin-file-descriptors-after-closing-them
            //set input direction
            if (newCmd->inputFile)
            {
                
                inFile = open(newCmd->inputFile, O_RDONLY);
                //if error
                if (inFile == -1)
                {
                    if(newCmd->exit == 0)
                    {
                        printf("cannot open %s for input \n", newCmd->inputFile);
                        fflush(stdout);
                    }
                    //*childStatus = 1;
                    exit(1);
                }
                dup2(inFile, 0); 
            }
            //if background and no input direction
            else if ((newCmd->background == 1) && (allowBackground == 1))
            {

                inDevNull = open("/dev/null", O_RDONLY);
                dup2(inDevNull, 0); 
            }

            //set output direction
            if (newCmd->outputFile)
            {

                outFile = open(newCmd->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                //if error
                if (outFile == -1)
                {
                    printf("cannot open \"%s\" for output \n", newCmd->outputFile);
                    fflush(stdout);
                    //*childStatus = 1;
                    exit(1);
                    //return;
                }
                dup2(outFile, 1); 
            }
            //if background and no output direction
            else if ((newCmd->background == 1) && (allowBackground == 1))
            {

                outDevNull = open("/dev/null", O_WRONLY);
                dup2(outDevNull, 1); 
            }

            //run the child process
            if (newCmd->argNum > 0)
            {
                //if SIGTSTP signal received, send it to main for main to use handler
                if (strncmp(newCmd->arg, "-SIGTSTP", 8) == 0)
                {
                    int mainPid = getpid();
                    kill(mainPid, SIGTSTP);
                    break;
				}
                //normal command exec with argument
                else
                {
                    execlp(newCmd->cmd, newCmd->cmd, newCmd->arg, NULL);
                }
                
            }
            //normal command exec with no argument
            else
            {
                execlp(newCmd->cmd, newCmd->cmd, NULL);
            }

            //Adapted from: https://stackoverflow.com/questions/9084099/re-opening-stdout-and-stdin-file-descriptors-after-closing-them
            //redirect back to stdin
            if(newCmd->inputFile)
            {
                close(inFile);
                dup2(stdin_copy, 0);
                close(stdin_copy);
            }
            else if(inDevNull)
            {
                close(inDevNull);
                dup2(stdin_copy, 0);
                close(stdin_copy);
            }
            
            //redirect back to stdout
            if(newCmd->outputFile)
            {
                close(outFile);
                dup2(stdout_copy, 1);
                close(stdout_copy);
            }
            else if(outDevNull)
            {
                close(outDevNull);
                dup2(stdout_copy, 1);
                close(stdout_copy);
            }

            //if exec error
            perror(newCmd->cmd);
            fflush(stdout);
            exit(2);
            break;

        //parent process    
        default:
            // In the parent process
            // Wait for child's termination
            //if background process
            if ((newCmd->background == 1) && (allowBackground == 1))
            {
                printf("background pid is %d \n", spawnPid);
                fflush(stdout);
                spawnPid = waitpid(spawnPid, childStatus, WNOHANG);
            }
            //foreground process
            else
            {
                spawnPid = waitpid(spawnPid, childStatus, 0);
                if (WIFSIGNALED(*childStatus))
                {
                    printf("terminated by signal %d\n", WTERMSIG(*childStatus));
                    fflush(stdout);
                }
            }

        	// Check for terminated background processes!	
            while ((spawnPid = waitpid(-1, childStatus, WNOHANG)) > 0) {
                printf("background pid %d is done: ", spawnPid);
                printExitStatus(*childStatus);
                fflush(stdout);
            }
    }
}

//built-in function for status
//Adapted from: https://canvas.oregonstate.edu/courses/1830250/pages/exploration-process-api-monitoring-child-processes?module_item_id=21468873
void printExitStatus(int childExitMethod) {
	
	if (WIFEXITED(childExitMethod)) {
		// If child process exited normally
		printf("exit value %d\n", WEXITSTATUS(childExitMethod));
	} else {
		// If child process is terminated by signal
		printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
	}
}

/* Our signal handler for SIGINT */
// Adapted from: https://canvas.oregonstate.edu/courses/1830250/pages/exploration-signal-handling-api?module_item_id=21468881
void handle_SIGTSTP(int signo){
	if(allowBackground == 1)
    {
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        // We are using write rather than printf
        write(STDOUT_FILENO, message, 49);
        allowBackground = 0;  
    }
    else
    {
        char* message = "Exiting foreground-only mode\n";
        // We are using write rather than printf
        write(STDOUT_FILENO, message, 29);
        allowBackground = 1;     
    }
	
}