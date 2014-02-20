/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2011 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/inotify.h>
#include <sys/poll.h>

#include "util.h"
#include "cgroup-util.h"
#include "macro.h"
#include "sd-login.h"
#include "strv.h"
#include "fileio.h"

_public_ int sd_pid_get_session(pid_t pid, char **session) {
        if (pid < 0)
                return -EINVAL;

        if (!session)
                return -EINVAL;

        return cg_pid_get_session(pid, session);
}

_public_ int sd_pid_get_unit(pid_t pid, char **unit) {

        if (pid < 0)
                return -EINVAL;
        if (!unit)
                return -EINVAL;

        return cg_pid_get_unit(pid, unit);
}

_public_ int sd_pid_get_user_unit(pid_t pid, char **unit) {

        if (pid < 0)
                return -EINVAL;
        if (!unit)
                return -EINVAL;

        return cg_pid_get_user_unit(pid, unit);
}

_public_ int sd_pid_get_machine_name(pid_t pid, char **name) {

        if (pid < 0)
                return -EINVAL;
        if (!name)
                return -EINVAL;

        return cg_pid_get_machine_name(pid, name);
}

_public_ int sd_pid_get_owner_uid(pid_t pid, uid_t *uid) {

        if (pid < 0)
                return -EINVAL;

        if (!uid)
                return -EINVAL;

        return cg_pid_get_owner_uid(pid, uid);
}

_public_ int sd_uid_get_state(uid_t uid, char**state) {
        char *p, *s = NULL;
        int r;

        if (!state)
                return -EINVAL;

        if (asprintf(&p, "/run/systemd/users/%lu", (unsigned long) uid) < 0)
                return -ENOMEM;

        r = parse_env_file(p, NEWLINE, "STATE", &s, NULL);
        free(p);

        if (r == -ENOENT) {
                free(s);
                s = strdup("offline");
                if (!s)
                        return -ENOMEM;

                *state = s;
                return 0;
        } else if (r < 0) {
                free(s);
                return r;
        } else if (!s)
                return -EIO;

        *state = s;
        return 0;
}

_public_ int sd_uid_is_on_seat(uid_t uid, int require_active, const char *seat) {
        char *w, *state;
        _cleanup_free_ char *t = NULL, *s = NULL, *p = NULL;
        size_t l;
        int r;
        const char *variable;

        if (!seat)
                return -EINVAL;

        variable = require_active ? "ACTIVE_UID" : "UIDS";

        p = strappend("/run/systemd/seats/", seat);
        if (!p)
                return -ENOMEM;

        r = parse_env_file(p, NEWLINE, variable, &s, NULL);

        if (r < 0)
                return r;

        if (!s)
                return -EIO;

        if (asprintf(&t, "%lu", (unsigned long) uid) < 0)
                return -ENOMEM;

        FOREACH_WORD(w, l, s, state) {
                if (strneq(t, w, l))
                        return 1;
        }

        return 0;
}

static int uid_get_array(uid_t uid, const char *variable, char ***array) {
        _cleanup_free_ char *p = NULL, *s = NULL;
        char **a;
        int r;

        if (asprintf(&p, "/run/systemd/users/%lu", (unsigned long) uid) < 0)
                return -ENOMEM;

        r = parse_env_file(p, NEWLINE,
                           variable, &s,
                           NULL);
        if (r < 0) {
                if (r == -ENOENT) {
                        if (array)
                                *array = NULL;
                        return 0;
                }

                return r;
        }

        if (!s) {
                if (array)
                        *array = NULL;
                return 0;
        }

        a = strv_split(s, " ");

        if (!a)
                return -ENOMEM;

        strv_uniq(a);
        r = strv_length(a);

        if (array)
                *array = a;
        else
                strv_free(a);

        return r;
}

_public_ int sd_uid_get_sessions(uid_t uid, int require_active, char ***sessions) {
        return uid_get_array(
                        uid,
                        require_active == 0 ? "ONLINE_SESSIONS" :
                        require_active > 0  ? "ACTIVE_SESSIONS" :
                                              "SESSIONS",
                        sessions);
}

