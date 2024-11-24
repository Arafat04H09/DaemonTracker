/*
 * Legion: Command-line interface
 */

#include "legion.h"
#include "debug.h"
#include "string.h"
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#define DAEMON_STATE_INACTIVE 0
#define DAEMON_STATE_STARTING 1
#define DAEMON_STATE_ACTIVE 2
#define DAEMON_STATE_STOPPING 3
#define DAEMON_STATE_EXITED 4
#define DAEMON_STATE_CRASHED 5

#define MAX_DAEMONS 100

int parseAndExecuteCommand(char* input, FILE* out);
void handleHelp(FILE* out);
void handleRegister(char** args, int nargs, FILE* out);
void handleUnregister(char** args, int nargs, FILE* out);
void handleStart(char** args, int nargs, FILE* out);
void handleStop(char** args, int nargs, FILE* out);
void handleStatus(char** args, int nargs, FILE* out);
void handleStatusAll(FILE* out);
void handleLogrotate(char** args, int nargs, FILE* out);

void handleAlarm(int sig);
void handle_sigchld(int sig);
void setup_signal_handlers();

typedef struct daemon {
    char* name;                           
    char* command;                     
    char** args;                         
    int pid;                              
    int state;                            
    int exited;                         
    union {
        int exit_status;                
        int crash_signal;                
    };
    struct timeval last_change_time;      
    struct timeval last_event_time;       
    struct timeval next_event_timeout;   
} DAEMON;

DAEMON* daemons[MAX_DAEMONS]; 
int numDaemons = 0;           
volatile sig_atomic_t timeout = 0;
volatile sig_atomic_t programClose = 0;
volatile sig_atomic_t sigchildRecieved;

DAEMON* findDaemon(const char* name) {
    for (int i = 0; i < numDaemons; i++) {
        if (strcmp(daemons[i]->name, name) == 0) {
            return daemons[i];
        }
    }
    return NULL;
}

void handle_alarm(int sig) {
    timeout = 1;
}

void handle_sigchld(int sig) {
    sigchildRecieved = 1;
}

void handle_sigint(int sig) {
    programClose = 1;
}

void setup_signal_handlers() {
    struct sigaction saAlarm , saChild, saInt;

    memset(&saAlarm, 0, sizeof(saAlarm));
    memset(&saChild, 0, sizeof(saChild));
    memset(&saInt, 0, sizeof(saInt));

    saAlarm.sa_handler = handle_alarm;
    saChild.sa_handler = handle_sigchld;
    saInt.sa_handler = handle_sigint;

    sigaction(SIGALRM, &saAlarm, NULL);
    sigaction(SIGCHLD, &saChild, NULL);
    sigaction(SIGINT, &saInt, NULL);

}

void run_cli(FILE* in, FILE* out) {
    char* input = NULL;
    size_t bufsize = 0;
    ssize_t nread;
    setup_signal_handlers();  

    while (programClose != 1) {
        sf_prompt();
        fprintf(out, "legion> ");
        fflush(out);

        nread = getline(&input, &bufsize, in);
        if (nread == EOF) {
            free(input);
            sf_error("EOF. \n");
            break; 
        }

        if (parseAndExecuteCommand(input, out) == 0) {
            free(input); 
            break; 
        }
    }

    char* stopArgs[3];  
    stopArgs[0] = NULL;

    for (int i = 0; i < numDaemons; i++) {
        if (((daemons[i]-> state) == DAEMON_STATE_ACTIVE)) {
            stopArgs[1] = daemons[i]->name; 
            handleStop(stopArgs, 1, out); 
        } 
    }
}

