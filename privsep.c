/* Privilege separation.  Closely based on OpenBSD syslogd's
 * privsep, which is:
 *
 * Copyright (c) 2003 Anil Madhavapeddy <anil@recoil.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *
 * We also use some utility functions from privsep_fdpass.c, which is:
 *
 *
 * Copyright 2001 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Copyright (c) 2002 Matthieu Herrb
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#ifndef CONFIG_STUBDOM
#include <sys/prctl.h>
#endif
#include <dirent.h>

#include "qemu-common.h"
#include "hw/hw.h"
#include "sysemu.h"
#include "exec-all.h"
#include "privsep.h"
#include "qemu-xen.h"

static int privsep_fd = -1;
static uid_t qemu_uid;
static gid_t qemu_gid;
static struct xs_handle * priv_xsh;
static int parent_fd;
static int parent_pid;
static char root_directory[64];
static int termsig;

enum privsep_opcode {
    privsep_op_open_iso,
    privsep_op_eject_cd,
    privsep_op_lock_cd,
    privsep_op_unlock_cd,
    privsep_op_set_rtc,
    privsep_op_ack_logdirty_switch,
    privsep_op_open_vm_dump,
    privsep_op_open_keymap,
    privsep_op_record_dm,
    privsep_op_write_vslots,
    privsep_op_read_dm
};

#define MAX_CDS (MAX_DRIVES+1)

/* We have a list of xenstore paths which correspond to CD backends,
   and we validate CD and ISO commands against that.  New backends can
   only be added to that list before you drop privileges.
*/
static char *
cd_backend_areas[MAX_CDS];

static void clean_exit(int ret)
{
    if (strcmp(root_directory, "/var/empty")) {
        char name[80];
        struct stat buf;
        strcpy(name, root_directory);
        strcat(name, "/etc/localtime");
        unlink(name);
        strcpy(name, root_directory);
        strcat(name, "/etc");
        rmdir(name);

        snprintf(name, 80, "%s/core.%d", root_directory, parent_pid);
        if (!stat(name, &buf) && !buf.st_size)
            unlink(name);

        rmdir(root_directory);
    }
    _exit(ret);
}

/* Read data with the assertion that it all must come through, or
 * else abort the process.  Based on atomicio() from openssh. */
static void
must_read(int fd, void *buf, size_t n)
{
        char *s = buf;
        ssize_t res, pos = 0;

        while (n > pos) {
                res = read(fd, s + pos, n - pos);
                switch (res) {
                case -1:
                        if (errno == EINTR || errno == EAGAIN)
                                continue;
                case 0:
                        clean_exit(0);
                default:
                        pos += res;
                }
        }
}

/* Write data with the assertion that it all has to be written, or
 * else abort the process.  Based on atomicio() from openssh. */
static void
must_write(int fd, const void *buf, size_t n)
{
        const char *s = buf;
        ssize_t res, pos = 0;

        while (n > pos) {
                res = write(fd, s + pos, n - pos);
                switch (res) {
                case -1:
                        if (errno == EINTR || errno == EAGAIN)
                                continue;
                case 0:
                        clean_exit(0);
                default:
                        pos += res;
                }
        }
}

#ifndef CONFIG_STUBDOM
static void
send_fd(int sock, int fd)
{
        struct msghdr msg;
        char tmp[CMSG_SPACE(sizeof(int))];
        struct cmsghdr *cmsg;
        struct iovec vec;
        int result = 0;
        ssize_t n;

        memset(&msg, 0, sizeof(msg));

        if (fd >= 0) {
                msg.msg_control = (caddr_t)tmp;
                msg.msg_controllen = CMSG_LEN(sizeof(int));
                cmsg = CMSG_FIRSTHDR(&msg);
                cmsg->cmsg_len = CMSG_LEN(sizeof(int));
                cmsg->cmsg_level = SOL_SOCKET;
                cmsg->cmsg_type = SCM_RIGHTS;
                *(int *)CMSG_DATA(cmsg) = fd;
        } else {
                result = errno;
        }

        vec.iov_base = &result;
        vec.iov_len = sizeof(int);
        msg.msg_iov = &vec;
        msg.msg_iovlen = 1;

        if ((n = sendmsg(sock, &msg, 0)) == -1)
                warn("%s: sendmsg(%d)", "send_fd", sock);
        if (n != sizeof(int))
                warnx("%s: sendmsg: expected sent 1 got %ld",
                    "send_fd", (long)n);
}
#endif

