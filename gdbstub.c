/*
 * gdb server stub
 * 
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "vl.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>

//#define DEBUG_GDB

enum RSState {
    RS_IDLE,
    RS_GETLINE,
    RS_CHKSUM1,
    RS_CHKSUM2,
};

static int gdbserver_fd;

typedef struct GDBState {
    enum RSState state;
    int fd;
    char line_buf[4096];
    int line_buf_index;
    int line_csum;
} GDBState;

static int get_char(GDBState *s)
{
    uint8_t ch;
    int ret;

    for(;;) {
        ret = read(s->fd, &ch, 1);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN)
                return -1;
        } else if (ret == 0) {
            return -1;
        } else {
            break;
        }
    }
    return ch;
}

static void put_buffer(GDBState *s, const uint8_t *buf, int len)
{
    int ret;

    while (len > 0) {
        ret = write(s->fd, buf, len);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN)
                return;
        } else {
            buf += ret;
            len -= ret;
        }
    }
}

static inline int fromhex(int v)
{
    if (v >= '0' && v <= '9')
        return v - '0';
    else if (v >= 'A' && v <= 'F')
        return v - 'A' + 10;
    else if (v >= 'a' && v <= 'f')
        return v - 'a' + 10;
    else
        return 0;
}

static inline int tohex(int v)
{
    if (v < 10)
        return v + '0';
    else
        return v - 10 + 'a';
}

static void memtohex(char *buf, const uint8_t *mem, int len)
{
    int i, c;
    char *q;
    q = buf;
    for(i = 0; i < len; i++) {
        c = mem[i];
        *q++ = tohex(c >> 4);
        *q++ = tohex(c & 0xf);
    }
    *q = '\0';
}

static void hextomem(uint8_t *mem, const char *buf, int len)
{
    int i;

    for(i = 0; i < len; i++) {
        mem[i] = (fromhex(buf[0]) << 4) | fromhex(buf[1]);
        buf += 2;
    }
}

/* return -1 if error, 0 if OK */
static int put_packet(GDBState *s, char *buf)
{
    char buf1[3];
    int len, csum, ch, i;

#ifdef DEBUG_GDB
    printf("reply='%s'\n", buf);
#endif

    for(;;) {
        buf1[0] = '$';
        put_buffer(s, buf1, 1);
        len = strlen(buf);
        put_buffer(s, buf, len);
        csum = 0;
        for(i = 0; i < len; i++) {
            csum += buf[i];
        }
        buf1[0] = '#';
        buf1[1] = tohex((csum >> 4) & 0xf);
        buf1[2] = tohex((csum) & 0xf);

        put_buffer(s, buf1, 3);

        ch = get_char(s);
        if (ch < 0)
            return -1;
        if (ch == '+')
            break;
    }
    return 0;
}

#if defined(TARGET_I386)

static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    uint32_t *registers = (uint32_t *)mem_buf;
    int i, fpus;

    for(i = 0; i < 8; i++) {
        registers[i] = env->regs[i];
    }
    registers[8] = env->eip;
    registers[9] = env->eflags;
    registers[10] = env->segs[R_CS].selector;
    registers[11] = env->segs[R_SS].selector;
    registers[12] = env->segs[R_DS].selector;
    registers[13] = env->segs[R_ES].selector;
    registers[14] = env->segs[R_FS].selector;
    registers[15] = env->segs[R_GS].selector;
    /* XXX: convert floats */
    for(i = 0; i < 8; i++) {
        memcpy(mem_buf + 16 * 4 + i * 10, &env->fpregs[i], 10);
    }
    registers[36] = env->fpuc;
    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    registers[37] = fpus;
    registers[38] = 0; /* XXX: convert tags */
    registers[39] = 0; /* fiseg */
    registers[40] = 0; /* fioff */
    registers[41] = 0; /* foseg */
    registers[42] = 0; /* fooff */
    registers[43] = 0; /* fop */
    
    for(i = 0; i < 16; i++)
        tswapls(&registers[i]);
    for(i = 36; i < 44; i++)
        tswapls(&registers[i]);
    return 44 * 4;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    uint32_t *registers = (uint32_t *)mem_buf;
    int i;

    for(i = 0; i < 8; i++) {
        env->regs[i] = tswapl(registers[i]);
    }
    env->eip = tswapl(registers[8]);
    env->eflags = tswapl(registers[9]);
