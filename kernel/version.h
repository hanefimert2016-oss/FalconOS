/* FalconOS v2-beta -- single authoritative, user-visible release identity.
 * This header exists in v2-beta only; V1 release remains unchanged. */
#ifndef FALCON_VERSION_H
#define FALCON_VERSION_H

/* Release artifacts are built from the same reviewed source, with an
 * explicit immutable OS revision marker that the updater checks on reboot. */
#ifdef FALCON_OS_UPDATE_VERSION
#define FALCON_RELEASE_LABEL         "FalconOS 2.0.1 Beta"
#define FALCON_RELEASE_SEMVER        "2.0.1-beta"
#define FALCON_RELEASE_BADGE         "V2 BETA"
#define FALCON_RELEASE_CHANNEL       "v2-beta"
#else
#define FALCON_RELEASE_LABEL         "FalconOS 2.0 Beta"
#define FALCON_RELEASE_BADGE         "V2 BETA"
#define FALCON_RELEASE_CHANNEL       "v2-beta"
#define FALCON_RELEASE_SEMVER        "2.0.0-beta"
#endif
#define FALCON_RELEASE_KERNEL_PREFIX FALCON_RELEASE_LABEL " kernel "

#endif