#ifndef CONFIG_STUBDOM
static int
receive_fd(int sock)
{
        struct msghdr msg;
        char tmp[CMSG_SPACE(sizeof(int))];
        struct cmsghdr *cmsg;
        struct iovec vec;
        ssize_t n;
        int result;
        int fd;

        memset(&msg, 0, sizeof(msg));
        vec.iov_base = &result;
        vec.iov_len = sizeof(int);
        msg.msg_iov = &vec;
        msg.msg_iovlen = 1;
        msg.msg_control = tmp;
        msg.msg_controllen = sizeof(tmp);

retry:
        if ((n = recvmsg(sock, &msg, 0)) == -1)
	{
	    warn("%s: recvmsg", "receive_fd");
	    if ( errno == EINTR )
	      { 
		warn("%s: Interrupted system call.  termsig %d.  Retrying.\n",
		     __func__, termsig);
		goto retry;
	      }
	}
        if (n != sizeof(int))
                warnx("%s: recvmsg: expected received 1 got %zd",
                      "receive_fd", n);
        if (result == 0) {
                cmsg = CMSG_FIRSTHDR(&msg);
                if (cmsg == NULL) {
                        warnx("%s: no message header", "receive_fd");
                        return (-1);
                }
                if (cmsg->cmsg_type != SCM_RIGHTS)
                        warnx("%s: expected type %d got %d", "receive_fd",
                            SCM_RIGHTS, cmsg->cmsg_type);
                fd = (*(int *)CMSG_DATA(cmsg));
                return fd;
        } else {
                errno = result;
                return -1;
        }
}
#else

static int receive_fd(int sock)
{
    return -1;
}
#endif

static QEMUFile *
receive_qemufile(int sock, const char *mode)
{
    int fd;
    QEMUFile *res;
    int e;

    fd = receive_fd(sock);
    if (fd < 0)
        return NULL;
    res = qemu_fdopen(fd, mode);
    return res;
}

static FILE *
receive_file(int sock, const char *mode)
{
    int fd;
    FILE *res;
    int e;

    fd = receive_fd(sock);
    if (fd < 0)
        return NULL;
    res = fdopen(fd, mode);
    if (!res) {
        e = errno;
        close(fd);
        errno = e;
    }
    return res;
}

#ifndef CONFIG_STUBDOM
static void
open_iso(void)
{
    int i;
    char *params_path;
    char *path;
    size_t path_len;
    char *allowed_path;
    unsigned len;
    int fd;

    /* This is a bit icky.  We get a path from the unprivileged qemu,
       and then scan the defined CD areas to make sure it matches.
       The internal structure of qemu means that by the time you do
       the open(), it's kind of hard to map back to the actual CD
       drive. */
    must_read(parent_fd, &path_len, sizeof(size_t));
    if (path_len == 0 || path_len > 65536)
        clean_exit(0);
    path = malloc(path_len+1);
    if (!path)
        clean_exit(0);
    must_read(parent_fd, path, path_len);
    path[path_len - 1] = 0;

    /* Have a path.  Validate against xenstore. */
    for (i = 0; i < MAX_CDS; i++) {
        if (asprintf(&params_path, "%s/params", cd_backend_areas[i]) < 0) {
            /* Umm, not sure what to do now */
            continue;
        }
        allowed_path = xs_read(priv_xsh, XBT_NULL, params_path, &len);
        free(params_path);
        if (!allowed_path)
            continue;
        if (!strcmp(allowed_path, path)) {
            free(allowed_path);
            break;
        }
        free(allowed_path);
    }
    if (i >= MAX_CDS) {
        errno = EPERM;
        send_fd(parent_fd, -1);
    } else {
        fd = open(path, O_RDONLY|O_LARGEFILE|O_BINARY);
        send_fd(parent_fd, fd);
        if (fd >= 0)
            close(fd);
        free(path);
    }
}
#endif

