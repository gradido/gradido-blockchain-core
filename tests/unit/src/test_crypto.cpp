#include "gradido_blockchain_core/crypto/sign.h"
#include "gradido_blockchain_core/utils/converter.h"

#include "utils.h"
#include "gtest/gtest.h"

#include "memory_limit.h"
#include <string>

/*
 *hostmem_result grdc_sign_key_pair_copy_slip10_public_key(
   uint8_t slip10_public_key[SIGN_PUBLIC_KEY_SIZE+1],
   const grdc_sign_key_pair* sign_key_pair
 );
 */

std::string getSlip10PublicKeyHex(const grdc_sign_key_pair *keyPair) {
  uint8_t slip10_public_key[SIGN_PUBLIC_KEY_SIZE + 1];
  grdc_sign_key_pair_copy_slip10_public_key(slip10_public_key, keyPair);
  return toHex(slip10_public_key, SIGN_PUBLIC_KEY_SIZE + 1);
}

/*
 * hostmem_result sign_key_pair_slip10_derive_child(
   grdc_sign_key_pair* sign_key_pair,
   const grdc_sign_key_pair* sign_parent_key_pair,
   uint32_t index
 );
 */
// https://slips.readthedocs.io/en/latest/slip-0010/#test-vectors
TEST(TestEd25519Bip32, SLIP0010TestVectors1) {
  const char *seedString = "000102030405060708090a0b0c0d0e0f";
  const char *testPayload = "Test Payload for sign";
  grdc_sign_key_pair rootKeyPair;
  EXPECT_EQ(
      grdc_sign_key_pair_generate_from_seed(&rootKeyPair, fromHex(seedString).data(), 16),
      HOSTMEM_SUCCESS
  );

  // test root
  EXPECT_EQ(
      getSlip10PublicKeyHex(&rootKeyPair),
      "00a4b2856bfec510abab89753fac1ac0e1112364e7d250545963f135f2a33188ed"
  );
  EXPECT_EQ(
      toHex(rootKeyPair.chain_code),
      "90046a93de5380a72b5e45010748567d5ea02bbf6522f979e05c0d8d8ca9fffb"
  );
  EXPECT_EQ(
      toHex(rootKeyPair.seed), "2b4be7f19ee27bbf30c667b642d5f4aa69fd169872f8fc3059c08ebae2eb19e7"
  );

  // auto signature = root->sign(testPayload);
  // EXPECT_TRUE(root->verify(testPayload, signature.copyAsString()));

  // Chain m/0H
  grdc_sign_key_pair c0;
  EXPECT_EQ(grdc_sign_key_pair_derive(&c0, &rootKeyPair, 0), HOSTMEM_SUCCESS);
  EXPECT_EQ(
      getSlip10PublicKeyHex(&c0),
      "008c8a13df77a28f3445213a0f432fde644acaa215fc72dcdf300d5efaa85d350c"
  );
  EXPECT_EQ(
      toHex(c0.chain_code), "8b59aa11380b624e81507a27fedda59fea6d0b779a778918a2fd3590e16e9c69"
  );
  EXPECT_EQ(toHex(c0.seed), "68e0fe46dfb67e368c75379acec591dad19df3cde26e63b93a8e704f1dade7a3");

  // signature = c0->sign(testPayload);
  // EXPECT_TRUE(c0->verify(testPayload, signature.copyAsString()));

  // Chain m/0H/1
  grdc_sign_key_pair c01;
  EXPECT_EQ(grdc_sign_key_pair_derive(&c01, &c0, 1), HOSTMEM_SUCCESS);
  EXPECT_EQ(
      getSlip10PublicKeyHex(&c01),
      "001932a5270f335bed617d5b935c80aedb1a35bd9fc1e31acafd5372c30f5c1187"
  );
  EXPECT_EQ(
      toHex(c01.chain_code), "a320425f77d1b5c2505a6b1b27382b37368ee640e3557c315416801243552f14"
  );
  EXPECT_EQ(toHex(c01.seed), "b1d0bad404bf35da785a64ca1ac54b2617211d2777696fbffaf208f746ae84f2");

  // signature = c01->sign(testPayload);
  // EXPECT_TRUE(c01->verify(testPayload, signature.copyAsString()));

  // Chain m/0H/1/2H
  grdc_sign_key_pair c012;
  EXPECT_EQ(grdc_sign_key_pair_derive(&c012, &c01, 2), HOSTMEM_SUCCESS);
  EXPECT_EQ(
      getSlip10PublicKeyHex(&c012),
      "00ae98736566d30ed0e9d2f4486a64bc95740d89c7db33f52121f8ea8f76ff0fc1"
  );
  EXPECT_EQ(
      toHex(c012.chain_code), "2e69929e00b5ab250f49c3fb1c12f252de4fed2c1db88387094a0f8c4c9ccd6c"
  );
  EXPECT_EQ(toHex(c012.seed), "92a5b23c0b8a99e37d07df3fb9966917f5d06e02ddbd909c7e184371463e9fc9");

  // signature = c012->sign(testPayload);
  // EXPECT_TRUE(c012->verify(testPayload, signature.copyAsString()));

  // Chain m/0H/1/2H/2
  grdc_sign_key_pair c0122;
  EXPECT_EQ(grdc_sign_key_pair_derive(&c0122, &c012, 2), HOSTMEM_SUCCESS);
  EXPECT_EQ(
      getSlip10PublicKeyHex(&c0122),
      "008abae2d66361c879b900d204ad2cc4984fa2aa344dd7ddc46007329ac76c429c"
  );
  EXPECT_EQ(
      toHex(c0122.chain_code), "8f6d87f93d750e0efccda017d662a1b31a266e4a6f5993b15f5c1f07f74dd5cc"
  );
  EXPECT_EQ(toHex(c0122.seed), "30d1dc7e5fc04c31219ab25a27ae00b50f6fd66622f6e9c913253d6511d1e662");

  // signature = c0122->sign(testPayload);
  // EXPECT_TRUE(c0122->verify(testPayload, signature.copyAsString()));

  // Chain m/0H/1/2H/2/1000000000
  grdc_sign_key_pair c01221Mrd;
  EXPECT_EQ(grdc_sign_key_pair_derive(&c01221Mrd, &c0122, 1000000000), HOSTMEM_SUCCESS);
  EXPECT_EQ(
      getSlip10PublicKeyHex(&c01221Mrd),
      "003c24da049451555d51a7014a37337aa4e12d41e485abccfa46b47dfb2af54b7a"
  );
  EXPECT_EQ(
      toHex(c01221Mrd.chain_code),
      "68789923a0cac2cd5a29172a475fe9e0fb14cd6adb5ad98a3fa70333e7afa230"
  );
  EXPECT_EQ(
      toHex(c01221Mrd.seed), "8f94d394a8e8fd6b1bc2f3f49f5c47e385281d5c17e65324b0f62483e37e8793"
  );

  // signature = c01221Mrd->sign(testPayload);
  // EXPECT_TRUE(c01221Mrd->verify(testPayload, signature.copyAsString()));
}

