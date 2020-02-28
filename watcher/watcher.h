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

#include <stdlib.h>
#include <sys/wait.h>

#define WATCHER_TIMEOUT_DEFAULT -1

enum watcher_exit_e {
    WATCHER_SUCCESS,
    WATCHER_TIMEOUT,
    WATCHER_CLEANUP_FAILED,
    WATCHER_SIGTERM_SETUP_FAILED,
    WATCHER_SIGTERM_HANDLER_FAILED,
    WATCHER_SIGUSR1_SETUP_FAILED,
    WATCHER_SIGUSR1_HANDLER_FAILED,
    WATCHER_TIMESTAMP_FAILED,
    WATCHER_OOM,
    WATCHER_CANNOT_KILL,
    WATCHER_CANNOT_WAIT,
    WATCHER_COMMAND_TOO_LONG,
    WATCHER_EXEC_FAILED,
    WATCHER_ENV_TOO_LONG,
    WATCHER_COMMAND_CORE_DUMP,
    WATCHER_COMMAND_RETURNED_NON_ZERO,
};

struct timestamp_st {
    long useconds;
    long seconds;
};

struct watcher_ctx {
    pid_t pid;
    long timeout;
    struct timestamp_st ts;
};
