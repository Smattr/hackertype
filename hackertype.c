#define _BSD_SOURCE
#define _XOPEN_SOURCE 600
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static struct termios old;

static void reset_terminal(void) {
    (void)tcsetattr(0, TCSADRAIN, &old);
}

static int setup_terminal(void) {
    if (tcgetattr(0, &old) != 0)
        return -1;

    /* Set the terminal to non-canonical, non-echoing */
    struct termios new = old;
    new.c_lflag &= ~ICANON;
    new.c_lflag &= ~ECHO;
    new.c_cc[VMIN] = 1;
    new.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &new) != 0)
        return -1;

    if (atexit(reset_terminal) != 0)
        return -1;

    return 0;
}

/* Run a command. You might think we could just `popen` and be done with it.
 * However, many programs detect when their standard streams are not a TTY and
 * act differently. To cope with this, we give them a PTY. This function
 * returns an open file descriptor to the master side of the PTY.
 */
static int run(char *command) {

    /* Create a new PTY */
    int master = posix_openpt(O_RDWR|O_NOCTTY);
    if (master < 0)
        return -1;
    if (grantpt(master) != 0)
        return -1;
    if (unlockpt(master) != 0)
        return -1;

    /* Flush before fork to avoid duplicated output */
    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    switch (pid) {

        case 0: { /* child */

            /* Become a new session leader */
            if (setsid() == -1)
                exit(-1);

            /* Open the slave side of the PTY */
            const char *name = ptsname(master);
            if (name == NULL)
                exit(-1);
            int slave = open(name, O_RDWR);
            if (slave < 0)
                exit(-1);

            /* Close the master side of the PTY that we don't need */
            close(master);

            /* Try to set the PTY to have the same dimensions as the original
             * terminal; ignore failure
             */
            struct winsize ws;
            ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
            ioctl(slave, TIOCSWINSZ, &ws);

            /* Swap our standard descriptors for the PTY */
            if (dup2(slave, STDIN_FILENO) < 0 ||
                dup2(slave, STDOUT_FILENO) < 0 ||
                dup2(slave, STDERR_FILENO) < 0)
                exit(-1);
            close(slave);

            /* Make the PTY our controlling terminal */
            if (ioctl(fileno(stdin), TIOCSCTTY, 1) != 0)
                exit(-1);

            /* Run the command */
            char *argv[] = {"sh", "-c", command, NULL};
            execvp("sh", argv);
            /* If we returned from exec, something went wrong */
            exit(-1);

        }

        case -1: /* fork failed */
            return -1;
    }

    /* Only the parent of the fork reaches here */
    return master;
}

int main(int argc, char **argv) {
    char *command = NULL,
         *script = NULL;

    struct option options[] = {
        {"command", required_argument, 0, 'c'},
        {"help", no_argument, 0, '?'},
        {"script", required_argument, 0, 's'},
        {0, 0, 0, 0},
    };

    while (true) {
        int index;
        int c = getopt_long(argc, argv, "c:s:", options, &index);

        if (c == -1)
            break;

        switch (c) {
            case 'c':
                command = optarg;
                break;

            case '?':
                printf("Usage: %s options...\n"
                       "Options:\n"
                       " --command COMMAND\n"
                       " -c COMMAND          Command to run\n"
                       " --help\n"
                       " -?                  Show this help text\n"
                       " --script SCRIPT\n"
                       " -s SCRIPT           Script of input to enter\n",
                       argv[0]);
                return 0;

            case 's':
                script = optarg;
                break;

            default:
                return -1;
        }
    }

    if (command == NULL) {
        fprintf(stderr, "no command provided\n");
        return -1;
    }

    if (script == NULL) {
        fprintf(stderr, "no script provided\n");
        return -1;
    }

    FILE *input = fopen(script, "r");
    if (input == NULL) {
        fprintf(stderr, "failed to open %s\n", script);
        return -1;
    }

    char buffer[1024];
    unsigned int buffer_offset = 0;
    size_t buffer_sz = 0;

    int p = run(command);
    if (p == -1) {
        fprintf(stderr, "failed to start %s\n", command);
        return -1;
    }

    /* Note that we alter our terminal settings *after* forking to avoid
     * affecting the child's terminal settings
     */
    if (setup_terminal() != 0) {
        fprintf(stderr, "failed to set terminal unbuffered\n");
        char c = EOF;
        ssize_t ignored __attribute__((unused)) = write(p, &c, 1);
        fclose(input);
        return -1;
    }

    while (true) {

        if (buffer_sz == 0) {
            /* Read in some more input from the script file */
            buffer_sz = fread(buffer, 1, sizeof(buffer), input);
            if (buffer_sz == 0)
                break;
            buffer_offset = 0;
        }

        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        FD_SET(p, &set);

        /* See if either `stdin` or the child have data for us */
        if (select(p > STDIN_FILENO ? p + 1 : STDIN_FILENO + 1, &set, NULL,
                NULL, NULL) == -1) {
            fprintf(stderr, "select failed\n");
            return -1;
        }

        if (FD_ISSET(STDIN_FILENO, &set)) {
            char ignored;
            if (read(STDIN_FILENO, &ignored, 1) == 1) {
                if (write(p, &buffer[buffer_offset], 1) != 1)
                    break;
                buffer_offset++;
                buffer_sz--;
            }
        }

        if (FD_ISSET(p, &set)) {
            char buf[1024];
            ssize_t r = read(p, buf, sizeof(buf));
            if (r > 0) {
                int ignored __attribute__((unused)) = write(STDOUT_FILENO, buf, r);
            } else {
                break;
            }
        }
    }

    fclose(input);
    return 0;
}