static int
client_open_iso(const char *path)
{
    enum privsep_opcode cmd;
    size_t l;

    cmd = privsep_op_open_iso;
    l = strlen(path) + 1;
    must_write(privsep_fd, &cmd, sizeof(cmd));
    must_write(privsep_fd, &l, sizeof(l));
    must_write(privsep_fd, path, l);

    return receive_fd(privsep_fd);
}

static void
_eject_cd(int id)
{
    char *param_path;
    if (cd_backend_areas[id]) {
        if (asprintf(&param_path, "%s/params", cd_backend_areas[id]) >= 0){
            xs_write(priv_xsh, XBT_NULL, param_path, "", 0);
            free(param_path);
        }
    }
}

static void
eject_cd(void)
{
    int i;
    must_read(parent_fd, &i, sizeof(int));
    if (i < 0 || i >= MAX_CDS)
        clean_exit(0);
    _eject_cd(i);
}

void
privsep_eject_cd(int id)
{
    if (privsep_fd < 0) {
        _eject_cd(id);
    } else {
        enum privsep_opcode cmd;

        cmd = privsep_op_eject_cd;
        must_write(privsep_fd, &cmd, sizeof(cmd));
        must_write(privsep_fd, &id, sizeof(id));
    }
}

static void
set_cd_lock_state(int id, char *state)
{
    char *locked_path;
    if (cd_backend_areas[id]) {
        if (asprintf(&locked_path, "%s/locked", cd_backend_areas[id]) >= 0) {
            xs_write(priv_xsh, XBT_NULL, locked_path, state, strlen(state));
            free(locked_path);
        }
    }
}

static void
lock_cd(void)
{
    int i;
    must_read(parent_fd, &i, sizeof(int));
    if (i < 0 || i >= MAX_CDS)
        clean_exit(0);
    set_cd_lock_state(i, "true");
}

void
privsep_lock_cd(int id)
{
    if (privsep_fd < 0) {
        set_cd_lock_state(id, "true");
    } else {
        enum privsep_opcode cmd;

        cmd = privsep_op_lock_cd;
        must_write(privsep_fd, &cmd, sizeof(cmd));
        must_write(privsep_fd, &id, sizeof(id));
    }
}

static void
unlock_cd(void)
{
    int i;
    must_read(parent_fd, &i, sizeof(int));
    if (i < 0 || i >= MAX_CDS)
        clean_exit(0);
    set_cd_lock_state(i, "false");
}

void
privsep_unlock_cd(int id)
{
    if (privsep_fd < 0) {
        set_cd_lock_state(id, "false");
    } else {
        enum privsep_opcode cmd;

        cmd = privsep_op_unlock_cd;
        must_write(privsep_fd, &cmd, sizeof(cmd));
        must_write(privsep_fd, &id, sizeof(id));
    }
}

static int 
xenstore_vm_write(int domid, char *key, char *value)
{
    char *buf, *path;
    int rc;

    path = xs_get_domain_path(priv_xsh, domid);
    if (path == NULL) {
        fprintf(logfile, "xs_get_domain_path: error\n");
        return 0;
    }

    asprintf(&buf, "%s/vm", path);
    free(path);
    path = xs_read(priv_xsh, XBT_NULL, buf, NULL);
    free(buf);
    if (path == NULL) {
        fprintf(logfile, "xs_read(%s): read error\n", buf);
        return 0;
    }

    asprintf(&buf, "%s/%s", path, key);
    rc = xs_write(priv_xsh, XBT_NULL, buf, value, strlen(value));
    free(buf);
    if (!rc)
        fprintf(logfile, "xs_write(%s, %s): write error\n", buf, key);
    return rc;
}

static void
set_rtc(void)
{
    long time_offset;
    char b[64];

    must_read(parent_fd, &time_offset, sizeof(time_offset));
    sprintf(b, "%ld", time_offset);
    xenstore_vm_write(domid, "rtc/timeoffset", b);
}

void
privsep_set_rtc_timeoffset(long offset)
{
    if (privsep_fd < 0) {
        char b[64];
        sprintf(b, "%ld", time_offset);
        xenstore_vm_write(domid, "rtc/timeoffset", b);
    } else {
        enum privsep_opcode cmd;
        cmd = privsep_op_set_rtc;
        must_write(privsep_fd, &cmd, sizeof(cmd));
        must_write(privsep_fd, &offset, sizeof(offset));
    }
}

