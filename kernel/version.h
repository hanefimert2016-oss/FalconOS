/* =============================================================================
 *  FalconOS — version header  (FalconOS 2.0 Alpha)
 * ============================================================================= */
#ifndef VERSION_H
#define VERSION_H

#define FALCON_VERSION_MAJOR    2
#define FALCON_VERSION_MINOR    0
#define FALCON_VERSION_PATCH    0
#define FALCON_VERSION_LABEL    "Alpha"
#define FALCON_VERSION_CODENAME "Blue Dragon"

#define FALCON_VERSION_STRING   "FalconOS 2.0.0 Alpha \"Blue Dragon\""

/* Feature flags for v2.0 Alpha */
#define FEATURE_PERF_OPT        1   /* Performance optimizations enabled */
#define FEATURE_FAST_BOOT       1   /* Fast boot sequence */
#define FEATURE_SMP_READY       0   /* SMP support (coming soon) */
#define FEATURE_VFS             1   /* Virtual filesystem layer */
#define FEATURE_NET_STACK       1   /* Network stack enabled */
#define FEATURE_USB_ENHANCED    0   /* Enhanced USB (coming soon) */
#define FEATURE_MULTITASK       0   /* Preemptive multitasking (coming soon) */

/* Build info - will be populated by build system */
#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP         __DATE__ " " __TIME__
#endif

#ifndef BUILD_GIT_HASH
#define BUILD_GIT_HASH          "unknown"
#endif

#endif /* VERSION_H */
