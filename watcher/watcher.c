/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2019 by Red Hat, Inc.
 *
 * Author: Anderson Toshiyuki Sasaki <ansasaki@redhat.com>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "config.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <time.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_ARGP_H
#include <argp.h>
#endif

#include "watcher.h"

#define MAX_ARGS 256
#define MAX_ENV_VARS 256
#define BUFFER_SIZE 1024

static struct watcher_ctx *ctx = NULL;

struct arguments_st {
    char *argv[MAX_ARGS];
    char *env[MAX_ENV_VARS];
    int argc;
    int envc;
    long timeout;
    char *pid_file;
    bool fork;
};

#ifdef _POSIX_MONOTONIC_CLOCK
#define CLOCK CLOCK_MONOTONIC
#else
#define CLOCK CLOCK_REALTIME
#endif

#ifdef HAVE_CLOCK_GETTIME
static int watcher_timestamp(struct timestamp_st *ts)
{
    struct timespec tp;
    clock_gettime(CLOCK, &tp);
    ts->useconds = tp.tv_nsec / 1000;
#else
    struct timeval tp;
    gettimeofday(&tp, NULL);
    ts->useconds = tp.tv_usec;
#endif
    ts->seconds = tp.tv_sec;

    return 0;
}

#undef CLOCK

static int watcher_finish(int watcher_exit, int status)
{
    bool is_running = true;
    int count;
    int rc;
    pid_t changed_pid;
    int process_exit;

    if (ctx == NULL) {
        return(watcher_exit);
    }

    switch(watcher_exit) {
    case WATCHER_SUCCESS:
        if (WIFEXITED(status)) {
            process_exit = WEXITSTATUS(status);
            fprintf(stderr, "The process %u exited with "
                    "code %d\n", ctx->pid, process_exit);
            is_running = 0;
            if (process_exit != 0) {
                watcher_exit = WATCHER_COMMAND_RETURNED_NON_ZERO;
            }
            break;
        } else if (WIFSIGNALED(status)) {
#ifdef WCOREDUMP
            if (WCOREDUMP(status)) {
                fprintf(stderr, "The process %u core dumped "
                        "with signal %d\n", ctx->pid,
                        WTERMSIG(status));
                watcher_exit = WATCHER_COMMAND_CORE_DUMP;
                is_running = 0;
                break;

            }
#endif
            fprintf(stderr, "The process %u was signaled with "
                    "signal %d\n", ctx->pid, WTERMSIG(status));
            is_running = 0;
            break;
        }
        break;
    case WATCHER_TIMEOUT:
        for (count = 0; count < 100; count++) {
            /* Check if status changed since last wait */
            changed_pid = waitpid(ctx->pid, &status, WNOHANG);
            if (changed_pid < 0) {
                /* Failed to wait, give up and die */
                watcher_exit = WATCHER_CANNOT_WAIT;
                break;
            } else if (changed_pid == 0) {
                /* Send kill signal to the process */
                rc = kill(ctx->pid, SIGKILL);
                if (rc < 0) {
                    watcher_exit = WATCHER_CANNOT_KILL;
                    break;
                }
                /* Wait and try again */
                usleep(10 * 1000);
            } else if (changed_pid == ctx->pid) {
                /* Process successfully killed */
                if (WIFEXITED(status)) {
                    process_exit = WEXITSTATUS(status);
                    fprintf(stderr, "The process %u exited with "
                            "code %d\n", ctx->pid, process_exit);
                    if (process_exit != 0) {
                        watcher_exit = WATCHER_COMMAND_RETURNED_NON_ZERO;
                    }
                } else if (WIFSIGNALED(status)) {
#ifdef WCOREDUMP
                    if (WCOREDUMP(status)) {
                        fprintf(stderr, "The process %u core dumped "
                                "with signal %d\n", ctx->pid,
                                WTERMSIG(status));
                        watcher_exit = WATCHER_COMMAND_CORE_DUMP;
                    }
#endif
                    fprintf(stderr, "The process %u was signaled with "
                            "signal %d\n", ctx->pid, WTERMSIG(status));
                }
                is_running = 0;
                break;
            } else {
                watcher_exit = WATCHER_CANNOT_WAIT;
                break;
            }
        }
        break;
    default:
        fprintf(stderr, "The watcher gave up with status %d. "
                "The process status was %d\n", watcher_exit, status);
    }

    if (is_running) {
        fprintf(stderr, "The process %u is still running! "
                "Watcher could not kill it.\n", ctx->pid);
    }
    return watcher_exit;
}

