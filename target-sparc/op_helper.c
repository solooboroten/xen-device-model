#include "exec.h"
#include "host-utils.h"
#include "helper.h"

//#define DEBUG_PCALL
//#define DEBUG_MMU
//#define DEBUG_MXCC
//#define DEBUG_UNALIGNED
//#define DEBUG_UNASSIGNED
//#define DEBUG_ASI

#ifdef DEBUG_MMU
#define DPRINTF_MMU(fmt, args...) \
do { printf("MMU: " fmt , ##args); } while (0)
#else
#define DPRINTF_MMU(fmt, args...)
#endif

#ifdef DEBUG_MXCC
#define DPRINTF_MXCC(fmt, args...) \
do { printf("MXCC: " fmt , ##args); } while (0)
#else
#define DPRINTF_MXCC(fmt, args...)
#endif

#ifdef DEBUG_ASI
#define DPRINTF_ASI(fmt, args...) \
do { printf("ASI: " fmt , ##args); } while (0)
#else
#define DPRINTF_ASI(fmt, args...)
#endif

void raise_exception(int tt)
{
    env->exception_index = tt;
    cpu_loop_exit();
}

void helper_trap(target_ulong nb_trap)
{
    env->exception_index = TT_TRAP + (nb_trap & 0x7f);
    cpu_loop_exit();
}

void helper_trapcc(target_ulong nb_trap, target_ulong do_trap)
{
    if (do_trap) {
        env->exception_index = TT_TRAP + (nb_trap & 0x7f);
        cpu_loop_exit();
    }
}

void check_ieee_exceptions(void)
{
    target_ulong status;

    status = get_float_exception_flags(&env->fp_status);
    if (status) {
        /* Copy IEEE 754 flags into FSR */
        if (status & float_flag_invalid)
            env->fsr |= FSR_NVC;
        if (status & float_flag_overflow)
            env->fsr |= FSR_OFC;
        if (status & float_flag_underflow)
            env->fsr |= FSR_UFC;
        if (status & float_flag_divbyzero)
            env->fsr |= FSR_DZC;
        if (status & float_flag_inexact)
            env->fsr |= FSR_NXC;

        if ((env->fsr & FSR_CEXC_MASK) & ((env->fsr & FSR_TEM_MASK) >> 23)) {
            /* Unmasked exception, generate a trap */
            env->fsr |= FSR_FTT_IEEE_EXCP;
            raise_exception(TT_FP_EXCP);
        } else {
            /* Accumulate exceptions */
            env->fsr |= (env->fsr & FSR_CEXC_MASK) << 5;
        }
    }
}

#ifdef USE_INT_TO_FLOAT_HELPERS
void do_fitos(void)
{
    set_float_exception_flags(0, &env->fp_status);
    FT0 = int32_to_float32(*((int32_t *)&FT1), &env->fp_status);
    check_ieee_exceptions();
}

void do_fitod(void)
{
    DT0 = int32_to_float64(*((int32_t *)&FT1), &env->fp_status);
}

#if defined(CONFIG_USER_ONLY)
void do_fitoq(void)
{
    QT0 = int32_to_float128(*((int32_t *)&FT1), &env->fp_status);
}
#endif

#ifdef TARGET_SPARC64
void do_fxtos(void)
{
    set_float_exception_flags(0, &env->fp_status);
    FT0 = int64_to_float32(*((int64_t *)&DT1), &env->fp_status);
    check_ieee_exceptions();
}

void do_fxtod(void)
{
    set_float_exception_flags(0, &env->fp_status);
    DT0 = int64_to_float64(*((int64_t *)&DT1), &env->fp_status);
    check_ieee_exceptions();
}

#if defined(CONFIG_USER_ONLY)
void do_fxtoq(void)
{
    set_float_exception_flags(0, &env->fp_status);
    QT0 = int64_to_float128(*((int32_t *)&DT1), &env->fp_status);
    check_ieee_exceptions();
}
#endif
#endif
#endif

void do_fabss(void)
{
    FT0 = float32_abs(FT1);
}

#ifdef TARGET_SPARC64
void do_fabsd(void)
{
    DT0 = float64_abs(DT1);
}

#if defined(CONFIG_USER_ONLY)
void do_fabsq(void)
{
    QT0 = float128_abs(QT1);
}
#endif
#endif

void do_fsqrts(void)
{
    set_float_exception_flags(0, &env->fp_status);
    FT0 = float32_sqrt(FT1, &env->fp_status);
    check_ieee_exceptions();
}

void do_fsqrtd(void)
{
    set_float_exception_flags(0, &env->fp_status);
    DT0 = float64_sqrt(DT1, &env->fp_status);
    check_ieee_exceptions();
}

#if defined(CONFIG_USER_ONLY)
void do_fsqrtq(void)
{
    set_float_exception_flags(0, &env->fp_status);
    QT0 = float128_sqrt(QT1, &env->fp_status);
    check_ieee_exceptions();
}
#endif

#define GEN_FCMP(name, size, reg1, reg2, FS, TRAP)                      \
    void glue(do_, name) (void)                                         \
    {                                                                   \
        target_ulong new_fsr;                                           \
                                                                        \
        env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                     \
        switch (glue(size, _compare) (reg1, reg2, &env->fp_status)) {   \
        case float_relation_unordered:                                  \
            new_fsr = (FSR_FCC1 | FSR_FCC0) << FS;                      \
            if ((env->fsr & FSR_NVM) || TRAP) {                         \
                env->fsr |= new_fsr;                                    \
                env->fsr |= FSR_NVC;                                    \
                env->fsr |= FSR_FTT_IEEE_EXCP;                          \
                raise_exception(TT_FP_EXCP);                            \
            } else {                                                    \
                env->fsr |= FSR_NVA;                                    \
            }                                                           \
            break;                                                      \
        case float_relation_less:                                       \
            new_fsr = FSR_FCC0 << FS;                                   \
            break;                                                      \
        case float_relation_greater:                                    \
            new_fsr = FSR_FCC1 << FS;                                   \
            break;                                                      \
        default:                                                        \
            new_fsr = 0;                                                \
            break;                                                      \
        }                                                               \
        env->fsr |= new_fsr;                                            \
    }

GEN_FCMP(fcmps, float32, FT0, FT1, 0, 0);
GEN_FCMP(fcmpd, float64, DT0, DT1, 0, 0);

GEN_FCMP(fcmpes, float32, FT0, FT1, 0, 1);
GEN_FCMP(fcmped, float64, DT0, DT1, 0, 1);

#ifdef CONFIG_USER_ONLY
GEN_FCMP(fcmpq, float128, QT0, QT1, 0, 0);
GEN_FCMP(fcmpeq, float128, QT0, QT1, 0, 1);
#endif

#ifdef TARGET_SPARC64
GEN_FCMP(fcmps_fcc1, float32, FT0, FT1, 22, 0);
GEN_FCMP(fcmpd_fcc1, float64, DT0, DT1, 22, 0);

GEN_FCMP(fcmps_fcc2, float32, FT0, FT1, 24, 0);
GEN_FCMP(fcmpd_fcc2, float64, DT0, DT1, 24, 0);

GEN_FCMP(fcmps_fcc3, float32, FT0, FT1, 26, 0);
GEN_FCMP(fcmpd_fcc3, float64, DT0, DT1, 26, 0);

GEN_FCMP(fcmpes_fcc1, float32, FT0, FT1, 22, 1);
GEN_FCMP(fcmped_fcc1, float64, DT0, DT1, 22, 1);

GEN_FCMP(fcmpes_fcc2, float32, FT0, FT1, 24, 1);
GEN_FCMP(fcmped_fcc2, float64, DT0, DT1, 24, 1);

GEN_FCMP(fcmpes_fcc3, float32, FT0, FT1, 26, 1);
GEN_FCMP(fcmped_fcc3, float64, DT0, DT1, 26, 1);
#ifdef CONFIG_USER_ONLY
GEN_FCMP(fcmpq_fcc1, float128, QT0, QT1, 22, 0);
GEN_FCMP(fcmpq_fcc2, float128, QT0, QT1, 24, 0);
GEN_FCMP(fcmpq_fcc3, float128, QT0, QT1, 26, 0);
GEN_FCMP(fcmpeq_fcc1, float128, QT0, QT1, 22, 1);
GEN_FCMP(fcmpeq_fcc2, float128, QT0, QT1, 24, 1);
GEN_FCMP(fcmpeq_fcc3, float128, QT0, QT1, 26, 1);
#endif
#endif

#if !defined(TARGET_SPARC64) && !defined(CONFIG_USER_ONLY) && defined(DEBUG_MXCC)
static void dump_mxcc(CPUState *env)
{
    printf("mxccdata: %016llx %016llx %016llx %016llx\n",
        env->mxccdata[0], env->mxccdata[1], env->mxccdata[2], env->mxccdata[3]);
    printf("mxccregs: %016llx %016llx %016llx %016llx\n"
           "          %016llx %016llx %016llx %016llx\n",
        env->mxccregs[0], env->mxccregs[1], env->mxccregs[2], env->mxccregs[3],
        env->mxccregs[4], env->mxccregs[5], env->mxccregs[6], env->mxccregs[7]);
}
#endif

#if (defined(TARGET_SPARC64) || !defined(CONFIG_USER_ONLY)) \
    && defined(DEBUG_ASI)
static void dump_asi(const char *txt, target_ulong addr, int asi, int size,
                     uint64_t r1)
{
    switch (size)
    {
    case 1:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %02" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xff);
        break;
    case 2:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %04" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xffff);
        break;
    case 4:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %08" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xffffffff);
        break;
    case 8:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %016" PRIx64 "\n", txt,
                    addr, asi, r1);
        break;
    }
}
#endif

#ifndef TARGET_SPARC64
#ifndef CONFIG_USER_ONLY
uint64_t helper_ld_asi(target_ulong addr, int asi, int size, int sign)
{
    uint64_t ret = 0;
#if defined(DEBUG_MXCC) || defined(DEBUG_ASI)
    uint32_t last_addr = addr;
#endif

    switch (asi) {
    case 2: /* SuperSparc MXCC registers */
        switch (addr) {
        case 0x01c00a00: /* MXCC control register */
            if (size == 8)
                ret = env->mxccregs[3];
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        case 0x01c00a04: /* MXCC control register */
            if (size == 4)
                ret = env->mxccregs[3];
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        case 0x01c00c00: /* Module reset register */
            if (size == 8) {
                ret = env->mxccregs[5];
                // should we do something here?
            } else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        case 0x01c00f00: /* MBus port address register */
            if (size == 8)
                ret = env->mxccregs[7];
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        default:
            DPRINTF_MXCC("%08x: unimplemented address, size: %d\n", addr, size);
            break;
        }
        DPRINTF_MXCC("asi = %d, size = %d, sign = %d, addr = %08x -> ret = %08x,"
                     "addr = %08x\n", asi, size, sign, last_addr, ret, addr);
#ifdef DEBUG_MXCC
        dump_mxcc(env);
#endif
        break;
    case 3: /* MMU probe */
        {
            int mmulev;

            mmulev = (addr >> 8) & 15;
            if (mmulev > 4)
                ret = 0;
            else
                ret = mmu_probe(env, addr, mmulev);
            DPRINTF_MMU("mmu_probe: 0x%08x (lev %d) -> 0x%08" PRIx64 "\n",
                        addr, mmulev, ret);
        }
        break;
    case 4: /* read MMU regs */
        {
            int reg = (addr >> 8) & 0x1f;

            ret = env->mmuregs[reg];
            if (reg == 3) /* Fault status cleared on read */
                env->mmuregs[3] = 0;
            else if (reg == 0x13) /* Fault status read */
                ret = env->mmuregs[3];
            else if (reg == 0x14) /* Fault address read */
                ret = env->mmuregs[4];
            DPRINTF_MMU("mmu_read: reg[%d] = 0x%08" PRIx64 "\n", reg, ret);
        }
        break;
    case 5: // Turbosparc ITLB Diagnostic
    case 6: // Turbosparc DTLB Diagnostic
    case 7: // Turbosparc IOTLB Diagnostic
        break;
    case 9: /* Supervisor code access */
        switch(size) {
        case 1:
            ret = ldub_code(addr);
            break;
        case 2:
            ret = lduw_code(addr & ~1);
            break;
        default:
        case 4:
            ret = ldl_code(addr & ~3);
            break;
        case 8:
            ret = ldq_code(addr & ~7);
            break;
        }
        break;
    case 0xa: /* User data access */
        switch(size) {
        case 1:
            ret = ldub_user(addr);
            break;
        case 2:
            ret = lduw_user(addr & ~1);
            break;
        default:
        case 4:
            ret = ldl_user(addr & ~3);
            break;
        case 8:
            ret = ldq_user(addr & ~7);
            break;
        }
        break;
    case 0xb: /* Supervisor data access */
        switch(size) {
        case 1:
            ret = ldub_kernel(addr);
            break;
        case 2:
            ret = lduw_kernel(addr & ~1);
            break;
        default:
        case 4:
            ret = ldl_kernel(addr & ~3);
            break;
        case 8:
            ret = ldq_kernel(addr & ~7);
            break;
        }
        break;
    case 0xc: /* I-cache tag */
    case 0xd: /* I-cache data */
    case 0xe: /* D-cache tag */
    case 0xf: /* D-cache data */
        break;
    case 0x20: /* MMU passthrough */
        switch(size) {
        case 1:
            ret = ldub_phys(addr);
            break;
        case 2:
            ret = lduw_phys(addr & ~1);
            break;
        default:
        case 4:
            ret = ldl_phys(addr & ~3);
            break;
        case 8:
            ret = ldq_phys(addr & ~7);
            break;
        }
        break;
    case 0x21 ... 0x2f: /* MMU passthrough, 0x100000000 to 0xfffffffff */
        switch(size) {
        case 1:
            ret = ldub_phys((target_phys_addr_t)addr
                            | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        case 2:
            ret = lduw_phys((target_phys_addr_t)(addr & ~1)
                            | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        default:
        case 4:
            ret = ldl_phys((target_phys_addr_t)(addr & ~3)
                           | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        case 8:
            ret = ldq_phys((target_phys_addr_t)(addr & ~7)
                           | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        }
        break;
    case 0x30: // Turbosparc secondary cache diagnostic
    case 0x31: // Turbosparc RAM snoop
    case 0x32: // Turbosparc page table descriptor diagnostic
    case 0x39: /* data cache diagnostic register */
        ret = 0;
        break;
    case 8: /* User code access, XXX */
    default:
        do_unassigned_access(addr, 0, 0, asi);
        ret = 0;
        break;
    }
    if (sign) {
        switch(size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(target_ulong addr, uint64_t val, int asi, int size)
{
    switch(asi) {
    case 2: /* SuperSparc MXCC registers */
        switch (addr) {
        case 0x01c00000: /* MXCC stream data register 0 */
            if (size == 8)
                env->mxccdata[0] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        case 0x01c00008: /* MXCC stream data register 1 */
            if (size == 8)
                env->mxccdata[1] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        case 0x01c00010: /* MXCC stream data register 2 */
            if (size == 8)
                env->mxccdata[2] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        case 0x01c00018: /* MXCC stream data register 3 */
            if (size == 8)
                env->mxccdata[3] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        case 0x01c00100: /* MXCC stream source */
            if (size == 8)
                env->mxccregs[0] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            env->mxccdata[0] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) +  0);
            env->mxccdata[1] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) +  8);
            env->mxccdata[2] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) + 16);
            env->mxccdata[3] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) + 24);
            break;
        case 0x01c00200: /* MXCC stream destination */
            if (size == 8)
                env->mxccregs[1] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) +  0, env->mxccdata[0]);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) +  8, env->mxccdata[1]);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) + 16, env->mxccdata[2]);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) + 24, env->mxccdata[3]);
            break;
        case 0x01c00a00: /* MXCC control register */
            if (size == 8)
                env->mxccregs[3] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        case 0x01c00a04: /* MXCC control register */
            if (size == 4)
                env->mxccregs[3] = (env->mxccregs[0xa] & 0xffffffff00000000ULL) | val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        case 0x01c00e00: /* MXCC error register  */
            // writing a 1 bit clears the error
            if (size == 8)
                env->mxccregs[6] &= ~val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        case 0x01c00f00: /* MBus port address register */
            if (size == 8)
                env->mxccregs[7] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr, size);
            break;
        default:
            DPRINTF_MXCC("%08x: unimplemented address, size: %d\n", addr, size);
            break;
        }
        DPRINTF_MXCC("asi = %d, size = %d, addr = %08x, val = %08x\n", asi, size, addr, val);
