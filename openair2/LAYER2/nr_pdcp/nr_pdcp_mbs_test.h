/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* TEMPORARY PDCP-Config of the multicast MRB used by the MBS test adapter of the gNB
 * (openair2/LAYER2/NR_MAC_gNB/nr_mbs_test_adapter.h) and by the UE, until the gNB-CU sends the MRB-ToAddMod of
 * RadioBearerConfig (TS 38.331 §6.3.2) to the UE */

#ifndef NR_PDCP_MBS_TEST_H
#define NR_PDCP_MBS_TEST_H

/* pdcp-SN-SizeDL len12bits */
#define NR_MBS_TEST_PDCP_SN_SIZE 12
/* t-Reordering ms100 */
#define NR_MBS_TEST_PDCP_T_REORDERING 100
/* highest PDU session identity (TS 24.007 §11.2.3.1b), to find the PDU session the MRB is associated with */
#define NR_MBS_TEST_MAX_PDU_SESSION_ID 15

#endif /* NR_PDCP_MBS_TEST_H */