static void watcher_sigusr1_handler(int signo)
{
    (void) signo;
    int rc;

    if (ctx == NULL) {
        goto error;
    }

    if (ctx->pid <= 0) {
        goto error;
    }

    rc = watcher_timestamp(&ctx->ts);
    if (rc != 0) {
        goto error;
    }

    return;

error:
    free(ctx);
    ctx = NULL;
    exit(watcher_finish(WATCHER_SIGUSR1_HANDLER_FAILED, 0));
}

static void watcher_sigterm_handler(int signo)
{
    (void) signo;
    exit(watcher_finish(WATCHER_SUCCESS, 0));
}

static int watcher_timestamp_difference(struct timestamp_st *old,
                                        struct timestamp_st *new)
{
    long seconds, usecs, msecs;
    seconds = new->seconds - old->seconds;
    usecs = new->useconds - old->useconds;
    if (usecs < 0) {
        seconds--;
        usecs += 1000000;
    }
    msecs = seconds * 1000 + usecs/1000;
    return msecs;
}

/**
 * @brief Execute the given command in a child process and kill it after the
 * given timeout.
 *
 * If the timeout elapses, the process is killed and the watcher process exits
 * with failure.  If the watched process finishes by itself, the watcher process
 * exits with success (regardless of the exit status of the watched process).
 *
 * If the watcher process receives an SIGUSR1 signal, it will reset the timeout
 * counter. This is useful when the watched process is a long living process
 * which will perform operations that would take long time to execute.
 *
 * If the watcher process receives an SIGTERM signal, it will kill the watched
 * process and then exit with success.
 *
 * @param[in] command   The command to be executed.
 * @param[in] env       The environment variables to be used when running the
 *                      process
 * @param[in] timeout   The timeout before killing the watched process. It can
 *                      be set as WATCHER_TIMEOUT_DEFAULT, which sets the
 *                      timeout to the default of 1 minute.
 *
 * @returns 0 if successful; -1 otherwise
 */
