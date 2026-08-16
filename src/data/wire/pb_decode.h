#ifndef GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_PB_DECODE_H
#define GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_PB_DECODE_H

#include "hostmem/memory.h"
#include "hostmem/memory_block.h"
#include "hostmem/result.h"

#include <stdint.h>

/**
 * @file
 * @brief The half of a wire decode that does not depend on the message type.
 *
 * Internal to `src/data/wire`, not part of the public API.
 *
 * All three decoders in this folder run the same course: take what is left of the arena as
 * workspace for pbtools, let the generated code decode into it, hand the unused tail back, and
 * translate pbtools' answer into a @ref hostmem_result. Only the three generated calls in
 * between differ by message type. The course itself lives here, because it was copied three
 * times and every misjudgement in it had to be found three times.
 *
 * @note **A failing decode keeps its workspace on purpose.** These decoders are meant to run on
 * a static arena that the caller resets after the call, so nothing is lost — and what pbtools
 * managed to write before it gave up is exactly what someone wants to read when a message will
 * not decode. Releasing it on the error paths looks tidier and destroys the evidence. It has
 * been proposed more than once; the answer is still no.
 *
 * @whisper What the failed reading left behind is the trace that explains it
 */

/**
 * @brief Claim the arena's remaining space as workspace for pbtools.
 *
 * pbtools allocates from a buffer it is handed rather than from the heap, and how much a
 * message needs is not known before it is decoded — so it gets everything that is free.
 *
 * @param[out]    workspace Receives the block; untouched on failure.
 * @param[in,out] allocator Arena to draw from; must not be NULL and must be in arena mode.
 * @retval HOSTMEM_SUCCESS              Workspace claimed.
 * @retval HOSTMEM_ERROR_INVALID_PARAM  @p allocator is in default mode and has nothing to lend.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY  The arena is already full.
 */
hostmem_result grdw_pb_workspace_take(hostmem_memory_block *workspace, hostmem *allocator);

/**
 * @brief Return the unused tail of the workspace and judge what pbtools reported.
 *
 * Shrinking the workspace to what was actually written is the last step of a decode; an arena
 * that refuses it leaves address and recorded size in place, which is a completed resize and
 * not a failure — the block still knows the size it must be released with.
 *
 * @param[in,out] workspace     Block from grdw_pb_workspace_take(); shrunk in place.
 * @param[in]     used_bytes    Bytes pbtools wrote, i.e. `proto->base.heap_p->pos`.
 * @param[in]     decoded_bytes What the generated decode returned: bytes consumed, or a
 *                              negated PBTOOLS_* code.
 * @param[in]     source_size   Bytes handed in. A decode that consumed a different number read
 *                              a message that does not match its own length.
 * @param[in,out] allocator     Allocator the workspace came from.
 * @retval HOSTMEM_SUCCESS              Decode complete and consistent.
 * @retval HOSTMEM_ERROR_OUT_OF_MEMORY  pbtools ran out of workspace.
 * @retval HOSTMEM_ERROR_DECODE_FAILED  Malformed message, or a length that does not add up.
 * @retval Anything hostmem_realloc() reports as an error.
 */
hostmem_result grdw_pb_decode_finish(
    hostmem_memory_block *workspace,
    int used_bytes,
    int decoded_bytes,
    uint32_t source_size,
    hostmem *allocator
);

#endif // GRADIDO_BLOCKCHAIN_CORE_DATA_WIRE_PB_DECODE_H
