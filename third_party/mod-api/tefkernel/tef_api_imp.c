/*******************************************************************************
* File: tef_api_imp
 * Project: tefkernel
 * Created: 2026/06/06
 * Author: eternalfuture-e38299
 * Github: https://github.com/eternalfuture-e38299
 *
 * MIT License
 *
 * Copyright (c) 2025 eternalfuture-e38299
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *******************************************************************************/

#define TEF_API_IMPL 1

#include "tef_api.h"
#include "patchlib/field.h"
#include "patchlib/method.h"
#include "patchlib/property.h"
#include "patchlib/type.h"
#include "patchlib/struct/array.h"
#include "patchlib/struct/dictionary.h"
#include "patchlib/struct/string.h"
#include "patchlib/struct/list.h"
#include "tefstd/vector.h"
#include "tefstd/hashmap.h"
#include "tefstd/skipmap.h"
#include "memdl/memdl.h"
#include "modloader/modloader_core.h"
#include "module/module_core.h"
#include "tefpackage/tefpkg.h"
#include "tefplugin/tpf_core.h"

/* 本文件在 Android 头文件里才会导出该函数指针；本地 Zig musl 交叉编译时
 * 需补一个定义，避免 .so 携带未定义符号导致 TEFKernel 无法加载模组。 */
#if !defined(__ANDROID__)
DEFINE_FUNCTION(void *, patchlib_field_get_pointer, patch_handle_t field,
                void *instance)
#endif
