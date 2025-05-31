#include <stdlib.h>
#include <syslog.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Incorrect number of arguments.");
        return EXIT_FAILURE;
    }

    const char* filename = argv[1];
    const char* text = argv[2];

    openlog(NULL, 0, LOG_USER);

    syslog(LOG_DEBUG, "Writing %s to %s", filename, text);

    FILE* file = fopen(filename, "w");
    if (file == NULL)
    {
        syslog(LOG_ERR, "Cannot open file for writing. Filename: \"%s\", Error: \"%s\".",
            filename,
            strerror(errno)    
        );
        
        return EXIT_FAILURE;
    }

    int result = fprintf(file, "%s", text);
    if (result < 0)
    {
        syslog(LOG_ERR, "Cannot write to file. Filename: \"%s\", Error: \"%s\".",
            filename,
            strerror(errno)    
        );
        fclose(file);
        return EXIT_FAILURE;
    }

    result = fclose(file);
    if (result < 0)
    {
        syslog(LOG_ERR, "Cannot close file. Filename: \"%s\", Error: \"%s\".",
            filename,
            strerror(errno)    
        );
        return EXIT_FAILURE;
    }

    closelog();

    return EXIT_SUCCESS;
}