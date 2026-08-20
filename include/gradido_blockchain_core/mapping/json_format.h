#ifndef GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FORMAT_H
#define GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup grdm_json_format grdm_json_format
 *  @ingroup mapping
 *  @brief How rendered JSON is laid out
 *
 *  One choice, shared by every mapping in this folder that produces JSON, so a caller that
 *  renders a wire structure and a runtime structure in the same breath names the layout once
 *  and means the same thing both times.
 *
 *  @whisper The same words, set either close together or spread out
 *  @{
 */

/** @brief Shape of the rendered text. */
typedef enum grdm_json_format {
  //! No whitespace between tokens -- the form to store or to send.
  GRDM_JSON_COMPACT = 0,
  //! Four space indent, one member per line -- the form to read.
  GRDM_JSON_PRETTY
} grdm_json_format;

/** @} */

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_CORE_MAPPING_JSON_FORMAT_H
