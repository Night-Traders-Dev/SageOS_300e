#ifndef SYSINFO_H
#define SYSINFO_H

/* Print CPU frequency, memory use, and storage use to the console. */
void sysinfo_cmd(void);
int  sysinfo_is_qemu(void);

#endif /* SYSINFO_H */
