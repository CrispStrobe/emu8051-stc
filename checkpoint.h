#ifndef EMU8051_CHECKPOINT_H
#define EMU8051_CHECKPOINT_H

#include "debug.h"
#include "emu8051.h"
#include "stc12.h"
#include <stdint.h>

#define EMU_CHECKPOINT_VERSION 1u
/* Layout fingerprint, not a source revision. Change for any incompatible field
 * change. */
#define EMU_CHECKPOINT_BUILD_ID 0x80510101u

enum emu_checkpoint_result {
  EMU_CHECKPOINT_OK = 0,
  EMU_CHECKPOINT_NOT_INITIALIZED = -1,
  EMU_CHECKPOINT_NULL_BUFFER = -2,
  EMU_CHECKPOINT_WRONG_LENGTH = -3,
  EMU_CHECKPOINT_MALFORMED = -4,
  EMU_CHECKPOINT_UNSUPPORTED_VERSION = -5,
  EMU_CHECKPOINT_INCOMPATIBLE_BUILD = -6,
  EMU_CHECKPOINT_INVALID_STATE = -7,
  EMU_CHECKPOINT_ALLOCATION_FAILED = -8
};

uint32_t emu_checkpoint_codec_size(void);
int emu_checkpoint_encode(const struct em8051 *, const struct stc12_state *,
                          const struct dbg_target *, int, uint8_t *, uint32_t);
int emu_checkpoint_decode(struct em8051 *, struct stc12_state *,
                          struct dbg_target *, struct dbg_task_pos *, int *,
                          const uint8_t *, uint32_t);

#endif
