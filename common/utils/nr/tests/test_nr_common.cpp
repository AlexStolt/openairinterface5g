/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <gtest/gtest.h>
extern "C" {
#include "nr_common.h"
#include "common/utils/LOG/log.h"
}

TEST(nr_common, nr_timer) {
  NR_timer_t timer;
  nr_timer_setup(&timer, 10, 1);
  nr_timer_start(&timer);
  EXPECT_TRUE(nr_timer_is_active(&timer));
  EXPECT_FALSE(nr_timer_expired(&timer));
  for (auto i = 0; i < 10; i++) {
    nr_timer_tick(&timer);
  }
  EXPECT_FALSE(nr_timer_is_active(&timer));
  EXPECT_TRUE(nr_timer_expired(&timer));
}

// TS 38.214 §5.1.2.2.3: largest K in {1, 2, 4, 6, 8, 10, 12} with K <= N_CFR / N_initial
TEST(nr_common, multicast_type1_rb_granularity) {
  EXPECT_EQ(get_multicast_type1_rb_granularity(48, 48), 1);
  EXPECT_EQ(get_multicast_type1_rb_granularity(24, 48), 1);
  EXPECT_EQ(get_multicast_type1_rb_granularity(106, 48), 2);
  EXPECT_EQ(get_multicast_type1_rb_granularity(273, 48), 4);
  EXPECT_EQ(get_multicast_type1_rb_granularity(150, 24), 6);
  EXPECT_EQ(get_multicast_type1_rb_granularity(275, 24), 10);
  EXPECT_EQ(get_multicast_type1_rb_granularity(273, 22), 12);
}


int main(int argc, char **argv)
{
  logInit();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
