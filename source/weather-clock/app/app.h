#ifndef __APP_H__
#define __APP_H__

#define APP_VERSION "v1.0"
/* Configure these locally; real credentials are intentionally excluded. */
#define WIFI_SSID   "YOUR_WIFI_SSID"
#define WIFI_PASSWD "YOUR_WIFI_PASSWORD"

void wifi_init(void);
void wifi_wait_connect(void);

void main_loop_init(void);
void main_loop(void);


#endif /* __APP_H__ */
