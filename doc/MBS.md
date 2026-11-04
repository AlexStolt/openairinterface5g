<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Multicast Broadcast Services (MBS) in 5G



## Overview

The gNB sends one PTM transmission on a G-RNTI with DCI format 4_1 in a common search space, and every UE of the group receives it. 
Every UE acknowledges each PTM transport block on its own PUCCH (HARQ ACK/NACK). 
On NACK, only that UE gets a PTP retransmission on its C-RNTI.
To achieve this the whole user plane exists:
- **RAN**: SDAP -> PDCP (MRB) -> RLC (PTM entity) -> MAC (PTM)
- **UE**: MAC -> RLC -> PDCP -> SDAP

Remaining:
- 3GPP also allows PTM retransmissions (TS 38.300 §16.10.5.4 / TS 38.213 §18).
- DCI format 4_2, No-ACK and NACK-Only HARQ-ACK, a CFR smaller than the BWP (TS 38.213 §18)

## Example Deployment
Every UE runs in its own network namespace, so each has its own `oaitun_ue1`.
`mbs.sh ue <N>` re-creates the namespace `ue<N>` before starting the UE.
The MBS traffic is the DL traffic of the first UE.

```bash
# Terminal 1
./tools/scripts/mbs.sh cn
./tools/scripts/mbs.sh nr # or ./tools/scripts/mbs.sh nr noharq

# Terminal 2
./tools/scripts/mbs.sh ue 1

# Terminal 3
./tools/scripts/mbs.sh ll 1

# Terminal 4
./tools/scripts/mbs.sh ue 2 -44

# Terminal 5
./tools/scripts/mbs.sh ll 2

# Terminal 6
./tools/scripts/mbs.sh dl 1
```

Every UE takes the IP of the first UE and gets a default route on its `tun` (common/utils/tuntap_if.c). 


## Permanent (ours, stays when the other team arrives)

| What | Where |
|---|---|
| Group API the control plane must call | `gNB_scheduler_mbs.c`: `nr_mac_add_mbs_group()`, `nr_mac_release_mbs_group()`, `nr_mac_add_mbs_member()`, `nr_mac_remove_mbs_member()`, declared in `NR_MAC_gNB/mac_proto.h` |
| Group configuration given by the control plane | `nr_mbs_group_config_t` in `NR_MAC_gNB/nr_mac_gNB.h` (config_id, mrb_id, lcid, harq_feedback, num_harq) |
| PTM scheduler (DCI 4_1, RBs, MCS, HARQ process) | `gNB_scheduler_mbs.c`: `nr_schedule_mbs()`, `nr_schedule_mbs_group()`; MCS policy `nr_mbs_mcs_select_default()` in `gNB_scheduler_dlsch_default_policies.c` |
| HARQ-ACK of PTM (PUCCH, DAI, K1) | `gNB_scheduler_mbs.c`: `nr_mbs_acknack_scheduling()`; `gNB_scheduler_uci.c`: `nr_acknack_scheduling_ptm()`, `get_feedback_harq_pid()` |
| NACK -> PTP retransmission | `gNB_scheduler_mbs.c`: `nr_mbs_handle_dl_harq()` (called from `gNB_scheduler_uci.c`); reserved MCS `get_retx_mcs()` in `gNB_scheduler_dlsch.c` |
| Multicast part of the UE CellGroupConfig (G-RNTI, CFR, CORESET/search space, PTM RLC bearer) | `nr_radio_config.c`: `add_mbs_cellGroupConfig()`, `get_mbs_coreset_config()`, `get_mbs_search_space_config()` |
| Join sent to the CU (UE Context Modification Required) | `nr_mac_add_mbs_member()` -> `nr_mac_request_reconfiguration()` in `gNB_scheduler_primitives.c`; CU refuses -> member removed in `mac_rrc_dl_handler.c` |
| RLC / PDCP of the MRB | `nr_rlc/nr_rlc_oai_api.c`: `nr_rlc_add_mrb()`, `nr_rlc_mrb_recv_sdu()`; `nr_pdcp/nr_pdcp_oai_api.c`: `nr_pdcp_add_mrb()`, `nr_pdcp_mrb_data_req()`, `nr_pdcp_mrb_data_ind()` |
| UE MAC: G-RNTI, CFR, DCI 4_1, multicast HARQ-ACK | `NR_MAC_UE/config_ue.c`: `configure_g_rnti()`, `configure_cfr_multicast()`; `nr_ue_procedures.c`: `nr_ue_process_dci_dl_41()`, `get_multicast_downlink_ack()` |

