#ifndef _shengloong_elfcompat_h
#define _shengloong_elfcompat_h

// polyfill our machine number
#ifndef EM_LOONGARCH
#define EM_LOONGARCH 258
#endif

// and our object file ABI version and mask
#ifndef EF_LARCH_OBJABI_V1
#define EF_LARCH_OBJABI_V1 0x40
#endif

#ifndef EF_LARCH_OBJABI_MASK
#define EF_LARCH_OBJABI_MASK 0xC0
#endif

#endif  // _shengloong_elfcompat_h
