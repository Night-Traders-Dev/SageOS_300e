#ifndef SAGEOS_WIFI_QCA6174_H
#define SAGEOS_WIFI_QCA6174_H

int qca6174_init(void);
int qca6174_is_present(void);
void qca6174_cmd_info(void);
void qca6174_cmd_reset(void);
void qca6174_cmd_upload(void);
void qca6174_cmd_init_rings(void);
void qca6174_cmd_scan(void);
void qca6174_cmd_connect(const char *ssid, const char *pass);

#endif /* SAGEOS_WIFI_QCA6174_H */
