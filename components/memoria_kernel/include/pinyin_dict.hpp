#pragma once
/* Auto-generated pinyin dictionary interface */
#ifdef __cplusplus
extern "C" {
#endif

int pinyin_count();
const char* pinyin_key(int i);
const char* pinyin_val(int i);
int pinyin_find(const char* s);

#ifdef __cplusplus
}
#endif
