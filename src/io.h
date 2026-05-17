/*
 * Copyright (c) 2013 Todd Mortimer
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef IO_H_
#define IO_H_

#include <stdint.h>
#include <sys/types.h>
#include <unicode/utf.h>

#include "types.h"

int io_init(pref_t *prefs);
void io_uninit(void);
void io_set_master(int master_fd);
int  io_get_master(void);
int32_t io_upcase_last_write(UChar **buf, int32_t nUChar);
ssize_t io_write_master(const UChar *buf, size_t nUChar);
ssize_t io_write_master_char(const char *buf, size_t n);
ssize_t io_read_master_raw(char *buf, size_t nbytes);
/* output is stored in the UChar buf, which must be of size utf8len */
ssize_t io_read_utf8_string(const char* utf8, size_t utf8len, UChar* buf);
void io_paste_from_clipboard(void);

#endif /* IO_H_ */
