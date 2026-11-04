/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* DCI format 4_1 (TS 38.212 §7.3.1.5.2) scheduling multicast PDSCH with CRC scrambled by G-RNTI */

#include <stdbool.h>
#include <string.h>

#include "common/utils/bits.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "LAYER2/NR_MAC_COMMON/nr_mac_common.h"

static NR_SearchSpace_t *common_search_space(void)
{
  static struct NR_SearchSpace__searchSpaceType type = {.present = NR_SearchSpace__searchSpaceType_PR_common};
  static NR_SearchSpace_t ss = {.searchSpaceType = &type};
  return &ss;
}

// returns the size of DCI format 4_1 minus the size of DCI format 1_0 with C-RNTI in a CSS
int test_dci_4_1_size_vs_1_0(int cset0_bwp_size)
{
  dci_pdu_rel15_t dci_41 = {0};
  dci_pdu_rel15_t dci_10 = {0};
  const int ss_type = NR_SearchSpace__searchSpaceType_PR_common;
  int size_41 = nr_dci_size(NULL, NULL, NULL, 0, &dci_41, NR_DL_DCI_FORMAT_4_1, TYPE_G_RNTI_, NULL, ss_type, cset0_bwp_size, 0);
  int size_10 = nr_dci_size(NULL, NULL, NULL, 0, &dci_10, NR_DL_DCI_FORMAT_1_0, TYPE_C_RNTI_, NULL, ss_type, cset0_bwp_size, 0);
  return size_41 - size_10;
}

static uint32_t read_field(uint64_t payload, int size, int *pos, int nbits)
{
  uint32_t val = (payload >> (size - *pos - nbits)) & ((1ULL << nbits) - 1);
  *pos += nbits;
  return val;
}

// packs a DCI format 4_1 and reads back every field at the position given by TS 38.212 §7.3.1.5.2
bool test_dci_4_1_pack(int cset0_bwp_size)
{
  dci_pdu_rel15_t dci = {0};
  const int fda_bits = ceil_log2_u32((cset0_bwp_size * (cset0_bwp_size + 1)) >> 1);
  dci.frequency_domain_assignment.val = ((1U << fda_bits) - 1) & 0x2a5a5;
  dci.time_domain_assignment.val = 0x9;
  dci.vrb_to_prb_mapping.val = 1;
  dci.mcs = 0x13;
  dci.ndi = 1;
  dci.rv = 0x2;
  dci.harq_pid.val = 0xb;
  dci.dai[0].val = 0x1;
  dci.pucch_resource_indicator = 0x5;
  dci.pdsch_to_harq_feedback_timing_indicator.val = 0x6;

  nfapi_nr_dl_dci_pdu_t pdu = {0};
  fill_dci_pdu_rel15(NULL,
                     NULL,
                     NULL,
                     &pdu,
                     &dci,
                     NR_DL_DCI_FORMAT_4_1,
                     TYPE_G_RNTI_,
                     0,
                     common_search_space(),
                     NULL,
                     0,
                     cset0_bwp_size);

  const int size = pdu.PayloadSizeBits;
  if (size != fda_bits + 28)
    return false;
  uint64_t payload;
  memcpy(&payload, pdu.Payload, sizeof(payload));
  int pos = 0;
  bool ok = read_field(payload, size, &pos, fda_bits) == dci.frequency_domain_assignment.val;
  ok &= read_field(payload, size, &pos, 4) == dci.time_domain_assignment.val;
  ok &= read_field(payload, size, &pos, 1) == dci.vrb_to_prb_mapping.val;
  ok &= read_field(payload, size, &pos, 5) == dci.mcs;
  ok &= read_field(payload, size, &pos, 1) == dci.ndi;
  ok &= read_field(payload, size, &pos, 2) == dci.rv;
  ok &= read_field(payload, size, &pos, 4) == dci.harq_pid.val;
  ok &= read_field(payload, size, &pos, 2) == dci.dai[0].val;
  ok &= read_field(payload, size, &pos, 3) == dci.pucch_resource_indicator;
  ok &= read_field(payload, size, &pos, 3) == dci.pdsch_to_harq_feedback_timing_indicator.val;
  ok &= read_field(payload, size, &pos, NR_DCI_FORMAT_4_1_RESERVED_BITS) == 0;
  ok &= (payload >> size) == 0;
  return ok;
}
