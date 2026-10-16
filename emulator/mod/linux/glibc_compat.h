// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// Forced into every source of libbbsc.so by build-mod.sh. The mod runs on the glibc bb-probe was
// built with (BBport's AppImage brings 2.42), not on the one it is built on: glibc 2.43 gave these
// math functions new symbol versions, which an older glibc does not have, so the library would not
// load. Their first versions are in every glibc since.
#pragma once

__asm__(".symver asinf,asinf@GLIBC_2.2.5");
__asm__(".symver atan2f,atan2f@GLIBC_2.2.5");
__asm__(".symver sqrtf,sqrtf@GLIBC_2.2.5");