static int watch_process(char **command, char **env, long timeout)
{
    pid_t pid;
    int difference;
    int rc = 0;
    int status = 0;

    struct timestamp_st now;

    struct sigaction sa;
    pid_t changed_pid;

    char buffer[BUFFER_SIZE];
    char *argv[MAX_ARGS + 1];
    char env_buffer[BUFFER_SIZE];
    char *env_copy[MAX_ARGS + 1];
    int i = 0;
    ssize_t used = 0;
    ssize_t printed;

    if (command == NULL || command[0] == NULL || timeout == 0) {
        errno = EINVAL;
        return -1;
    }

    /* Copy the dynamic allocated strings to a local buffer and free the
     * memory to avoid leaks */
    while (command[i] != NULL && i < (MAX_ARGS + 1)) {
        if (used + strlen(command[i]) > BUFFER_SIZE) {
            fprintf(stderr, "Command line too long\n");
            errno = EINVAL;
            return WATCHER_COMMAND_TOO_LONG;
        }

        printed = snprintf(buffer + used, sizeof(buffer) - used, "%s",
                           command[i]);
        if (printed <= 0) {
            errno = EINVAL;
            return WATCHER_CLEANUP_FAILED;
        }

#ifdef HAVE_ARGP_H
        free(command[i]);
        command[i] = NULL;
#endif

        /* Set the local argv[i] */
        argv[i] = buffer + used;

        /* We need the plus 1 to skip the string terminating '\0' */
        used += (printed + 1);
        i++;
    }

    /* Mark the end of the list of arguments with a NULL */
    argv[i] = NULL;

    /* Reset the variables */
    used = 0;
    i = 0;

    /* Copy the dynamic allocated strings to a local buffer and free the
     * memory to avoid leaks */
    if (env != NULL) {
        while (env[i] != NULL && i < (MAX_ENV_VARS + 1)) {
            if (used + strlen(env[i]) > BUFFER_SIZE) {
                fprintf(stderr, "Command line too long\n");
                errno = EINVAL;
                return WATCHER_ENV_TOO_LONG;
            }

            printed = snprintf(env_buffer + used, sizeof(env_buffer) - used, "%s",
                               env[i]);
            if (printed <= 0) {
                errno = EINVAL;
                return WATCHER_CLEANUP_FAILED;
            }

    #ifdef HAVE_ARGP_H
            free(env[i]);
            env[i] = NULL;
    #endif

            /* Set the local argv[i] */
            env_copy[i] = env_buffer + used;

            /* We need the plus 1 to skip the string terminating '\0' */
            used += (printed + 1);
            i++;
        }
    }
    /* Mark the end of the list of environment variables with a NULL */
    env_copy[i] = NULL;

    pid = fork();
    switch(pid){
    case 0:
        {
            /* If the parent had a watcher in place, free it */
            if (ctx != NULL) {
                free(ctx);
                ctx = NULL;
            }

            errno = 0;

            /* Execute the command */
            rc = execve(argv[0], (char **)argv, (char **)env_copy);
            if (rc != 0) {
                fprintf(stderr, "Error in execve: %s\n", strerror(errno));
                return WATCHER_EXEC_FAILED;
            }
        }
        break;
    case -1:
        fprintf(stderr, "Failed to start process watcher\n");
        return -1;
    default:
        /* If we had a watcher in place, free it to setup a new one */
        if (ctx != NULL) {
            free(ctx);
            ctx = NULL;
        }

        /* Set up SIGTERM handler. */
        sa.sa_handler = watcher_sigterm_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;

        rc = sigaction(SIGTERM, &sa, NULL);
        if (rc != 0) {
            fprintf(stderr, "Could not set signal handler for SIGTERM\n");
            rc = watcher_finish(WATCHER_SIGTERM_SETUP_FAILED, status);
            goto end;
        }

        /* Set up SIGUSR1 handler. */
        sa.sa_handler = watcher_sigusr1_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;

        rc = sigaction(SIGUSR1, &sa, NULL);
        if (rc != 0) {
            fprintf(stderr, "Could not set signal handler for SIGUSR1\n");
            rc = watcher_finish(WATCHER_SIGUSR1_SETUP_FAILED, status);
            goto end;
        }

        ctx = calloc(1, sizeof(struct watcher_ctx));
        if (ctx == NULL) {
            exit(WATCHER_OOM);
        }

        rc = watcher_timestamp(&ctx->ts);
        if (rc != 0) {
            free(ctx);
            ctx = NULL;
            rc = watcher_finish(WATCHER_TIMESTAMP_FAILED, status);
            goto end;
        }

        if (timeout < 0) {
            /* Set default timeout of 60 seconds */
            ctx->timeout = 60 * 1000;
        } else {
            ctx->timeout = timeout;
        }
        ctx->pid = pid;

        while(1) {
            errno = 0;
            changed_pid = waitpid(ctx->pid, &status, WNOHANG);
            if (changed_pid == ctx->pid) {
                /* The process finished */
                rc = watcher_finish(WATCHER_SUCCESS, status);
                break;
            } else if (changed_pid < 0) {
                if (errno == ECHILD) {
                    fprintf(stderr, "No child\n");
                    /* The process was dead, we are happy */
                    rc = watcher_finish(WATCHER_SUCCESS, status);
                    break;
                }
                rc = watcher_finish(WATCHER_CANNOT_WAIT, status);
                break;
            } else if (changed_pid == 0) {
                /* Watched process did not change.  Check timeout. */
                watcher_timestamp(&now);
                difference = watcher_timestamp_difference(&ctx->ts, &now);
                if (difference >= ctx->timeout) {
                    /* Timeout, finish processes with failure */
                    fprintf(stderr, "Process %d timed out\n", pid);
                    rc = watcher_finish(WATCHER_TIMEOUT, status);
                    break;
                }
            } else {
                rc = watcher_finish(WATCHER_CANNOT_WAIT, status);
                break;
            }
        }
    }

end:
    return rc;
}

#ifdef HAVE_ARGP_H
const char *argp_program_version = "watcher-0.0.1";
const char *argp_program_bug_address = "<libssh@libssh.org>";

/* Program documentation. */
static char doc[] = "A simple watcher to kill a process after a timeout";

/* A description of the arguments we accept. */
static char args_doc[] = "COMMAND";

/* The options we understand. */
static struct argp_option options[] = {
    {
        .name  = "env",
        .key   = 'e',
        .arg   = "ENV_VAR_VALUE",
        .flags = OPTION_ARG_OPTIONAL,
        .doc   = "Use this environment variable when executing the process. "
                 "Can be used multiple times.",
        .group = 0
    },
    {
        .name  = "fork",
        .key   = 'f',
        .arg   = NULL,
        .flags = OPTION_ARG_OPTIONAL,
        .doc   = "Do not block, run the watcher in a child process and return "
                 "immediately",
        .group = 0
    },
    {
        .name  = "timeout",
        .key   = 't',
        .arg   = "TIMEOUT",
        .flags = OPTION_ARG_OPTIONAL,
        .doc   = "The timeout in ms to wait before killing the process. "
                 "If < 0, it means infinite timeout (will block until "
                 "killed). [default = 300000ms]",
        .group = 0
    },
    {
        .name  = "pid_file",
        .key   = 'p',
        .arg   = "FILE",
        .flags = OPTION_ARG_OPTIONAL,
        .doc   = "The path to the file in which the pid of the watcher process "
                 "will be written.",
        .group = 0
    },
    { .name = NULL }
};

