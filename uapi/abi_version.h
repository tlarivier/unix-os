#ifndef UAPI_ABI_VERSION_H
#define UAPI_ABI_VERSION_H

#define UNIXOS_ABI_MAJOR 1
#define UNIXOS_ABI_MINOR 0
#define UNIXOS_ABI_PATCH 0

/* Combined version number: (major << 16) | (minor << 8) | patch */
#define UNIXOS_ABI_VERSION \
    ((UNIXOS_ABI_MAJOR << 16) | (UNIXOS_ABI_MINOR << 8) | UNIXOS_ABI_PATCH)

#define UNIXOS_ABI_VERSION_STR "1.0.0"

#define ABI_VERSION_MAJOR(v) (((v) >> 16) & 0xFF)
#define ABI_VERSION_MINOR(v) (((v) >> 8) & 0xFF)
#define ABI_VERSION_PATCH(v) ((v) & 0xFF)

#define ABI_COMPATIBLE(v) \
    (ABI_VERSION_MAJOR(v) == UNIXOS_ABI_MAJOR && \
     ABI_VERSION_MINOR(v) <= UNIXOS_ABI_MINOR)

#define __NR_abi_version 254

#endif 
