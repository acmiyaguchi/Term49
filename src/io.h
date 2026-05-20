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

/* Process-scoped converters + the keyboard-write upcase buffer. The pty
 * master fd lives on session_t now; callers thread it through here. The
 * upcase buffer stays global: it tracks "the last keystroke we wrote",
 * which is by construction the active session's write path. */
int io_init(pref_t *prefs);
void io_uninit(void);
int32_t io_upcase_last_write(UChar **buf, int32_t nUChar);
ssize_t io_write_master(int fd, const UChar *buf, size_t nUChar);
ssize_t io_write_master_char(int fd, const char *buf, size_t n);
ssize_t io_read_master_raw(int fd, char *buf, size_t nbytes);
/* output is stored in the UChar buf, which must be of size utf8len */
ssize_t io_read_utf8_string(const char* utf8, size_t utf8len, UChar* buf);
void io_paste_from_clipboard(int fd);

#endif /* IO_H_ */
