/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_LOONGARCH_IMAGE_H
#define _ASM_LOONGARCH_IMAGE_H

#include <linux/pe.h>

#ifdef CONFIG_EFI_STUB
#define LOONGARCH_IMAGE_MAGIC	LINUX_PE_MAGIC
#else
#ifdef CONFIG_64BIT
#define LOONGARCH_IMAGE_MAGIC	0x3436414C /* 4C 41 36 34 "LA64" */
#else
#define LOONGARCH_IMAGE_MAGIC	0x3233414C /* 4C 41 33 32 "LA32" */
#endif
#endif

#define LOONGARCH_IMAGE_FLAG_BE_SHIFT	0
#define LOONGARCH_IMAGE_FLAG_PAGE_SIZE_SHIFT \
		(LOONGARCH_IMAGE_FLAG_BE_SHIFT + 1)
#define LOONGARCH_IMAGE_FLAG_RELOCATABLE_SHIFT \
		(LOONGARCH_IMAGE_FLAG_PAGE_SIZE_SHIFT + 2)
#define LOONGARCH_IMAGE_FLAG_BE_MASK		0x1
#define LOONGARCH_IMAGE_FLAG_BE_MASK		0x1
#define LOONGARCH_IMAGE_FLAG_PAGE_SIZE_MASK	0x3
#define LOONGARCH_IMAGE_RELOCATABLE_MASK	0x1

#define LOONGARCH_IMAGE_FLAG_LE			0
#define LOONGARCH_IMAGE_FLAG_BE			1
#define LOONGARCH_IMAGE_FLAG_PAGE_SIZE_4K	1
#define LOONGARCH_IMAGE_FLAG_PAGE_SIZE_16K	2
#define LOONGARCH_IMAGE_FLAG_PAGE_SIZE_64K	3
#define LOONGARCH_IMAGE_FLAG_RELOCATABLE	1

#define __HEAD_FLAG_BE		0
#define __HEAD_FLAG_PAGE_SIZE	((CONFIG_PAGE_SHIFT - 10) / 2)
#ifdef CONFIG_RELOCATABLE
#define __HEAD_FLAG_RELOCATABLE	1
#else
#define __HEAD_FLAG_RELOCATABLE	0
#endif

#define __HEAD_FLAG(field)	(__HEAD_FLAG_##field << \
				LOONGARCH_IMAGE_FLAG_##field##_SHIFT)

#define __HEAD_FLAGS		(__HEAD_FLAG(BE) || \
				 __HEAD_FLAG(PAGE_SIZE) || \
				 __HEAD_FLAG(RELOCATABLE))

#define LOONGARCH_HEADER_VERSION_MAJOR 0
#define LOONGARCH_HEADER_VERSION_MINOR 1

#define LOONGARCH_HEADER_VERSION (LOONGARCH_HEADER_VERSION_MAJOR << 16 | \
			      LOONGARCH_HEADER_VERSION_MINOR)

#ifndef __ASSEMBLY__
/**
 * struct loongarch_image_header - loongarch kernel image header
 * @code0:              Executable code, contains "MZ" for EFI Stub
 * @code1:              Executable code
 * @kernel_entry:       Kernel entry point
 * @image_size:         Effective Image size
 * @load_offset:        Kernel image load offset from start of RAM
 * @flags:              Image flags
 * @version:            Version
 * @res1:               Reserved
 * @res2:               Reserved
 * @magic:              Magic (LINUX_PE_MAGIC for EFI Stub or "LA32"/"LA64")
 * @pe_offset:          Offset to the PE header if exists, otherwise 0
 *
 */

struct loongarch_image_header {
	u32 code0;
	u32 code1;
	u64 kernel_entry;
	u64 image_size;
	u64 load_offset;
	u32 flags;
	u32 version;
	u64 res1;
	u64 res2;
	u32 magic;
	u32 pe_offset;
};
#endif /* __ASSEMBLY__ */

#endif /* _ASM_LOONGARCH_IMAGE_H */
