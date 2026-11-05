/* tools.h - مجمع and رابط used inside مترجم (decision 37: one command). */
#ifndef CC_TOOLS_H
#define CC_TOOLS_H

#include <stddef.h>
#include <stdint.h>

/* Assembles text (Arabic assembly) into a .كائن at out, with the encoded
 * interface region. name is used in diagnostics. Returns 0 on success. */
int tool_assemble(const uint8_t *text, size_t len, const char *name, const char *out,
                  const uint8_t *iface, size_t iface_len);

/* Links the inputs (objects, archives) into the executable out. */
int tool_link(const char *out, const char *const *inputs, size_t n, int bare);

#endif
