/* A check of EAGL's DynamicLoader object before DynamicLoader::Resolve (sub_0010AC20) relocates it
 * (dynload_guard.c). */
#ifndef DYNLOAD_GUARD_H
#define DYNLOAD_GUARD_H

#include <stdint.h>

/* Before Resolve(handle), from sub_0010AC20_enter with the return address the call left (MEM32(esp))
 * and the one its constructor's caller left (outer, 0 when unknown).
 * 0: the object is sound, or there is none, or it is resolved already (Resolve's own early returns).
 * 1: something Resolve would read is damaged; the load was taken back the way a failed dlopen leaves
 * it (handle[0] = 0, out of the loaded-object list), so Resolve returns at once, and a [DYNLOAD] line
 * names the damage, the caller and the image. */
int dynload_guard_check(uint32_t handle, uint32_t caller, uint32_t outer);
/* At dlopen (sub_00109F80_enter): logs an image that is no ELF object or whose section table runs past
 * it, with the caller; read only. outer: the caller of the dlopen + Resolve constructor, 0 unknown. */
void dynload_guard_image(uint32_t handle, uint32_t caller, uint32_t outer);

/* Loads checked and refused since the start. */
unsigned dynload_guard_checked(void);
unsigned dynload_guard_refused(void);

/* recomp_manual.c: a guest address the lifted code has as a function (the unresolved-symbol callback). */
int dynload_code_ok(uint32_t va);

#endif