_public_ int sd_uid_get_seats(uid_t uid, int require_active, char ***seats) {
        return uid_get_array(
                        uid,
                        require_active == 0 ? "ONLINE_SEATS" :
                        require_active > 0  ? "ACTIVE_SEATS" :
                                              "SEATS",
                        seats);
}

static int file_of_session(const char *session, char **_p) {
        char *p;
        int r;

        assert(_p);

        if (session)
                p = strappend("/run/systemd/sessions/", session);
        else {
                char *buf;

                r = sd_pid_get_session(0, &buf);
                if (r < 0)
                        return r;

                p = strappend("/run/systemd/sessions/", buf);
                free(buf);
        }

        if (!p)
                return -ENOMEM;

        *_p = p;
        return 0;
}

_public_ int sd_session_is_active(const char *session) {
        int r;
        _cleanup_free_ char *p = NULL, *s = NULL;

        r = file_of_session(session, &p);
        if (r < 0)
                return r;

        r = parse_env_file(p, NEWLINE, "ACTIVE", &s, NULL);

        if (r < 0)
                return r;

        if (!s)
                return -EIO;

        r = parse_boolean(s);

        return r;
}

_public_ int sd_session_get_state(const char *session, char **state) {
        _cleanup_free_ char *p = NULL, *s = NULL;
        int r;

        if (!state)
                return -EINVAL;

        r = file_of_session(session, &p);
        if (r < 0)
                return r;

        r = parse_env_file(p, NEWLINE, "STATE", &s, NULL);

        if (r < 0)
                return r;
        else if (!s)
                return -EIO;

        *state = s;
        s = NULL;

        return 0;
}

_public_ int sd_session_get_uid(const char *session, uid_t *uid) {
        int r;
        _cleanup_free_ char *p = NULL, *s = NULL;

        if (!uid)
                return -EINVAL;

        r = file_of_session(session, &p);
        if (r < 0)
                return r;

        r = parse_env_file(p, NEWLINE, "UID", &s, NULL);

        if (r < 0)
                return r;

        if (!s)
                return -EIO;

        r = parse_uid(s, uid);

        return r;
}

static int session_get_string(const char *session, const char *field, char **value) {
        _cleanup_free_ char *p = NULL, *s = NULL;
        int r;

        if (!value)
                return -EINVAL;

        r = file_of_session(session, &p);
        if (r < 0)
                return r;

        r = parse_env_file(p, NEWLINE, field, &s, NULL);

        if (r < 0)
                return r;

        if (isempty(s))
                return -ENOENT;

        *value = s;
        s = NULL;
        return 0;
}

_public_ int sd_session_get_seat(const char *session, char **seat) {
        return session_get_string(session, "SEAT", seat);
}

_public_ int sd_session_get_tty(const char *session, char **tty) {
        return session_get_string(session, "TTY", tty);
}

_public_ int sd_session_get_service(const char *session, char **service) {
        return session_get_string(session, "SERVICE", service);
}

_public_ int sd_session_get_type(const char *session, char **type) {
        return session_get_string(session, "TYPE", type);
}

_public_ int sd_session_get_class(const char *session, char **class) {
        return session_get_string(session, "CLASS", class);
}

_public_ int sd_session_get_display(const char *session, char **display) {
        return session_get_string(session, "DISPLAY", display);
}

static int file_of_seat(const char *seat, char **_p) {
        char *p;
        int r;

        assert(_p);

        if (seat)
                p = strappend("/run/systemd/seats/", seat);
        else {
                _cleanup_free_ char *buf = NULL;

                r = sd_session_get_seat(NULL, &buf);
                if (r < 0)
                        return r;

                p = strappend("/run/systemd/seats/", buf);
        }

        if (!p)
                return -ENOMEM;

        *_p = p;
        p = NULL;
        return 0;
}

_public_ int sd_seat_get_active(const char *seat, char **session, uid_t *uid) {
        _cleanup_free_ char *p = NULL, *s = NULL, *t = NULL;
        int r;

        if (!session && !uid)
                return -EINVAL;

        r = file_of_seat(seat, &p);
        if (r < 0)
                return r;

        r = parse_env_file(p, NEWLINE,
                           "ACTIVE", &s,
                           "ACTIVE_UID", &t,
                           NULL);
        if (r < 0)
                return r;

        if (session && !s)
                return -ENOENT;

        if (uid && !t)
                return -ENOENT;

        if (uid && t) {
                r = parse_uid(t, uid);
                if (r < 0)
                        return r;
        }

        if (session && s) {
                *session = s;
                s = NULL;
        }

        return 0;
}

