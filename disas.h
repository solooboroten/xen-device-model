#ifndef _QEMU_DISAS_H
#define _QEMU_DISAS_H

/* Disassemble this for me please... (debugging). */
void disas(FILE *out, void *code, unsigned long size, int is_host, int flags);
void monitor_disas(target_ulong pc, int nb_insn, int is_physical, int flags);

/* Look up symbol for debugging purpose.  Returns "" if unknown. */
const char *lookup_symbol(void *orig_addr);

/* Filled in by elfload.c.  Simplistic, but will do for now. */
extern struct syminfo {
    unsigned int disas_num_syms;
    void *disas_symtab;
    const char *disas_strtab;
    struct syminfo *next;
} *syminfos;

#endif /* _QEMU_DISAS_H */
