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
#include <pthread.h>

#define RETRY_ON_INTERRUPT(expression)                  \
({                                                      \
    int RETRY_ON_INTERRUPT_result = 0;                  \
    while (true)                                        \
    {                                                   \
        RETRY_ON_INTERRUPT_result = (expression);   \
        if (RETRY_ON_INTERRUPT_result == -1)            \
        {                                               \
            if (errno == EINTR)                         \
                continue;                               \
            else                                        \
                break;                                  \
        }                                               \
        else                                            \
        {                                               \
            break;                                      \
        }                                               \
    }                                                   \
    RETRY_ON_INTERRUPT_result;                          \
})

struct Client
{
    int socket;
    struct sockaddr_in address;
    pthread_t threadId;
    size_t lineBufferCursor;
    size_t lineBufferSize;
    char* lineBuffer;
    struct Client* next;
};

static bool g_exitProgram = false;
static int g_serverSocket = -1;
static int g_outputFile = -1;
static pthread_t timestampThread;
static struct Client* g_clientListHead;
static pthread_mutex_t g_clientListMutex;
static pthread_mutex_t g_outputFileMutex;
static const char* g_outputFilePath = "/var/tmp/aesdsocketdata";
static struct sigaction g_oldSigtermHandler;
static struct sigaction g_oldSigintHandler;
const size_t g_lineBufferStartSize = 64;

void TearDownClient(struct Client* client)
{
    syslog(LOG_INFO, "Terminating client... Client Id: %ld.", client->threadId);

    close(client->socket);

    if (client->lineBuffer != NULL)
    {
        free(client->lineBuffer);
        client->lineBuffer = NULL;
    }

    pthread_mutex_lock(&g_clientListMutex);
    if (g_clientListHead == client)
    {
        g_clientListHead = client->next;
    }
    else
    {
        struct Client* currentClient = g_clientListHead;
        while (currentClient != NULL)
        {
            if (currentClient->next == client)
            {
                currentClient->next = client->next;
                break;
            }
            currentClient = currentClient->next;
        }
    }
    pthread_mutex_unlock(&g_clientListMutex);
    free(client);
}

void TearDownServer(int exitCode)
{
    syslog(LOG_INFO, "Terminating server...");

    g_exitProgram = true;

    pthread_join(timestampThread, NULL);

    pthread_mutex_lock(&g_clientListMutex);
    struct Client* currentClient = g_clientListHead;
    while (currentClient != NULL)
    {
        pthread_t threadId = g_clientListHead->threadId;
        pthread_mutex_unlock(&g_clientListMutex);

        pthread_join(threadId, NULL);

        pthread_mutex_lock(&g_clientListMutex);
        currentClient = g_clientListHead;
    }
    pthread_mutex_unlock(&g_clientListMutex);

    g_clientListHead = NULL;

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

    if (g_exitProgram)
        syslog(LOG_ERR, "Caught signal exiting");
    
    closelog();

    sigaction(SIGTERM, &g_oldSigtermHandler, NULL);
    sigaction(SIGTERM, &g_oldSigintHandler, NULL);

    syslog(LOG_INFO, "Exiting process...");

    exit(exitCode);
}