_public_ int sd_seat_get_sessions(const char *seat, char ***sessions, uid_t **uids, unsigned *n_uids) {
        _cleanup_free_ char *p = NULL, *s = NULL, *t = NULL;
        _cleanup_strv_free_ char **a = NULL;
        _cleanup_free_ uid_t *b = NULL;
        unsigned n = 0;
        int r;

        r = file_of_seat(seat, &p);
        if (r < 0)
                return r;

        r = parse_env_file(p, NEWLINE,
                           "SESSIONS", &s,
                           "ACTIVE_SESSIONS", &t,
                           NULL);

        if (r < 0)
                return r;

        if (s) {
                a = strv_split(s, " ");
                if (!a)
                        return -ENOMEM;
        }

        if (uids && t) {
                char *w, *state;
                size_t l;

                FOREACH_WORD(w, l, t, state)
                        n++;

                if (n > 0) {
                        unsigned i = 0;

                        b = new(uid_t, n);
                        if (!b)
                                return -ENOMEM;

                        FOREACH_WORD(w, l, t, state) {
                                _cleanup_free_ char *k = NULL;

                                k = strndup(w, l);
                                if (!k)
                                        return -ENOMEM;

                                r = parse_uid(k, b + i);

                                if (r < 0)
                                        continue;

                                i++;
                        }
                }
        }

        r = strv_length(a);

        if (sessions) {
                *sessions = a;
                a = NULL;
        }

        if (uids) {
                *uids = b;
                b = NULL;
        }

        if (n_uids)
                *n_uids = n;

        return r;
}

static int seat_get_can(const char *seat, const char *variable) {
        _cleanup_free_ char *p = NULL, *s = NULL;
        int r;

        r = file_of_seat(seat, &p);
        if (r < 0)
                return r;

        r = parse_env_file(p, NEWLINE,
                           variable, &s,
                           NULL);
        if (r < 0)
                return r;

        if (s)
                r = parse_boolean(s);
        else
                r = 0;

        return r;
}

_public_ int sd_seat_can_multi_session(const char *seat) {
        return seat_get_can(seat, "CAN_MULTI_SESSION");
}

_public_ int sd_seat_can_tty(const char *seat) {
        return seat_get_can(seat, "CAN_TTY");
}

_public_ int sd_seat_can_graphical(const char *seat) {
        return seat_get_can(seat, "CAN_GRAPHICAL");
}

_public_ int sd_get_seats(char ***seats) {
        return get_files_in_directory("/run/systemd/seats/", seats);
}

_public_ int sd_get_sessions(char ***sessions) {
        return get_files_in_directory("/run/systemd/sessions/", sessions);
}

_public_ int sd_get_uids(uid_t **users) {
        _cleanup_closedir_ DIR *d;
        int r = 0;
        unsigned n = 0;
        _cleanup_free_ uid_t *l = NULL;

        d = opendir("/run/systemd/users/");
        if (!d)
                return -errno;

        for (;;) {
                struct dirent *de;
                union dirent_storage buf;
                int k;
                uid_t uid;

                k = readdir_r(d, &buf.de, &de);
                if (k != 0)
                        return -k;

                if (!de)
                        break;

                dirent_ensure_type(d, de);

                if (!dirent_is_file(de))
                        continue;

                k = parse_uid(de->d_name, &uid);
                if (k < 0)
                        continue;

                if (users) {
                        if ((unsigned) r >= n) {
                                uid_t *t;

                                n = MAX(16, 2*r);
                                t = realloc(l, sizeof(uid_t) * n);
                                if (!t)
                                        return -ENOMEM;

                                l = t;
                        }

                        assert((unsigned) r < n);
                        l[r++] = uid;
                } else
                        r++;
        }

        if (users) {
                *users = l;
                l = NULL;
        }

        return r;
}

