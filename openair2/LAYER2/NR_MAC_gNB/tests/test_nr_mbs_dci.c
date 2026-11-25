/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* DCI formats 4_0 (TS 38.212 §7.3.1.5.1, broadcast), 4_1 (§7.3.1.5.2) and 4_2 (§7.3.1.5.3, multicast) scheduling PDSCH with
 * CRC scrambled by G-RNTI */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "common/utils/bits.h"
#include "common/utils/oai_asn1.h"
#include "LAYER2/NR_MAC_gNB/mac_proto.h"
#include "LAYER2/NR_MAC_gNB/nr_radio_config.h"
#include "LAYER2/NR_MAC_COMMON/nr_mac_common.h"
#include "NR_MCCH-Message-r17.h"
#include "NR_MBSBroadcastConfiguration-r17.h"
#include "NR_MBSBroadcastConfiguration-r17-IEs.h"
#include "NR_MBS-SessionInfo-r17.h"
#include "NR_SIB20-r17.h"
#include "NR_CFR-ConfigMCCH-MTCH-r17.h"
#include "NR_LocationAndBandwidthBroadcast-r17.h"

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

/* TS 38.212 §7.3.1.5.3: check the production packer and UE extractor against the specified field order. */
bool test_dci_4_2_pack(int cfr_size, int tda_count, int k1_count)
{
  NR_PDSCH_Config_t p = {0};
  p.resourceAllocation = NR_PDSCH_Config__resourceAllocation_resourceAllocationType1;
  p.prb_BundlingType.present = NR_PDSCH_Config__prb_BundlingType_PR_staticBundling;
  p.pdsch_TimeDomainAllocationList = calloc(1, sizeof(*p.pdsch_TimeDomainAllocationList));
  p.pdsch_TimeDomainAllocationList->present = NR_SetupRelease_PDSCH_TimeDomainResourceAllocationList_PR_setup;
  p.pdsch_TimeDomainAllocationList->choice.setup = calloc(1, sizeof(*p.pdsch_TimeDomainAllocationList->choice.setup));
  p.pdsch_TimeDomainAllocationList->choice.setup->list.count = tda_count;
  NR_PUCCH_Config_t u = {0};
  u.dl_DataToUL_ACK = calloc(1, sizeof(*u.dl_DataToUL_ACK));
  u.dl_DataToUL_ACK->list.count = k1_count;
  NR_UE_DL_BWP_t dl = {.BWPSize = cfr_size, .pdsch_Config = &p};
  NR_UE_UL_BWP_t ul = {.BWPSize = cfr_size, .pucch_Config = &u};
  NR_UE_ServingCell_Info_t sc = {0};
  NR_ControlResourceSet_t coreset = {0};
  const int dynamic = NR_PhysicalCellGroupConfig__pdsch_HARQ_ACK_Codebook_dynamic;
  const int fda_bits = ceil_log2_u32(cfr_size * (cfr_size + 1) / 2);
  const int tda_bits = ceil_log2_u32(tda_count);
  const int k1_bits = ceil_log2_u32(k1_count);
  const int expected_size = fda_bits + tda_bits + k1_bits + 22;
  dci_pdu_rel15_t dci = {0};
  dci.frequency_domain_assignment.val = ((1U << fda_bits) - 1) & 0x125;
  dci.time_domain_assignment.val = tda_count - 1;
  dci.mcs = 19;
  dci.ndi = 1;
  dci.rv = 2;
  dci.harq_pid.val = 13;
  dci.dai[0].val = 2;
  dci.pucch_resource_indicator = 5;
  dci.pdsch_to_harq_feedback_timing_indicator.val = k1_count - 1;
  dci.antenna_ports.val = 3;
  dci.dmrs_sequence_initialization.val = 1;
  nfapi_nr_dl_dci_pdu_t pdu = {0};
  /* CORESET 0 deliberately differs from the CFR: DCI 4_2 addresses the CFR directly, without K scaling. */
  fill_dci_pdu_rel15(&sc, &dl, &ul, &pdu, &dci, NR_DL_DCI_FORMAT_4_2, TYPE_G_RNTI_, 0,
                     common_search_space(), &coreset, dynamic, 24);
  uint64_t bits;
  memcpy(&bits, pdu.Payload, sizeof(bits));
  int pos = 0;
  bool ok = pdu.PayloadSizeBits == expected_size;
  ok &= read_field(bits, expected_size, &pos, fda_bits) == dci.frequency_domain_assignment.val;
  ok &= read_field(bits, expected_size, &pos, tda_bits) == tda_count - 1;
  ok &= read_field(bits, expected_size, &pos, 5) == 19;
  ok &= read_field(bits, expected_size, &pos, 1) == 1;
  ok &= read_field(bits, expected_size, &pos, 2) == 2;
  ok &= read_field(bits, expected_size, &pos, 4) == 13;
  ok &= read_field(bits, expected_size, &pos, 2) == 2;
  ok &= read_field(bits, expected_size, &pos, 3) == 5;
  ok &= read_field(bits, expected_size, &pos, k1_bits) == k1_count - 1;
  ok &= read_field(bits, expected_size, &pos, 4) == 3;
  ok &= read_field(bits, expected_size, &pos, 1) == 1;
  ok &= pos == expected_size && (bits >> expected_size) == 0;
  dci_pdu_rel15_t rx = {0};
  ok &= nr_dci_size(&dl, &ul, &sc, dynamic, &rx, NR_DL_DCI_FORMAT_4_2, TYPE_G_RNTI_,
                    &coreset, NR_SearchSpace__searchSpaceType_PR_common, 24, 0) == expected_size;
  ok &= nr_dci_4_2_unpack(&rx, pdu.Payload, expected_size);
  ok &= rx.frequency_domain_assignment.val == dci.frequency_domain_assignment.val
        && rx.time_domain_assignment.val == dci.time_domain_assignment.val && rx.mcs == dci.mcs && rx.ndi == dci.ndi
        && rx.rv == dci.rv && rx.harq_pid.val == dci.harq_pid.val && rx.dai[0].val == dci.dai[0].val
        && rx.pucch_resource_indicator == dci.pucch_resource_indicator
        && rx.pdsch_to_harq_feedback_timing_indicator.val == dci.pdsch_to_harq_feedback_timing_indicator.val
        && rx.antenna_ports.val == 3 && rx.dmrs_sequence_initialization.val == 1;
  /* Fields not present in the profile cannot be silently added to a configured payload. */
  p.resourceAllocation = NR_PDSCH_Config__resourceAllocation_resourceAllocationType0;
  ok &= nr_dci_size(&dl, &ul, &sc, dynamic, &rx, NR_DL_DCI_FORMAT_4_2, TYPE_G_RNTI_,
                    &coreset, NR_SearchSpace__searchSpaceType_PR_common, 24, 0) == 0;
  p.resourceAllocation = NR_PDSCH_Config__resourceAllocation_resourceAllocationType1;
  long two_codewords = 1;
  p.maxNrofCodeWordsScheduledByDCI = &two_codewords;
  ok &= nr_dci_size(&dl, &ul, &sc, dynamic, &rx, NR_DL_DCI_FORMAT_4_2, TYPE_G_RNTI_,
                    &coreset, NR_SearchSpace__searchSpaceType_PR_common, 24, 0) == 0;
  p.maxNrofCodeWordsScheduledByDCI = NULL;
  p.ext3 = calloc(1, sizeof(*p.ext3));
  long configured_size = expected_size;
  p.ext3->sizeDCI_4_2_r17 = &configured_size;
  ok &= nr_dci_size(&dl, &ul, &sc, dynamic, &rx, NR_DL_DCI_FORMAT_4_2, TYPE_G_RNTI_,
                    &coreset, NR_SearchSpace__searchSpaceType_PR_common, 24, 0) == expected_size;
  configured_size++;
  ok &= nr_dci_size(&dl, &ul, &sc, dynamic, &rx, NR_DL_DCI_FORMAT_4_2, TYPE_G_RNTI_,
                    &coreset, NR_SearchSpace__searchSpaceType_PR_common, 24, 0) == 0;
  free(p.ext3);
  dci.antenna_ports.val = 16;
  ok &= !nr_dci_4_2_pack(&dci, pdu.Payload, expected_size);
  ok &= !nr_dci_4_2_unpack(&rx, pdu.Payload, 65);
  free(p.pdsch_TimeDomainAllocationList->choice.setup);
  free(p.pdsch_TimeDomainAllocationList);
  free(u.dl_DataToUL_ACK);
  return ok;
}

