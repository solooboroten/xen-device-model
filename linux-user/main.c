/*
 *  qemu user main
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "qemu.h"
#include "qemu-common.h"

#define DEBUG_LOGFILE "/tmp/qemu.log"

static const char *interp_prefix = CONFIG_QEMU_PREFIX;
const char *qemu_uname_release = CONFIG_UNAME_RELEASE;

#if defined(__i386__) && !defined(CONFIG_STATIC)
/* Force usage of an ELF interpreter even if it is an ELF shared
   object ! */
const char interp[] __attribute__((section(".interp"))) = "/lib/ld-linux.so.2";
#endif

/* for recent libc, we add these dummy symbols which are not declared
   when generating a linked object (bug in ld ?) */
#if (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 3)) && !defined(CONFIG_STATIC)
asm(".globl __preinit_array_start\n"
    ".globl __preinit_array_end\n"
    ".globl __init_array_start\n"
    ".globl __init_array_end\n"
    ".globl __fini_array_start\n"
    ".globl __fini_array_end\n"
    ".section \".rodata\"\n"
    "__preinit_array_start:\n"
    "__preinit_array_end:\n"
    "__init_array_start:\n"
    "__init_array_end:\n"
    "__fini_array_start:\n"
    "__fini_array_end:\n"
    ".long 0\n"
    ".previous\n");
#endif

/* XXX: on x86 MAP_GROWSDOWN only works if ESP <= address + 32, so
   we allocate a bigger stack. Need a better solution, for example
   by remapping the process stack directly at the right place */
unsigned long x86_stack_size = 512 * 1024;

void gemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void cpu_outb(CPUState *env, int addr, int val)
{
    fprintf(stderr, "outb: port=0x%04x, data=%02x\n", addr, val);
}

void cpu_outw(CPUState *env, int addr, int val)
{
    fprintf(stderr, "outw: port=0x%04x, data=%04x\n", addr, val);
}

void cpu_outl(CPUState *env, int addr, int val)
{
    fprintf(stderr, "outl: port=0x%04x, data=%08x\n", addr, val);
}

int cpu_inb(CPUState *env, int addr)
{
    fprintf(stderr, "inb: port=0x%04x\n", addr);
    return 0;
}

int cpu_inw(CPUState *env, int addr)
{
    fprintf(stderr, "inw: port=0x%04x\n", addr);
    return 0;
}

int cpu_inl(CPUState *env, int addr)
{
    fprintf(stderr, "inl: port=0x%04x\n", addr);
    return 0;
}

int cpu_get_pic_interrupt(CPUState *env)
{
    return -1;
}

/* timers for rdtsc */

#if 0

static uint64_t emu_time;

int64_t cpu_get_real_ticks(void)
{
    return emu_time++;
}

#endif

#ifdef TARGET_I386
/***********************************************************/
/* CPUX86 core interface */

void cpu_smm_update(CPUState *env)
{
}

uint64_t cpu_get_tsc(CPUX86State *env)
{
    return cpu_get_real_ticks();
}

static void write_dt(void *ptr, unsigned long addr, unsigned long limit,
                     int flags)
{
    unsigned int e1, e2;
    uint32_t *p;
    e1 = (addr << 16) | (limit & 0xffff);
    e2 = ((addr >> 16) & 0xff) | (addr & 0xff000000) | (limit & 0x000f0000);
    e2 |= flags;
    p = ptr;
    p[0] = tswapl(e1);
    p[1] = tswapl(e2);
}

#if TARGET_X86_64
uint64_t idt_table[512];

static void set_gate64(void *ptr, unsigned int type, unsigned int dpl,
                       uint64_t addr, unsigned int sel)
{
    uint32_t *p, e1, e2;
    e1 = (addr & 0xffff) | (sel << 16);
    e2 = (addr & 0xffff0000) | 0x8000 | (dpl << 13) | (type << 8);
    p = ptr;
    p[0] = tswap32(e1);
    p[1] = tswap32(e2);
    p[2] = tswap32(addr >> 32);
    p[3] = 0;
}
/* only dpl matters as we do only user space emulation */
static void set_idt(int n, unsigned int dpl)
{
    set_gate64(idt_table + n * 2, 0, dpl, 0, 0);
}
#else
uint64_t idt_table[256];

static void set_gate(void *ptr, unsigned int type, unsigned int dpl,
                     uint32_t addr, unsigned int sel)
{
    uint32_t *p, e1, e2;
    e1 = (addr & 0xffff) | (sel << 16);
    e2 = (addr & 0xffff0000) | 0x8000 | (dpl << 13) | (type << 8);
    p = ptr;
    p[0] = tswap32(e1);
    p[1] = tswap32(e2);
}

/* only dpl matters as we do only user space emulation */
static void set_idt(int n, unsigned int dpl)
{
    set_gate(idt_table + n, 0, dpl, 0, 0);
}
#endif

