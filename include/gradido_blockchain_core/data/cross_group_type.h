#ifndef GRADIDO_BLOCKCHAIN_C_DATA_CROSS_GROUP_TYPE_H
#define GRADIDO_BLOCKCHAIN_C_DATA_CROSS_GROUP_TYPE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum grdd_cross_group_type
{
  GRDD_CROSS_GROUP_TYPE_LOCAL = 0,
  GRDD_CROSS_GROUP_TYPE_INBOUND = 1,
  GRDD_CROSS_GROUP_TYPE_OUTBOUND = 2,
  GRDD_CROSS_GROUP_TYPE_CROSS = 3
} grdd_cross_group_type;

#ifdef __cplusplus
}
#endif

#endif // GRADIDO_BLOCKCHAIN_C_DATA_CROSS_GROUP_TYPE_H