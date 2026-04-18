#ifndef OPENIMP_CORE_MODULE_H
#define OPENIMP_CORE_MODULE_H

#include <stddef.h>
#include <stdint.h>

#define MODULE_OBSERVER_MAX 0x10

struct Module;

typedef struct Observer {
    struct Observer *next;
    struct Module *module;
    void *frame;
    int32_t output_index;
} Observer;

typedef struct Subject {
    void *data;
    struct Subject *next;
} Subject;

typedef struct Module {
    char name[0x10];
    uint8_t pad10[0x30];
    int32_t (*observer_add_fn)(struct Module *module, Observer *observer);
    int32_t (*observer_remove_fn)(struct Module *module, Observer *observer);
    int32_t (*notify_fn)(struct Module *module, void *frame);
    int32_t (*update_fn)(struct Module *module, void *frame);
    uint8_t pad50[0x8];
    Subject *subject_head;
    uint8_t pad5c[0x88];
    uint8_t sem_inputs[0x10];
    uint8_t sem_outputs[0x10];
    uint8_t thread[0x4];
    uint8_t mutex[0x20];
    Subject subject_node;
    int32_t channel;
    uint8_t pad134[0x10];
    void *observer_cb;
} Module;

_Static_assert(offsetof(Module, name) == 0x00, "Module.name offset");
_Static_assert(offsetof(Module, pad10) == 0x10, "Module.pad10 offset");
_Static_assert(offsetof(Module, observer_add_fn) == 0x40, "Module.observer_add_fn offset");
_Static_assert(offsetof(Module, observer_remove_fn) == 0x44, "Module.observer_remove_fn offset");
_Static_assert(offsetof(Module, notify_fn) == 0x48, "Module.notify_fn offset");
_Static_assert(offsetof(Module, update_fn) == 0x4c, "Module.update_fn offset");
_Static_assert(offsetof(Module, pad50) == 0x50, "Module.pad50 offset");
_Static_assert(offsetof(Module, subject_head) == 0x58, "Module.subject_head offset");
_Static_assert(offsetof(Module, pad5c) == 0x5c, "Module.pad5c offset");
_Static_assert(offsetof(Module, sem_inputs) == 0xe4, "Module.sem_inputs offset");
_Static_assert(offsetof(Module, sem_outputs) == 0xf4, "Module.sem_outputs offset");
_Static_assert(offsetof(Module, thread) == 0x104, "Module.thread offset");
_Static_assert(offsetof(Module, mutex) == 0x108, "Module.mutex offset");
_Static_assert(offsetof(Module, subject_node) == 0x128, "Module.subject_node offset");
_Static_assert(offsetof(Module, channel) == 0x130, "Module.channel offset");
_Static_assert(offsetof(Module, pad134) == 0x134, "Module.pad134 offset");
_Static_assert(offsetof(Module, observer_cb) == 0x144, "Module.observer_cb offset");
_Static_assert(sizeof(Module) == 0x148, "Module size");

#endif