static
void do_ack_logdirty_switch(char *act, size_t l)
{
    char *path;
    char *active_path;
    path = xs_get_domain_path(priv_xsh, domid);
    if (path) {
        if (asprintf(&active_path, "%s/logdirty/active", path) >= 0) {
            fprintf(logfile, "active_path %s\n", active_path);
            xs_write(priv_xsh, XBT_NULL, active_path, act, l);
            free(active_path);
        }
        free(path);
    }
}

static void
ack_logdirty_switch(void)
{
    size_t l;
    char *act;

    must_read(parent_fd, &l, sizeof(l));
    act = malloc(l);
    if (!act)
        return;
    must_read(parent_fd, act, l);
    fprintf(logfile, "ack2 logdirty %.*s\n", l, act);
    do_ack_logdirty_switch(act, l);
    free(act);
}

void
privsep_ack_logdirty_switch(char *act)
{
    size_t l;
    enum privsep_opcode cmd;

    if (privsep_fd < 0) {
        do_ack_logdirty_switch(act, strlen(act));
        return;
    }

    l = strlen(act);
    cmd = privsep_op_ack_logdirty_switch;
    must_write(privsep_fd, &cmd, sizeof(cmd));
    must_write(privsep_fd, &l, sizeof(l));
    must_write(privsep_fd, act, l);

}

#ifndef CONFIG_STUBDOM
static void
open_vm_dump(void)
{
    int fd, l;
    char *name = NULL;

    must_read(parent_fd, &l, sizeof(l));
    if (l == 0 || l > 256) {
        errno = EINVAL;
        goto done;
    }
    name = malloc(l+1);
    if (!name)
        goto done;
    must_read(parent_fd, name, l);
    name[l] = 0;

    fd = open(name, O_RDWR|O_CREAT|O_TRUNC, 0600);
done:
    send_fd(parent_fd, fd);
    free(name);
    if (fd >= 0)
        close(fd);
}
#endif

QEMUFile *
privsep_open_vm_dump(const char *name)
{
    enum privsep_opcode cmd;
    int l;

    if (privsep_fd < 0) {
           int fd;
           fd = open(name, O_RDWR|O_CREAT|O_TRUNC, 0600);
           return qemu_fdopen(fd, "wb");
    }
    cmd = privsep_op_open_vm_dump;
    must_write(privsep_fd, &cmd, sizeof(cmd));
    l = strlen(name);
    must_write(privsep_fd, &l, sizeof(l));
    must_write(privsep_fd, name, l);

    return receive_qemufile(privsep_fd, "wb");
}

static int
_open_keymap(const char *language)
{
    int e;
    int fd;
    char *filename;
    int x;

    for (x = 0; language[x]; x++) {
        if (!isalnum(language[x]) && language[x] != '-') {
            errno = EPERM;
            return -1;
        }
    }

    if (asprintf(&filename, "%s/keymaps/%s", bios_dir, language) < 0)
        return -1;
    fd = open(filename, O_RDONLY);
    e = errno;
    free(filename);
    errno = e;
    return fd;
}

#ifndef CONFIG_STUBDOM
static void
open_keymap(void)
{
    size_t l;
    char *language = NULL;
    int fd = -1;

    must_read(parent_fd, &l, sizeof(l));
    if (l == 0 || l > 256) {
        errno = EINVAL;
        goto done;
    }
    language = malloc(l+1);
    if (!language)
        goto done;
    must_read(parent_fd, language, l);
    language[l] = 0;

    fd = _open_keymap(language);
done:
    send_fd(parent_fd, fd);
    free(language);
    if (fd >= 0)
        close(fd);
}
#endif

FILE *
privsep_open_keymap(const char *language)
{
    enum privsep_opcode cmd;
    size_t l;
    int fd;
    FILE *res;
    int e;

    if (privsep_fd < 0) {
        fd = _open_keymap(language);
        if (fd < 0)
            return NULL;
        res = fdopen(fd, "r");
        if (!res) {
            e = errno;
            close(fd);
            errno = e;
        }
        return res;
    }
    cmd = privsep_op_open_keymap;
    must_write(privsep_fd, &cmd, sizeof(cmd));
    l = strlen(language);
    must_write(privsep_fd, &l, sizeof(l));
    must_write(privsep_fd, language, l);
    return receive_file(privsep_fd, "r");
}

