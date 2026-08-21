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
 *  The two enumerators below are the whole set of valid values. C hands an enum parameter over
 *  as a plain integer, so a caller can pass one that names neither -- a cast, a value read from
 *  configuration, a header from another version. Every rendering function refuses such a value
 *  with @c HOSTMEM_ERROR_ENUM_UNKNOWN, before any text is written and before its output block
 *  is touched, rather than falling back on compact and returning text nobody asked for.
 *
 *  The choice changes whitespace and nothing else: the same structure rendered either way
 *  carries the same members, in the same order, with the same texts. Rendering is
 *  deterministic -- one structure and one format always produce the same bytes, on every run
 *  and every platform -- because the members are written in a fixed order and every value goes
 *  through a conversion that has no locale and no floating point in it.
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
