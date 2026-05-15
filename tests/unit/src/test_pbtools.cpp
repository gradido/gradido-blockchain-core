#include <gtest/gtest.h>
#include <string.h>

#include "gradido_blockchain_core/data/wire/gradido_transaction.h"
#include "gradido_blockchain_core/data/wire/transaction_body.h"
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include "gradido_blockchain_core/utils/mono_timer.h"
#include "sodium.h"

// contain only createAt and version string
constexpr auto emptyTransactionBodyBase64 = "CgASCAiAzLn/BRAAGgMzLjMgAA==";
constexpr auto communityRootTransactionBase64 =
    "CmYKZAogqtErqCa5EURXVUxMVX+jqHivnyH4FiGww+YWFQYRKCkSQEmnjV7+"
    "r6Y7z0CrSceGF7xwvXFX4Hej50MV2W4ekQSJ53kekrjE5JWzGrG9K8RCnJrSoIXlXFYUcR38gn98/"
    "QoSdRIGCIDMuf8FGgMzLjVaZgogqtErqCa5EURXVUxMVX+jqHivnyH4FiGww+"
    "YWFQYRKCkSIBS4pthWO7s1l999gnW3C1agRhpbeR1jwIzAWUfSBhyJGiBGb91zu4FifzYPyOaLPep/"
    "wCCUs6bxzSrfvTmk8y4lnhoA";
constexpr auto registeAddressTransactionBase64 =
    "CrICCmQKIKrRK6gmuRFEV1VMTFV/"
    "o6h4r58h+BYhsMPmFhUGESgpEkDdBYnw6FWxdQXZ+"
    "K9cnXB3JDJFQT8KVhC1yqgnTKNuLgjiyxK6K9vzZEeFHiE6wXoxQ7o4wVPLnEd8QVItATQHCmQKILxxJbfkt7U00jLCFcL"
    "LzjRAiGBKDoruOTLpv0FqBs/+EkDc28tzP3cvgcwM4xCEktvEhPw0ikUb2xTSUa3zG5N5dKxMJ5GPJS/"
    "n78pLejfjQ8OiVm8NP7LP9YXZAyHlT8MFCmQKIO/ykZNQpTrID4mAYrfGf/gCgkX0zETgjvIviFFxVU87EkC4J/"
    "ByDZPNlb6Jgi39rH+B3YgbgITV2kL0S6HmspFymCouglWwR24ym7+c6J0CSfOyFTiSNAqLhzsGxCmc6doOEnkSBgiAzLn/"
    "BRoDMy41SmoKILxxJbfkt7U00jLCFcLLzjRAiGBKDoruOTLpv0FqBs/+EAEaIB/"
    "kH3lmpLYaljiGoPpNE22FeUJNnAKidR77oOjlVZ9hIiDv8pGTUKU6yA+JgGK3xn/4AoJF9MxE4I7yL4hRcVVPOygBGgA=";
constexpr auto creationTransactionBase64 =
    "CmYKZAogyy3XdFIKQdmOo6znQO6zP6F5nXcyrbd24930wGPWAbwSQLoWkuWdqFViwdGaETYuCAGtASEcfNc9dMocRAylm7"
    "S73bqNYsNuuqCp9fQeWNG4ZpIgO5ksd4b9Xq1gkKXV8wESXwodCAISGURlaW5lIGVyc3RlIFNjaG9lcGZ1bmcgOykSBgiA"
    "zLn/BRoDMy41OjEKJwog7/KRk1ClOsgPiYBit8Z/+AKCRfTMROCO8i+IUXFVTzsQgK3iBBoGCLjKuf8FGgA=";
constexpr auto transferTransactionBase64 =
    "CmYKZAog7/KRk1ClOsgPiYBit8Z/+AKCRfTMROCO8i+IUXFVTzsSQE77i3/OM2356rKFJYaktfYQpVTj3a0U/"
    "D+G5rYQ9L8mcU+OtSYG0sx/LLtBDB9gn/"
    "9Pu6NlHkVLib4fj5BXywAScQoVCAISEUljaCB0ZWlsZSBtaXQgZGlyEgYIgMy5/wUaAzMuNTJLCicKIO/"
    "ykZNQpTrID4mAYrfGf/gCgkX0zETgjvIviFFxVU87ELzBsQISIH71fuNOTK5eAUA0t/"
    "gEO6ZA0aJvB9YirDQuZK11ufYNGgA=";
