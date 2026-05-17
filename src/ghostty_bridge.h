#ifndef GHOSTTY_BRIDGE_H_
#define GHOSTTY_BRIDGE_H_

#include <stddef.h>
#include <stdint.h>

int ghostty_bridge_init(uint16_t cols, uint16_t rows, size_t max_scrollback);
void ghostty_bridge_uninit(void);
void ghostty_bridge_write(const uint8_t *data, size_t len);
int ghostty_bridge_resize(uint16_t cols, uint16_t rows,
                          uint32_t cell_width_px, uint32_t cell_height_px);
int ghostty_bridge_update_render_state(void);
int ghostty_bridge_link_probe(void);

#endif /* GHOSTTY_BRIDGE_H_ */
