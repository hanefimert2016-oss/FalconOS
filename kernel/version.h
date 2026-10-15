/* FalconOS v2-beta -- single authoritative, user-visible release identity.
 * This header exists in v2-beta only; V1 release remains unchanged. */
#ifndef FALCON_VERSION_H
#define FALCON_VERSION_H

/* Release artifacts are built from the same reviewed source, with an
 * explicit immutable OS revision marker that the updater checks on reboot. */
#if defined(FALCON_OS_UPDATE_VERSION) && FALCON_OS_UPDATE_VERSION == 202
#define FALCON_RELEASE_LABEL         "FalconOS 2.0.2 Preview"
#define FALCON_RELEASE_SEMVER        "2.0.2-preview"
#elif defined(FALCON_OS_UPDATE_VERSION) && FALCON_OS_UPDATE_VERSION == 201
#define FALCON_RELEASE_LABEL         "FalconOS 2.0.1 Preview"
#define FALCON_RELEASE_SEMVER        "2.0.1-preview"
#else
#define FALCON_RELEASE_LABEL         "FalconOS 2.0 Preview"
#define FALCON_RELEASE_SEMVER        "2.0.0-preview"
#endif
#define FALCON_RELEASE_BADGE         "V2 PREVIEW"
#define FALCON_RELEASE_CHANNEL       "v2"
#define FALCON_RELEASE_KERNEL_PREFIX FALCON_RELEASE_LABEL " kernel "

#endif