int parseAndExecuteCommand(char* input, FILE* out) {
    char** args = NULL;
    int nargs = 0;
    char* next;
    char* string = input;
    bool quotation = false;
    char* start;

    while (*string) {  
        while (isspace((unsigned char)*string)) string++;  

        if (*string == '\0') break; 

        if (*string == '\'') {
            quotation = true;
            start = ++string;  
            next = strchr(string, '\'');  
            if (next) {
                quotation = false;  
            } else {
                next = strchr(string, '\0');  
            }
        } else {
            start = string;
            while (*string && (quotation || !isspace((unsigned char)*string))) {
                if (*string == '\'' && !quotation) {
                    quotation = true;  
                } else if (quotation && *string == '\'') {
                    quotation = false;  
                }
                string++;
            }
            next = string;
        }

        if (*next) {
            *next = '\0';  
            string = next + 1;
        } else {
            string = next;
        }

        char** temp = realloc(args, sizeof(char*) * (nargs + 1));
        if (!temp) {
            sf_error("Error: Memory allocation failed\n");
            fflush(out);
            free(args);
            return -1;  
        }
        args = temp;
        args[nargs] = strdup(start);  
        nargs = nargs + 1;
    }

    if (nargs == 0) {
        sf_error("Error: No command entered.\n");
        free(args);
        return 1;  
    }

    if (strcmp(args[0], "help") == 0) {
        handleHelp(out);
    } else if (strcmp(args[0], "quit") == 0) {
        return 0; 
    } else if (strcmp(args[0], "register") == 0) {
        handleRegister(args, nargs, out);
    } else if (strcmp(args[0], "unregister") == 0) {
        handleUnregister(args, nargs, out);
    } else if (strcmp(args[0], "start") == 0) {
        handleStart(args, nargs, out);
    } else if (strcmp(args[0], "stop") == 0) {
        handleStop(args, nargs, out);
    } else if (strcmp(args[0], "status") == 0) {
        handleStatus(args, nargs, out);
    } else if (strcmp(args[0], "status-all") == 0) {
        handleStatusAll(out);
    } else if (strcmp(args[0], "logrotate") == 0) {
        handleLogrotate(args, nargs, out);
    } else {
        fprintf(out, "Unknown command!\n");
        fflush(out);
    }

    for (int i = 0; i < nargs; i++) {
        free(args[i]);
    }
    free(args);
    return 1;  
}

void handleHelp(FILE* out) {
    fprintf(out, "Available commands: \nhelp (0 args) Print this help message \nquit (0 args) Quit the program\nregister (2+ args) Register a daemon\nunregister (1 args) Unregister a daemon\nStatus (1 args) Show the status of a daemon\nstatus-all (0 args) Show the status of all daemons\nstart (1 args) Start a daemon\nstop (1 args) Stop a daemon\nlogrotate (1 args) Rotate log files for a daemon\n");
    fflush(out);
}

void addDaemonToList(DAEMON* daemon) {
    if (numDaemons < MAX_DAEMONS) {
        daemons[numDaemons++] = daemon;
    }
}

void handleRegister(char** args, int nargs, FILE* out) {
    if (nargs < 3) {
        sf_error("Error. Not enough arguments to register a daemon. \n");
        return;
    }

    if (numDaemons >= MAX_DAEMONS) {
        sf_error("Error. Maximum number of daemons reached. \n");
        return;
    }

    DAEMON* newDaemon = malloc(sizeof(DAEMON));
    if (newDaemon == NULL) {
        sf_error("Memory allocation failed for new daemon. \n");
        return;
    }

    newDaemon->name = strdup(args[1]);
    newDaemon->command = strdup(args[2]);
    newDaemon->state = DAEMON_STATE_INACTIVE;
    newDaemon->exited = 0;
    newDaemon->pid = 0;

    newDaemon->args = malloc(sizeof(char*) * (nargs - 2 + 1)); 
    if (newDaemon->args == NULL) {
        sf_error("Memory allocation failed for daemon arguments. \n");
        free(newDaemon->name);
        free(newDaemon->command);
        free(newDaemon);
        return;
    }

    for (int i = 2; i < nargs; i++) {
        newDaemon->args[i - 2] = strdup(args[i]);
    }
    newDaemon->args[nargs - 2] = NULL; 

    addDaemonToList(newDaemon);

    sf_register(newDaemon->name, newDaemon->command);
}

