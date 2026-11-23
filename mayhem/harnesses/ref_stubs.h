#ifndef YQ2_FUZZ_REF_STUBS_H
#define YQ2_FUZZ_REF_STUBS_H
#include <stddef.h>
#include <stdint.h>
void stubs_init(void);
void stubs_set_input(const uint8_t *data, size_t size);
#endif
