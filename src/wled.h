#pragma once
int wled_get(const char *path, char *buf, int max);   // -1 = not a WLED path
int wled_post_state(char *body);