#ifdef DEBUG_MXCC
        dump_mxcc(env);
#endif
        break;
    case 3: /* MMU flush */
        {
            int mmulev;

            mmulev = (addr >> 8) & 15;
            DPRINTF_MMU("mmu flush level %d\n", mmulev);
            switch (mmulev) {
            case 0: // flush page
                tlb_flush_page(env, addr & 0xfffff000);
                break;
            case 1: // flush segment (256k)
            case 2: // flush region (16M)
            case 3: // flush context (4G)
            case 4: // flush entire
                tlb_flush(env, 1);
                break;
            default:
                break;
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
        }
        break;
    case 4: /* write MMU regs */
        {
            int reg = (addr >> 8) & 0x1f;
            uint32_t oldreg;

            oldreg = env->mmuregs[reg];
            switch(reg) {
            case 0: // Control Register
                env->mmuregs[reg] = (env->mmuregs[reg] & 0xff000000) |
                                    (val & 0x00ffffff);
                // Mappings generated during no-fault mode or MMU
                // disabled mode are invalid in normal mode
                if ((oldreg & (MMU_E | MMU_NF | env->mmu_bm)) !=
                    (env->mmuregs[reg] & (MMU_E | MMU_NF | env->mmu_bm)))
                    tlb_flush(env, 1);
                break;
            case 1: // Context Table Pointer Register
                env->mmuregs[reg] = val & env->mmu_ctpr_mask;
                break;
            case 2: // Context Register
                env->mmuregs[reg] = val & env->mmu_cxr_mask;
                if (oldreg != env->mmuregs[reg]) {
                    /* we flush when the MMU context changes because
                       QEMU has no MMU context support */
                    tlb_flush(env, 1);
                }
                break;
            case 3: // Synchronous Fault Status Register with Clear
            case 4: // Synchronous Fault Address Register
                break;
            case 0x10: // TLB Replacement Control Register
                env->mmuregs[reg] = val & env->mmu_trcr_mask;
                break;
            case 0x13: // Synchronous Fault Status Register with Read and Clear
                env->mmuregs[3] = val & env->mmu_sfsr_mask;
                break;
            case 0x14: // Synchronous Fault Address Register
                env->mmuregs[4] = val;
                break;
            default:
                env->mmuregs[reg] = val;
                break;
            }
            if (oldreg != env->mmuregs[reg]) {
                DPRINTF_MMU("mmu change reg[%d]: 0x%08x -> 0x%08x\n", reg, oldreg, env->mmuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
        }
        break;
    case 5: // Turbosparc ITLB Diagnostic
    case 6: // Turbosparc DTLB Diagnostic
    case 7: // Turbosparc IOTLB Diagnostic
        break;
    case 0xa: /* User data access */
        switch(size) {
        case 1:
            stb_user(addr, val);
            break;
        case 2:
            stw_user(addr & ~1, val);
            break;
        default:
        case 4:
            stl_user(addr & ~3, val);
            break;
        case 8:
            stq_user(addr & ~7, val);
            break;
        }
        break;
    case 0xb: /* Supervisor data access */
        switch(size) {
        case 1:
            stb_kernel(addr, val);
            break;
        case 2:
            stw_kernel(addr & ~1, val);
            break;
        default:
        case 4:
            stl_kernel(addr & ~3, val);
            break;
        case 8:
            stq_kernel(addr & ~7, val);
            break;
        }
        break;
    case 0xc: /* I-cache tag */
    case 0xd: /* I-cache data */
    case 0xe: /* D-cache tag */
    case 0xf: /* D-cache data */
    case 0x10: /* I/D-cache flush page */
    case 0x11: /* I/D-cache flush segment */
    case 0x12: /* I/D-cache flush region */
    case 0x13: /* I/D-cache flush context */
    case 0x14: /* I/D-cache flush user */
        break;
    case 0x17: /* Block copy, sta access */
        {
            // val = src
            // addr = dst
            // copy 32 bytes
            unsigned int i;
            uint32_t src = val & ~3, dst = addr & ~3, temp;

            for (i = 0; i < 32; i += 4, src += 4, dst += 4) {
                temp = ldl_kernel(src);
                stl_kernel(dst, temp);
            }
        }
        break;
    case 0x1f: /* Block fill, stda access */
        {
            // addr = dst
            // fill 32 bytes with val
            unsigned int i;
            uint32_t dst = addr & 7;

            for (i = 0; i < 32; i += 8, dst += 8)
                stq_kernel(dst, val);
        }
        break;
    case 0x20: /* MMU passthrough */
        {
            switch(size) {
            case 1:
                stb_phys(addr, val);
                break;
            case 2:
                stw_phys(addr & ~1, val);
                break;
            case 4:
            default:
                stl_phys(addr & ~3, val);
                break;
            case 8:
                stq_phys(addr & ~7, val);
                break;
            }
        }
        break;
    case 0x21 ... 0x2f: /* MMU passthrough, 0x100000000 to 0xfffffffff */
        {
            switch(size) {
            case 1:
                stb_phys((target_phys_addr_t)addr
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            case 2:
                stw_phys((target_phys_addr_t)(addr & ~1)
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            case 4:
            default:
                stl_phys((target_phys_addr_t)(addr & ~3)
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            case 8:
                stq_phys((target_phys_addr_t)(addr & ~7)
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            }
        }
        break;
    case 0x30: // store buffer tags or Turbosparc secondary cache diagnostic
    case 0x31: // store buffer data, Ross RT620 I-cache flush or
               // Turbosparc snoop RAM
    case 0x32: // store buffer control or Turbosparc page table descriptor diagnostic
    case 0x36: /* I-cache flash clear */
    case 0x37: /* D-cache flash clear */
    case 0x38: /* breakpoint diagnostics */
    case 0x4c: /* breakpoint action */
        break;
    case 8: /* User code access, XXX */
    case 9: /* Supervisor code access, XXX */
    default:
        do_unassigned_access(addr, 1, 0, asi);
        break;
    }
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
}

#endif /* CONFIG_USER_ONLY */
#else /* TARGET_SPARC64 */

#ifdef CONFIG_USER_ONLY
uint64_t helper_ld_asi(target_ulong addr, int asi, int size, int sign)
{
    uint64_t ret = 0;
#if defined(DEBUG_ASI)
    target_ulong last_addr = addr;
#endif

    if (asi < 0x80)
        raise_exception(TT_PRIV_ACT);

    switch (asi) {
    case 0x80: // Primary
    case 0x82: // Primary no-fault
    case 0x88: // Primary LE
    case 0x8a: // Primary no-fault LE
        {
            switch(size) {
            case 1:
                ret = ldub_raw(addr);
                break;
            case 2:
                ret = lduw_raw(addr & ~1);
                break;
            case 4:
                ret = ldl_raw(addr & ~3);
                break;
            default:
            case 8:
                ret = ldq_raw(addr & ~7);
                break;
            }
        }
        break;
    case 0x81: // Secondary
    case 0x83: // Secondary no-fault
    case 0x89: // Secondary LE
    case 0x8b: // Secondary no-fault LE
        // XXX
        break;
    default:
        break;
    }

    /* Convert from little endian */
    switch (asi) {
    case 0x88: // Primary LE
    case 0x89: // Secondary LE
    case 0x8a: // Primary no-fault LE
    case 0x8b: // Secondary no-fault LE
        switch(size) {
        case 2:
            ret = bswap16(ret);
            break;
        case 4:
            ret = bswap32(ret);
            break;
        case 8:
            ret = bswap64(ret);
            break;
        default:
            break;
        }
    default:
        break;
    }

    /* Convert to signed number */
    if (sign) {
        switch(size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(target_ulong addr, target_ulong val, int asi, int size)
{
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
    if (asi < 0x80)
        raise_exception(TT_PRIV_ACT);

    /* Convert to little endian */
    switch (asi) {
    case 0x88: // Primary LE
    case 0x89: // Secondary LE
        switch(size) {
        case 2:
            addr = bswap16(addr);
            break;
        case 4:
            addr = bswap32(addr);
            break;
        case 8:
            addr = bswap64(addr);
            break;
        default:
            break;
        }
    default:
        break;
    }

    switch(asi) {
    case 0x80: // Primary
    case 0x88: // Primary LE
        {
            switch(size) {
            case 1:
                stb_raw(addr, val);
                break;
            case 2:
                stw_raw(addr & ~1, val);
                break;
            case 4:
                stl_raw(addr & ~3, val);
                break;
            case 8:
            default:
                stq_raw(addr & ~7, val);
                break;
            }
        }
        break;
    case 0x81: // Secondary
    case 0x89: // Secondary LE
        // XXX
        return;

    case 0x82: // Primary no-fault, RO
    case 0x83: // Secondary no-fault, RO
    case 0x8a: // Primary no-fault LE, RO
    case 0x8b: // Secondary no-fault LE, RO
    default:
        do_unassigned_access(addr, 1, 0, 1);
        return;
    }
}

#else /* CONFIG_USER_ONLY */

uint64_t helper_ld_asi(target_ulong addr, int asi, int size, int sign)
{
    uint64_t ret = 0;
#if defined(DEBUG_ASI)
    target_ulong last_addr = addr;
#endif

    if ((asi < 0x80 && (env->pstate & PS_PRIV) == 0)
        || (asi >= 0x30 && asi < 0x80 && !(env->hpstate & HS_PRIV)))
        raise_exception(TT_PRIV_ACT);

    switch (asi) {
    case 0x10: // As if user primary
    case 0x18: // As if user primary LE
    case 0x80: // Primary
    case 0x82: // Primary no-fault
    case 0x88: // Primary LE
    case 0x8a: // Primary no-fault LE
        if ((asi & 0x80) && (env->pstate & PS_PRIV)) {
            if (env->hpstate & HS_PRIV) {
                switch(size) {
                case 1:
                    ret = ldub_hypv(addr);
                    break;
                case 2:
                    ret = lduw_hypv(addr & ~1);
                    break;
                case 4:
                    ret = ldl_hypv(addr & ~3);
                    break;
                default:
                case 8:
                    ret = ldq_hypv(addr & ~7);
                    break;
                }
            } else {
                switch(size) {
                case 1:
                    ret = ldub_kernel(addr);
                    break;
                case 2:
                    ret = lduw_kernel(addr & ~1);
                    break;
                case 4:
                    ret = ldl_kernel(addr & ~3);
                    break;
                default:
                case 8:
                    ret = ldq_kernel(addr & ~7);
                    break;
                }
            }
        } else {
            switch(size) {
            case 1:
                ret = ldub_user(addr);
                break;
            case 2:
                ret = lduw_user(addr & ~1);
                break;
            case 4:
                ret = ldl_user(addr & ~3);
                break;
            default:
            case 8:
                ret = ldq_user(addr & ~7);
                break;
            }
        }
        break;
    case 0x14: // Bypass
    case 0x15: // Bypass, non-cacheable
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
        {
            switch(size) {
            case 1:
                ret = ldub_phys(addr);
                break;
            case 2:
                ret = lduw_phys(addr & ~1);
                break;
            case 4:
                ret = ldl_phys(addr & ~3);
                break;
            default:
            case 8:
                ret = ldq_phys(addr & ~7);
                break;
            }
            break;
        }
    case 0x04: // Nucleus
    case 0x0c: // Nucleus Little Endian (LE)
    case 0x11: // As if user secondary
    case 0x19: // As if user secondary LE
    case 0x24: // Nucleus quad LDD 128 bit atomic
    case 0x2c: // Nucleus quad LDD 128 bit atomic
    case 0x4a: // UPA config
    case 0x81: // Secondary
    case 0x83: // Secondary no-fault
    case 0x89: // Secondary LE
    case 0x8b: // Secondary no-fault LE
        // XXX
        break;
    case 0x45: // LSU
        ret = env->lsu;
        break;
    case 0x50: // I-MMU regs
        {
            int reg = (addr >> 3) & 0xf;

            ret = env->immuregs[reg];
            break;
        }
    case 0x51: // I-MMU 8k TSB pointer
    case 0x52: // I-MMU 64k TSB pointer
    case 0x55: // I-MMU data access
        // XXX
        break;
    case 0x56: // I-MMU tag read
        {
            unsigned int i;

            for (i = 0; i < 64; i++) {
                // Valid, ctx match, vaddr match
                if ((env->itlb_tte[i] & 0x8000000000000000ULL) != 0 &&
                    env->itlb_tag[i] == addr) {
                    ret = env->itlb_tag[i];
                    break;
                }
            }
            break;
        }
    case 0x58: // D-MMU regs
        {
            int reg = (addr >> 3) & 0xf;

            ret = env->dmmuregs[reg];
            break;
        }
    case 0x5e: // D-MMU tag read
        {
            unsigned int i;

            for (i = 0; i < 64; i++) {
                // Valid, ctx match, vaddr match
                if ((env->dtlb_tte[i] & 0x8000000000000000ULL) != 0 &&
                    env->dtlb_tag[i] == addr) {
                    ret = env->dtlb_tag[i];
                    break;
                }
            }
            break;
        }
    case 0x59: // D-MMU 8k TSB pointer
    case 0x5a: // D-MMU 64k TSB pointer
    case 0x5b: // D-MMU data pointer
    case 0x5d: // D-MMU data access
    case 0x48: // Interrupt dispatch, RO
    case 0x49: // Interrupt data receive
    case 0x7f: // Incoming interrupt vector, RO
        // XXX
        break;
    case 0x54: // I-MMU data in, WO
    case 0x57: // I-MMU demap, WO
    case 0x5c: // D-MMU data in, WO
    case 0x5f: // D-MMU demap, WO
    case 0x77: // Interrupt vector, WO
    default:
        do_unassigned_access(addr, 0, 0, 1);
        ret = 0;
        break;
    }

    /* Convert from little endian */
    switch (asi) {
    case 0x0c: // Nucleus Little Endian (LE)
    case 0x18: // As if user primary LE
    case 0x19: // As if user secondary LE
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
    case 0x88: // Primary LE
    case 0x89: // Secondary LE
    case 0x8a: // Primary no-fault LE
    case 0x8b: // Secondary no-fault LE
        switch(size) {
        case 2:
            ret = bswap16(ret);
            break;
        case 4:
            ret = bswap32(ret);
            break;
        case 8:
            ret = bswap64(ret);
            break;
        default:
            break;
        }
    default:
        break;
    }

    /* Convert to signed number */
    if (sign) {
        switch(size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(target_ulong addr, target_ulong val, int asi, int size)
{
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
    if ((asi < 0x80 && (env->pstate & PS_PRIV) == 0)
        || (asi >= 0x30 && asi < 0x80 && !(env->hpstate & HS_PRIV)))
        raise_exception(TT_PRIV_ACT);

    /* Convert to little endian */
    switch (asi) {
    case 0x0c: // Nucleus Little Endian (LE)
    case 0x18: // As if user primary LE
    case 0x19: // As if user secondary LE
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
    case 0x88: // Primary LE
    case 0x89: // Secondary LE
        switch(size) {
        case 2:
            addr = bswap16(addr);
            break;
        case 4:
            addr = bswap32(addr);
            break;
        case 8:
            addr = bswap64(addr);
            break;
        default:
            break;
        }
    default:
        break;
    }

    switch(asi) {
    case 0x10: // As if user primary
    case 0x18: // As if user primary LE
    case 0x80: // Primary
    case 0x88: // Primary LE
        if ((asi & 0x80) && (env->pstate & PS_PRIV)) {
            if (env->hpstate & HS_PRIV) {
                switch(size) {
                case 1:
                    stb_hypv(addr, val);
                    break;
                case 2:
                    stw_hypv(addr & ~1, val);
                    break;
                case 4:
                    stl_hypv(addr & ~3, val);
                    break;
                case 8:
                default:
                    stq_hypv(addr & ~7, val);
                    break;
                }
            } else {
                switch(size) {
                case 1:
                    stb_kernel(addr, val);
                    break;
                case 2:
                    stw_kernel(addr & ~1, val);
                    break;
                case 4:
                    stl_kernel(addr & ~3, val);
                    break;
                case 8:
                default:
                    stq_kernel(addr & ~7, val);
                    break;
                }
            }
        } else {
            switch(size) {
            case 1:
                stb_user(addr, val);
                break;
            case 2:
                stw_user(addr & ~1, val);
                break;
            case 4:
                stl_user(addr & ~3, val);
                break;
            case 8:
            default:
                stq_user(addr & ~7, val);
                break;
            }
        }
        break;
    case 0x14: // Bypass
    case 0x15: // Bypass, non-cacheable
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
        {
            switch(size) {
            case 1:
                stb_phys(addr, val);
                break;
            case 2:
                stw_phys(addr & ~1, val);
                break;
            case 4:
                stl_phys(addr & ~3, val);
                break;
            case 8:
            default:
                stq_phys(addr & ~7, val);
                break;
            }
        }
        return;
    case 0x04: // Nucleus
    case 0x0c: // Nucleus Little Endian (LE)
    case 0x11: // As if user secondary
    case 0x19: // As if user secondary LE
    case 0x24: // Nucleus quad LDD 128 bit atomic
    case 0x2c: // Nucleus quad LDD 128 bit atomic
    case 0x4a: // UPA config
    case 0x81: // Secondary
    case 0x89: // Secondary LE
        // XXX
        return;
    case 0x45: // LSU
        {
            uint64_t oldreg;

            oldreg = env->lsu;
            env->lsu = val & (DMMU_E | IMMU_E);
            // Mappings generated during D/I MMU disabled mode are
            // invalid in normal mode
            if (oldreg != env->lsu) {
                DPRINTF_MMU("LSU change: 0x%" PRIx64 " -> 0x%" PRIx64 "\n", oldreg, env->lsu);
#ifdef DEBUG_MMU
                dump_mmu(env);
#endif
                tlb_flush(env, 1);
            }
            return;
        }
    case 0x50: // I-MMU regs
        {
            int reg = (addr >> 3) & 0xf;
            uint64_t oldreg;

            oldreg = env->immuregs[reg];
            switch(reg) {
            case 0: // RO
            case 4:
                return;
            case 1: // Not in I-MMU
            case 2:
            case 7:
            case 8:
                return;
            case 3: // SFSR
                if ((val & 1) == 0)
                    val = 0; // Clear SFSR
                break;
            case 5: // TSB access
            case 6: // Tag access
            default:
                break;
            }
            env->immuregs[reg] = val;
            if (oldreg != env->immuregs[reg]) {
                DPRINTF_MMU("mmu change reg[%d]: 0x%08" PRIx64 " -> 0x%08" PRIx64 "\n", reg, oldreg, env->immuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
            return;
        }
    case 0x54: // I-MMU data in
        {
            unsigned int i;

            // Try finding an invalid entry
            for (i = 0; i < 64; i++) {
                if ((env->itlb_tte[i] & 0x8000000000000000ULL) == 0) {
                    env->itlb_tag[i] = env->immuregs[6];
                    env->itlb_tte[i] = val;
                    return;
                }
            }
            // Try finding an unlocked entry
            for (i = 0; i < 64; i++) {
                if ((env->itlb_tte[i] & 0x40) == 0) {
                    env->itlb_tag[i] = env->immuregs[6];
                    env->itlb_tte[i] = val;
                    return;
                }
            }
            // error state?
            return;
        }
    case 0x55: // I-MMU data access
        {
            unsigned int i = (addr >> 3) & 0x3f;

            env->itlb_tag[i] = env->immuregs[6];
            env->itlb_tte[i] = val;
            return;
        }
    case 0x57: // I-MMU demap
        // XXX
        return;
    case 0x58: // D-MMU regs
        {
            int reg = (addr >> 3) & 0xf;
            uint64_t oldreg;

            oldreg = env->dmmuregs[reg];
            switch(reg) {
            case 0: // RO
            case 4:
                return;
            case 3: // SFSR
                if ((val & 1) == 0) {
                    val = 0; // Clear SFSR, Fault address
                    env->dmmuregs[4] = 0;
                }
                env->dmmuregs[reg] = val;
                break;
            case 1: // Primary context
            case 2: // Secondary context
            case 5: // TSB access
            case 6: // Tag access
            case 7: // Virtual Watchpoint
            case 8: // Physical Watchpoint
            default:
                break;
            }
            env->dmmuregs[reg] = val;
            if (oldreg != env->dmmuregs[reg]) {
                DPRINTF_MMU("mmu change reg[%d]: 0x%08" PRIx64 " -> 0x%08" PRIx64 "\n", reg, oldreg, env->dmmuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
            return;
        }
    case 0x5c: // D-MMU data in
        {
            unsigned int i;

            // Try finding an invalid entry
            for (i = 0; i < 64; i++) {
                if ((env->dtlb_tte[i] & 0x8000000000000000ULL) == 0) {
                    env->dtlb_tag[i] = env->dmmuregs[6];
                    env->dtlb_tte[i] = val;
                    return;
                }
            }
            // Try finding an unlocked entry
            for (i = 0; i < 64; i++) {
                if ((env->dtlb_tte[i] & 0x40) == 0) {
                    env->dtlb_tag[i] = env->dmmuregs[6];
                    env->dtlb_tte[i] = val;
                    return;
                }
            }
            // error state?
            return;
        }
    case 0x5d: // D-MMU data access
        {
            unsigned int i = (addr >> 3) & 0x3f;

            env->dtlb_tag[i] = env->dmmuregs[6];
            env->dtlb_tte[i] = val;
            return;
        }
    case 0x5f: // D-MMU demap
    case 0x49: // Interrupt data receive
        // XXX
        return;
    case 0x51: // I-MMU 8k TSB pointer, RO
    case 0x52: // I-MMU 64k TSB pointer, RO
    case 0x56: // I-MMU tag read, RO
    case 0x59: // D-MMU 8k TSB pointer, RO
    case 0x5a: // D-MMU 64k TSB pointer, RO
    case 0x5b: // D-MMU data pointer, RO
    case 0x5e: // D-MMU tag read, RO
    case 0x48: // Interrupt dispatch, RO
    case 0x7f: // Incoming interrupt vector, RO
    case 0x82: // Primary no-fault, RO
    case 0x83: // Secondary no-fault, RO
    case 0x8a: // Primary no-fault LE, RO
    case 0x8b: // Secondary no-fault LE, RO
    default:
        do_unassigned_access(addr, 1, 0, 1);
        return;
    }
}
#endif /* CONFIG_USER_ONLY */

void helper_ldf_asi(target_ulong addr, int asi, int size, int rd)
{
    unsigned int i;
    target_ulong val;

    switch (asi) {
    case 0xf0: // Block load primary
    case 0xf1: // Block load secondary
    case 0xf8: // Block load primary LE
    case 0xf9: // Block load secondary LE
        if (rd & 7) {
            raise_exception(TT_ILL_INSN);
            return;
        }
        if (addr & 0x3f) {
            raise_exception(TT_UNALIGNED);
            return;
        }
        for (i = 0; i < 16; i++) {
            *(uint32_t *)&env->fpr[rd++] = helper_ld_asi(addr, asi & 0x8f, 4, 0);
            addr += 4;
        }

        return;
    default:
        break;
    }

    val = helper_ld_asi(addr, asi, size, 0);
    switch(size) {
    default:
    case 4:
        *((uint32_t *)&FT0) = val;
        break;
    case 8:
        *((int64_t *)&DT0) = val;
        break;
#if defined(CONFIG_USER_ONLY)
    case 16:
        // XXX
        break;
#endif
    }
}

void helper_stf_asi(target_ulong addr, int asi, int size, int rd)
{
    unsigned int i;
    target_ulong val = 0;

    switch (asi) {
    case 0xf0: // Block store primary
    case 0xf1: // Block store secondary
    case 0xf8: // Block store primary LE
    case 0xf9: // Block store secondary LE
        if (rd & 7) {
            raise_exception(TT_ILL_INSN);
            return;
        }
        if (addr & 0x3f) {
            raise_exception(TT_UNALIGNED);
            return;
        }
        for (i = 0; i < 16; i++) {
            val = *(uint32_t *)&env->fpr[rd++];
            helper_st_asi(addr, val, asi & 0x8f, 4);
            addr += 4;
        }

        return;
    default:
        break;
    }

    switch(size) {
    default:
    case 4:
        val = *((uint32_t *)&FT0);
        break;
    case 8:
        val = *((int64_t *)&DT0);
        break;
#if defined(CONFIG_USER_ONLY)
    case 16:
        // XXX
        break;
#endif
    }
    helper_st_asi(addr, val, asi, size);
}

target_ulong helper_cas_asi(target_ulong addr, target_ulong val1,
                            target_ulong val2, uint32_t asi)
{
    target_ulong ret;

    val1 &= 0xffffffffUL;
    ret = helper_ld_asi(addr, asi, 4, 0);
    ret &= 0xffffffffUL;
    if (val1 == ret)
        helper_st_asi(addr, val2 & 0xffffffffUL, asi, 4);
    return ret;
}

target_ulong helper_casx_asi(target_ulong addr, target_ulong val1,
                             target_ulong val2, uint32_t asi)
{
    target_ulong ret;

    ret = helper_ld_asi(addr, asi, 8, 0);
    if (val1 == ret)
        helper_st_asi(addr, val2, asi, 8);
    return ret;
}
#endif /* TARGET_SPARC64 */

#ifndef TARGET_SPARC64
void helper_rett(void)
{
    unsigned int cwp;

    if (env->psret == 1)
        raise_exception(TT_ILL_INSN);

    env->psret = 1;
    cwp = (env->cwp + 1) & (NWINDOWS - 1);
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_UNF);
    }
    set_cwp(cwp);
    env->psrs = env->psrps;
}
#endif

uint64_t helper_pack64(target_ulong high, target_ulong low)
{
    return ((uint64_t)high << 32) | (uint64_t)(low & 0xffffffff);
}

void helper_ldfsr(void)
{
    int rnd_mode;
    switch (env->fsr & FSR_RD_MASK) {
    case FSR_RD_NEAREST:
        rnd_mode = float_round_nearest_even;
        break;
    default:
    case FSR_RD_ZERO:
        rnd_mode = float_round_to_zero;
        break;
    case FSR_RD_POS:
        rnd_mode = float_round_up;
        break;
    case FSR_RD_NEG:
        rnd_mode = float_round_down;
        break;
    }
    set_float_rounding_mode(rnd_mode, &env->fp_status);
}

void helper_debug()
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit();
}

#ifndef TARGET_SPARC64
void helper_wrpsr(target_ulong new_psr)
{
    if ((new_psr & PSR_CWP) >= NWINDOWS)
        raise_exception(TT_ILL_INSN);
    else
        PUT_PSR(env, new_psr);
}

target_ulong helper_rdpsr(void)
{
    return GET_PSR(env);
}

#else

target_ulong helper_popc(target_ulong val)
{
    return ctpop64(val);
}

static inline uint64_t *get_gregset(uint64_t pstate)
{
    switch (pstate) {
    default:
    case 0:
        return env->bgregs;
    case PS_AG:
        return env->agregs;
    case PS_MG:
        return env->mgregs;
    case PS_IG:
        return env->igregs;
    }
}

static inline void change_pstate(uint64_t new_pstate)
{
    uint64_t pstate_regs, new_pstate_regs;
    uint64_t *src, *dst;

    pstate_regs = env->pstate & 0xc01;
    new_pstate_regs = new_pstate & 0xc01;
    if (new_pstate_regs != pstate_regs) {
        // Switch global register bank
        src = get_gregset(new_pstate_regs);
        dst = get_gregset(pstate_regs);
        memcpy32(dst, env->gregs);
        memcpy32(env->gregs, src);
    }
    env->pstate = new_pstate;
}

void helper_wrpstate(target_ulong new_state)
{
    change_pstate(new_state & 0xf3f);
}

void helper_done(void)
{
    env->tl--;
    env->pc = env->tnpc[env->tl];
    env->npc = env->tnpc[env->tl] + 4;
    PUT_CCR(env, env->tstate[env->tl] >> 32);
    env->asi = (env->tstate[env->tl] >> 24) & 0xff;
    change_pstate((env->tstate[env->tl] >> 8) & 0xf3f);
    PUT_CWP64(env, env->tstate[env->tl] & 0xff);
}

void helper_retry(void)
{
    env->tl--;
    env->pc = env->tpc[env->tl];
    env->npc = env->tnpc[env->tl];
    PUT_CCR(env, env->tstate[env->tl] >> 32);
    env->asi = (env->tstate[env->tl] >> 24) & 0xff;
    change_pstate((env->tstate[env->tl] >> 8) & 0xf3f);
    PUT_CWP64(env, env->tstate[env->tl] & 0xff);
}
#endif

void set_cwp(int new_cwp)
{
    /* put the modified wrap registers at their proper location */
    if (env->cwp == (NWINDOWS - 1))
        memcpy32(env->regbase, env->regbase + NWINDOWS * 16);
    env->cwp = new_cwp;
    /* put the wrap registers at their temporary location */
    if (new_cwp == (NWINDOWS - 1))
        memcpy32(env->regbase + NWINDOWS * 16, env->regbase);
    env->regwptr = env->regbase + (new_cwp * 16);
    REGWPTR = env->regwptr;
}

void cpu_set_cwp(CPUState *env1, int new_cwp)
{
    CPUState *saved_env;
#ifdef reg_REGWPTR
    target_ulong *saved_regwptr;
#endif

    saved_env = env;
#ifdef reg_REGWPTR
    saved_regwptr = REGWPTR;
#endif
    env = env1;
    set_cwp(new_cwp);
    env = saved_env;
#ifdef reg_REGWPTR
    REGWPTR = saved_regwptr;
#endif
}

#ifdef TARGET_SPARC64
#ifdef DEBUG_PCALL
static const char * const excp_names[0x50] = {
    [TT_TFAULT] = "Instruction Access Fault",
    [TT_TMISS] = "Instruction Access MMU Miss",
    [TT_CODE_ACCESS] = "Instruction Access Error",
    [TT_ILL_INSN] = "Illegal Instruction",
    [TT_PRIV_INSN] = "Privileged Instruction",
    [TT_NFPU_INSN] = "FPU Disabled",
    [TT_FP_EXCP] = "FPU Exception",
    [TT_TOVF] = "Tag Overflow",
    [TT_CLRWIN] = "Clean Windows",
    [TT_DIV_ZERO] = "Division By Zero",
    [TT_DFAULT] = "Data Access Fault",
    [TT_DMISS] = "Data Access MMU Miss",
    [TT_DATA_ACCESS] = "Data Access Error",
    [TT_DPROT] = "Data Protection Error",
    [TT_UNALIGNED] = "Unaligned Memory Access",
    [TT_PRIV_ACT] = "Privileged Action",
    [TT_EXTINT | 0x1] = "External Interrupt 1",
    [TT_EXTINT | 0x2] = "External Interrupt 2",
    [TT_EXTINT | 0x3] = "External Interrupt 3",
    [TT_EXTINT | 0x4] = "External Interrupt 4",
    [TT_EXTINT | 0x5] = "External Interrupt 5",
    [TT_EXTINT | 0x6] = "External Interrupt 6",
    [TT_EXTINT | 0x7] = "External Interrupt 7",
    [TT_EXTINT | 0x8] = "External Interrupt 8",
    [TT_EXTINT | 0x9] = "External Interrupt 9",
    [TT_EXTINT | 0xa] = "External Interrupt 10",
    [TT_EXTINT | 0xb] = "External Interrupt 11",
    [TT_EXTINT | 0xc] = "External Interrupt 12",
    [TT_EXTINT | 0xd] = "External Interrupt 13",
    [TT_EXTINT | 0xe] = "External Interrupt 14",
    [TT_EXTINT | 0xf] = "External Interrupt 15",
};
#endif

void do_interrupt(int intno)
{
#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_INT) {
        static int count;
        const char *name;

        if (intno < 0 || intno >= 0x180 || (intno > 0x4f && intno < 0x80))
            name = "Unknown";
        else if (intno >= 0x100)
            name = "Trap Instruction";
        else if (intno >= 0xc0)
            name = "Window Fill";
        else if (intno >= 0x80)
            name = "Window Spill";
        else {
            name = excp_names[intno];
            if (!name)
                name = "Unknown";
        }

        fprintf(logfile, "%6d: %s (v=%04x) pc=%016" PRIx64 " npc=%016" PRIx64
                " SP=%016" PRIx64 "\n",
                count, name, intno,
                env->pc,
                env->npc, env->regwptr[6]);
        cpu_dump_state(env, logfile, fprintf, 0);
#if 0
        {
            int i;
            uint8_t *ptr;

            fprintf(logfile, "       code=");
            ptr = (uint8_t *)env->pc;
            for(i = 0; i < 16; i++) {
                fprintf(logfile, " %02x", ldub(ptr + i));
            }
            fprintf(logfile, "\n");
        }
#endif
        count++;
    }
#endif
#if !defined(CONFIG_USER_ONLY)
    if (env->tl == MAXTL) {
        cpu_abort(env, "Trap 0x%04x while trap level is MAXTL, Error state", env->exception_index);
        return;
    }
#endif
    env->tstate[env->tl] = ((uint64_t)GET_CCR(env) << 32) | ((env->asi & 0xff) << 24) |
        ((env->pstate & 0xf3f) << 8) | GET_CWP64(env);
    env->tpc[env->tl] = env->pc;
    env->tnpc[env->tl] = env->npc;
    env->tt[env->tl] = intno;
    change_pstate(PS_PEF | PS_PRIV | PS_AG);

    if (intno == TT_CLRWIN)
        set_cwp((env->cwp - 1) & (NWINDOWS - 1));
    else if ((intno & 0x1c0) == TT_SPILL)
        set_cwp((env->cwp - env->cansave - 2) & (NWINDOWS - 1));
    else if ((intno & 0x1c0) == TT_FILL)
        set_cwp((env->cwp + 1) & (NWINDOWS - 1));
    env->tbr &= ~0x7fffULL;
    env->tbr |= ((env->tl > 1) ? 1 << 14 : 0) | (intno << 5);
    if (env->tl < MAXTL - 1) {
        env->tl++;
    } else {
        env->pstate |= PS_RED;
        if (env->tl != MAXTL)
            env->tl++;
    }
    env->pc = env->tbr;
    env->npc = env->pc + 4;
    env->exception_index = 0;
}
#else
#ifdef DEBUG_PCALL
static const char * const excp_names[0x80] = {
    [TT_TFAULT] = "Instruction Access Fault",
    [TT_ILL_INSN] = "Illegal Instruction",
    [TT_PRIV_INSN] = "Privileged Instruction",
    [TT_NFPU_INSN] = "FPU Disabled",
    [TT_WIN_OVF] = "Window Overflow",
    [TT_WIN_UNF] = "Window Underflow",
    [TT_UNALIGNED] = "Unaligned Memory Access",
    [TT_FP_EXCP] = "FPU Exception",
    [TT_DFAULT] = "Data Access Fault",
    [TT_TOVF] = "Tag Overflow",
    [TT_EXTINT | 0x1] = "External Interrupt 1",
    [TT_EXTINT | 0x2] = "External Interrupt 2",
    [TT_EXTINT | 0x3] = "External Interrupt 3",
    [TT_EXTINT | 0x4] = "External Interrupt 4",
    [TT_EXTINT | 0x5] = "External Interrupt 5",
    [TT_EXTINT | 0x6] = "External Interrupt 6",
    [TT_EXTINT | 0x7] = "External Interrupt 7",
    [TT_EXTINT | 0x8] = "External Interrupt 8",
    [TT_EXTINT | 0x9] = "External Interrupt 9",
    [TT_EXTINT | 0xa] = "External Interrupt 10",
    [TT_EXTINT | 0xb] = "External Interrupt 11",
    [TT_EXTINT | 0xc] = "External Interrupt 12",
    [TT_EXTINT | 0xd] = "External Interrupt 13",
    [TT_EXTINT | 0xe] = "External Interrupt 14",
    [TT_EXTINT | 0xf] = "External Interrupt 15",
    [TT_TOVF] = "Tag Overflow",
    [TT_CODE_ACCESS] = "Instruction Access Error",
    [TT_DATA_ACCESS] = "Data Access Error",
    [TT_DIV_ZERO] = "Division By Zero",
    [TT_NCP_INSN] = "Coprocessor Disabled",
};
#endif

void do_interrupt(int intno)
{
    int cwp;

#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_INT) {
        static int count;
        const char *name;

        if (intno < 0 || intno >= 0x100)
            name = "Unknown";
        else if (intno >= 0x80)
            name = "Trap Instruction";
        else {
            name = excp_names[intno];
            if (!name)
                name = "Unknown";
        }

        fprintf(logfile, "%6d: %s (v=%02x) pc=%08x npc=%08x SP=%08x\n",
                count, name, intno,
                env->pc,
                env->npc, env->regwptr[6]);
        cpu_dump_state(env, logfile, fprintf, 0);
#if 0
        {
            int i;
            uint8_t *ptr;

            fprintf(logfile, "       code=");
            ptr = (uint8_t *)env->pc;
            for(i = 0; i < 16; i++) {
                fprintf(logfile, " %02x", ldub(ptr + i));
            }
            fprintf(logfile, "\n");
        }
#endif
        count++;
    }
#endif
#if !defined(CONFIG_USER_ONLY)
    if (env->psret == 0) {
        cpu_abort(env, "Trap 0x%02x while interrupts disabled, Error state", env->exception_index);
        return;
    }
#endif
    env->psret = 0;
    cwp = (env->cwp - 1) & (NWINDOWS - 1);
    set_cwp(cwp);
    env->regwptr[9] = env->pc;
    env->regwptr[10] = env->npc;
    env->psrps = env->psrs;
    env->psrs = 1;
    env->tbr = (env->tbr & TBR_BASE_MASK) | (intno << 4);
    env->pc = env->tbr;
    env->npc = env->pc + 4;
    env->exception_index = 0;
}
#endif

#if !defined(CONFIG_USER_ONLY)

static void do_unaligned_access(target_ulong addr, int is_write, int is_user,
                                void *retaddr);

#define MMUSUFFIX _mmu
#define ALIGNED_ONLY
#ifdef __s390__
# define GETPC() ((void*)((unsigned long)__builtin_return_address(0) & 0x7fffffffUL))
#else
# define GETPC() (__builtin_return_address(0))
#endif

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

static void do_unaligned_access(target_ulong addr, int is_write, int is_user,
                                void *retaddr)
{
#ifdef DEBUG_UNALIGNED
    printf("Unaligned access to 0x%x from 0x%x\n", addr, env->pc);
#endif
    raise_exception(TT_UNALIGNED);
}

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    TranslationBlock *tb;
    int ret;
    unsigned long pc;
    CPUState *saved_env;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;

    ret = cpu_sparc_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, (void *)T2);
            }
        }
        cpu_loop_exit();
    }
    env = saved_env;
}

#endif

#ifndef TARGET_SPARC64
void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int is_asi)
{
    CPUState *saved_env;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
#ifdef DEBUG_UNASSIGNED
    if (is_asi)
        printf("Unassigned mem %s access to " TARGET_FMT_plx " asi 0x%02x from "
               TARGET_FMT_lx "\n",
               is_exec ? "exec" : is_write ? "write" : "read", addr, is_asi,
               env->pc);
    else
        printf("Unassigned mem %s access to " TARGET_FMT_plx " from "
               TARGET_FMT_lx "\n",
               is_exec ? "exec" : is_write ? "write" : "read", addr, env->pc);
#endif
    if (env->mmuregs[3]) /* Fault status register */
        env->mmuregs[3] = 1; /* overflow (not read before another fault) */
    if (is_asi)
        env->mmuregs[3] |= 1 << 16;
    if (env->psrs)
        env->mmuregs[3] |= 1 << 5;
    if (is_exec)
        env->mmuregs[3] |= 1 << 6;
    if (is_write)
        env->mmuregs[3] |= 1 << 7;
    env->mmuregs[3] |= (5 << 2) | 2;
    env->mmuregs[4] = addr; /* Fault address register */
    if ((env->mmuregs[0] & MMU_E) && !(env->mmuregs[0] & MMU_NF)) {
        if (is_exec)
            raise_exception(TT_CODE_ACCESS);
        else
            raise_exception(TT_DATA_ACCESS);
    }
    env = saved_env;
}
#else
void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int is_asi)
{
#ifdef DEBUG_UNASSIGNED
    CPUState *saved_env;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    printf("Unassigned mem access to " TARGET_FMT_plx " from " TARGET_FMT_lx "\n",
           addr, env->pc);
    env = saved_env;
#endif
    if (is_exec)
        raise_exception(TT_CODE_ACCESS);
    else
        raise_exception(TT_DATA_ACCESS);
}
#endif

