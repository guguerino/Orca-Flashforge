// Type descriptors for the FlashNetwork bridge (shared by shim and helper).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#include "FlashNetwork.h"

namespace fb {

enum FieldKind {
    FK_STR, FK_STRARR, FK_DBLARR, FK_INTARR, FK_BYTES, FK_ARR,
    FK_INLINE, FK_VOIDDATA, FK_VOIDARR, FK_CB
};

// kind, off, cnt_off, type, elem, aux_off, aux
//   FK_CB:       aux_off = offset of the user-data pointer, aux = callback kind
//   FK_VOIDDATA: aux_off = offset of the type discriminator, aux = VD map
struct Field {
    int kind;
    uint32_t off;
    uint32_t cnt_off;
    int type;
    uint32_t elem;
    uint32_t aux_off;
    int aux;
};

struct TypeDesc {
    const char *name;
    uint32_t size;
    const Field *f;
    int nf;
};

struct VdEntry {
    int vk;      // 0 none, 1 string, 2 struct
    int type;
};

}  // namespace fb