_public_ int sd_get_machine_names(char ***machines) {
        _cleanup_closedir_ DIR *d = NULL;
        _cleanup_strv_free_ char **l = NULL;
        _cleanup_free_ char *md = NULL;
        char *n;
        int c = 0, r;

        r = cg_get_machine_path(NULL, &md);
        if (r < 0)
                return r;

        r = cg_enumerate_subgroups(SYSTEMD_CGROUP_CONTROLLER, md, &d);
        if (r < 0)
                return r;

        while ((r = cg_read_subgroup(d, &n)) > 0) {

                r = strv_push(&l, n);
                if (r < 0) {
                        free(n);
                        return -ENOMEM;
                }

                c++;
        }

        if (r < 0)
                return r;

        if (machines) {
                *machines = l;
                l = NULL;
        }

        return c;
}

static inline int MONITOR_TO_FD(sd_login_monitor *m) {
        return (int) (unsigned long) m - 1;
}

static inline sd_login_monitor* FD_TO_MONITOR(int fd) {
        return (sd_login_monitor*) (unsigned long) (fd + 1);
}

_public_ int sd_login_monitor_new(const char *category, sd_login_monitor **m) {
        int fd, k;
        bool good = false;

        if (!m)
                return -EINVAL;

        fd = inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
        if (fd < 0)
                return -errno;

        if (!category || streq(category, "seat")) {
                k = inotify_add_watch(fd, "/run/systemd/seats/", IN_MOVED_TO|IN_DELETE);
                if (k < 0) {
                        close_nointr_nofail(fd);
                        return -errno;
                }

                good = true;
        }

        if (!category || streq(category, "session")) {
                k = inotify_add_watch(fd, "/run/systemd/sessions/", IN_MOVED_TO|IN_DELETE);
                if (k < 0) {
                        close_nointr_nofail(fd);
                        return -errno;
                }

                good = true;
        }

        if (!category || streq(category, "uid")) {
                k = inotify_add_watch(fd, "/run/systemd/users/", IN_MOVED_TO|IN_DELETE);
                if (k < 0) {
                        close_nointr_nofail(fd);
                        return -errno;
                }

                good = true;
        }

        if (!category || streq(category, "machine")) {
                _cleanup_free_ char *md = NULL, *p = NULL;
                int r;

                r = cg_get_machine_path(NULL, &md);
                if (r < 0)
                        return r;

                r = cg_get_path(SYSTEMD_CGROUP_CONTROLLER, md, NULL, &p);
                if (r < 0)
                        return r;

                k = inotify_add_watch(fd, p, IN_MOVED_TO|IN_CREATE|IN_DELETE);
                if (k < 0) {
                        close_nointr_nofail(fd);
                        return -errno;
                }

                good = true;
        }

        if (!good) {
                close_nointr(fd);
                return -EINVAL;
        }

        *m = FD_TO_MONITOR(fd);
        return 0;
}

_public_ sd_login_monitor* sd_login_monitor_unref(sd_login_monitor *m) {
        int fd;

        if (!m)
                return NULL;

        fd = MONITOR_TO_FD(m);
        close_nointr(fd);

        return NULL;
}

_public_ int sd_login_monitor_flush(sd_login_monitor *m) {

        if (!m)
                return -EINVAL;

        return flush_fd(MONITOR_TO_FD(m));
}

_public_ int sd_login_monitor_get_fd(sd_login_monitor *m) {

        if (!m)
                return -EINVAL;

        return MONITOR_TO_FD(m);
}

_public_ int sd_login_monitor_get_events(sd_login_monitor *m) {

        if (!m)
                return -EINVAL;

        /* For now we will only return POLLIN here, since we don't
         * need anything else ever for inotify.  However, let's have
         * this API to keep our options open should we later on need
         * it. */
        return POLLIN;
}

_public_ int sd_login_monitor_get_timeout(sd_login_monitor *m, uint64_t *timeout_usec) {

        if (!m)
                return -EINVAL;
        if (!timeout_usec)
                return -EINVAL;

        /* For now we will only return (uint64_t) -1, since we don't
         * need any timeout. However, let's have this API to keep our
         * options open should we later on need it. */
        *timeout_usec = (uint64_t) -1;
        return 0;
}
