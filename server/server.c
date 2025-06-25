#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>

static bool g_exitProgram = false;
static int g_serverSocket = -1;
static int g_clientSocket = -1;
static int g_outputFile = -1;
static size_t g_lineBufferCursor = 0;
static const size_t g_lineBufferStartSize = 64;
static size_t g_lineBufferSize = 0;
static char* g_lineBuffer = NULL;
static const char* g_outputFilePath = "/var/tmp/aesdsocketdata";
static struct sigaction g_oldSigtermHandler;
static struct sigaction g_oldSigintHandler;

void SignalHandler()
{
    g_exitProgram = true;
}

void TearDown(int exitCode)
{
        syslog(LOG_INFO, "Terminating...");
        if (g_clientSocket != -1)
        {
            close(g_clientSocket);
            g_clientSocket = -1;
        }

        if (g_outputFile != -1)
        {
            close(g_outputFile);
            g_outputFile = -1;
        }

        remove(g_outputFilePath);

        if (g_serverSocket != -1)
        {
            close(g_serverSocket);
            g_serverSocket = -1;
        }

        if (g_lineBuffer != NULL)
        {
            free(g_lineBuffer);
            g_lineBuffer = NULL;
            g_lineBufferSize = 0;
        }

        if (g_exitProgram)
            syslog(LOG_ERR, "Caught signal exiting");
        
        closelog();

        sigaction(SIGTERM, &g_oldSigtermHandler, NULL);
        sigaction(SIGTERM, &g_oldSigintHandler, NULL);

        exit(exitCode);
}

