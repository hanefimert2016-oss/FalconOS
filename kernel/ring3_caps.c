/* Ring3 cooperative ELF task syscall grant policy.
 * Pure kernel code: no trust is delegated to user memory, ELF metadata,
 * the FAPP publisher, a host bridge or caller-controlled pointers.
 * This policy is experimental until per-task address spaces and
 * preemptive scheduling are implemented. */
#include "falcon.h"

/* Explicit grants: unknown bits never expand task authority. */
#define RING3_CAP_MASK 3u
bool ring3_caps_valid(u32 caps){
    return (caps & ~RING3_CAP_MASK)==0u;
}
bool ring3_caps_authorize(u32 caps,u64 syscall_no){
    if(!ring3_caps_valid(caps))caps=0;
    if(syscall_no==1u)return (caps&1u)!=0u; /* diagnostic */
    if(syscall_no==4u)return (caps&2u)!=0u; /* clock ticks */
    /* Exit, getpid, yield remain callable without grants.
     * Unknown syscall numbers retain the dispatcher's ENOSYS result. */
    return true;
}