#if defined(CONFIG_USER_ONLY)
#define LOAD_SEG(index, sreg)\
            if (tswapl(registers[index]) != env->segs[sreg].selector)\
                cpu_x86_load_seg(env, sreg, tswapl(registers[index]));
            LOAD_SEG(10, R_CS);
            LOAD_SEG(11, R_SS);
            LOAD_SEG(12, R_DS);
            LOAD_SEG(13, R_ES);
            LOAD_SEG(14, R_FS);
            LOAD_SEG(15, R_GS);
#endif
}

#elif defined (TARGET_PPC)
static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    uint32_t *registers = (uint32_t *)mem_buf, tmp;
    int i;

    /* fill in gprs */
    for(i = 0; i < 32; i++) {
        registers[i] = tswapl(env->gpr[i]);
    }
    /* fill in fprs */
    for (i = 0; i < 32; i++) {
        registers[(i * 2) + 32] = tswapl(*((uint32_t *)&env->fpr[i]));
	registers[(i * 2) + 33] = tswapl(*((uint32_t *)&env->fpr[i] + 1));
    }
    /* nip, msr, ccr, lnk, ctr, xer, mq */
    registers[96] = tswapl(env->nip);
    registers[97] = tswapl(_load_msr(env));
    tmp = 0;
    for (i = 0; i < 8; i++)
        tmp |= env->crf[i] << (32 - ((i + 1) * 4));
    registers[98] = tswapl(tmp);
    registers[99] = tswapl(env->lr);
    registers[100] = tswapl(env->ctr);
    registers[101] = tswapl(_load_xer(env));
    registers[102] = 0;

    return 103 * 4;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    uint32_t *registers = (uint32_t *)mem_buf;
    int i;

    /* fill in gprs */
    for (i = 0; i < 32; i++) {
        env->gpr[i] = tswapl(registers[i]);
    }
    /* fill in fprs */
    for (i = 0; i < 32; i++) {
        *((uint32_t *)&env->fpr[i]) = tswapl(registers[(i * 2) + 32]);
	*((uint32_t *)&env->fpr[i] + 1) = tswapl(registers[(i * 2) + 33]);
    }
    /* nip, msr, ccr, lnk, ctr, xer, mq */
    env->nip = tswapl(registers[96]);
    _store_msr(env, tswapl(registers[97]));
    registers[98] = tswapl(registers[98]);
    for (i = 0; i < 8; i++)
        env->crf[i] = (registers[98] >> (32 - ((i + 1) * 4))) & 0xF;
    env->lr = tswapl(registers[99]);
    env->ctr = tswapl(registers[100]);
    _store_xer(env, tswapl(registers[101]));
}
#elif defined (TARGET_SPARC)
static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    uint32_t *registers = (uint32_t *)mem_buf, tmp;
    int i;

    /* fill in g0..g7 */
    for(i = 0; i < 7; i++) {
        registers[i] = tswapl(env->gregs[i]);
    }
    /* fill in register window */
    for(i = 0; i < 24; i++) {
        registers[i + 8] = tswapl(env->regwptr[i]);
    }
    /* fill in fprs */
    for (i = 0; i < 32; i++) {
        registers[i + 32] = tswapl(*((uint32_t *)&env->fpr[i]));
    }
    /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
    registers[64] = tswapl(env->y);
    tmp = GET_PSR(env);
    registers[65] = tswapl(tmp);
    registers[66] = tswapl(env->wim);
    registers[67] = tswapl(env->tbr);
    registers[68] = tswapl(env->pc);
    registers[69] = tswapl(env->npc);
    registers[70] = tswapl(env->fsr);
    registers[71] = 0; /* csr */
    registers[72] = 0;

    return 73 * 4;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    uint32_t *registers = (uint32_t *)mem_buf;
    int i;

    /* fill in g0..g7 */
    for(i = 0; i < 7; i++) {
        env->gregs[i] = tswapl(registers[i]);
    }
    /* fill in register window */
    for(i = 0; i < 24; i++) {
        env->regwptr[i] = tswapl(registers[i]);
    }
    /* fill in fprs */
    for (i = 0; i < 32; i++) {
        *((uint32_t *)&env->fpr[i]) = tswapl(registers[i + 32]);
    }
    /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
    env->y = tswapl(registers[64]);
    PUT_PSR(env, tswapl(registers[65]));
    env->wim = tswapl(registers[66]);
    env->tbr = tswapl(registers[67]);
    env->pc = tswapl(registers[68]);
    env->npc = tswapl(registers[69]);
    env->fsr = tswapl(registers[70]);
}
#else