## Temporary (to delete when the other team's code arrives)

| Temporary piece | Where | Replaced by (other team) |
|---|---|---|
| gNB test adapter: creates the group, makes every UE join, creates the MRB PDCP | `NR_MAC_gNB/nr_mbs_test_adapter.c` (whole file), started by `nr_mbs_test_adapter_init()` at the end of `RCconfig_nr_macrlc()` in `GNB_APP/gnb_config.c`, config `MBS_TEST` in `ci-scripts/conf_files/mbs-gnb.yaml` | F1AP Multicast Context Setup (TS 38.473 §8.14.6) -> `nr_mac_add_mbs_group()`; UE Context Setup/Modification with the multicast session -> `nr_mac_add_mbs_member()`; CU-UP bearer setup -> `nr_pdcp_add_mrb()` |
| Shared delivery: DL traffic of UE 1's PDU session goes on the MRB | `shared_delivery()` in `nr_mbs_test_adapter.c`, hook `nr_sdap_set_dl_redirect()` in `SDAP/nr_sdap/nr_sdap.c` | Shared N3 tunnel of the MBS session (TS 23.247 §7.2.1.4) feeding `nr_pdcp_mrb_data_req()` |
| PDCP-Config of the MRB (SN 12 bits, t-Reordering 100 ms) | `nr_pdcp/nr_pdcp_mbs_test.h` | MRB-ToAddMod of RadioBearerConfig from the CU RRC (TS 38.331 §6.3.2) |
| UE creates its MRB PDCP itself | `RRC/NR_UE/rrc_UE.c`: `nr_rrc_add_mbs_test_mrb()` | UE RRC handling of MRB-ToAddMod |
| UE delivers MRB packets to the tun of its first PDU session | `SDAP/nr_sdap/nr_sdap.c`: `sdap_mrb_data_ind()` | UE multicast reception (the app joins the IP multicast group of the session) |
| Other UEs borrow UE 1's IP + default route | `./tools/scripts/mbs.sh ll <N>` | Real multicast group address in the IP packets |
| F1-U for MRBs (CU-UP alone) | stub `nr_rlc_mrb_recv_sdu()` in `executables/nr-cuup.c` | F1-U transport of the MRB between CU-UP and DU |

## If the other team changes something, where I change my side

- F1AP gives other/more group parameters -> extend `nr_mbs_group_config_t` (`nr_mac_gNB.h`) and read
  them in `nr_mac_add_mbs_group()` (`gNB_scheduler_mbs.c`).
- They want the CU (not the DU) to build the multicast CellGroupConfig -> remove the call of
  `add_mbs_cellGroupConfig()` in `nr_mac_add_mbs_member()` (`gNB_scheduler_mbs.c`) and keep only the
  MAC state (members, reserved HARQ processes).
- They send the join/leave differently -> only the callers change; the entry points stay
  `nr_mac_add_mbs_member()` / `nr_mac_remove_mbs_member()`. Refused join: `mac_rrc_dl_handler.c`.
- They configure NACK-only, `pucch-ConfigurationListMulticast` or `dl-DataToUL-ACK-Multicast`:
  gNB `nr_mbs_acknack_scheduling()` (`gNB_scheduler_mbs.c`) and `nr_acknack_scheduling_ptm()`
  (`gNB_scheduler_uci.c`); UE `configure_g_rnti()` (`config_ue.c`, today it rejects
  NACK-only) and `get_multicast_downlink_ack()` (`nr_ue_procedures.c`).
- They want PTM retransmissions instead of PTP -> `nr_mbs_handle_dl_harq()` (`gNB_scheduler_mbs.c`).
- The MRB data reaches the DU over F1-U (CU/DU split) -> `nr_rlc_mrb_recv_sdu()` (`nr_rlc_oai_api.c`) is the DU entry; the gNB PDCP output is `deliver_pdu_mrb_gnb()` (`nr_pdcp_oai_api.c`).
- Their PDCP context ID for the session differs -> `NR_PDCP_MBS_CONTEXT_ID()` (`nr_pdcp_oai_api.h`).
- The UE MRB is bound to the session, not to the first PDU session -> `sdap_mrb_data_ind()` (`nr_sdap.c`) and `nr_rrc_add_mbs_test_mrb()` (`rrc_UE.c`).
