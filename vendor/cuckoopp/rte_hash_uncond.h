/**
 * Copyright (c) 2018 - Present – Thomson Licensing, SAS
 * All rights reserved.
 *
 * This source code is licensed under the Clear BSD license found in the
 * LICENSE.md file in the root directory of this source tree.
 */

#ifndef LIBRTE_TCH_HASH_RTE_HASH_UNCOND_H_
#define LIBRTE_TCH_HASH_RTE_HASH_UNCOND_H_

#ifdef H
#undef H
#undef BLOOM
#undef HORTON
#undef UNCONDITIONAL_PREFETCH
#undef CONDITIONAL_PREFETCH
#undef NO_PREFETCH
#undef TIMER
#endif

#define H(x,y) x##_uncond_##y


/* Configuration Flags */
/* A. When to prefetch bucket */
#define BLOOM 0
#define HORTON 0
#define UNCONDITIONAL_PREFETCH 1
#define CONDITIONAL_PREFETCH 0
#define NO_PREFETCH 0
/* B. Enable time management or not */
#define TIMER 0


#include "rte_hash_template.h"


#endif