/* Parse a single option. */
static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
    /* Get the input argument from argp_parse, which we
     * know is a pointer to our arguments structure.
     */
    struct arguments_st *arguments = state->input;
    error_t rc = 0;

    if (arguments == NULL) {
        fprintf(stderr, "NULL pointer to arguments structure provided\n");
        rc = EINVAL;
        goto end;
    }

    switch (key) {
    case 'e':
        if (arguments->envc >= MAX_ENV_VARS) {
            fprintf(stderr, "Too many environment variables\n");
            argp_usage(state);
            rc = EINVAL;
            goto end;
        }

        /* Duplicate the current argument */
        arguments->env[arguments->envc] = strdup(arg);
        if (arguments->env[arguments->envc] == NULL) {
            rc = ENOMEM;
            goto end;
        }
        arguments->envc++;
        break;
    case 'f':
        arguments->fork = true;
        break;
    case 't':
        arguments->timeout = strtol(arg, NULL, 10);
        break;
    case 'p':
        arguments->pid_file = strdup(arg);
        if (arguments->pid_file == NULL) {
            rc = ENOMEM;
            goto end;
        }
        break;
    case ARGP_KEY_ARG:
        if (arguments->argc >= MAX_ARGS) {
            fprintf(stderr, "Too many arguments\n");
            argp_usage(state);
            rc = EINVAL;
            goto end;
        }

        /* Duplicate the current argument */
        arguments->argv[arguments->argc] = strdup(arg);
        if (arguments->argv[arguments->argc] == NULL) {
            rc = ENOMEM;
            goto end;
        }
        arguments->argc++;

        /* Steal the rest of arguments to be passed to command instead of being
         * parsed*/
        while (state->next != state->argc) {
            arguments->argv[arguments->argc] = strdup(state->argv[state->next]);
            arguments->argc++;
            state->next++;
        }
        break;
    case ARGP_KEY_END:
        if (state->arg_num < 1) {
            fprintf(stderr, "No command provided\n");
            argp_usage(state);
            rc = EINVAL;
            goto end;
        }
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

end:
    return rc;
}
#endif /* HAVE_ARGP_H */

/* Our argp parser. */
static struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};

int main(int argc, char *argv[])
{
    int rc;
    FILE *file;
    pid_t watcher_pid;
    int i;

    struct arguments_st arguments = {
        .argv = {0},
        .argc = 0,
        .env = {0},
        .envc = 0,
        .timeout = 300000,
        .pid_file = NULL,
        .fork = false,
    };

#ifdef HAVE_ARGP_H
    rc = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, 0, &arguments);
    if (rc != 0) {
        goto end;
    }

    if (arguments.argc == 0) {
        fprintf(stderr, "No command provided\n");
        return EINVAL;
    }

    if (arguments.fork) {
        watcher_pid = fork();
        if (watcher_pid != 0) {
            if (watcher_pid < 0) {
                rc = -1;
            }

            if (arguments.argv != NULL) {
                for (i = 0; i < arguments.argc; i++) {
                    free(arguments.argv[i]);
                }
            }

            if (arguments.env != NULL) {
                for (i = 0; i < arguments.envc; i++) {
                    free(arguments.env[i]);
                }
            }

            goto end;
        }
    }

    if (arguments.pid_file != NULL) {
        watcher_pid = getpid();
        errno = 0;
        file = fopen(arguments.pid_file, "w");
        if (file == NULL) {
            fprintf(stderr, "Could not open file %s: %s\n", arguments.pid_file,
                    strerror(errno));
            return EINVAL;
        }
        fprintf(file, "%d\n", watcher_pid);
        fclose(file);
    }

    rc = watch_process(arguments.argv, arguments.env, arguments.timeout);
#else
    /* If argp is not available, always use default timeout of 5 minutes */
    rc = watch_process(&argv[1], NULL, 300000);
#endif

    if (ctx != NULL) {
        free(ctx);
        ctx = NULL;
    }

end:
    return rc;
}