bool ProcessPackage(struct Client* client)
{
    pthread_mutex_lock(&g_outputFileMutex);

    if (RETRY_ON_INTERRUPT(write(g_outputFile, client->lineBuffer, client->lineBufferCursor)) == -1)
    {
        pthread_mutex_unlock(&g_outputFileMutex);

        syslog(LOG_ERR, "Cannot write to file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
        TearDownClient(client);
        return false;                  
    }

    fsync(g_outputFile);
    pthread_mutex_unlock(&g_outputFileMutex);

    pthread_mutex_lock(&g_outputFileMutex);
    off_t fileSize = lseek(g_outputFile, 0, SEEK_END);
    if (fileSize == -1)
    {
        pthread_mutex_unlock(&g_outputFileMutex);
        
        syslog(LOG_ERR, "Cannot seek at the end of file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
        TearDownClient(client);
        return false;
    }
    
    if (lseek(g_outputFile, 0, SEEK_SET) == -1)
    {
        pthread_mutex_unlock(&g_outputFileMutex);

        syslog(LOG_ERR, "Cannot seek at the start of file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
        TearDownClient(client);
        return false;
    }
    
    while (fileSize > 0)
    {
        char fileBuffer[512];
        int readBytes = RETRY_ON_INTERRUPT(read(g_outputFile, fileBuffer, sizeof(fileBuffer)));
        if (readBytes == -1)
        {
            pthread_mutex_unlock(&g_outputFileMutex);

            syslog(LOG_ERR, "Cannot read from file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
            TearDownClient(client);
            return false;
        }

        int sendResult = RETRY_ON_INTERRUPT(send(client->socket, fileBuffer, readBytes, 0));
        if (sendResult == -1)
        {
            pthread_mutex_unlock(&g_outputFileMutex);

            syslog(LOG_ERR, "Cannot send bytes to file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
            TearDownClient(client);
            return false;
        }

        fileSize -= readBytes;
    }
    pthread_mutex_unlock(&g_outputFileMutex);

    client->lineBufferCursor = 0;

    return true;
}

bool ParsePackage(struct Client* client, const char* recvBuffer, size_t recvBytes)
{
    for (size_t i = 0; i < recvBytes; i++)
    {
        // Exponential Line Buffer Heap Allocation
        if (client->lineBufferCursor + 1 > client->lineBufferSize)
        {
            if (client->lineBuffer == NULL)
            {
                client->lineBufferSize = g_lineBufferStartSize;
                client->lineBuffer = malloc(client->lineBufferSize);
                if (client->lineBuffer == NULL)
                {
                    syslog(LOG_ERR, "Cannot allocate line buffer memory.");
                    TearDownClient(client);
                    return false;
                }
            }
            else
            {
                client->lineBufferSize *= 2;
                client->lineBuffer = realloc(client->lineBuffer, client->lineBufferSize);
                if (client->lineBuffer == NULL)
                {
                    syslog(LOG_ERR, "Cannot reallocate line buffer memory.");
                    TearDownClient(client);
                    return false;
                }
            }
        }

        client->lineBuffer[client->lineBufferCursor] = recvBuffer[i];
        client->lineBufferCursor++;
        
        if (recvBuffer[i] == '\n')
        {
            if (!ProcessPackage(client))
                return false;
        }
    }

    return true;
}

void* ClientLoop(void* argument)
{
    pthread_mutex_lock(&g_clientListMutex);
    struct Client* client = (struct Client*)argument;
    client->threadId = pthread_self();
    if (g_clientListHead == NULL)
    {
        g_clientListHead = client;
    }
    else
    {
        struct Client* currentClient = g_clientListHead;
        while (currentClient->next != NULL)
            currentClient = currentClient->next;
        currentClient->next = client;
    }
    pthread_mutex_unlock(&g_clientListMutex);

    while (!g_exitProgram)
    {
        char recvBuffer[512];
        int recvBytes = RETRY_ON_INTERRUPT(recv(client->socket, recvBuffer, sizeof(recvBuffer), 0));
        if (recvBytes == 0)
        {
            syslog(LOG_INFO, "Closed connection from %d.%d.%d.%d", 
                (int)((uint8_t*)&client->address.sin_addr)[3],
                (int)((uint8_t*)&client->address.sin_addr)[2],
                (int)((uint8_t*)&client->address.sin_addr)[1],
                (int)((uint8_t*)&client->address.sin_addr)[0]
            );
            TearDownClient(client);
            return NULL;
        }
        else if (recvBytes == -1)
        {
            syslog(LOG_ERR, "Socket recv error. Error No: %d, Error Text: \"%s\".", errno, strerror(errno));
            TearDownClient(client);
            return NULL;
        }

        if (!ParsePackage(client, recvBuffer, recvBytes))
            return NULL;
    }

    TearDownClient(client);
    return NULL;
}

void* TimestampLoop(void* params)
{
    (void)params;

    syslog(LOG_INFO, "Timestamp thread created.");


    int remaining = 10;
    while (!g_exitProgram)
    {
        remaining = sleep(remaining);
        if (remaining != 0)
            continue;
              
        remaining = 10;

        char timeBuffer[256];
        time_t currentTime = time(NULL);
        struct tm localTime;
        localtime_r(&currentTime, &localTime);
        strftime(timeBuffer, 256, "%a, %d %b %Y %T %z", &localTime);
        
        char lineBuffer[256];
        int lineSize = snprintf(lineBuffer, 256, "timestamp:%s\n", timeBuffer);

        syslog(LOG_INFO, "Writing timestamp %s.", timeBuffer);
     
        pthread_mutex_lock(&g_outputFileMutex);
        if (RETRY_ON_INTERRUPT(write(g_outputFile, lineBuffer, lineSize)) == -1)
        {
            pthread_mutex_unlock(&g_outputFileMutex);

            syslog(LOG_ERR, "Cannot write to file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
            g_exitProgram = true;
            return NULL;
        }     
        pthread_mutex_unlock(&g_outputFileMutex);
    }
    
    return NULL;
}

void SignalHandler()
{
    g_exitProgram = true;
}

void InitializeServer()
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
        TearDownServer(EXIT_FAILURE);   
    }
    
    if (sigaction(SIGINT, &signalAction, &g_oldSigintHandler) != 0)
    {
        syslog(LOG_ERR, "Cannot register signal handler.");
        TearDownServer(EXIT_FAILURE);   
    }
 
    g_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_serverSocket == -1)
    {
        syslog(LOG_ERR, "Cannot create socket. Error No: %d, Error Text: \"%s\".", errno, strerror(errno));
        TearDownServer(EXIT_FAILURE);
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
        TearDownServer(EXIT_FAILURE);
    }
}

void ExecuteServer()
{
    int listenResult = listen(g_serverSocket, 10);
    if (listenResult == -1)
    {
        syslog(LOG_ERR, "Cannot listen socket. Error No: %d, Error Text: \"%s\".", errno, strerror(errno));
        TearDownServer(EXIT_FAILURE);
    }

    g_outputFile = open(g_outputFilePath, O_RDWR | O_CREAT, 0666);
    if (g_outputFile == -1)
    {
        syslog(LOG_ERR, "Cannot open file. File Path: \"%s\", Error No: %d, Error Text: \"%s\".", g_outputFilePath, errno, strerror(errno));
        TearDownServer(EXIT_FAILURE);
    }

    if (pthread_create(&timestampThread, NULL, &TimestampLoop, NULL) != 0)
    {
        syslog(LOG_ERR, "Cannot create timestamp thread.");
        TearDownServer(EXIT_FAILURE);
    }

    while (!g_exitProgram)
    {
        struct sockaddr_in clientAddress;
        unsigned int clientAddressSize = sizeof(clientAddressSize);
        memset(&clientAddress, 0, sizeof(clientAddress));
        int clientSocket = accept(g_serverSocket, (struct sockaddr*)&clientAddress, &clientAddressSize);
        if (clientSocket == -1)
        {
            if (errno == EINTR && g_exitProgram)
                TearDownServer(EXIT_SUCCESS);

            syslog(LOG_ERR, "Cannot accept socket. Error No: %d, Error Text: \"%s\".", errno, strerror(errno));
            TearDownServer(EXIT_FAILURE);
        }

        struct Client* newClient = (struct Client*)malloc(sizeof(struct Client));
        if (newClient == NULL)
        {
            syslog(LOG_ERR, "Cannot allocate thread memory.");
            TearDownServer(EXIT_FAILURE);
        }

        memset(newClient, 0, sizeof(struct Client));
        newClient->socket = clientSocket;
        memcpy(&newClient->address, &clientAddress, sizeof(clientAddress));

        pthread_t newThread;
        if (pthread_create(&newThread, NULL, &ClientLoop, newClient) != 0)
        {
            syslog(LOG_ERR, "Cannot create client thread.");
            free(newClient);
            TearDownServer(EXIT_FAILURE);
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
            TearDownServer(EXIT_FAILURE);
        }

        int nullFile = open("/dev/null", O_RDWR);
        dup2(nullFile, STDIN_FILENO);
        dup2(nullFile, STDOUT_FILENO);
        dup2(nullFile, STDERR_FILENO);
        close(nullFile);

        ExecuteServer();
        TearDownServer(EXIT_SUCCESS);
    }
    else
    {
        TearDownServer(EXIT_SUCCESS);
    }
}

void StartApplication()
{
    syslog(LOG_INFO, "Running as application...");
    ExecuteServer();
    TearDownServer(EXIT_SUCCESS);
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

    InitializeServer();
    
    if (daemonMode)
        StartDaemon();
    else
        StartApplication();

    return EXIT_SUCCESS;
}