constexpr auto deferredTransferTransactionBase64 =
    "CmYKZAog7/KRk1ClOsgPiYBit8Z/+AKCRfTMROCO8i+IUXFVTzsSQLNxKNCE73Y/"
    "6R6DCWPghfgrHGyI3j7SaDTDbRq2EJbrcDpMQbU7zNUffvatlheOJltUXy8++KFI0GFy/"
    "0JMZAsSewoWCAISEkxpbmsgenVtIGVpbmxvZXNlbhIGCIDMuf8FGgMzLjVSVApLCicKIO/ykZNQpTrID4mAYrfGf/"
    "gCgkX0zETgjvIviFFxVU87EKyK0wISIH71fuNOTK5eAUA0t/gEO6ZA0aJvB9YirDQuZK11ufYNEgUI1sLhAxoA";
constexpr auto communityFriendsUpdateBase64 =
    "CmYKZAogqtErqCa5EURXVUxMVX+jqHivnyH4FiGww+"
    "YWFQYRKCkSQP7aJIvxwxyoUFMAOXQsI5tBWJtr5RxDMiUKJKNUOxJsY3aR8z16kzuYamk9/"
    "kPuMVS8NHCEzVcbEFCqQTAqYQoSERIGCIDMuf8FGgMzLjVCAggBGgA=";

grd_memory_block fromBase64(
    const char *base64String, size_t size, int variant = sodium_base64_VARIANT_ORIGINAL
)
{
  grd_memory_block result{};
  size_t binSize = (size / 4) * 3;

  uint8_t *buffer = (uint8_t *)malloc(binSize);
  if (!buffer) { return result; }
  size_t resultBinSize = 0;
  const char *firstInvalidByte = nullptr;
  auto convertResult = sodium_base642bin(
      buffer, binSize, base64String, size, nullptr, &resultBinSize, &firstInvalidByte, variant
  );
  if (0 != convertResult) {
    printf("invalid base64: error at: %lld\n", firstInvalidByte - base64String);
  }
  if (resultBinSize < binSize) {
    result.data = (uint8_t *)malloc(resultBinSize);
    if (!result.data) { return result; }
    memcpy(result.data, buffer, resultBinSize);
    free(buffer);
  } else {
    result.data = buffer;
  }
  result.size = resultBinSize;
  return result;
}

TEST(PBToolsTest, TransactionBody_Decode)
{
  grd_memory mem;
  grdw_transaction_body body{};
  auto bin = fromBase64(emptyTransactionBodyBase64, strlen(emptyTransactionBodyBase64));
  EXPECT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  grdw_transaction_body_decode(&body, &bin, &mem);
  grd_memory_free(&mem);
}

TEST(PBToolsTest, GradidoTransaction_Decode_CommunityRoot)
{
  grdu_mono_timer timeUsed;
  grd_memory mem;
  grdw_gradido_transaction tx{};
  grdu_mono_timer_reset(&timeUsed);
  auto bin = fromBase64(communityRootTransactionBase64, strlen(communityRootTransactionBase64));
  EXPECT_EQ(grd_memory_init_arena(&mem, 2048), GRD_SUCCESS);
  char buffer[256];
  grdu_mono_timer_reset(&timeUsed);
  for (int i = 0; i < 10; i++) {
    grd_memory_reset(&mem);
    EXPECT_EQ(grdw_gradido_transaction_decode(&tx, &bin, &mem), GRD_SUCCESS);
  }

  grdu_mono_timer_string(buffer, 256, timeUsed);
  printf("time for decode community root: %s\n", buffer);

  grd_memory_reset(&mem);
  grdw_transaction_body body{};
  grdu_mono_timer_reset(&timeUsed);

  EXPECT_EQ(grdw_transaction_body_decode(&body, &tx.body_bytes, &mem), GRD_SUCCESS);

  grdu_mono_timer_string(buffer, 256, timeUsed);
  printf("time for decode transaction body: %s\n", buffer);

  grd_memory_free(&mem);
}
