/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include "gtest/gtest.h"

extern "C" int test_dci_4_1_size_vs_1_0(int cset0_bwp_size);
extern "C" bool test_dci_4_1_pack(int cset0_bwp_size);

// parameter: size of CORESET 0 in RBs (TS 38.213 Tables 13-1 to 13-10)
class DciFormat41 : public testing::TestWithParam<int> {};

// TS 38.212 §7.3.1.5.2: same size as DCI format 1_0 in a CSS, the UE monitors both without size alignment
TEST_P(DciFormat41, SameSizeAsFormat10InCss)
{
  EXPECT_EQ(test_dci_4_1_size_vs_1_0(GetParam()), 0);
}

TEST_P(DciFormat41, FieldPositions)
{
  EXPECT_TRUE(test_dci_4_1_pack(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(Coreset0Size, DciFormat41, testing::Values(24, 48, 96), testing::PrintToStringParamName());