/* TS 38.212 §7.3.1.5.1: packs a DCI format 4_0 with the gNB packer, checks every field at its specified position and the
 * size against DCI formats 4_1 and 1_0 in a CSS, then reads it back with the UE unpacker */
bool test_dci_4_0_pack(int cset0_bwp_size)
{
  const int ss_type = NR_SearchSpace__searchSpaceType_PR_common;
  dci_pdu_rel15_t dci_41 = {0};
  dci_pdu_rel15_t dci_10 = {0};
  const int size_41 =
      nr_dci_size(NULL, NULL, NULL, 0, &dci_41, NR_DL_DCI_FORMAT_4_1, TYPE_G_RNTI_, NULL, ss_type, cset0_bwp_size, 0);
  const int size_10 =
      nr_dci_size(NULL, NULL, NULL, 0, &dci_10, NR_DL_DCI_FORMAT_1_0, TYPE_C_RNTI_, NULL, ss_type, cset0_bwp_size, 0);

  const int fda_bits = ceil_log2_u32((cset0_bwp_size * (cset0_bwp_size + 1)) >> 1);
  dci_pdu_rel15_t dci = {0};
  dci.frequency_domain_assignment.val = ((1U << fda_bits) - 1) & 0x2a5a5;
  dci.time_domain_assignment.val = 0x9;
  dci.vrb_to_prb_mapping.val = 1;
  dci.mcs = 0x13;
  dci.rv = 0x2;
  // fields DCI format 4_0 does not have, which must not reach the payload
  dci.ndi = 1;
  dci.harq_pid.val = 0xf;
  dci.dai[0].val = 0x3;
  dci.pucch_resource_indicator = 0x7;
  dci.pdsch_to_harq_feedback_timing_indicator.val = 0x7;

  nfapi_nr_dl_dci_pdu_t pdu = {0};
  fill_dci_pdu_rel15(NULL,
                     NULL,
                     NULL,
                     &pdu,
                     &dci,
                     NR_DL_DCI_FORMAT_4_0,
                     TYPE_G_RNTI_,
                     0,
                     common_search_space(),
                     NULL,
                     0,
                     cset0_bwp_size);

  const int size = pdu.PayloadSizeBits;
  bool ok = size == fda_bits + 28 && size == size_41 && size == size_10;
  dci_pdu_rel15_t rx_size = {0};
  ok &= nr_dci_size(NULL, NULL, NULL, 0, &rx_size, NR_DL_DCI_FORMAT_4_0, TYPE_G_RNTI_, NULL, ss_type, cset0_bwp_size, 0) == size;
  ok &= rx_size.frequency_domain_assignment.nbits == fda_bits;
  uint64_t payload;
  memcpy(&payload, pdu.Payload, sizeof(payload));
  int pos = 0;
  ok &= read_field(payload, size, &pos, fda_bits) == dci.frequency_domain_assignment.val;
  ok &= read_field(payload, size, &pos, 4) == dci.time_domain_assignment.val;
  ok &= read_field(payload, size, &pos, 1) == dci.vrb_to_prb_mapping.val;
  ok &= read_field(payload, size, &pos, 5) == dci.mcs;
  ok &= read_field(payload, size, &pos, 2) == dci.rv;
  // MCCH change notification, reserved with G-RNTI, and reserved bits
  ok &= read_field(payload, size, &pos, NR_DCI_FORMAT_4_0_RESERVED_BITS) == 0;
  ok &= pos == size && (payload >> size) == 0;

  dci_pdu_rel15_t rx = {0};
  ok &= nr_dci_4_0_unpack(&rx, pdu.Payload, size);
  ok &= rx.frequency_domain_assignment.val == dci.frequency_domain_assignment.val
        && rx.time_domain_assignment.val == dci.time_domain_assignment.val
        && rx.vrb_to_prb_mapping.val == dci.vrb_to_prb_mapping.val && rx.mcs == dci.mcs && rx.rv == dci.rv;
  // a payload too short to hold the fixed fields is rejected
  ok &= !nr_dci_4_0_unpack(&rx, pdu.Payload, 12 + NR_DCI_FORMAT_4_0_RESERVED_BITS);
  return ok;
}

