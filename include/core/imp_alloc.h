#ifndef OPENIMP_CORE_IMP_ALLOC_H
#define OPENIMP_CORE_IMP_ALLOC_H

#include <stdint.h>

typedef enum IMP_MEM_METHOD {
    KMEM = 0,
    MALLOC = 1,
    PMEM = 2,
} IMP_MEM_METHOD;

typedef struct IMP_Alloc_Attr {
    uint8_t pad0[0x80];
    IMP_MEM_METHOD method;
    uint8_t pad84[0xc];
    int32_t attr;
} IMP_Alloc_Attr;

typedef struct IMP_Alloc_Info {
    uint8_t pad0[0x60];
    char name[0x20];
    IMP_MEM_METHOD method;
    uint32_t phys_addr;
    uint32_t virt_addr;
    uint32_t length;
    int32_t attr;
} IMP_Alloc_Info;

#endif
