#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>

int main(int argc, char *argv[])
{
    openlog("writer", LOG_PID, LOG_USER);

    if (argc != 3) {
        fprintf(stderr, "Incorrect number of arguments, must be equal 2.\n");
        syslog(LOG_ERR, "Incorrect number of arguments, must be equal 2.");
        closelog();
        return 1;
    }

    char *writefile = argv[1];
    char *writestr = argv[2];

    char *dirpath = strdup(writefile);
    if (dirpath == NULL) {
        syslog(LOG_ERR, "Error allocating memory.");
        closelog();
        return 1;
    }
    dirpath = dirname(dirpath);

    FILE *file = fopen(writefile, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Error creating file '%s': %s", writefile, strerror(errno));
        free(dirpath);
        closelog();
        return 1;
    }

    if (fprintf(file, "%s", writestr) < 0) {
        syslog(LOG_ERR, "Error writting to file '%s': %s", writefile, strerror(errno));
        fclose(file);
        free(dirpath);
        closelog();
        return 1;
    } else {
        syslog(LOG_DEBUG, "Writting %s to %s", writestr, writefile);
    }

    fclose(file);
    free(dirpath);
    closelog();

    return 0;

}