void handleUnregister(char** args, int nargs, FILE* out) {
    if (nargs != 2) {
        sf_error("Error. Incorrect number of arguments for unregister command. \n");
        return;
    }

    char* daemonName = args[1];
    int foundIndex = -1;

    for (int i = 0; i < numDaemons; i++) {
        if (strcmp(daemons[i]->name, daemonName) == 0) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex == -1) {
        sf_error("Error. Daemon not found. \n");
        return;
    }

    if (daemons[foundIndex]->state != DAEMON_STATE_INACTIVE) {
        sf_error("Error. Daemon must be inactive to unregister. \n");
        return;
    }

    sf_unregister(daemons[foundIndex]->name);

    free(daemons[foundIndex]->name);
    free(daemons[foundIndex]->command);
    for (int i = 0; daemons[foundIndex]->args[i] != NULL; i++) {
        free(daemons[foundIndex]->args[i]);
    }
    free(daemons[foundIndex]->args);
    free(daemons[foundIndex]);

    for (int i = foundIndex; i < numDaemons - 1; i++) {
        daemons[i] = daemons[i + 1];
    }
    daemons[numDaemons - 1] = NULL;
    numDaemons--;
}

const char* stateToString(int state) {
    switch (state) {
        case DAEMON_STATE_INACTIVE: return "inactive";
        case DAEMON_STATE_STARTING: return "starting";
        case DAEMON_STATE_ACTIVE: return "active";
        case DAEMON_STATE_STOPPING: return "stopping";
        case DAEMON_STATE_EXITED: return "exited";
        case DAEMON_STATE_CRASHED: return "crashed";
        default: return "unknown";
    }
}

void handleStatus(char** args, int nargs, FILE* out) {
    if (nargs < 2) {
        sf_error("Invalid number of arguments for status command. \n");
        return;
    }

    const char* daemonName = args[1];
    bool found = false;

    for (int i = 0; i < numDaemons; i++) {
        if (strcmp(daemons[i]->name, daemonName) == 0) {
            fprintf(out, "%s\t%d\t%s\n", daemons[i]->name, daemons[i]->pid, stateToString(daemons[i]->state));
            fflush(out);
            found = true;
            break;
        }
    }

    if (!found) {
        sf_error("No daemon registered under that name! \n");
    }
}

void handleStatusAll(FILE* out) {
    if (numDaemons == 0) {
        fprintf(out, "No daemons registered.\n");
        fflush(out);
        return;
    }

    for (int i = 0; i < numDaemons; i++) {
        fprintf(out, "%s\t%d\t%s\n", daemons[i]->name, daemons[i]->pid, stateToString(daemons[i]->state));
        fflush(out);
        sf_status(daemons[i]-> name);
    }
}

void handleStart(char** args, int nargs, FILE* out) {
    if (nargs < 2) {
        sf_error("Unable to start with unsufficient arguments. \n");
        return;
    }

    const char* daemonName = args[1];
    DAEMON* daemon = findDaemon(daemonName);
    if (!daemon || daemon->state != DAEMON_STATE_INACTIVE) {
        sf_error("Could not find daemon or daemon state is not inactive. \n");
        return;
    }

    mkdir(LOGFILE_DIR, 0755);  
    sf_start(daemon->name);
    daemon->state = DAEMON_STATE_STARTING;

    int pipefds[2];
    if (pipe(pipefds) < 0) {
        sf_error("Error executing Start. \n");
        daemon-> state = DAEMON_STATE_INACTIVE;
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed. \n");
        daemon-> state = DAEMON_STATE_INACTIVE;
        close(pipefds[0]);
        close(pipefds[1]);
        return;
    }

    if (pid == 0) { 
        daemon->pid = pid;
        close(pipefds[0]); 
        dup2(pipefds[1], SYNC_FD);

        if (setpgid(0, 0) != 0) {  
            sf_error("setpgid failed. \n");
            close(pipefds[1]);
            return;
        }

        char logFilePath[256];
        snprintf(logFilePath, sizeof(logFilePath), "%s/%s.log.0", LOGFILE_DIR, daemonName);
        int logFd = open(logFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (logFd < 0) {
            sf_error("Failed to open log file. \n");
            close(pipefds[1]);
            return;
        }

        if (dup2(logFd, STDOUT_FILENO) < 0) {  
            sf_error("dup2 failed. \n");
            close(logFd);
            close(pipefds[1]);
            return;
        }

        close(logFd);  

        char fullPath[1024];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", DAEMONS_DIR, daemon->command);

        char* newEnv[] = {NULL, NULL};
        char newPath[1024];
        snprintf(newPath, sizeof(newPath), "%s:%s", DAEMONS_DIR, getenv("PATH"));
        newEnv[0] = newPath;
        execvpe(fullPath, daemon->args, newEnv); 
        sf_error("execvpe failed. \n");
        daemon->state = DAEMON_STATE_INACTIVE;
        close(pipefds[1]);
        return;
       }
    else {
        daemon->pid = pid;
        close(pipefds[1]); 
        char syncByte;
        ssize_t size = 0;

        alarm(CHILD_TIMEOUT);  
        while (timeout == 0) {
            size = read(pipefds[0], &syncByte, sizeof(char));
            if (size > 0) {
                alarm(0);
                break;
            }
        }
        if(size < 1) {
                if (timeout) {
                    sf_error("Daemon startup timed out.\n");
                } else {
                    sf_error("Failed to synchronize with daemon.\n");
                }
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);  
                daemon->state = DAEMON_STATE_INACTIVE;
                close(pipefds[0]);
        }
        daemon-> state = DAEMON_STATE_ACTIVE;  
        sf_active(daemon-> name, daemon-> pid);
        close(pipefds[0]);
    }
}