void Initialize()
{
    syslog(LOG_INFO, "Initializing...");

    openlog(NULL, LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Started");

    struct sigaction signalAction;
    memset(&signalAction, 0, sizeof(signalAction));
    signalAction.sa_handler = SignalHandler;
    
    if (sigaction(SIGTERM, &signalAction, &g_oldSigtermHandler) != 0)
    {
        syslog(LOG_ERR, "Cannot register signal handler.");
        TearDown(EXIT_FAILURE);   
    }
    
    if (sigaction(SIGINT, &signalAction, &g_oldSigintHandler) != 0)
    {
        syslog(LOG_ERR, "Cannot register signal handler.");
        TearDown(EXIT_FAILURE);   
    }
 
    g_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_serverSocket == -1)
    {
        syslog(LOG_ERR, "Cannot create socket. Error No: %d, Error Text: \"%s\".", errno, strerror(errno));
        TearDown(EXIT_FAILURE);
    }

    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(9000);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    int bindResult = bind(g_serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    if (bindResult == -1)
    {
        syslog(LOG_ERR, "Cannot bind socket. Error No: %d, Error Text: \"%s\".", errno, strerror(errno));
        TearDown(EXIT_FAILURE);
    }
}

void ProcessPackage()
{
    if (write(g_outputFile, g_lineBuffer, g_lineBufferCursor) == -1)
    {
        if (errno == EINTR && g_exitProgram)
            TearDown(EXIT_SUCCESS);

        syslog(LOG_ERR, "Cannot write to file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
        TearDown(EXIT_FAILURE);                   
    }

    fsync(g_outputFile);

    off_t fileSize = lseek(g_outputFile, 0, SEEK_END);
    if (fileSize == -1)
    {
        syslog(LOG_ERR, "Cannot seek at the end of file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
        TearDown(EXIT_FAILURE);
    }
    
    if (lseek(g_outputFile, 0, SEEK_SET) == -1)
    {
        syslog(LOG_ERR, "Cannot seek at the start of file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
        TearDown(EXIT_FAILURE); 
    }
    
    while (fileSize > 0)
    {
        char fileBuffer[512];
        int readBytes = read(g_outputFile, fileBuffer, sizeof(fileBuffer));
        if (readBytes == -1)
        {
            if (errno == EINTR && g_exitProgram)
                TearDown(EXIT_SUCCESS);

            syslog(LOG_ERR, "Cannot read from file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
            TearDown(EXIT_FAILURE);
        }

        int sendResult = send(g_clientSocket, fileBuffer, readBytes, 0);
        if (sendResult == -1)
        {
            if (errno == EINTR && g_exitProgram)
                TearDown(EXIT_SUCCESS);

            syslog(LOG_ERR, "Cannot send bytes to file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
            break;
        }

        fileSize -= readBytes;
    }

    g_lineBufferCursor = 0;
}

void ParsePackage(const char* recvBuffer, size_t recvBytes)
{
    for (size_t i = 0; i < recvBytes; i++)
    {
        // Exponential Line Buffer Heap Allocation
        if (g_lineBufferCursor + 1 > g_lineBufferSize)
        {
            if (g_lineBuffer == NULL)
            {
                g_lineBufferSize = g_lineBufferStartSize;
                g_lineBuffer = malloc(g_lineBufferSize);
                if (g_lineBuffer == NULL)
                {
                    syslog(LOG_ERR, "Cannot allocate line buffer memory.");
                    TearDown(EXIT_FAILURE);
                }
            }
            else
            {
                g_lineBufferSize *= 2;
                g_lineBuffer = realloc(g_lineBuffer, g_lineBufferSize);
                if (g_lineBuffer == NULL)
                {
                    syslog(LOG_ERR, "Cannot reallocate line buffer memory.");
                    TearDown(EXIT_FAILURE);
                }
            }
        }

        g_lineBuffer[g_lineBufferCursor] = recvBuffer[i];
        g_lineBufferCursor++;
        
        if (recvBuffer[i] == '\n')
            ProcessPackage();
    }
}

void MainLoop()
{
    int listenResult = listen(g_serverSocket, 10);
    if (listenResult == -1)
    {
        syslog(LOG_ERR, "Cannot listen socket. Error No: %d, Error Text: \"%s\".", errno, strerror(errno));
        TearDown(EXIT_FAILURE);
    }

    g_outputFile = open(g_outputFilePath, O_RDWR | O_CREAT, 0666);
    if (g_outputFile == -1)
    {
        syslog(LOG_ERR, "Cannot open file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
        TearDown(EXIT_FAILURE);
    }

    while (!g_exitProgram)
    {
        struct sockaddr_in clientAddress;
        unsigned int clientAddressSize = sizeof(clientAddressSize);
        memset(&clientAddress, 0, sizeof(clientAddress));
        g_clientSocket = accept(g_serverSocket, (struct sockaddr*)&clientAddress, &clientAddressSize);
        if (g_clientSocket == -1)
        {
            if (errno == EINTR && g_exitProgram)
                TearDown(EXIT_SUCCESS);

            syslog(LOG_ERR, "Cannot accept socket. Error No: %d, Error Text: \"%s\".", errno, strerror(errno));
            TearDown(EXIT_FAILURE);
        }

        while (!g_exitProgram)
        {
            char recvBuffer[512];
            int recvBytes = recv(g_clientSocket, recvBuffer, sizeof(recvBuffer), 0);
            if (recvBytes == 0)
            {
                syslog(LOG_INFO, "Closed connection from %d.%d.%d.%d", 
                    (int)((uint8_t*)&clientAddress.sin_addr)[3],
                    (int)((uint8_t*)&clientAddress.sin_addr)[2],
                    (int)((uint8_t*)&clientAddress.sin_addr)[1],
                    (int)((uint8_t*)&clientAddress.sin_addr)[0]
                );
                
                break;
            }
            else if (recvBytes == -1)
            {
                if (errno == EINTR)
                {
                    if (g_exitProgram)
                        TearDown(EXIT_SUCCESS);
                }

                syslog(LOG_ERR, "Socket recv error. Error No: %d, Error Text: \"%s\".", errno, strerror(errno));
                close(g_clientSocket);
                break;
            }

            ParsePackage(recvBuffer, recvBytes);
        }
    }
}

void StartDaemon()
{
    syslog(LOG_INFO, "Forking daemon...");

    fflush(stdout);
    fflush(stderr);

    int pid = fork();
    if (pid == 0)
    {
        syslog(LOG_INFO, "Running as daemon...");

        setsid();
        
        if (chdir("/") == -1)
        {
            syslog(LOG_ERR, "Cannot change current working directory.");
            TearDown(EXIT_FAILURE);
        }

        int nullFile = open("/dev/null", O_RDWR);
        dup2(nullFile, STDIN_FILENO);
        dup2(nullFile, STDOUT_FILENO);
        dup2(nullFile, STDERR_FILENO);
        close(nullFile);

        MainLoop();
        TearDown(EXIT_SUCCESS);
    }
    else
    {
        TearDown(EXIT_SUCCESS);
    }
}

void StartApplication()
{
    syslog(LOG_INFO, "Running as application...");
    MainLoop();
    TearDown(EXIT_SUCCESS);
}

void PrintHelp()
{
    printf(
        "aesdsocket - Simple Socket Utility\n"
        "---------------------------------------\n"
        "Usage: aesdsocket [-d]\n"
        "\n"
        "Arguments:\n"
        "  -d   Run as daemon.\n"
        "  -h   Display this help text.\n"
    );
}

int main(int argc, char** argv)
{
    bool daemonMode = false;

    int opt;
    while ((opt = getopt(argc, argv, "dh")) != -1)
    {
        switch (opt)
        {
            case 'd':
                daemonMode = true;
                break;
            
            case 'h':
                PrintHelp();
                exit(EXIT_SUCCESS);
                break;

            default:
                PrintHelp();
                exit(EXIT_FAILURE);
                break;
        }
    }

    Initialize();
    
    if (daemonMode)
        StartDaemon();
    else
        StartApplication();

    return EXIT_SUCCESS;
}