#include "systemcalls.h"
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    int result = system(cmd);
    if (result == -1)
    {
        fprintf(stderr, "Error: System call failed. Command: \"%s\".\n", cmd);
        return false;
    }

    return (result == EXIT_SUCCESS);
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    va_end(args);

    fflush(stdout);
    fflush(stderr);

    int pid = fork();
    if (pid == -1)
    {
        fprintf(stderr, "Error: Fork call failed. Error: \"%s\".\n", strerror(errno));
        return false;
    }
    else if (pid == 0)
    {
        execv(command[0], command);
        fprintf(stderr, "Error: Execv call failed in child process. Error: \"%s\".\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    else
    {
        int waitStatus;
        int result = waitpid(pid, &waitStatus, 0);
        if (result == -1)
        {
            fprintf(stderr, "Error: Wait call failed. Error: \"%s\".\n", strerror(errno));
            return false;   
        }
        else if (WIFSIGNALED(waitStatus))
        {
            fprintf(stderr, "Error: Child process has been terminated. Signal: \"%s\".\n", strsignal(WTERMSIG(waitStatus)));
            return false;   
        }
        else if (WIFEXITED(waitStatus) && WEXITSTATUS(waitStatus) != EXIT_SUCCESS)
        {
            fprintf(stderr, "Error: Child process has been exited with failure. Exit Code: %d.\n", WEXITSTATUS(waitStatus));
            return false;   
        }
    }

    return true;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
    va_end(args);

    int fd = open(outputfile, O_WRONLY | O_CREAT);
    if (fd == -1)
    {
        fprintf(stderr, "Error: Cannot open file for writing. File Path: \"%s\", Error: \"%s\".\n", outputfile, strerror(errno));
        return false;
    }

    fflush(stdout);
    fflush(stderr);

    int pid = fork();
    if (pid == -1)
    {
        fprintf(stderr, "Error: Fork call failed. Error: \"%s\".\n", strerror(errno));
        return false;
    }
    else if (pid == 0)
    {
        int origStdout = dup(STDOUT_FILENO);
        int origStderr = dup(STDERR_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        execv(command[0], command);

        dup2(origStdout, STDOUT_FILENO);
        dup2(origStderr, STDERR_FILENO);
        fprintf(stderr, "Error: Execv call failed in child process. Error: \"%s\".\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    else
    {
        close(fd);

        int waitStatus;
        int result = waitpid(pid, &waitStatus, 0);
        if (result == -1)
        {
            fprintf(stderr, "Error: Wait call failed. Error: \"%s\".\n", strerror(errno));
            return false;   
        }
        else if (WIFSIGNALED(waitStatus))
        {
            fprintf(stderr, "Error: Child process has been terminated. Signal: \"%s\".\n", strsignal(WTERMSIG(waitStatus)));
            return false;   
        }
        else if (WIFEXITED(waitStatus) && WEXITSTATUS(waitStatus) != EXIT_SUCCESS)
        {
            fprintf(stderr, "Error: Child process has been exited with failure. Exit Code: %d.\n", WEXITSTATUS(waitStatus));
            return false;   
        }
    }

    return true;
}