void handleStop(char** args, int nargs, FILE* out) {
    const char* daemonName = args[1];
    DAEMON* daemon = findDaemon(daemonName);
    if (!daemon) {
        sf_error("Could not find daemon! \n");
        return;
    }

    if (((daemon-> state) == DAEMON_STATE_CRASHED) || ((daemon-> state) == DAEMON_STATE_EXITED)) {
        daemon-> state = DAEMON_STATE_INACTIVE;
        sf_reset(daemon->name);
        return;
    }

    if (daemon-> state != DAEMON_STATE_ACTIVE) {
        sf_error("Error with stop");
        return;
    }

    daemon-> state = DAEMON_STATE_STOPPING;
    
    kill(daemon-> pid, SIGTERM);
    sf_stop(daemon-> name, daemon-> pid);

    alarm(CHILD_TIMEOUT);
    pid_t exit = waitpid(daemon-> pid, &(daemon-> exit_status),0);

    while (timeout == 0) {
        if (exit != -1) {
            daemon-> state = DAEMON_STATE_EXITED;
            sf_term(daemon-> name, daemon-> pid, daemon-> exit_status);
            daemon-> pid = 0;
            return;
        }
    }

    alarm(0);
    kill(daemon-> pid, SIGKILL);
    daemon-> pid = 0;
    sf_error("Timed out");
}

void handleLogrotate(char** args, int nargs, FILE* out) {
    if (nargs < 2) {
        sf_error("Not enough arguments to run Log Rotate, \n");
        return;
    }

    const char* daemonName = args[1];
    DAEMON* daemon = findDaemon(daemonName);
    if (!daemon) {
        sf_error("No such daemon registered.\n");
        return;
    }

    char oldPath[256], newPath[256];
    int maxVersion = LOG_VERSIONS - 1; 

    mkdir(LOGFILE_DIR, 0755);

    snprintf(oldPath, sizeof(oldPath), "%s/%s.log.%d", LOGFILE_DIR, daemonName, maxVersion);
    unlink(oldPath);

    for (int i = maxVersion - 1; i >= 0; i--) {
        snprintf(oldPath, sizeof(oldPath), "%s/%s.log.%d", LOGFILE_DIR, daemonName, i);
        snprintf(newPath, sizeof(newPath), "%s/%s.log.%d", LOGFILE_DIR, daemonName, i + 1);
        rename(oldPath, newPath);
    }

    if (daemon->state == DAEMON_STATE_ACTIVE) {
        char* stopArgs[] = { "stop", (daemon-> name), NULL };
        handleStop(stopArgs, 2, out);
        daemon-> state = DAEMON_STATE_INACTIVE;
        
        sf_logrotate(daemon -> name);

        char* startArgs[] = { "start", (daemon-> name), NULL };
        handleStart(startArgs, 2, out);
    }
}