static void _xenstore_record_dm(char *subpath, char *state)
{
    char *path = NULL;

    if (asprintf(&path, 
                "/local/domain/0/device-model/%u/%s", domid, subpath) <= 0) {
        fprintf(logfile, "out of memory recording dm \n");
        goto out;
    }
    if (!xs_write(priv_xsh, XBT_NULL, path, state, strlen(state)))
        fprintf(logfile, "error recording dm \n");

out:
    free(path);
}

static void xenstore_record_dm(void)
{
    size_t l;
    char *subpath = NULL;
    char *state = NULL;

    must_read(parent_fd, &l, sizeof(l));
    if (l == 0 || l > 256) {
        errno = EINVAL;
        goto done;
    }
    subpath = malloc(l+1);
    if (!subpath)
        goto done;
    must_read(parent_fd, subpath, l);
    subpath[l] = 0;

    must_read(parent_fd, &l, sizeof(l));
    if (l == 0 || l > 256) {
        errno = EINVAL;
        goto done;
    }
    state = malloc(l+1);
    if (!state)
        goto done;
    must_read(parent_fd, state, l);
    state[l] = 0;

    _xenstore_record_dm(subpath, state);

done:
    free(subpath);
    free(state);
}

static char *_xenstore_read_dm(char *subpath)
{
    int len;
    char *path = NULL;

    if (asprintf(&path, 
                "/local/domain/0/device-model/%u/%s", domid, subpath) <= 0) {
        fprintf(logfile, "out of memory recording dm \n");
        free(path);
        return NULL;
    }
    return xs_read(priv_xsh, XBT_NULL, path, &len);
}

static void xenstore_read_dm(void)
{
    size_t l;
    char *subpath = NULL;
    char *value = NULL;

    must_read(parent_fd, &l, sizeof(l));
    if (l == 0 || l > 256) {
        errno = EINVAL;
        goto done;
    }
    subpath = malloc(l+1);
    if (!subpath)
        goto done;
    must_read(parent_fd, subpath, l);
    subpath[l] = 0;

    value = _xenstore_read_dm(subpath);

    if (value) l = strlen(value);
    else l = 0;
    must_write(parent_fd, &l, sizeof(l));
    if (l) must_write(parent_fd, value, l);

done:
    free(subpath);
    free(value);
}

static void _xenstore_write_vslots(char *vslots)
{
    char *path = NULL;
    int pci_devid = 0;

    if (asprintf(&path, 
                "/local/domain/0/backend/pci/%u/%u/vslots", domid, pci_devid) == -1) {
        fprintf(logfile, "out of memory when updating vslots.\n");
        goto out;
    }
    if (!xs_write(priv_xsh, XBT_NULL, path, vslots, strlen(vslots)))
        fprintf(logfile, "error updating vslots \n");

out:
    free(path);
}

static void xenstore_write_vslots(void)
{
    size_t l;
    char *vslots = NULL;

    must_read(parent_fd, &l, sizeof(l));
    if (l == 0 || l > 256) {
        errno = EINVAL;
        goto done;
    }
    vslots = malloc(l+1);
    if (!vslots)
        goto done;
    must_read(parent_fd, vslots, l);
    vslots[l] = 0;

    _xenstore_write_vslots(vslots);

done:
    free(vslots);
}
 
#ifndef CONFIG_STUBDOM
static void sigxfsz_handler_f(int num)
{
    struct rlimit rlim;

    getrlimit(RLIMIT_FSIZE, &rlim);
    rlim.rlim_cur = rlim.rlim_max;
    setrlimit(RLIMIT_FSIZE, &rlim);

    write(2, "SIGXFSZ received: exiting\n", 26);

    exit(1);
}

