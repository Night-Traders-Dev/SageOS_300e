#ifndef POWER_H
#define POWER_H

/* Exit QEMU via isa-debug-exit device (port 0x501). No-op on real hardware. */
void power_qemu_exit(void);

void power_reboot(void);
void power_halt(void);
void power_shutdown(void);
void power_suspend(void);

#endif /* POWER_H */
