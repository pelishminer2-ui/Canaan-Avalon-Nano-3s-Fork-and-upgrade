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

#ifndef MM_UI_CFG_H
#define MM_UI_CFG_H

#include <stdint.h>

void mm_ui_cfg_load(uint16_t *show, int *onoff, int *pelish);
int  mm_ui_cfg_save(uint16_t show, int onoff, int pelish);

#endif /* MM_UI_CFG_H */