/* TS 38.212 §7.3.1.5.1: the MCCH change notification of DCI format 4_0 follows the RV with MCCH-RNTI, read back by the UE
 * unpacker; with G-RNTI the same bits are reserved, i.e., 0 */
bool test_dci_4_0_mcch_change(int cset0_bwp_size, int change)
{
  const int fda_bits = ceil_log2_u32((cset0_bwp_size * (cset0_bwp_size + 1)) >> 1);
  dci_pdu_rel15_t dci = {.mcs = 5, .rv = 1, .mcch_change_notification = change};
  dci.frequency_domain_assignment.val = 3;
  bool ok = true;
  for (int with_mcch_rnti = 0; with_mcch_rnti < 2; with_mcch_rnti++) {
    nfapi_nr_dl_dci_pdu_t pdu = {0};
    fill_dci_pdu_rel15(NULL,
                       NULL,
                       NULL,
                       &pdu,
                       &dci,
                       NR_DL_DCI_FORMAT_4_0,
                       with_mcch_rnti ? TYPE_MCCH_RNTI_ : TYPE_G_RNTI_,
                       0,
                       common_search_space(),
                       NULL,
                       0,
                       cset0_bwp_size);
    const int size = pdu.PayloadSizeBits;
    uint64_t payload;
    memcpy(&payload, pdu.Payload, sizeof(payload));
    int pos = fda_bits + 4 + 1 + 5 + 2;
    const int expected = with_mcch_rnti ? change : 0;
    ok &= size == fda_bits + 28;
    ok &= read_field(payload, size, &pos, NR_DCI_FORMAT_4_0_MCCH_CHANGE_NOTIFICATION_BITS) == (uint32_t)expected;
    ok &= read_field(payload, size, &pos, NR_DCI_FORMAT_4_0_RESERVED_BITS - NR_DCI_FORMAT_4_0_MCCH_CHANGE_NOTIFICATION_BITS) == 0;
    dci_pdu_rel15_t rx = {0};
    ok &= nr_dci_4_0_unpack(&rx, pdu.Payload, size) && rx.mcch_change_notification == expected && rx.mcs == dci.mcs
          && rx.rv == dci.rv && rx.frequency_domain_assignment.val == dci.frequency_domain_assignment.val;
  }
  return ok;
}