static void sigterm_handler_f(int num)
{
    char buf[128];
    if (termsig) {
        /* Hmm, we got a exit signal before.  Still running.
         * Main loop is probably stuck somewhere ... */
        snprintf(buf, 128, "Termination signal %d received but we should already be exited, force exit now!\n", num);
        write(2, buf, strlen(buf));
        _exit(1);
    }
    snprintf(buf, 128, "Termination signal %d received, requesting clean shutdown\n", num);
    write(2, buf, strlen(buf));
    qemu_system_exit_request();
    termsig = num;
}

static void create_localtime(void)
{
    int rd, wr, count;
    char name[80];
    char buf[256];

    strcpy(name, root_directory);
    strcat(name, "/etc");
    if (mkdir(name, 00755) < 0) {
        fprintf(stderr, "cannot create directory %s\n", name);
        return;
    }

    rd = open("/etc/localtime", O_RDONLY);
    if (rd < 0) {
        fprintf(stderr, "cannot open /etc/localtime\n");
        return;
    }
    strcat(name, "/localtime");
    wr = open(name, O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW, 0644);
    if (wr < 0) {
        fprintf(stderr, "cannot create %s\n", name);
        close(rd);
        return;
    }
    while ((count = read(rd, buf, 256)) > 0) {
        write(wr, buf, count);
    }
    close(rd);
    close(wr);
}

void
init_privsep(void)
{
    int socks[2];
    pid_t child;
    struct passwd *pw;
    struct group *gr;
    enum privsep_opcode opcode;
    struct rlimit limit;
    int i;

    xs_daemon_close(priv_xsh);

    pw = getpwnam("qemu_base");
    if (!pw)
        err(1, "cannot get qemu user id");
    qemu_uid = pw->pw_uid + (unsigned short)domid;

    gr = getgrnam("qemu_base");
    if (!gr)
        err(1, "cannot get qemu group id");
    qemu_gid = gr->gr_gid + (unsigned short)domid;

    if (socketpair(AF_LOCAL, SOCK_STREAM, PF_UNSPEC, socks) == -1)
        err(1, "socketpair() failed");
    
    snprintf(root_directory, 64, "/var/xen/qemu/%d", getpid());
    if (mkdir(root_directory, 00755) < 0) {
        fprintf(stderr, "cannot create qemu scratch directory");
        strcpy(root_directory, "/var/empty");
    } else
        create_localtime();
        
    parent_pid = getpid();
    child = fork();
    if (child < 0)
        err(1, "fork() failed");
    if (child == 0) {
        /* Child of privilege. */

        parent_fd = socks[0];

        if (getrlimit(RLIMIT_NOFILE, &limit) < 0)
            limit.rlim_max = 1024;

        /* The only file descriptor we really need is the socket to
           the parent.  Close everything else. */
        closelog();
        for (i = 0; i < limit.rlim_max; i++) {
            if (i != parent_fd)
                close(i);
        }

        /* Try to get something safe on to stdin, stdout, and stderr,
           to avoid embarrassing bugs if someone tries to fprintf to
           stderr and crashes xenstored. */
        open("/dev/null", O_RDONLY);
        open("/dev/null", O_RDONLY);
        open("/dev/null", O_RDONLY);

        logfile = NULL;

        priv_xsh = xs_daemon_open();
        if (!priv_xsh) {
            printf("couldn't open privileged connection to xenstore\n");
            clean_exit(0);
        }

        while (1) {
            must_read(parent_fd, &opcode, sizeof(opcode));
            switch (opcode) {
            case privsep_op_open_iso:
                open_iso();
                break;
            case privsep_op_eject_cd:
                eject_cd();
                break;
            case privsep_op_lock_cd:
                lock_cd();
                break;
            case privsep_op_unlock_cd:
                unlock_cd();
                break;
            case privsep_op_set_rtc:
                set_rtc();
                break;
            case privsep_op_ack_logdirty_switch:
                ack_logdirty_switch();
                break;
            case privsep_op_open_vm_dump:
                open_vm_dump();
                break;
            case privsep_op_open_keymap:
                open_keymap();
                break;
            case privsep_op_record_dm:
                xenstore_record_dm();
                break;
            case privsep_op_write_vslots:
                xenstore_write_vslots();
                break;
            case privsep_op_read_dm:
                xenstore_read_dm();
                break;
            default:
                clean_exit(0);
            }
        }
     } else {
         struct sigaction sigterm_handler, sigxfsz_handler;
         memset (&sigterm_handler, 0, sizeof(struct sigaction));
         memset (&sigxfsz_handler, 0, sizeof(struct sigaction));
         sigterm_handler.sa_handler = sigterm_handler_f;
         sigxfsz_handler.sa_handler = sigxfsz_handler_f;
         struct rlimit rlim;
         char name[64];
         int f;

         /* We are the parent.  chroot and drop privileges. */
         close(socks[0]);
         privsep_fd = socks[1];

         rlim.rlim_cur = 64 * 1024 * 1024;
         rlim.rlim_max = 64 * 1024 * 1024 + 64;
         setrlimit(RLIMIT_FSIZE, &rlim);

         chdir(root_directory);
         chroot(root_directory);

         snprintf(name, 64, "core.%d", parent_pid);
         f = open(name, O_WRONLY|O_TRUNC|O_CREAT|O_NOFOLLOW, 0644);
         if (f > 0) {
             close(f);
             chown(name, qemu_uid, qemu_gid);
         }

         if (setgid(qemu_gid) < 0)
             err(1, "setgid()");
         if (setuid(qemu_uid) < 0)
             err(1, "setuid()");

         /* qemu core dumps are often useful; make sure they're allowed. */
         prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);

         /* handling termination signals */
         sigaction (SIGTERM, &sigterm_handler, NULL);
         sigaction (SIGINT, &sigterm_handler, NULL);
         sigaction (SIGHUP, &sigterm_handler, NULL);
         sigaction (SIGXFSZ, &sigxfsz_handler, NULL);
    }
}
#endif