void cpu_loop(CPUX86State *env)
{
    int trapnr;
    abi_ulong pc;
    target_siginfo_t info;

    for(;;) {
        trapnr = cpu_x86_exec(env);
        switch(trapnr) {
        case 0x80:
            /* linux syscall from int $0x80 */
            env->regs[R_EAX] = do_syscall(env,
                                          env->regs[R_EAX],
                                          env->regs[R_EBX],
                                          env->regs[R_ECX],
                                          env->regs[R_EDX],
                                          env->regs[R_ESI],
                                          env->regs[R_EDI],
                                          env->regs[R_EBP]);
            break;
#ifndef TARGET_ABI32
        case EXCP_SYSCALL:
            /* linux syscall from syscall intruction */
            env->regs[R_EAX] = do_syscall(env,
                                          env->regs[R_EAX],
                                          env->regs[R_EDI],
                                          env->regs[R_ESI],
                                          env->regs[R_EDX],
                                          env->regs[10],
                                          env->regs[8],
                                          env->regs[9]);
            env->eip = env->exception_next_eip;
            break;
#endif
        case EXCP0B_NOSEG:
        case EXCP0C_STACK:
            info.si_signo = SIGBUS;
            info.si_errno = 0;
            info.si_code = TARGET_SI_KERNEL;
            info._sifields._sigfault._addr = 0;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP0D_GPF:
            /* XXX: potential problem if ABI32 */
#ifndef TARGET_X86_64
            if (env->eflags & VM_MASK) {
                handle_vm86_fault(env);
            } else
#endif
            {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SI_KERNEL;
                info._sifields._sigfault._addr = 0;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP0E_PAGE:
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            if (!(env->error_code & 1))
                info.si_code = TARGET_SEGV_MAPERR;
            else
                info.si_code = TARGET_SEGV_ACCERR;
            info._sifields._sigfault._addr = env->cr[2];
            queue_signal(info.si_signo, &info);
            break;
        case EXCP00_DIVZ:
#ifndef TARGET_X86_64
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else
#endif
            {
                /* division by zero */
                info.si_signo = SIGFPE;
                info.si_errno = 0;
                info.si_code = TARGET_FPE_INTDIV;
                info._sifields._sigfault._addr = env->eip;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP01_SSTP:
        case EXCP03_INT3:
#ifndef TARGET_X86_64
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else
#endif
            {
                info.si_signo = SIGTRAP;
                info.si_errno = 0;
                if (trapnr == EXCP01_SSTP) {
                    info.si_code = TARGET_TRAP_BRKPT;
                    info._sifields._sigfault._addr = env->eip;
                } else {
                    info.si_code = TARGET_SI_KERNEL;
                    info._sifields._sigfault._addr = 0;
                }
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP04_INTO:
        case EXCP05_BOUND:
#ifndef TARGET_X86_64
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else
#endif
            {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SI_KERNEL;
                info._sifields._sigfault._addr = 0;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP06_ILLOP:
            info.si_signo = SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->eip;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            pc = env->segs[R_CS].base + env->eip;
            fprintf(stderr, "qemu: 0x%08lx: unhandled CPU exception 0x%x - aborting\n",
                    (long)pc, trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}
#endif

#ifdef TARGET_ARM

/* XXX: find a better solution */
extern void tb_invalidate_page_range(abi_ulong start, abi_ulong end);

static void arm_cache_flush(abi_ulong start, abi_ulong last)
{
    abi_ulong addr, last1;

    if (last < start)
        return;
    addr = start;
    for(;;) {
        last1 = ((addr + TARGET_PAGE_SIZE) & TARGET_PAGE_MASK) - 1;
        if (last1 > last)
            last1 = last;
        tb_invalidate_page_range(addr, last1 + 1);
        if (last1 == last)
            break;
        addr = last1 + 1;
    }
}

void cpu_loop(CPUARMState *env)
{
    int trapnr;
    unsigned int n, insn;
    target_siginfo_t info;
    uint32_t addr;

    for(;;) {
        trapnr = cpu_arm_exec(env);
        switch(trapnr) {
        case EXCP_UDEF:
            {
                TaskState *ts = env->opaque;
                uint32_t opcode;
                int rc;

                /* we handle the FPU emulation here, as Linux */
                /* we get the opcode */
                /* FIXME - what to do if get_user() fails? */
                get_user_u32(opcode, env->regs[15]);

                rc = EmulateAll(opcode, &ts->fpa, env);
                if (rc == 0) { /* illegal instruction */
                    info.si_signo = SIGILL;
                    info.si_errno = 0;
                    info.si_code = TARGET_ILL_ILLOPN;
                    info._sifields._sigfault._addr = env->regs[15];
                    queue_signal(info.si_signo, &info);
                } else if (rc < 0) { /* FP exception */
                    int arm_fpe=0;

                    /* translate softfloat flags to FPSR flags */
                    if (-rc & float_flag_invalid)
                      arm_fpe |= BIT_IOC;
                    if (-rc & float_flag_divbyzero)
                      arm_fpe |= BIT_DZC;
                    if (-rc & float_flag_overflow)
                      arm_fpe |= BIT_OFC;
                    if (-rc & float_flag_underflow)
                      arm_fpe |= BIT_UFC;
                    if (-rc & float_flag_inexact)
                      arm_fpe |= BIT_IXC;

                    FPSR fpsr = ts->fpa.fpsr;
                    //printf("fpsr 0x%x, arm_fpe 0x%x\n",fpsr,arm_fpe);

                    if (fpsr & (arm_fpe << 16)) { /* exception enabled? */
                      info.si_signo = SIGFPE;
                      info.si_errno = 0;

                      /* ordered by priority, least first */
                      if (arm_fpe & BIT_IXC) info.si_code = TARGET_FPE_FLTRES;
                      if (arm_fpe & BIT_UFC) info.si_code = TARGET_FPE_FLTUND;
                      if (arm_fpe & BIT_OFC) info.si_code = TARGET_FPE_FLTOVF;
                      if (arm_fpe & BIT_DZC) info.si_code = TARGET_FPE_FLTDIV;
                      if (arm_fpe & BIT_IOC) info.si_code = TARGET_FPE_FLTINV;

                      info._sifields._sigfault._addr = env->regs[15];
                      queue_signal(info.si_signo, &info);
                    } else {
                      env->regs[15] += 4;
                    }

                    /* accumulate unenabled exceptions */
                    if ((!(fpsr & BIT_IXE)) && (arm_fpe & BIT_IXC))
                      fpsr |= BIT_IXC;
                    if ((!(fpsr & BIT_UFE)) && (arm_fpe & BIT_UFC))
                      fpsr |= BIT_UFC;
                    if ((!(fpsr & BIT_OFE)) && (arm_fpe & BIT_OFC))
                      fpsr |= BIT_OFC;
                    if ((!(fpsr & BIT_DZE)) && (arm_fpe & BIT_DZC))
                      fpsr |= BIT_DZC;
                    if ((!(fpsr & BIT_IOE)) && (arm_fpe & BIT_IOC))
                      fpsr |= BIT_IOC;
                    ts->fpa.fpsr=fpsr;
                } else { /* everything OK */
                    /* increment PC */
                    env->regs[15] += 4;
                }
            }
            break;
        case EXCP_SWI:
        case EXCP_BKPT:
            {
                env->eabi = 1;
                /* system call */
                if (trapnr == EXCP_BKPT) {
                    if (env->thumb) {
                        /* FIXME - what to do if get_user() fails? */
                        get_user_u16(insn, env->regs[15]);
                        n = insn & 0xff;
                        env->regs[15] += 2;
                    } else {
                        /* FIXME - what to do if get_user() fails? */
                        get_user_u32(insn, env->regs[15]);
                        n = (insn & 0xf) | ((insn >> 4) & 0xff0);
                        env->regs[15] += 4;
                    }
                } else {
                    if (env->thumb) {
                        /* FIXME - what to do if get_user() fails? */
                        get_user_u16(insn, env->regs[15] - 2);
                        n = insn & 0xff;
                    } else {
                        /* FIXME - what to do if get_user() fails? */
                        get_user_u32(insn, env->regs[15] - 4);
                        n = insn & 0xffffff;
                    }
                }

                if (n == ARM_NR_cacheflush) {
                    arm_cache_flush(env->regs[0], env->regs[1]);
                } else if (n == ARM_NR_semihosting
                           || n == ARM_NR_thumb_semihosting) {
                    env->regs[0] = do_arm_semihosting (env);
                } else if (n == 0 || n >= ARM_SYSCALL_BASE
                           || (env->thumb && n == ARM_THUMB_SYSCALL)) {
                    /* linux syscall */
                    if (env->thumb || n == 0) {
                        n = env->regs[7];
                    } else {
                        n -= ARM_SYSCALL_BASE;
                        env->eabi = 0;
                    }
                    env->regs[0] = do_syscall(env,
                                              n,
                                              env->regs[0],
                                              env->regs[1],
                                              env->regs[2],
                                              env->regs[3],
                                              env->regs[4],
                                              env->regs[5]);
                } else {
                    goto error;
                }
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_PREFETCH_ABORT:
            addr = env->cp15.c6_data;
            goto do_segv;
        case EXCP_DATA_ABORT:
            addr = env->cp15.c6_insn;
            goto do_segv;
        do_segv:
            {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = addr;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
        error:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n",
                    trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            abort();
        }
        process_pending_signals(env);
    }
}

#endif

#ifdef TARGET_SPARC

//#define DEBUG_WIN

/* WARNING: dealing with register windows _is_ complicated. More info
   can be found at http://www.sics.se/~psm/sparcstack.html */
static inline int get_reg_index(CPUSPARCState *env, int cwp, int index)
{
    index = (index + cwp * 16) & (16 * NWINDOWS - 1);
    /* wrap handling : if cwp is on the last window, then we use the
       registers 'after' the end */
    if (index < 8 && env->cwp == (NWINDOWS - 1))
        index += (16 * NWINDOWS);
    return index;
}

/* save the register window 'cwp1' */
static inline void save_window_offset(CPUSPARCState *env, int cwp1)
{
    unsigned int i;
    abi_ulong sp_ptr;

    sp_ptr = env->regbase[get_reg_index(env, cwp1, 6)];
#if defined(DEBUG_WIN)
    printf("win_overflow: sp_ptr=0x%x save_cwp=%d\n",
           (int)sp_ptr, cwp1);
#endif
    for(i = 0; i < 16; i++) {
        /* FIXME - what to do if put_user() fails? */
        put_user_ual(env->regbase[get_reg_index(env, cwp1, 8 + i)], sp_ptr);
        sp_ptr += sizeof(abi_ulong);
    }
}

static void save_window(CPUSPARCState *env)
{
#ifndef TARGET_SPARC64
    unsigned int new_wim;
    new_wim = ((env->wim >> 1) | (env->wim << (NWINDOWS - 1))) &
        ((1LL << NWINDOWS) - 1);
    save_window_offset(env, (env->cwp - 2) & (NWINDOWS - 1));
    env->wim = new_wim;
#else
    save_window_offset(env, (env->cwp - 2) & (NWINDOWS - 1));
    env->cansave++;
    env->canrestore--;
#endif
}

static void restore_window(CPUSPARCState *env)
{
    unsigned int new_wim, i, cwp1;
    abi_ulong sp_ptr;

    new_wim = ((env->wim << 1) | (env->wim >> (NWINDOWS - 1))) &
        ((1LL << NWINDOWS) - 1);

    /* restore the invalid window */
    cwp1 = (env->cwp + 1) & (NWINDOWS - 1);
    sp_ptr = env->regbase[get_reg_index(env, cwp1, 6)];
#if defined(DEBUG_WIN)
    printf("win_underflow: sp_ptr=0x%x load_cwp=%d\n",
           (int)sp_ptr, cwp1);
#endif
    for(i = 0; i < 16; i++) {
        /* FIXME - what to do if get_user() fails? */
        get_user_ual(env->regbase[get_reg_index(env, cwp1, 8 + i)], sp_ptr);
        sp_ptr += sizeof(abi_ulong);
    }
    env->wim = new_wim;
#ifdef TARGET_SPARC64
    env->canrestore++;
    if (env->cleanwin < NWINDOWS - 1)
	env->cleanwin++;
    env->cansave--;
#endif
}

static void flush_windows(CPUSPARCState *env)
{
    int offset, cwp1;

    offset = 1;
    for(;;) {
        /* if restore would invoke restore_window(), then we can stop */
        cwp1 = (env->cwp + offset) & (NWINDOWS - 1);
        if (env->wim & (1 << cwp1))
            break;
        save_window_offset(env, cwp1);
        offset++;
    }
    /* set wim so that restore will reload the registers */
    cwp1 = (env->cwp + 1) & (NWINDOWS - 1);
    env->wim = 1 << cwp1;
#if defined(DEBUG_WIN)
    printf("flush_windows: nb=%d\n", offset - 1);
#endif
}

void cpu_loop (CPUSPARCState *env)
{
    int trapnr, ret;
    target_siginfo_t info;

    while (1) {
        trapnr = cpu_sparc_exec (env);

        switch (trapnr) {
#ifndef TARGET_SPARC64
        case 0x88:
        case 0x90:
#else
        case 0x110:
        case 0x16d:
#endif
            ret = do_syscall (env, env->gregs[1],
                              env->regwptr[0], env->regwptr[1],
                              env->regwptr[2], env->regwptr[3],
                              env->regwptr[4], env->regwptr[5]);
            if ((unsigned int)ret >= (unsigned int)(-515)) {
#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
                env->xcc |= PSR_CARRY;
#else
                env->psr |= PSR_CARRY;
#endif
                ret = -ret;
            } else {
#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
                env->xcc &= ~PSR_CARRY;
#else
                env->psr &= ~PSR_CARRY;
#endif
            }
            env->regwptr[0] = ret;
            /* next instruction */
            env->pc = env->npc;
            env->npc = env->npc + 4;
            break;
        case 0x83: /* flush windows */
#ifdef TARGET_ABI32
        case 0x103:
#endif
            flush_windows(env);
            /* next instruction */
            env->pc = env->npc;
            env->npc = env->npc + 4;
            break;
#ifndef TARGET_SPARC64
        case TT_WIN_OVF: /* window overflow */
            save_window(env);
            break;
        case TT_WIN_UNF: /* window underflow */
            restore_window(env);
            break;
        case TT_TFAULT:
        case TT_DFAULT:
            {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->mmuregs[4];
                queue_signal(info.si_signo, &info);
            }
            break;
#else
        case TT_SPILL: /* window overflow */
            save_window(env);
            break;
        case TT_FILL: /* window underflow */
            restore_window(env);
            break;
        case TT_TFAULT:
        case TT_DFAULT:
            {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                if (trapnr == TT_DFAULT)
                    info._sifields._sigfault._addr = env->dmmuregs[4];
                else
                    info._sifields._sigfault._addr = env->tsptr->tpc;
                queue_signal(info.si_signo, &info);
            }
            break;
#ifndef TARGET_ABI32
        case 0x16e:
            flush_windows(env);
            sparc64_get_context(env);
            break;
        case 0x16f:
            flush_windows(env);
            sparc64_set_context(env);
            break;
#endif
#endif
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            printf ("Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            exit (1);
        }
        process_pending_signals (env);
    }
}

#endif

#ifdef TARGET_PPC
static inline uint64_t cpu_ppc_get_tb (CPUState *env)
{
    /* TO FIX */
    return 0;
}

uint32_t cpu_ppc_load_tbl (CPUState *env)
{
    return cpu_ppc_get_tb(env) & 0xFFFFFFFF;
}

uint32_t cpu_ppc_load_tbu (CPUState *env)
{
    return cpu_ppc_get_tb(env) >> 32;
}

uint32_t cpu_ppc_load_atbl (CPUState *env)
{
    return cpu_ppc_get_tb(env) & 0xFFFFFFFF;
}

uint32_t cpu_ppc_load_atbu (CPUState *env)
{
    return cpu_ppc_get_tb(env) >> 32;
}

uint32_t cpu_ppc601_load_rtcu (CPUState *env)
__attribute__ (( alias ("cpu_ppc_load_tbu") ));

uint32_t cpu_ppc601_load_rtcl (CPUState *env)
{
    return cpu_ppc_load_tbl(env) & 0x3FFFFF80;
}

/* XXX: to be fixed */
int ppc_dcr_read (ppc_dcr_t *dcr_env, int dcrn, target_ulong *valp)
{
    return -1;
}

int ppc_dcr_write (ppc_dcr_t *dcr_env, int dcrn, target_ulong val)
{
    return -1;
}

#define EXCP_DUMP(env, fmt, args...)                                         \
do {                                                                          \
    fprintf(stderr, fmt , ##args);                                            \
    cpu_dump_state(env, stderr, fprintf, 0);                                  \
    if (loglevel != 0) {                                                      \
        fprintf(logfile, fmt , ##args);                                       \
        cpu_dump_state(env, logfile, fprintf, 0);                             \
    }                                                                         \
} while (0)

void cpu_loop(CPUPPCState *env)
{
    target_siginfo_t info;
    int trapnr;
    uint32_t ret;

    for(;;) {
        trapnr = cpu_ppc_exec(env);
        switch(trapnr) {
        case POWERPC_EXCP_NONE:
            /* Just go on */
            break;
        case POWERPC_EXCP_CRITICAL: /* Critical input                        */
            cpu_abort(env, "Critical interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_MCHECK:   /* Machine check exception               */
            cpu_abort(env, "Machine check exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_DSI:      /* Data storage exception                */
            EXCP_DUMP(env, "Invalid data memory access: 0x" ADDRX "\n",
                      env->spr[SPR_DAR]);
            /* XXX: check this. Seems bugged */
            switch (env->error_code & 0xFF000000) {
            case 0x40000000:
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_MAPERR;
                break;
            case 0x04000000:
                info.si_signo = TARGET_SIGILL;
                info.si_errno = 0;
                info.si_code = TARGET_ILL_ILLADR;
                break;
            case 0x08000000:
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_ACCERR;
                break;
            default:
                /* Let's send a regular segfault... */
                EXCP_DUMP(env, "Invalid segfault errno (%02x)\n",
                          env->error_code);
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_MAPERR;
                break;
            }
            info._sifields._sigfault._addr = env->nip;
            queue_signal(info.si_signo, &info);
            break;
        case POWERPC_EXCP_ISI:      /* Instruction storage exception         */
            EXCP_DUMP(env, "Invalid instruction fetch: 0x\n" ADDRX "\n",
                      env->spr[SPR_SRR0]);
            /* XXX: check this */
            switch (env->error_code & 0xFF000000) {
            case 0x40000000:
                info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
                info.si_code = TARGET_SEGV_MAPERR;
                break;
            case 0x10000000:
            case 0x08000000:
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_ACCERR;
                break;
            default:
                /* Let's send a regular segfault... */
                EXCP_DUMP(env, "Invalid segfault errno (%02x)\n",
                          env->error_code);
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_MAPERR;
                break;
            }
            info._sifields._sigfault._addr = env->nip - 4;
            queue_signal(info.si_signo, &info);
            break;
        case POWERPC_EXCP_EXTERNAL: /* External input                        */
            cpu_abort(env, "External interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_ALIGN:    /* Alignment exception                   */
            EXCP_DUMP(env, "Unaligned memory access\n");
            /* XXX: check this */
            info.si_signo = TARGET_SIGBUS;
            info.si_errno = 0;
            info.si_code = TARGET_BUS_ADRALN;
            info._sifields._sigfault._addr = env->nip - 4;
            queue_signal(info.si_signo, &info);
            break;
        case POWERPC_EXCP_PROGRAM:  /* Program exception                     */
            /* XXX: check this */
            switch (env->error_code & ~0xF) {
            case POWERPC_EXCP_FP:
                EXCP_DUMP(env, "Floating point program exception\n");
                info.si_signo = TARGET_SIGFPE;
                info.si_errno = 0;
                switch (env->error_code & 0xF) {
                case POWERPC_EXCP_FP_OX:
                    info.si_code = TARGET_FPE_FLTOVF;
                    break;
                case POWERPC_EXCP_FP_UX:
                    info.si_code = TARGET_FPE_FLTUND;
                    break;
                case POWERPC_EXCP_FP_ZX:
                case POWERPC_EXCP_FP_VXZDZ:
                    info.si_code = TARGET_FPE_FLTDIV;
                    break;
                case POWERPC_EXCP_FP_XX:
                    info.si_code = TARGET_FPE_FLTRES;
                    break;
                case POWERPC_EXCP_FP_VXSOFT:
                    info.si_code = TARGET_FPE_FLTINV;
                    break;
                case POWERPC_EXCP_FP_VXSNAN:
                case POWERPC_EXCP_FP_VXISI:
                case POWERPC_EXCP_FP_VXIDI:
                case POWERPC_EXCP_FP_VXIMZ:
                case POWERPC_EXCP_FP_VXVC:
                case POWERPC_EXCP_FP_VXSQRT:
                case POWERPC_EXCP_FP_VXCVI:
                    info.si_code = TARGET_FPE_FLTSUB;
                    break;
                default:
                    EXCP_DUMP(env, "Unknown floating point exception (%02x)\n",
                              env->error_code);
                    break;
                }
                break;
            case POWERPC_EXCP_INVAL:
                EXCP_DUMP(env, "Invalid instruction\n");
                info.si_signo = TARGET_SIGILL;
                info.si_errno = 0;
                switch (env->error_code & 0xF) {
                case POWERPC_EXCP_INVAL_INVAL:
                    info.si_code = TARGET_ILL_ILLOPC;
                    break;
                case POWERPC_EXCP_INVAL_LSWX:
                    info.si_code = TARGET_ILL_ILLOPN;
                    break;
                case POWERPC_EXCP_INVAL_SPR:
                    info.si_code = TARGET_ILL_PRVREG;
                    break;
                case POWERPC_EXCP_INVAL_FP:
                    info.si_code = TARGET_ILL_COPROC;
                    break;
                default:
                    EXCP_DUMP(env, "Unknown invalid operation (%02x)\n",
                              env->error_code & 0xF);
                    info.si_code = TARGET_ILL_ILLADR;
                    break;
                }
                break;
            case POWERPC_EXCP_PRIV:
                EXCP_DUMP(env, "Privilege violation\n");
                info.si_signo = TARGET_SIGILL;
                info.si_errno = 0;
                switch (env->error_code & 0xF) {
                case POWERPC_EXCP_PRIV_OPC:
                    info.si_code = TARGET_ILL_PRVOPC;
                    break;
                case POWERPC_EXCP_PRIV_REG:
                    info.si_code = TARGET_ILL_PRVREG;
                    break;
                default:
                    EXCP_DUMP(env, "Unknown privilege violation (%02x)\n",
                              env->error_code & 0xF);
                    info.si_code = TARGET_ILL_PRVOPC;
                    break;
                }
                break;
            case POWERPC_EXCP_TRAP:
                cpu_abort(env, "Tried to call a TRAP\n");
                break;
            default:
                /* Should not happen ! */
                cpu_abort(env, "Unknown program exception (%02x)\n",
                          env->error_code);
                break;
            }
            info._sifields._sigfault._addr = env->nip - 4;
            queue_signal(info.si_signo, &info);
            break;
        case POWERPC_EXCP_FPU:      /* Floating-point unavailable exception  */
            EXCP_DUMP(env, "No floating point allowed\n");
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_COPROC;
            info._sifields._sigfault._addr = env->nip - 4;
            queue_signal(info.si_signo, &info);
            break;
        case POWERPC_EXCP_SYSCALL:  /* System call exception                 */
            cpu_abort(env, "Syscall exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_APU:      /* Auxiliary processor unavailable       */
            EXCP_DUMP(env, "No APU instruction allowed\n");
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_COPROC;
            info._sifields._sigfault._addr = env->nip - 4;
            queue_signal(info.si_signo, &info);
            break;
        case POWERPC_EXCP_DECR:     /* Decrementer exception                 */
            cpu_abort(env, "Decrementer interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_FIT:      /* Fixed-interval timer interrupt        */
            cpu_abort(env, "Fix interval timer interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_WDT:      /* Watchdog timer interrupt              */
            cpu_abort(env, "Watchdog timer interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_DTLB:     /* Data TLB error                        */
            cpu_abort(env, "Data TLB exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_ITLB:     /* Instruction TLB error                 */
            cpu_abort(env, "Instruction TLB exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_DEBUG:    /* Debug interrupt                       */
            /* XXX: check this */
            {
                int sig;

                sig = gdb_handlesig(env, TARGET_SIGTRAP);
                if (sig) {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        case POWERPC_EXCP_SPEU:     /* SPE/embedded floating-point unavail.  */
            EXCP_DUMP(env, "No SPE/floating-point instruction allowed\n");
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_COPROC;
            info._sifields._sigfault._addr = env->nip - 4;
            queue_signal(info.si_signo, &info);
            break;
        case POWERPC_EXCP_EFPDI:    /* Embedded floating-point data IRQ      */
            cpu_abort(env, "Embedded floating-point data IRQ not handled\n");
            break;
        case POWERPC_EXCP_EFPRI:    /* Embedded floating-point round IRQ     */
            cpu_abort(env, "Embedded floating-point round IRQ not handled\n");
            break;
        case POWERPC_EXCP_EPERFM:   /* Embedded performance monitor IRQ      */
            cpu_abort(env, "Performance monitor exception not handled\n");
            break;
        case POWERPC_EXCP_DOORI:    /* Embedded doorbell interrupt           */
            cpu_abort(env, "Doorbell interrupt while in user mode. "
                       "Aborting\n");
            break;
        case POWERPC_EXCP_DOORCI:   /* Embedded doorbell critical interrupt  */
            cpu_abort(env, "Doorbell critical interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_RESET:    /* System reset exception                */
            cpu_abort(env, "Reset interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_DSEG:     /* Data segment exception                */
            cpu_abort(env, "Data segment exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_ISEG:     /* Instruction segment exception         */
            cpu_abort(env, "Instruction segment exception "
                      "while in user mode. Aborting\n");
            break;
        /* PowerPC 64 with hypervisor mode support */
        case POWERPC_EXCP_HDECR:    /* Hypervisor decrementer exception      */
            cpu_abort(env, "Hypervisor decrementer interrupt "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_TRACE:    /* Trace exception                       */
            /* Nothing to do:
             * we use this exception to emulate step-by-step execution mode.
             */
            break;
        /* PowerPC 64 with hypervisor mode support */
        case POWERPC_EXCP_HDSI:     /* Hypervisor data storage exception     */
            cpu_abort(env, "Hypervisor data storage exception "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_HISI:     /* Hypervisor instruction storage excp   */
            cpu_abort(env, "Hypervisor instruction storage exception "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_HDSEG:    /* Hypervisor data segment exception     */
            cpu_abort(env, "Hypervisor data segment exception "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_HISEG:    /* Hypervisor instruction segment excp   */
            cpu_abort(env, "Hypervisor instruction segment exception "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_VPU:      /* Vector unavailable exception          */
            EXCP_DUMP(env, "No Altivec instructions allowed\n");
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_COPROC;
            info._sifields._sigfault._addr = env->nip - 4;
            queue_signal(info.si_signo, &info);
            break;
        case POWERPC_EXCP_PIT:      /* Programmable interval timer IRQ       */
            cpu_abort(env, "Programable interval timer interrupt "
                      "while in user mode. Aborting\n");
            break;
        case POWERPC_EXCP_IO:       /* IO error exception                    */
            cpu_abort(env, "IO error exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_RUNM:     /* Run mode exception                    */
            cpu_abort(env, "Run mode exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_EMUL:     /* Emulation trap exception              */
            cpu_abort(env, "Emulation trap exception not handled\n");
            break;
        case POWERPC_EXCP_IFTLB:    /* Instruction fetch TLB error           */
            cpu_abort(env, "Instruction fetch TLB exception "
                      "while in user-mode. Aborting");
            break;
        case POWERPC_EXCP_DLTLB:    /* Data load TLB miss                    */
            cpu_abort(env, "Data load TLB exception while in user-mode. "
                      "Aborting");
            break;
        case POWERPC_EXCP_DSTLB:    /* Data store TLB miss                   */
            cpu_abort(env, "Data store TLB exception while in user-mode. "
                      "Aborting");
            break;
        case POWERPC_EXCP_FPA:      /* Floating-point assist exception       */
            cpu_abort(env, "Floating-point assist exception not handled\n");
            break;
        case POWERPC_EXCP_IABR:     /* Instruction address breakpoint        */
            cpu_abort(env, "Instruction address breakpoint exception "
                      "not handled\n");
            break;
        case POWERPC_EXCP_SMI:      /* System management interrupt           */
            cpu_abort(env, "System management interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_THERM:    /* Thermal interrupt                     */
            cpu_abort(env, "Thermal interrupt interrupt while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_PERFM:   /* Embedded performance monitor IRQ      */
            cpu_abort(env, "Performance monitor exception not handled\n");
            break;
        case POWERPC_EXCP_VPUA:     /* Vector assist exception               */
            cpu_abort(env, "Vector assist exception not handled\n");
            break;
        case POWERPC_EXCP_SOFTP:    /* Soft patch exception                  */
            cpu_abort(env, "Soft patch exception not handled\n");
            break;
        case POWERPC_EXCP_MAINT:    /* Maintenance exception                 */
            cpu_abort(env, "Maintenance exception while in user mode. "
                      "Aborting\n");
            break;
        case POWERPC_EXCP_STOP:     /* stop translation                      */
            /* We did invalidate the instruction cache. Go on */
            break;
        case POWERPC_EXCP_BRANCH:   /* branch instruction:                   */
            /* We just stopped because of a branch. Go on */
            break;
        case POWERPC_EXCP_SYSCALL_USER:
            /* system call in user-mode emulation */
            /* WARNING:
             * PPC ABI uses overflow flag in cr0 to signal an error
             * in syscalls.
             */
#if 0
            printf("syscall %d 0x%08x 0x%08x 0x%08x 0x%08x\n", env->gpr[0],
                   env->gpr[3], env->gpr[4], env->gpr[5], env->gpr[6]);
#endif
            env->crf[0] &= ~0x1;
            ret = do_syscall(env, env->gpr[0], env->gpr[3], env->gpr[4],
                             env->gpr[5], env->gpr[6], env->gpr[7],
                             env->gpr[8]);
            if (ret > (uint32_t)(-515)) {
                env->crf[0] |= 0x1;
                ret = -ret;
            }
            env->gpr[3] = ret;
#if 0
            printf("syscall returned 0x%08x (%d)\n", ret, ret);
#endif
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        default:
            cpu_abort(env, "Unknown exception 0x%d. Aborting\n", trapnr);
            break;
        }
        process_pending_signals(env);
    }
}
#endif

#ifdef TARGET_MIPS

#define MIPS_SYS(name, args) args,

static const uint8_t mips_syscall_args[] = {
	MIPS_SYS(sys_syscall	, 0)	/* 4000 */
	MIPS_SYS(sys_exit	, 1)
	MIPS_SYS(sys_fork	, 0)
	MIPS_SYS(sys_read	, 3)
	MIPS_SYS(sys_write	, 3)
	MIPS_SYS(sys_open	, 3)	/* 4005 */
	MIPS_SYS(sys_close	, 1)
	MIPS_SYS(sys_waitpid	, 3)
	MIPS_SYS(sys_creat	, 2)
	MIPS_SYS(sys_link	, 2)
	MIPS_SYS(sys_unlink	, 1)	/* 4010 */
	MIPS_SYS(sys_execve	, 0)
	MIPS_SYS(sys_chdir	, 1)
	MIPS_SYS(sys_time	, 1)
	MIPS_SYS(sys_mknod	, 3)
	MIPS_SYS(sys_chmod	, 2)	/* 4015 */
	MIPS_SYS(sys_lchown	, 3)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_stat */
	MIPS_SYS(sys_lseek	, 3)
	MIPS_SYS(sys_getpid	, 0)	/* 4020 */
	MIPS_SYS(sys_mount	, 5)
	MIPS_SYS(sys_oldumount	, 1)
	MIPS_SYS(sys_setuid	, 1)
	MIPS_SYS(sys_getuid	, 0)
	MIPS_SYS(sys_stime	, 1)	/* 4025 */
	MIPS_SYS(sys_ptrace	, 4)
	MIPS_SYS(sys_alarm	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_fstat */
	MIPS_SYS(sys_pause	, 0)
	MIPS_SYS(sys_utime	, 2)	/* 4030 */
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_access	, 2)
	MIPS_SYS(sys_nice	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* 4035 */
	MIPS_SYS(sys_sync	, 0)
	MIPS_SYS(sys_kill	, 2)
	MIPS_SYS(sys_rename	, 2)
	MIPS_SYS(sys_mkdir	, 2)
	MIPS_SYS(sys_rmdir	, 1)	/* 4040 */
	MIPS_SYS(sys_dup		, 1)
	MIPS_SYS(sys_pipe	, 0)
	MIPS_SYS(sys_times	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_brk		, 1)	/* 4045 */
	MIPS_SYS(sys_setgid	, 1)
	MIPS_SYS(sys_getgid	, 0)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was signal(2) */
	MIPS_SYS(sys_geteuid	, 0)
	MIPS_SYS(sys_getegid	, 0)	/* 4050 */
	MIPS_SYS(sys_acct	, 0)
	MIPS_SYS(sys_umount	, 2)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_ioctl	, 3)
	MIPS_SYS(sys_fcntl	, 3)	/* 4055 */
	MIPS_SYS(sys_ni_syscall	, 2)
	MIPS_SYS(sys_setpgid	, 2)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_olduname	, 1)
	MIPS_SYS(sys_umask	, 1)	/* 4060 */
	MIPS_SYS(sys_chroot	, 1)
	MIPS_SYS(sys_ustat	, 2)
	MIPS_SYS(sys_dup2	, 2)
	MIPS_SYS(sys_getppid	, 0)
	MIPS_SYS(sys_getpgrp	, 0)	/* 4065 */
	MIPS_SYS(sys_setsid	, 0)
	MIPS_SYS(sys_sigaction	, 3)
	MIPS_SYS(sys_sgetmask	, 0)
	MIPS_SYS(sys_ssetmask	, 1)
	MIPS_SYS(sys_setreuid	, 2)	/* 4070 */
	MIPS_SYS(sys_setregid	, 2)
	MIPS_SYS(sys_sigsuspend	, 0)
	MIPS_SYS(sys_sigpending	, 1)
	MIPS_SYS(sys_sethostname	, 2)
	MIPS_SYS(sys_setrlimit	, 2)	/* 4075 */
	MIPS_SYS(sys_getrlimit	, 2)
	MIPS_SYS(sys_getrusage	, 2)
	MIPS_SYS(sys_gettimeofday, 2)
	MIPS_SYS(sys_settimeofday, 2)
	MIPS_SYS(sys_getgroups	, 2)	/* 4080 */
	MIPS_SYS(sys_setgroups	, 2)
	MIPS_SYS(sys_ni_syscall	, 0)	/* old_select */
	MIPS_SYS(sys_symlink	, 2)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_lstat */
	MIPS_SYS(sys_readlink	, 3)	/* 4085 */
	MIPS_SYS(sys_uselib	, 1)
	MIPS_SYS(sys_swapon	, 2)
	MIPS_SYS(sys_reboot	, 3)
	MIPS_SYS(old_readdir	, 3)
	MIPS_SYS(old_mmap	, 6)	/* 4090 */
	MIPS_SYS(sys_munmap	, 2)
	MIPS_SYS(sys_truncate	, 2)
	MIPS_SYS(sys_ftruncate	, 2)
	MIPS_SYS(sys_fchmod	, 2)
	MIPS_SYS(sys_fchown	, 3)	/* 4095 */
	MIPS_SYS(sys_getpriority	, 2)
	MIPS_SYS(sys_setpriority	, 3)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_statfs	, 2)
	MIPS_SYS(sys_fstatfs	, 2)	/* 4100 */
	MIPS_SYS(sys_ni_syscall	, 0)	/* was ioperm(2) */
	MIPS_SYS(sys_socketcall	, 2)
	MIPS_SYS(sys_syslog	, 3)
	MIPS_SYS(sys_setitimer	, 3)
	MIPS_SYS(sys_getitimer	, 2)	/* 4105 */
	MIPS_SYS(sys_newstat	, 2)
	MIPS_SYS(sys_newlstat	, 2)
	MIPS_SYS(sys_newfstat	, 2)
	MIPS_SYS(sys_uname	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* 4110 was iopl(2) */
	MIPS_SYS(sys_vhangup	, 0)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_idle() */
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_vm86 */
	MIPS_SYS(sys_wait4	, 4)
	MIPS_SYS(sys_swapoff	, 1)	/* 4115 */
	MIPS_SYS(sys_sysinfo	, 1)
	MIPS_SYS(sys_ipc		, 6)
	MIPS_SYS(sys_fsync	, 1)
	MIPS_SYS(sys_sigreturn	, 0)
	MIPS_SYS(sys_clone	, 0)	/* 4120 */
	MIPS_SYS(sys_setdomainname, 2)
	MIPS_SYS(sys_newuname	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* sys_modify_ldt */
	MIPS_SYS(sys_adjtimex	, 1)
	MIPS_SYS(sys_mprotect	, 3)	/* 4125 */
	MIPS_SYS(sys_sigprocmask	, 3)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was create_module */
	MIPS_SYS(sys_init_module	, 5)
	MIPS_SYS(sys_delete_module, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* 4130	was get_kernel_syms */
	MIPS_SYS(sys_quotactl	, 0)
	MIPS_SYS(sys_getpgid	, 1)
	MIPS_SYS(sys_fchdir	, 1)
	MIPS_SYS(sys_bdflush	, 2)
	MIPS_SYS(sys_sysfs	, 3)	/* 4135 */
	MIPS_SYS(sys_personality	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* for afs_syscall */
	MIPS_SYS(sys_setfsuid	, 1)
	MIPS_SYS(sys_setfsgid	, 1)
	MIPS_SYS(sys_llseek	, 5)	/* 4140 */
	MIPS_SYS(sys_getdents	, 3)
	MIPS_SYS(sys_select	, 5)
	MIPS_SYS(sys_flock	, 2)
	MIPS_SYS(sys_msync	, 3)
	MIPS_SYS(sys_readv	, 3)	/* 4145 */
	MIPS_SYS(sys_writev	, 3)
	MIPS_SYS(sys_cacheflush	, 3)
	MIPS_SYS(sys_cachectl	, 3)
	MIPS_SYS(sys_sysmips	, 4)
	MIPS_SYS(sys_ni_syscall	, 0)	/* 4150 */
	MIPS_SYS(sys_getsid	, 1)
	MIPS_SYS(sys_fdatasync	, 0)
	MIPS_SYS(sys_sysctl	, 1)
	MIPS_SYS(sys_mlock	, 2)
	MIPS_SYS(sys_munlock	, 2)	/* 4155 */
	MIPS_SYS(sys_mlockall	, 1)
	MIPS_SYS(sys_munlockall	, 0)
	MIPS_SYS(sys_sched_setparam, 2)
	MIPS_SYS(sys_sched_getparam, 2)
	MIPS_SYS(sys_sched_setscheduler, 3)	/* 4160 */
	MIPS_SYS(sys_sched_getscheduler, 1)
	MIPS_SYS(sys_sched_yield	, 0)
	MIPS_SYS(sys_sched_get_priority_max, 1)
	MIPS_SYS(sys_sched_get_priority_min, 1)
	MIPS_SYS(sys_sched_rr_get_interval, 2)	/* 4165 */
	MIPS_SYS(sys_nanosleep,	2)
	MIPS_SYS(sys_mremap	, 4)
	MIPS_SYS(sys_accept	, 3)
	MIPS_SYS(sys_bind	, 3)
	MIPS_SYS(sys_connect	, 3)	/* 4170 */
	MIPS_SYS(sys_getpeername	, 3)
	MIPS_SYS(sys_getsockname	, 3)
	MIPS_SYS(sys_getsockopt	, 5)
	MIPS_SYS(sys_listen	, 2)
	MIPS_SYS(sys_recv	, 4)	/* 4175 */
	MIPS_SYS(sys_recvfrom	, 6)
	MIPS_SYS(sys_recvmsg	, 3)
	MIPS_SYS(sys_send	, 4)
	MIPS_SYS(sys_sendmsg	, 3)
	MIPS_SYS(sys_sendto	, 6)	/* 4180 */
	MIPS_SYS(sys_setsockopt	, 5)
	MIPS_SYS(sys_shutdown	, 2)
	MIPS_SYS(sys_socket	, 3)
	MIPS_SYS(sys_socketpair	, 4)
	MIPS_SYS(sys_setresuid	, 3)	/* 4185 */
	MIPS_SYS(sys_getresuid	, 3)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_query_module */
	MIPS_SYS(sys_poll	, 3)
	MIPS_SYS(sys_nfsservctl	, 3)
	MIPS_SYS(sys_setresgid	, 3)	/* 4190 */
	MIPS_SYS(sys_getresgid	, 3)
	MIPS_SYS(sys_prctl	, 5)
	MIPS_SYS(sys_rt_sigreturn, 0)
	MIPS_SYS(sys_rt_sigaction, 4)
	MIPS_SYS(sys_rt_sigprocmask, 4)	/* 4195 */
	MIPS_SYS(sys_rt_sigpending, 2)
	MIPS_SYS(sys_rt_sigtimedwait, 4)
	MIPS_SYS(sys_rt_sigqueueinfo, 3)
	MIPS_SYS(sys_rt_sigsuspend, 0)
	MIPS_SYS(sys_pread64	, 6)	/* 4200 */
	MIPS_SYS(sys_pwrite64	, 6)
	MIPS_SYS(sys_chown	, 3)
	MIPS_SYS(sys_getcwd	, 2)
	MIPS_SYS(sys_capget	, 2)
	MIPS_SYS(sys_capset	, 2)	/* 4205 */
	MIPS_SYS(sys_sigaltstack	, 0)
	MIPS_SYS(sys_sendfile	, 4)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_mmap2	, 6)	/* 4210 */
	MIPS_SYS(sys_truncate64	, 4)
	MIPS_SYS(sys_ftruncate64	, 4)
	MIPS_SYS(sys_stat64	, 2)
	MIPS_SYS(sys_lstat64	, 2)
	MIPS_SYS(sys_fstat64	, 2)	/* 4215 */
	MIPS_SYS(sys_pivot_root	, 2)
	MIPS_SYS(sys_mincore	, 3)
	MIPS_SYS(sys_madvise	, 3)
	MIPS_SYS(sys_getdents64	, 3)
	MIPS_SYS(sys_fcntl64	, 3)	/* 4220 */
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_gettid	, 0)
	MIPS_SYS(sys_readahead	, 5)
	MIPS_SYS(sys_setxattr	, 5)
	MIPS_SYS(sys_lsetxattr	, 5)	/* 4225 */
	MIPS_SYS(sys_fsetxattr	, 5)
	MIPS_SYS(sys_getxattr	, 4)
	MIPS_SYS(sys_lgetxattr	, 4)
	MIPS_SYS(sys_fgetxattr	, 4)
	MIPS_SYS(sys_listxattr	, 3)	/* 4230 */
	MIPS_SYS(sys_llistxattr	, 3)
	MIPS_SYS(sys_flistxattr	, 3)
	MIPS_SYS(sys_removexattr	, 2)
	MIPS_SYS(sys_lremovexattr, 2)
	MIPS_SYS(sys_fremovexattr, 2)	/* 4235 */
	MIPS_SYS(sys_tkill	, 2)
	MIPS_SYS(sys_sendfile64	, 5)
	MIPS_SYS(sys_futex	, 2)
	MIPS_SYS(sys_sched_setaffinity, 3)
	MIPS_SYS(sys_sched_getaffinity, 3)	/* 4240 */
	MIPS_SYS(sys_io_setup	, 2)
	MIPS_SYS(sys_io_destroy	, 1)
	MIPS_SYS(sys_io_getevents, 5)
	MIPS_SYS(sys_io_submit	, 3)
	MIPS_SYS(sys_io_cancel	, 3)	/* 4245 */
	MIPS_SYS(sys_exit_group	, 1)
	MIPS_SYS(sys_lookup_dcookie, 3)
	MIPS_SYS(sys_epoll_create, 1)
	MIPS_SYS(sys_epoll_ctl	, 4)
	MIPS_SYS(sys_epoll_wait	, 3)	/* 4250 */
	MIPS_SYS(sys_remap_file_pages, 5)
	MIPS_SYS(sys_set_tid_address, 1)
	MIPS_SYS(sys_restart_syscall, 0)
	MIPS_SYS(sys_fadvise64_64, 7)
	MIPS_SYS(sys_statfs64	, 3)	/* 4255 */
	MIPS_SYS(sys_fstatfs64	, 2)
	MIPS_SYS(sys_timer_create, 3)
	MIPS_SYS(sys_timer_settime, 4)
	MIPS_SYS(sys_timer_gettime, 2)
	MIPS_SYS(sys_timer_getoverrun, 1)	/* 4260 */
	MIPS_SYS(sys_timer_delete, 1)
	MIPS_SYS(sys_clock_settime, 2)
	MIPS_SYS(sys_clock_gettime, 2)
	MIPS_SYS(sys_clock_getres, 2)
	MIPS_SYS(sys_clock_nanosleep, 4)	/* 4265 */
	MIPS_SYS(sys_tgkill	, 3)
	MIPS_SYS(sys_utimes	, 2)
	MIPS_SYS(sys_mbind	, 4)
	MIPS_SYS(sys_ni_syscall	, 0)	/* sys_get_mempolicy */
	MIPS_SYS(sys_ni_syscall	, 0)	/* 4270 sys_set_mempolicy */
	MIPS_SYS(sys_mq_open	, 4)
	MIPS_SYS(sys_mq_unlink	, 1)
	MIPS_SYS(sys_mq_timedsend, 5)
	MIPS_SYS(sys_mq_timedreceive, 5)
	MIPS_SYS(sys_mq_notify	, 2)	/* 4275 */
	MIPS_SYS(sys_mq_getsetattr, 3)
	MIPS_SYS(sys_ni_syscall	, 0)	/* sys_vserver */
	MIPS_SYS(sys_waitid	, 4)
	MIPS_SYS(sys_ni_syscall	, 0)	/* available, was setaltroot */
	MIPS_SYS(sys_add_key	, 5)
	MIPS_SYS(sys_request_key, 4)
	MIPS_SYS(sys_keyctl	, 5)
	MIPS_SYS(sys_set_thread_area, 1)
	MIPS_SYS(sys_inotify_init, 0)
	MIPS_SYS(sys_inotify_add_watch, 3) /* 4285 */
	MIPS_SYS(sys_inotify_rm_watch, 2)
	MIPS_SYS(sys_migrate_pages, 4)
	MIPS_SYS(sys_openat, 4)
	MIPS_SYS(sys_mkdirat, 3)
	MIPS_SYS(sys_mknodat, 4)	/* 4290 */
	MIPS_SYS(sys_fchownat, 5)
	MIPS_SYS(sys_futimesat, 3)
	MIPS_SYS(sys_fstatat64, 4)
	MIPS_SYS(sys_unlinkat, 3)
	MIPS_SYS(sys_renameat, 4)	/* 4295 */
	MIPS_SYS(sys_linkat, 5)
	MIPS_SYS(sys_symlinkat, 3)
	MIPS_SYS(sys_readlinkat, 4)
	MIPS_SYS(sys_fchmodat, 3)
	MIPS_SYS(sys_faccessat, 3)	/* 4300 */
	MIPS_SYS(sys_pselect6, 6)
	MIPS_SYS(sys_ppoll, 5)
	MIPS_SYS(sys_unshare, 1)
	MIPS_SYS(sys_splice, 4)
	MIPS_SYS(sys_sync_file_range, 7) /* 4305 */
	MIPS_SYS(sys_tee, 4)
	MIPS_SYS(sys_vmsplice, 4)
	MIPS_SYS(sys_move_pages, 6)
	MIPS_SYS(sys_set_robust_list, 2)
	MIPS_SYS(sys_get_robust_list, 3) /* 4310 */
	MIPS_SYS(sys_kexec_load, 4)
	MIPS_SYS(sys_getcpu, 3)
	MIPS_SYS(sys_epoll_pwait, 6)
	MIPS_SYS(sys_ioprio_set, 3)
	MIPS_SYS(sys_ioprio_get, 2)
};

#undef MIPS_SYS

void cpu_loop(CPUMIPSState *env)
{
    target_siginfo_t info;
    int trapnr, ret;
    unsigned int syscall_num;

    for(;;) {
        trapnr = cpu_mips_exec(env);
        switch(trapnr) {
        case EXCP_SYSCALL:
            syscall_num = env->gpr[env->current_tc][2] - 4000;
            env->PC[env->current_tc] += 4;
            if (syscall_num >= sizeof(mips_syscall_args)) {
                ret = -ENOSYS;
            } else {
                int nb_args;
                abi_ulong sp_reg;
                abi_ulong arg5 = 0, arg6 = 0, arg7 = 0, arg8 = 0;

                nb_args = mips_syscall_args[syscall_num];
                sp_reg = env->gpr[env->current_tc][29];
                switch (nb_args) {
                /* these arguments are taken from the stack */
                /* FIXME - what to do if get_user() fails? */
                case 8: get_user_ual(arg8, sp_reg + 28);
                case 7: get_user_ual(arg7, sp_reg + 24);
                case 6: get_user_ual(arg6, sp_reg + 20);
                case 5: get_user_ual(arg5, sp_reg + 16);
                default:
                    break;
                }
                ret = do_syscall(env, env->gpr[env->current_tc][2],
                                 env->gpr[env->current_tc][4],
                                 env->gpr[env->current_tc][5],
                                 env->gpr[env->current_tc][6],
                                 env->gpr[env->current_tc][7],
                                 arg5, arg6/*, arg7, arg8*/);
            }
            if ((unsigned int)ret >= (unsigned int)(-1133)) {
                env->gpr[env->current_tc][7] = 1; /* error flag */
                ret = -ret;
            } else {
                env->gpr[env->current_tc][7] = 0; /* error flag */
            }
            env->gpr[env->current_tc][2] = ret;
            break;
        case EXCP_TLBL:
        case EXCP_TLBS:
        case EXCP_CpU:
        case EXCP_RI:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = 0;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            //        error:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n",
                    trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            abort();
        }
        process_pending_signals(env);
    }
}
#endif

#ifdef TARGET_SH4
void cpu_loop (CPUState *env)
{
    int trapnr, ret;
    target_siginfo_t info;

    while (1) {
        trapnr = cpu_sh4_exec (env);

        switch (trapnr) {
        case 0x160:
            ret = do_syscall(env,
                             env->gregs[3],
                             env->gregs[4],
                             env->gregs[5],
                             env->gregs[6],
                             env->gregs[7],
                             env->gregs[0],
                             env->gregs[1]);
            env->gregs[0] = ret;
            env->pc += 2;
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
	case 0xa0:
	case 0xc0:
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            info.si_code = TARGET_SEGV_MAPERR;
            info._sifields._sigfault._addr = env->tea;
            queue_signal(info.si_signo, &info);
	    break;

        default:
            printf ("Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            exit (1);
        }
        process_pending_signals (env);
    }
}
#endif

#ifdef TARGET_CRIS
void cpu_loop (CPUState *env)
{
    int trapnr, ret;
    target_siginfo_t info;
    
    while (1) {
        trapnr = cpu_cris_exec (env);
        switch (trapnr) {
        case 0xaa:
            {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->debug1;
                queue_signal(info.si_signo, &info);
            }
            break;
	case EXCP_INTERRUPT:
	  /* just indicate that signals should be handled asap */
	  break;
        case EXCP_BREAK:
            ret = do_syscall(env, 
                             env->regs[9], 
                             env->regs[10], 
                             env->regs[11], 
                             env->regs[12], 
                             env->regs[13], 
                             env->pregs[7], 
                             env->pregs[11]);
            env->regs[10] = ret;
            env->pc += 2;
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            printf ("Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            exit (1);
        }
        process_pending_signals (env);
    }
}
#endif

#ifdef TARGET_M68K

void cpu_loop(CPUM68KState *env)
{
    int trapnr;
    unsigned int n;
    target_siginfo_t info;
    TaskState *ts = env->opaque;

    for(;;) {
        trapnr = cpu_m68k_exec(env);
        switch(trapnr) {
        case EXCP_ILLEGAL:
            {
                if (ts->sim_syscalls) {
                    uint16_t nr;
                    nr = lduw(env->pc + 2);
                    env->pc += 4;
                    do_m68k_simcall(env, nr);
                } else {
                    goto do_sigill;
                }
            }
            break;
        case EXCP_HALT_INSN:
            /* Semihosing syscall.  */
            env->pc += 4;
            do_m68k_semihosting(env, env->dregs[0]);
            break;
        case EXCP_LINEA:
        case EXCP_LINEF:
        case EXCP_UNSUPPORTED:
        do_sigill:
            info.si_signo = SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_TRAP0:
            {
                ts->sim_syscalls = 0;
                n = env->dregs[0];
                env->pc += 2;
                env->dregs[0] = do_syscall(env,
                                          n,
                                          env->dregs[1],
                                          env->dregs[2],
                                          env->dregs[3],
                                          env->dregs[4],
                                          env->dregs[5],
                                          env->aregs[0]);
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_ACCESS:
            {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->mmu.ar;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n",
                    trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            abort();
        }
        process_pending_signals(env);
    }
}
#endif /* TARGET_M68K */

#ifdef TARGET_ALPHA
void cpu_loop (CPUState *env)
{
    int trapnr;
    target_siginfo_t info;

    while (1) {
        trapnr = cpu_alpha_exec (env);

        switch (trapnr) {
        case EXCP_RESET:
            fprintf(stderr, "Reset requested. Exit\n");
            exit(1);
            break;
        case EXCP_MCHK:
            fprintf(stderr, "Machine check exception. Exit\n");
            exit(1);
            break;
        case EXCP_ARITH:
            fprintf(stderr, "Arithmetic trap.\n");
            exit(1);
            break;
        case EXCP_HW_INTERRUPT:
            fprintf(stderr, "External interrupt. Exit\n");
            exit(1);
            break;
        case EXCP_DFAULT:
            fprintf(stderr, "MMU data fault\n");
            exit(1);
            break;
        case EXCP_DTB_MISS_PAL:
            fprintf(stderr, "MMU data TLB miss in PALcode\n");
            exit(1);
            break;
        case EXCP_ITB_MISS:
            fprintf(stderr, "MMU instruction TLB miss\n");
            exit(1);
            break;
        case EXCP_ITB_ACV:
            fprintf(stderr, "MMU instruction access violation\n");
            exit(1);
            break;
        case EXCP_DTB_MISS_NATIVE:
            fprintf(stderr, "MMU data TLB miss\n");
            exit(1);
            break;
        case EXCP_UNALIGN:
            fprintf(stderr, "Unaligned access\n");
            exit(1);
            break;
        case EXCP_OPCDEC:
            fprintf(stderr, "Invalid instruction\n");
            exit(1);
            break;
        case EXCP_FEN:
            fprintf(stderr, "Floating-point not allowed\n");
            exit(1);
            break;
        case EXCP_CALL_PAL ... (EXCP_CALL_PALP - 1):
            fprintf(stderr, "Call to PALcode\n");
            call_pal(env, (trapnr >> 6) | 0x80);
            break;
        case EXCP_CALL_PALP ... (EXCP_CALL_PALE - 1):
            fprintf(stderr, "Privileged call to PALcode\n");
            exit(1);
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            printf ("Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            exit (1);
        }
        process_pending_signals (env);
    }
}
#endif /* TARGET_ALPHA */

void usage(void)
{
    printf("qemu-" TARGET_ARCH " version " QEMU_VERSION ", Copyright (c) 2003-2008 Fabrice Bellard\n"
           "usage: qemu-" TARGET_ARCH " [options] program [arguments...]\n"
           "Linux CPU emulator (compiled for %s emulation)\n"
           "\n"
           "Standard options:\n"
           "-h                print this help\n"
           "-g port           wait gdb connection to port\n"
           "-L path           set the elf interpreter prefix (default=%s)\n"
           "-s size           set the stack size in bytes (default=%ld)\n"
           "-cpu model        select CPU (-cpu ? for list)\n"
           "-drop-ld-preload  drop LD_PRELOAD for target process\n"
           "\n"
           "Debug options:\n"
           "-d options   activate log (logfile=%s)\n"
           "-p pagesize  set the host page size to 'pagesize'\n"
           "-strace      log system calls\n"
           "\n"
           "Environment variables:\n"
           "QEMU_STRACE       Print system calls and arguments similar to the\n"
           "                  'strace' program.  Enable by setting to any value.\n"
           ,
           TARGET_ARCH,
           interp_prefix,
           x86_stack_size,
           DEBUG_LOGFILE);
    _exit(1);
}

/* XXX: currently only used for async signals (see signal.c) */
CPUState *global_env;

/* used to free thread contexts */
TaskState *first_task_state;

int main(int argc, char **argv)
{
    const char *filename;
    const char *cpu_model;
    struct target_pt_regs regs1, *regs = &regs1;
    struct image_info info1, *info = &info1;
    TaskState ts1, *ts = &ts1;
    CPUState *env;
    int optind;
    const char *r;
    int gdbstub_port = 0;
    int drop_ld_preload = 0, environ_count = 0;
    char **target_environ, **wrk, **dst;

    if (argc <= 1)
        usage();

    /* init debug */
    cpu_set_log_filename(DEBUG_LOGFILE);

    cpu_model = NULL;
    optind = 1;
    for(;;) {
        if (optind >= argc)
            break;
        r = argv[optind];
        if (r[0] != '-')
            break;
        optind++;
        r++;
        if (!strcmp(r, "-")) {
            break;
        } else if (!strcmp(r, "d")) {
            int mask;
            CPULogItem *item;

	    if (optind >= argc)
		break;

	    r = argv[optind++];
            mask = cpu_str_to_log_mask(r);
            if (!mask) {
                printf("Log items (comma separated):\n");
                for(item = cpu_log_items; item->mask != 0; item++) {
                    printf("%-10s %s\n", item->name, item->help);
                }
                exit(1);
            }
            cpu_set_log(mask);
        } else if (!strcmp(r, "s")) {
            r = argv[optind++];
            x86_stack_size = strtol(r, (char **)&r, 0);
            if (x86_stack_size <= 0)
                usage();
            if (*r == 'M')
                x86_stack_size *= 1024 * 1024;
            else if (*r == 'k' || *r == 'K')
                x86_stack_size *= 1024;
        } else if (!strcmp(r, "L")) {
            interp_prefix = argv[optind++];
        } else if (!strcmp(r, "p")) {
            qemu_host_page_size = atoi(argv[optind++]);
            if (qemu_host_page_size == 0 ||
                (qemu_host_page_size & (qemu_host_page_size - 1)) != 0) {
                fprintf(stderr, "page size must be a power of two\n");
                exit(1);
            }
        } else if (!strcmp(r, "g")) {
            gdbstub_port = atoi(argv[optind++]);
	} else if (!strcmp(r, "r")) {
	    qemu_uname_release = argv[optind++];
        } else if (!strcmp(r, "cpu")) {
            cpu_model = argv[optind++];
            if (strcmp(cpu_model, "?") == 0) {
/* XXX: implement xxx_cpu_list for targets that still miss it */
#if defined(cpu_list)
                    cpu_list(stdout, &fprintf);
#endif
                _exit(1);
            }
        } else if (!strcmp(r, "drop-ld-preload")) {
            drop_ld_preload = 1;
        } else if (!strcmp(r, "strace")) {
            do_strace = 1;
        } else
        {
            usage();
        }
    }
    if (optind >= argc)
        usage();
    filename = argv[optind];

    /* Zero out regs */
    memset(regs, 0, sizeof(struct target_pt_regs));

    /* Zero out image_info */
    memset(info, 0, sizeof(struct image_info));

    /* Scan interp_prefix dir for replacement files. */
    init_paths(interp_prefix);

    if (cpu_model == NULL) {
#if defined(TARGET_I386)
#ifdef TARGET_X86_64
        cpu_model = "qemu64";
#else
        cpu_model = "qemu32";
#endif
#elif defined(TARGET_ARM)
        cpu_model = "arm926";
#elif defined(TARGET_M68K)
        cpu_model = "any";
#elif defined(TARGET_SPARC)
#ifdef TARGET_SPARC64
        cpu_model = "TI UltraSparc II";
#else
        cpu_model = "Fujitsu MB86904";
#endif
#elif defined(TARGET_MIPS)
#if defined(TARGET_ABI_MIPSN32) || defined(TARGET_ABI_MIPSN64)
        cpu_model = "20Kc";
#else
        cpu_model = "24Kf";
#endif
#elif defined(TARGET_PPC)
#ifdef TARGET_PPC64
        cpu_model = "970";
#else
        cpu_model = "750";
#endif
#else
        cpu_model = "any";
#endif
    }
    /* NOTE: we need to init the CPU at this stage to get
       qemu_host_page_size */
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    global_env = env;

    if (getenv("QEMU_STRACE")) {
        do_strace = 1;
    }

    wrk = environ;
    while (*(wrk++))
        environ_count++;

    target_environ = malloc((environ_count + 1) * sizeof(char *));
    if (!target_environ)
        abort();
    for (wrk = environ, dst = target_environ; *wrk; wrk++) {
        if (drop_ld_preload && !strncmp(*wrk, "LD_PRELOAD=", 11))
            continue;
        *(dst++) = strdup(*wrk);
    }
    *dst = NULL; /* NULL terminate target_environ */

    if (loader_exec(filename, argv+optind, target_environ, regs, info) != 0) {
        printf("Error loading %s\n", filename);
        _exit(1);
    }

    for (wrk = target_environ; *wrk; wrk++) {
        free(*wrk);
    }

    free(target_environ);

    if (loglevel) {
        page_dump(logfile);

        fprintf(logfile, "start_brk   0x" TARGET_ABI_FMT_lx "\n", info->start_brk);
        fprintf(logfile, "end_code    0x" TARGET_ABI_FMT_lx "\n", info->end_code);
        fprintf(logfile, "start_code  0x" TARGET_ABI_FMT_lx "\n",
                info->start_code);
        fprintf(logfile, "start_data  0x" TARGET_ABI_FMT_lx "\n",
                info->start_data);
        fprintf(logfile, "end_data    0x" TARGET_ABI_FMT_lx "\n", info->end_data);
        fprintf(logfile, "start_stack 0x" TARGET_ABI_FMT_lx "\n",
                info->start_stack);
        fprintf(logfile, "brk         0x" TARGET_ABI_FMT_lx "\n", info->brk);
        fprintf(logfile, "entry       0x" TARGET_ABI_FMT_lx "\n", info->entry);
    }

    target_set_brk(info->brk);
    syscall_init();
    signal_init();

    /* build Task State */
    memset(ts, 0, sizeof(TaskState));
    env->opaque = ts;
    ts->used = 1;
    ts->info = info;
    env->user_mode_only = 1;

#if defined(TARGET_I386)
    cpu_x86_set_cpl(env, 3);

    env->cr[0] = CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK;
    env->hflags |= HF_PE_MASK;
    if (env->cpuid_features & CPUID_SSE) {
        env->cr[4] |= CR4_OSFXSR_MASK;
        env->hflags |= HF_OSFXSR_MASK;
    }
#ifndef TARGET_ABI32
    /* enable 64 bit mode if possible */
    if (!(env->cpuid_ext2_features & CPUID_EXT2_LM)) {
        fprintf(stderr, "The selected x86 CPU does not support 64 bit mode\n");
        exit(1);
    }
    env->cr[4] |= CR4_PAE_MASK;
    env->efer |= MSR_EFER_LMA | MSR_EFER_LME;
    env->hflags |= HF_LMA_MASK;
#endif

    /* flags setup : we activate the IRQs by default as in user mode */
    env->eflags |= IF_MASK;

    /* linux register setup */
#ifndef TARGET_ABI32
    env->regs[R_EAX] = regs->rax;
    env->regs[R_EBX] = regs->rbx;
    env->regs[R_ECX] = regs->rcx;
    env->regs[R_EDX] = regs->rdx;
    env->regs[R_ESI] = regs->rsi;
    env->regs[R_EDI] = regs->rdi;
    env->regs[R_EBP] = regs->rbp;
    env->regs[R_ESP] = regs->rsp;
    env->eip = regs->rip;
#else
    env->regs[R_EAX] = regs->eax;
    env->regs[R_EBX] = regs->ebx;
    env->regs[R_ECX] = regs->ecx;
    env->regs[R_EDX] = regs->edx;
    env->regs[R_ESI] = regs->esi;
    env->regs[R_EDI] = regs->edi;
    env->regs[R_EBP] = regs->ebp;
    env->regs[R_ESP] = regs->esp;
    env->eip = regs->eip;
#endif

    /* linux interrupt setup */
    env->idt.base = h2g(idt_table);
    env->idt.limit = sizeof(idt_table) - 1;
    set_idt(0, 0);
    set_idt(1, 0);
    set_idt(2, 0);
    set_idt(3, 3);
    set_idt(4, 3);
    set_idt(5, 3);
    set_idt(6, 0);
    set_idt(7, 0);
    set_idt(8, 0);
    set_idt(9, 0);
    set_idt(10, 0);
    set_idt(11, 0);
    set_idt(12, 0);
    set_idt(13, 0);
    set_idt(14, 0);
    set_idt(15, 0);
    set_idt(16, 0);
    set_idt(17, 0);
    set_idt(18, 0);
    set_idt(19, 0);
    set_idt(0x80, 3);

    /* linux segment setup */
    {
        uint64_t *gdt_table;
        gdt_table = qemu_mallocz(sizeof(uint64_t) * TARGET_GDT_ENTRIES);
        env->gdt.base = h2g((unsigned long)gdt_table);
        env->gdt.limit = sizeof(uint64_t) * TARGET_GDT_ENTRIES - 1;
#ifdef TARGET_ABI32
        write_dt(&gdt_table[__USER_CS >> 3], 0, 0xfffff,
                 DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                 (3 << DESC_DPL_SHIFT) | (0xa << DESC_TYPE_SHIFT));
#else
        /* 64 bit code segment */
        write_dt(&gdt_table[__USER_CS >> 3], 0, 0xfffff,
                 DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                 DESC_L_MASK |
                 (3 << DESC_DPL_SHIFT) | (0xa << DESC_TYPE_SHIFT));
#endif
        write_dt(&gdt_table[__USER_DS >> 3], 0, 0xfffff,
                 DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
                 (3 << DESC_DPL_SHIFT) | (0x2 << DESC_TYPE_SHIFT));
    }
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
#ifdef TARGET_ABI32
    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_FS, __USER_DS);
    cpu_x86_load_seg(env, R_GS, __USER_DS);
    /* This hack makes Wine work... */
    env->segs[R_FS].selector = 0;
#else
    cpu_x86_load_seg(env, R_DS, 0);
    cpu_x86_load_seg(env, R_ES, 0);
    cpu_x86_load_seg(env, R_FS, 0);
    cpu_x86_load_seg(env, R_GS, 0);
#endif
#elif defined(TARGET_ARM)
    {
        int i;
        cpsr_write(env, regs->uregs[16], 0xffffffff);
        for(i = 0; i < 16; i++) {
            env->regs[i] = regs->uregs[i];
        }
    }
#elif defined(TARGET_SPARC)
    {
        int i;
	env->pc = regs->pc;
	env->npc = regs->npc;
        env->y = regs->y;
        for(i = 0; i < 8; i++)
            env->gregs[i] = regs->u_regs[i];
        for(i = 0; i < 8; i++)
            env->regwptr[i] = regs->u_regs[i + 8];
    }
#elif defined(TARGET_PPC)
    {
        int i;

#if defined(TARGET_PPC64)
#if defined(TARGET_ABI32)
        env->msr &= ~((target_ulong)1 << MSR_SF);
#else
        env->msr |= (target_ulong)1 << MSR_SF;
#endif
#endif
        env->nip = regs->nip;
        for(i = 0; i < 32; i++) {
            env->gpr[i] = regs->gpr[i];
        }
    }
#elif defined(TARGET_M68K)
    {
        env->pc = regs->pc;
        env->dregs[0] = regs->d0;
        env->dregs[1] = regs->d1;
        env->dregs[2] = regs->d2;
        env->dregs[3] = regs->d3;
        env->dregs[4] = regs->d4;
        env->dregs[5] = regs->d5;
        env->dregs[6] = regs->d6;
        env->dregs[7] = regs->d7;
        env->aregs[0] = regs->a0;
        env->aregs[1] = regs->a1;
        env->aregs[2] = regs->a2;
        env->aregs[3] = regs->a3;
        env->aregs[4] = regs->a4;
        env->aregs[5] = regs->a5;
        env->aregs[6] = regs->a6;
        env->aregs[7] = regs->usp;
        env->sr = regs->sr;
        ts->sim_syscalls = 1;
    }
#elif defined(TARGET_MIPS)
    {
        int i;

        for(i = 0; i < 32; i++) {
            env->gpr[env->current_tc][i] = regs->regs[i];
        }
        env->PC[env->current_tc] = regs->cp0_epc;
    }
#elif defined(TARGET_SH4)
    {
        int i;

        for(i = 0; i < 16; i++) {
            env->gregs[i] = regs->regs[i];
        }
        env->pc = regs->pc;
    }
#elif defined(TARGET_ALPHA)
    {
        int i;

        for(i = 0; i < 28; i++) {
            env->ir[i] = ((abi_ulong *)regs)[i];
        }
        env->ipr[IPR_USP] = regs->usp;
        env->ir[30] = regs->usp;
        env->pc = regs->pc;
        env->unique = regs->unique;
    }
#elif defined(TARGET_CRIS)
    {
	    env->regs[0] = regs->r0;
	    env->regs[1] = regs->r1;
	    env->regs[2] = regs->r2;
	    env->regs[3] = regs->r3;
	    env->regs[4] = regs->r4;
	    env->regs[5] = regs->r5;
	    env->regs[6] = regs->r6;
	    env->regs[7] = regs->r7;
	    env->regs[8] = regs->r8;
	    env->regs[9] = regs->r9;
	    env->regs[10] = regs->r10;
	    env->regs[11] = regs->r11;
	    env->regs[12] = regs->r12;
	    env->regs[13] = regs->r13;
	    env->regs[14] = info->start_stack;
	    env->regs[15] = regs->acr;	    
	    env->pc = regs->erp;
    }
#else
#error unsupported target CPU
#endif

#if defined(TARGET_ARM) || defined(TARGET_M68K)
    ts->stack_base = info->start_stack;
    ts->heap_base = info->brk;
    /* This will be filled in on the first SYS_HEAPINFO call.  */
    ts->heap_limit = 0;
#endif

    if (gdbstub_port) {
        gdbserver_start (gdbstub_port);
        gdb_handlesig(env, 0);
    }
    cpu_loop(env);
    /* never exits */
    return 0;
}