/* TS 38.331 §6.2.1, §6.3.1: the MCCH-Message the gNB encodes decodes into its sessions, as the UE decodes it, and SIB20
 * gives the MCCH configuration of nr_radio_config.h with the CORESET of the CFR */
bool test_mcch_message_and_sib20(int num_sessions)
{
  nr_mbs_broadcast_session_t sessions[NR_MAX_MBS_GROUPS];
  for (int i = 0; i < num_sessions; i++)
    sessions[i] =
        (nr_mbs_broadcast_session_t){.service_id = {0xbe, 0xd4, i}, .g_rnti = 0x4000 + i, .lcid = 32 - i, .t_reassembly = 7};
  uint8_t buf[NR_MCCH_MAX_SIZE];
  const int len = encode_MCCH_message_NR(num_sessions, sessions, buf, sizeof(buf));
  NR_MCCH_Message_r17_t *msg = NULL;
  asn_dec_rval_t dec = uper_decode_complete(NULL, &asn_DEF_NR_MCCH_Message_r17, (void **)&msg, buf, len);
  bool ok = dec.code == RC_OK && msg->message.present == NR_MCCH_MessageType_r17_PR_c1
            && msg->message.choice.c1->present == NR_MCCH_MessageType_r17__c1_PR_mbsBroadcastConfiguration_r17;
  if (ok) {
    const NR_MBSBroadcastConfiguration_r17_t *cfg = msg->message.choice.c1->choice.mbsBroadcastConfiguration_r17;
    const NR_MBS_SessionInfoList_r17_t *list =
        cfg->criticalExtensions.choice.mbsBroadcastConfiguration_r17->mbs_SessionInfoList_r17;
    ok &= num_sessions == 0 ? list == NULL : list != NULL && list->list.count == num_sessions;
    for (int i = 0; ok && i < num_sessions; i++) {
      const NR_MBS_SessionInfo_r17_t *s = list->list.array[i];
      ok &= s->g_RNTI_r17 == sessions[i].g_rnti && s->mbs_SessionId_r17.serviceId_r17.size == 3
            && memcmp(s->mbs_SessionId_r17.serviceId_r17.buf, sessions[i].service_id, 3) == 0
            && s->mrb_ListBroadcast_r17.list.count == 1
            && s->mrb_ListBroadcast_r17.list.array[0]->rlc_Config_r17.logicalChannelIdentity_r17 == sessions[i].lcid;
    }
  }
  ASN_STRUCT_FREE(asn_DEF_NR_MCCH_Message_r17, msg);

  uint8_t fdr[6] = {0xff, 0xff, 0xff, 0xf8, 0, 0};
  NR_ControlResourceSet_t coreset = {.controlResourceSetId = 11,
                                     .frequencyDomainResources = {.buf = fdr, .size = 6, .bits_unused = 3},
                                     .duration = 1,
                                     .cce_REG_MappingType.present = NR_ControlResourceSet__cce_REG_MappingType_PR_nonInterleaved};
  NR_SIB20_r17_t *sib20 = get_SIB20_NR(&coreset);
  const NR_MCCH_Config_r17_t *mcch = &sib20->mcch_Config_r17;
  ok &= mcch->mcch_RepetitionPeriodAndOffset_r17.present == NR_MCCH_RepetitionPeriodAndOffset_r17_PR_rf16_r17
        && mcch->mcch_RepetitionPeriodAndOffset_r17.choice.rf16_r17 == NR_MCCH_REPETITION_OFFSET_RF
        && mcch->mcch_WindowStartSlot_r17 == NR_MCCH_WINDOW_START_SLOT && mcch->mcch_WindowDuration_r17
        && *mcch->mcch_WindowDuration_r17 == NR_MCCH_Config_r17__mcch_WindowDuration_r17_sl10
        && mcch->mcch_ModificationPeriod_r17 == NR_MCCH_Config_r17__mcch_ModificationPeriod_r17_rf64;
  ok &= sib20->cfr_ConfigMCCH_MTCH_r17 && sib20->cfr_ConfigMCCH_MTCH_r17->commonControlResourceSetExt_r17
        && sib20->cfr_ConfigMCCH_MTCH_r17->commonControlResourceSetExt_r17->controlResourceSetId == 11;
  // the CFR is the initial DL BWP, not CORESET#0 as without locationAndBandwidthBroadcast
  ok &= sib20->cfr_ConfigMCCH_MTCH_r17->locationAndBandwidthBroadcast_r17
        && sib20->cfr_ConfigMCCH_MTCH_r17->locationAndBandwidthBroadcast_r17->present
               == NR_LocationAndBandwidthBroadcast_r17_PR_sameAsSib1ConfiguredLocationAndBW;
  // a valid SIB20, which no longer carries the sessions
  uint8_t sib_buf[256];
  ok &= uper_encode_to_buffer(&asn_DEF_NR_SIB20_r17, NULL, sib20, sib_buf, sizeof(sib_buf)).encoded > 0;
  ok &= sib20->lateNonCriticalExtension == NULL;
  ASN_STRUCT_FREE(asn_DEF_NR_SIB20_r17, sib20);
  return ok;
}
