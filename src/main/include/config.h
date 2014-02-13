#ifndef CONFIG_H
#define CONFIG_H

#include "string-funcs.h"

extern void read_config(struct string *jvm_options);
extern int is_modern_config(const char *text);
extern void parse_legacy_config(struct string *jvm_options);
extern void parse_modern_config(struct string *jvm_options);

extern int legacy_mode;
extern struct string *legacy_ij1_options;

#endif
