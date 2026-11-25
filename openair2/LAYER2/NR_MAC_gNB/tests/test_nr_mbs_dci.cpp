/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <tuple>
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

extern "C" bool test_dci_4_2_pack(int cfr_size, int tda_count, int k1_count);

class DciFormat42 : public testing::TestWithParam<std::tuple<int, int, int>> {};

TEST_P(DciFormat42, FieldPositionsAndUeExtraction)
{
  EXPECT_TRUE(test_dci_4_2_pack(std::get<0>(GetParam()), std::get<1>(GetParam()), std::get<2>(GetParam())));
}

INSTANTIATE_TEST_SUITE_P(CfrTdaAndK1, DciFormat42,
                        testing::Combine(testing::Values(24, 48, 106, 275), testing::Values(1, 3, 16), testing::Values(1, 5, 8)));

extern "C" bool test_dci_4_0_pack(int cset0_bwp_size);

// parameter: size of CORESET 0 in RBs (TS 38.213 Tables 13-1 to 13-10)
class DciFormat40 : public testing::TestWithParam<int> {};

// TS 38.212 §7.3.1.5.1: fields at their positions, read back by the UE; they add up to the size of DCI formats 4_1 and
// 1_0 in a CSS, so the UE monitors no new DCI size
TEST_P(DciFormat40, FieldPositionsSizeAndUeExtraction)
{
  EXPECT_TRUE(test_dci_4_0_pack(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(Coreset0Size, DciFormat40, testing::Values(24, 48, 96), testing::PrintToStringParamName());

extern "C" bool test_dci_4_0_mcch_change(int cset0_bwp_size, int change);

class DciFormat40Mcch : public testing::TestWithParam<std::tuple<int, int>> {};

// TS 38.212 §7.3.1.5.1: MCCH change notification with MCCH-RNTI, reserved with G-RNTI
TEST_P(DciFormat40Mcch, ChangeNotification)
{
  EXPECT_TRUE(test_dci_4_0_mcch_change(std::get<0>(GetParam()), std::get<1>(GetParam())));
}

INSTANTIATE_TEST_SUITE_P(Coreset0SizeAndChange, DciFormat40Mcch, testing::Combine(testing::Values(24, 48, 96), testing::Values(0, 1, 2, 3)));

extern "C" bool test_mcch_message_and_sib20(int num_sessions);

class McchMessage : public testing::TestWithParam<int> {};

// TS 38.331 §6.2.1, §6.3.1: MCCH-Message with the sessions of the cell, SIB20 pointing at the MCCH
TEST_P(McchMessage, EncodeDecodeAndSib20)
{
  EXPECT_TRUE(test_mcch_message_and_sib20(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(Sessions, McchMessage, testing::Values(0, 1, 3, 16), testing::PrintToStringParamName());
