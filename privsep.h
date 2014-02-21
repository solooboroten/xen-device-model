#ifndef PRIVSEP_H
#define PRIVSEP_H

/* privsep.c */
void init_privsep(void);
void init_privxsh(void);
int privsep_open_ro(const char *path);
void privsep_eject_cd(int id);
void privsep_lock_cd(int id);
void privsep_unlock_cd(int id);
void privsep_set_rtc_timeoffset(long offset);
void privsep_ack_logdirty_switch(char *act);
void privsep_set_cd_backend(int id, const char *path);
QEMUFile* privsep_open_vm_dump(const char *name);
FILE* privsep_open_keymap(const char *language);
void privsep_record_dm(char *subpath, char *state);
void privsep_write_vslots(char *vslots);
char* privsep_read_dm(char *subpath);

#endif