TEST(TestEd25519Bip32, SLIP0010TestVectors2) {
  std::string hexSeed(
      "fffcf9f6f3f0edeae7e4e1dedbd8d5d2cfccc9c6c3c0bdbab7b4b1aeaba8a5a29f9c999693908d8a8784817e7b78"
      "75726f6c696663605d5a5754514e4b484542"
  );
  std::string testPayload = "Test Payload for sign2";
  grdc_sign_key_pair root;
  EXPECT_EQ(
      grdc_sign_key_pair_generate_from_seed(&root, fromHex(hexSeed.data()).data(), 64),
      HOSTMEM_SUCCESS
  );

  // test root
  EXPECT_EQ(
      getSlip10PublicKeyHex(&root),
      "008fe9693f8fa62a4305a140b9764c5ee01e455963744fe18204b4fb948249308a"
  );
  EXPECT_EQ(
      toHex(root.chain_code), "ef70a74db9c3a5af931b5fe73ed8e1a53464133654fd55e7a66f8570b8e33c3b"
  );
  EXPECT_EQ(toHex(root.seed), "171cb88b1b3c1db25add599712e36245d75bc65a1a5c9e18d76f9f2b1eab4012");

  // auto signature = root->sign(testPayload);
  // EXPECT_TRUE(root->verify(testPayload, signature.copyAsString()));

  // Chain m/0H
  grdc_sign_key_pair c0;
  EXPECT_EQ(grdc_sign_key_pair_derive(&c0, &root, 0), HOSTMEM_SUCCESS);
  EXPECT_EQ(
      getSlip10PublicKeyHex(&c0),
      "0086fab68dcb57aa196c77c5f264f215a112c22a912c10d123b0d03c3c28ef1037"
  );
  EXPECT_EQ(
      toHex(c0.chain_code), "0b78a3226f915c082bf118f83618a618ab6dec793752624cbeb622acb562862d"
  );
  EXPECT_EQ(toHex(c0.seed), "1559eb2bbec5790b0c65d8693e4d0875b1747f4970ae8b650486ed7470845635");

  // signature = c0->sign(testPayload);
  // EXPECT_TRUE(c0->verify(testPayload, signature.copyAsString()));

  // Chain m/0H/2147483647H
  grdc_sign_key_pair c0_2147483647;
  EXPECT_EQ(grdc_sign_key_pair_derive(&c0_2147483647, &c0, 2147483647), HOSTMEM_SUCCESS);
  EXPECT_EQ(
      getSlip10PublicKeyHex(&c0_2147483647),
      "005ba3b9ac6e90e83effcd25ac4e58a1365a9e35a3d3ae5eb07b9e4d90bcf7506d"
  );
  EXPECT_EQ(
      toHex(c0_2147483647.chain_code),
      "138f0b2551bcafeca6ff2aa88ba8ed0ed8de070841f0c4ef0165df8181eaad7f"
  );
  EXPECT_EQ(
      toHex(c0_2147483647.seed), "ea4f5bfe8694d8bb74b7b59404632fd5968b774ed545e810de9c32a4fb4192f4"
  );

  // signature = c0_2147483647->sign(testPayload);
  // EXPECT_TRUE(c0_2147483647->verify(testPayload, signature.copyAsString()));

  // Chain m/0H/2147483647H/1H
  grdc_sign_key_pair c0_2147483647_1;
  EXPECT_EQ(grdc_sign_key_pair_derive(&c0_2147483647_1, &c0_2147483647, 1), HOSTMEM_SUCCESS);
  EXPECT_EQ(
      getSlip10PublicKeyHex(&c0_2147483647_1),
      "002e66aa57069c86cc18249aecf5cb5a9cebbfd6fadeab056254763874a9352b45"
  );
  EXPECT_EQ(
      toHex(c0_2147483647_1.chain_code),
      "73bd9fff1cfbde33a1b846c27085f711c0fe2d66fd32e139d3ebc28e5a4a6b90"
  );
  EXPECT_EQ(
      toHex(c0_2147483647_1.seed),
      "3757c7577170179c7868353ada796c839135b3d30554bbb74a4b1e4a5a58505c"
  );

  // signature = c0_2147483647_1->sign(testPayload);
  // EXPECT_TRUE(c0_2147483647_1->verify(testPayload, signature.copyAsString()));

  // Chain m/0H/2147483647H/1H/2147483646H
  grdc_sign_key_pair c0_2147483647_1_2147483646;
  EXPECT_EQ(
      grdc_sign_key_pair_derive(&c0_2147483647_1_2147483646, &c0_2147483647_1, 2147483646),
      HOSTMEM_SUCCESS
  );
  EXPECT_EQ(
      getSlip10PublicKeyHex(&c0_2147483647_1_2147483646),
      "00e33c0f7d81d843c572275f287498e8d408654fdf0d1e065b84e2e6f157aab09b"
  );
  EXPECT_EQ(
      toHex(c0_2147483647_1_2147483646.chain_code),
      "0902fe8a29f9140480a00ef244bd183e8a13288e4412d8389d140aac1794825a"
  );
  EXPECT_EQ(
      toHex(c0_2147483647_1_2147483646.seed),
      "5837736c89570de861ebc173b1086da4f505d4adb387c6a1b1342d5e4ac9ec72"
  );

  // signature = c0_2147483647_1_2147483646->sign(testPayload);
  // EXPECT_TRUE(c0_2147483647_1_2147483646->verify(testPayload, signature.copyAsString()));

  // Chain m/0H/2147483647H/1H/2147483646H/2H
  grdc_sign_key_pair c0_2147483647_1_2147483646_2;
  EXPECT_EQ(
      grdc_sign_key_pair_derive(&c0_2147483647_1_2147483646_2, &c0_2147483647_1_2147483646, 2),
      HOSTMEM_SUCCESS
  );
  EXPECT_EQ(
      getSlip10PublicKeyHex(&c0_2147483647_1_2147483646_2),
      "0047150c75db263559a70d5778bf36abbab30fb061ad69f69ece61a72b0cfa4fc0"
  );
  EXPECT_EQ(
      toHex(c0_2147483647_1_2147483646_2.chain_code),
      "5d70af781f3a37b829f0d060924d5e960bdc02e85423494afc0b1a41bbe196d4"
  );
  EXPECT_EQ(
      toHex(c0_2147483647_1_2147483646_2.seed),
      "551d333177df541ad876a60ea71f00447931c0a9da16f227c11ea080d7391b8d"
  );

  // signature = c0_2147483647_1_2147483646_2->sign(testPayload);
  // EXPECT_TRUE(c0_2147483647_1_2147483646_2->verify(testPayload, signature.copyAsString()));
}
