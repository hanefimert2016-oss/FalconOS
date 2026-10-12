#ifndef FALCON_UEFI_H
#define FALCON_UEFI_H
#include <stdint.h>
typedef uint64_t STATUS;
typedef void* HANDLE;
typedef uint16_t CHAR16;
typedef struct {uint32_t a;uint16_t b,c;uint8_t d[8];} GUID;
typedef struct {uint64_t signature;uint32_t revision,header_size,crc32,reserved;} HDR;
typedef struct {HDR h;void* f[44];} BS;
typedef struct {HDR h;void*vendor;uint32_t revision,pad;void*in_handle,*in,*out_handle,*out,*err_handle,*err,*runtime;BS*boot;uint64_t nconfig;void*config;} ST;
typedef struct {uint32_t revision,pad;HANDLE parent;ST*system;HANDLE device;void*path,*reserved;uint32_t options_size,pad2;void*options,*image_base;uint64_t image_size;uint32_t code_type,data_type;void*unload;} LOADED;
typedef struct {uint64_t revision;void*open_volume;} FS;
typedef struct {uint64_t revision;void*open,*close,*del,*read,*write,*getpos,*setpos,*getinfo,*setinfo,*flush,*open_ex,*read_ex,*write_ex,*flush_ex;} FILEP;
typedef struct {uint32_t version,width,height,pixel_format;uint32_t mask[4],scanline;} GOPINFO;
typedef struct {uint32_t max,mode;GOPINFO*info;uint64_t info_size,base,size;} GOPMODE;
typedef struct {void*query,*set,*blt;GOPMODE*mode;} GOP;
typedef struct {uint32_t type,pad;uint64_t physical,virtual_addr,pages,attr;} MEMDESC;
#define GUID_VAL(A,B,C,D0,D1,D2,D3,D4,D5,D6,D7) {A,B,C,{D0,D1,D2,D3,D4,D5,D6,D7}}
static const GUID ID_LOADED=GUID_VAL(0x5b1b31a1,0x9562,0x11d2,0x8e,0x3f,0,0xa0,0xc9,0x69,0x72,0x3b);
static const GUID ID_FS=GUID_VAL(0x964e5b22,0x6459,0x11d2,0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b);
static const GUID ID_GOP=GUID_VAL(0x9042a9de,0x23dc,0x4a38,0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a);
#define EFI_FN(S,N,T) ((T)((S)->f[N]))
#endif