void init_privxsh(void)
{
    priv_xsh = xs_daemon_open();
    if (!priv_xsh) {
        fprintf(logfile, "couldn't open privileged connection to xenstore\n");
        exit(0);
    }
}

int
privsep_open_ro(const char *path)
{
    if (privsep_fd < 0)
        return open(path, O_RDONLY|O_LARGEFILE|O_BINARY);

    return client_open_iso(path);
}

void
privsep_set_cd_backend(int id, const char *path)
{
    /* It's only meaningful to call this before we fork. */
    assert(privsep_fd < 0);
    assert(!cd_backend_areas[id]);
    cd_backend_areas[id] = strdup(path);
    if (!cd_backend_areas[id])
        err(1, "cloning cd backend path %s", path);
}

void privsep_record_dm(char *subpath, char *state)
{
    enum privsep_opcode cmd;
    size_t l;

    if (privsep_fd < 0) {
        _xenstore_record_dm(subpath, state);
        return;
    }
    cmd = privsep_op_record_dm;
    must_write(privsep_fd, &cmd, sizeof(cmd));
    l = strlen(subpath);
    must_write(privsep_fd, &l, sizeof(l));
    must_write(privsep_fd, subpath, l);
    l = strlen(state);
    must_write(privsep_fd, &l, sizeof(l));
    must_write(privsep_fd, state, l);
    return;
}

void privsep_write_vslots(char *vslots)
{
    enum privsep_opcode cmd;
    size_t l;

    if (privsep_fd < 0) {
        _xenstore_write_vslots(vslots);
        return;
    }
    cmd = privsep_op_write_vslots;
    must_write(privsep_fd, &cmd, sizeof(cmd));
    l = strlen(vslots);
    must_write(privsep_fd, &l, sizeof(l));
    must_write(privsep_fd, vslots, l);
    return;
}

char* privsep_read_dm(char *subpath)
{
    enum privsep_opcode cmd;
    size_t l;
    char *value;

    if (privsep_fd < 0)
        return _xenstore_read_dm(subpath);

    cmd = privsep_op_read_dm;
    must_write(privsep_fd, &cmd, sizeof(cmd));
    l = strlen(subpath);
    must_write(privsep_fd, &l, sizeof(l));
    must_write(privsep_fd, subpath, l);

    must_read(privsep_fd, &l, sizeof(l));
    if (l) {
        value = (char *) qemu_mallocz(l+1);
        if (value == NULL)
            clean_exit(0);
        must_read(privsep_fd, value, l);
        return value;
    } else
        return NULL;
}
