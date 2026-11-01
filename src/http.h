#pragma once
void http_init(void);
void http_task(void);
int  http_install_pending(void);
extern volatile int http_upload_active;
