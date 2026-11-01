#pragma once
void hue_init(void);
void hue_task(void);
int  hue_http(const char *method, const char *path, char *body, char *out, int max, const char **ctype);