static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    return 0;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
}

#endif

/* port = 0 means default port */
static int gdb_handle_packet(GDBState *s, const char *line_buf)
{
    CPUState *env = cpu_single_env;
    const char *p;
    int ch, reg_size, type;
    char buf[4096];
    uint8_t mem_buf[2000];
    uint32_t *registers;
    uint32_t addr, len;
    
#ifdef DEBUG_GDB
    printf("command='%s'\n", line_buf);
#endif
    p = line_buf;
    ch = *p++;
    switch(ch) {
    case '?':
        snprintf(buf, sizeof(buf), "S%02x", SIGTRAP);
        put_packet(s, buf);
        break;
    case 'c':
        if (*p != '\0') {
            addr = strtoul(p, (char **)&p, 16);
#if defined(TARGET_I386)
            env->eip = addr;
#elif defined (TARGET_PPC)
            env->nip = addr;
#elif defined (TARGET_SPARC)
            env->pc = addr;
            env->npc = addr + 4;
#endif
        }
        vm_start();
        break;
    case 's':
        if (*p != '\0') {
            addr = strtoul(p, (char **)&p, 16);
#if defined(TARGET_I386)
            env->eip = addr;
#elif defined (TARGET_PPC)
            env->nip = addr;
#elif defined (TARGET_SPARC)
            env->pc = addr;
            env->npc = addr + 4;
#endif
        }
        cpu_single_step(env, 1);
        vm_start();
        break;
    case 'g':
        reg_size = cpu_gdb_read_registers(env, mem_buf);
        memtohex(buf, mem_buf, reg_size);
        put_packet(s, buf);
        break;
    case 'G':
        registers = (void *)mem_buf;
        len = strlen(p) / 2;
        hextomem((uint8_t *)registers, p, len);
        cpu_gdb_write_registers(env, mem_buf, len);
        put_packet(s, "OK");
        break;
    case 'm':
        addr = strtoul(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoul(p, NULL, 16);
        if (cpu_memory_rw_debug(env, addr, mem_buf, len, 0) != 0)
            memset(mem_buf, 0, len);
        memtohex(buf, mem_buf, len);
        put_packet(s, buf);
        break;
    case 'M':
        addr = strtoul(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoul(p, (char **)&p, 16);
        if (*p == ':')
            p++;
        hextomem(mem_buf, p, len);
        if (cpu_memory_rw_debug(env, addr, mem_buf, len, 1) != 0)
            put_packet(s, "ENN");
        else
            put_packet(s, "OK");
        break;
    case 'Z':
        type = strtoul(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        addr = strtoul(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoul(p, (char **)&p, 16);
        if (type == 0 || type == 1) {
            if (cpu_breakpoint_insert(env, addr) < 0)
                goto breakpoint_error;
            put_packet(s, "OK");
        } else {
        breakpoint_error:
            put_packet(s, "ENN");
        }
        break;
    case 'z':
        type = strtoul(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        addr = strtoul(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoul(p, (char **)&p, 16);
        if (type == 0 || type == 1) {
            cpu_breakpoint_remove(env, addr);
            put_packet(s, "OK");
        } else {
            goto breakpoint_error;
        }
        break;
    default:
        //        unknown_command:
        /* put empty packet */
        buf[0] = '\0';
        put_packet(s, buf);
        break;
    }
    return RS_IDLE;
}

extern void tb_flush(CPUState *env);

static void gdb_vm_stopped(void *opaque, int reason)
{
    GDBState *s = opaque;
    char buf[256];
    int ret;

    /* disable single step if it was enable */
    cpu_single_step(cpu_single_env, 0);

    if (reason == EXCP_DEBUG) {
	tb_flush(cpu_single_env);
        ret = SIGTRAP;
    }
    else
        ret = 0;
    snprintf(buf, sizeof(buf), "S%02x", ret);
    put_packet(s, buf);
}

static void gdb_read_byte(GDBState *s, int ch)
{
    int i, csum;
    char reply[1];

    if (vm_running) {
        /* when the CPU is running, we cannot do anything except stop
           it when receiving a char */
        vm_stop(EXCP_INTERRUPT);
    } else {
        switch(s->state) {
        case RS_IDLE:
            if (ch == '$') {
                s->line_buf_index = 0;
                s->state = RS_GETLINE;
            }
            break;
        case RS_GETLINE:
            if (ch == '#') {
            s->state = RS_CHKSUM1;
            } else if (s->line_buf_index >= sizeof(s->line_buf) - 1) {
                s->state = RS_IDLE;
            } else {
            s->line_buf[s->line_buf_index++] = ch;
            }
            break;
        case RS_CHKSUM1:
            s->line_buf[s->line_buf_index] = '\0';
            s->line_csum = fromhex(ch) << 4;
            s->state = RS_CHKSUM2;
            break;
        case RS_CHKSUM2:
            s->line_csum |= fromhex(ch);
            csum = 0;
            for(i = 0; i < s->line_buf_index; i++) {
                csum += s->line_buf[i];
            }
            if (s->line_csum != (csum & 0xff)) {
                reply[0] = '-';
                put_buffer(s, reply, 1);
                s->state = RS_IDLE;
            } else {
                reply[0] = '+';
                put_buffer(s, reply, 1);
                s->state = gdb_handle_packet(s, s->line_buf);
            }
            break;
        }
    }
}

static int gdb_can_read(void *opaque)
{
    return 256;
}

static void gdb_read(void *opaque, const uint8_t *buf, int size)
{
    GDBState *s = opaque;
    int i;
    if (size == 0) {
        /* end of connection */
        qemu_del_vm_stop_handler(gdb_vm_stopped, s);
        qemu_del_fd_read_handler(s->fd);
        qemu_free(s);
        vm_start();
    } else {
        for(i = 0; i < size; i++)
            gdb_read_byte(s, buf[i]);
    }
}

static void gdb_accept(void *opaque, const uint8_t *buf, int size)
{
    GDBState *s;
    struct sockaddr_in sockaddr;
    socklen_t len;
    int val, fd;

    for(;;) {
        len = sizeof(sockaddr);
        fd = accept(gdbserver_fd, (struct sockaddr *)&sockaddr, &len);
        if (fd < 0 && errno != EINTR) {
            perror("accept");
            return;
        } else if (fd >= 0) {
            break;
        }
    }

    /* set short latency */
    val = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    
    s = qemu_mallocz(sizeof(GDBState));
    if (!s) {
        close(fd);
        return;
    }
    s->fd = fd;

    fcntl(fd, F_SETFL, O_NONBLOCK);

    /* stop the VM */
    vm_stop(EXCP_INTERRUPT);

    /* start handling I/O */
    qemu_add_fd_read_handler(s->fd, gdb_can_read, gdb_read, s);
    /* when the VM is stopped, the following callback is called */
    qemu_add_vm_stop_handler(gdb_vm_stopped, s);
}

static int gdbserver_open(int port)
{
    struct sockaddr_in sockaddr;
    int fd, val, ret;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    /* allow fast reuse */
    val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = 0;
    ret = bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (ret < 0) {
        perror("bind");
        return -1;
    }
    ret = listen(fd, 0);
    if (ret < 0) {
        perror("listen");
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}

int gdbserver_start(int port)
{
    gdbserver_fd = gdbserver_open(port);
    if (gdbserver_fd < 0)
        return -1;
    /* accept connections */
    qemu_add_fd_read_handler(gdbserver_fd, NULL, gdb_accept, NULL);
    return 0;
}
