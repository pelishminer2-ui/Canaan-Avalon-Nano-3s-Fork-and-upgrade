/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Canaan Creative Co., Ltd.
 *
 * For the licensing terms, see the LICENSE file (MIT License).
 *
 * NO WARRANTY. The Software is provided "AS IS", without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability, fitness for a
 * particular purpose and non-infringement. In no event shall the authors or copyright
 * holders be liable for any claim, damages or other liability arising from, out of or in
 * connection with the Software or the use or other dealings in the Software.
 */

#ifndef MM_TUNE_H
#define MM_TUNE_H

/* syscfg autotune: AUTOTUNE_OFF or AUTOTUNE_MILD. Default off. */
int mm_tune_autotune(void);
/* syscfg aftermarket_psu: 1 raises the stock PD software cap. Default off. */
int mm_tune_aftermarket(void);

#endif /* MM_TUNE_H */
