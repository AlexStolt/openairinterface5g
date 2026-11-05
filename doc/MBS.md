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
- DCI format 4_2, NACK-Only HARQ-ACK, a CFR smaller than the BWP (TS 38.213 §18)

## Example Deployment

Every UE runs in its own network namespace, so each has its own `oaitun_ue1`.
The MBS traffic is the DL traffic of the first UE.
Before you start run to get info

```bash
./tools/scripts/mbs.sh help
```

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

## Development

### Creating MBS groups

1. Picks the G-RNTI and stores the group config and cell.
2. Reserves PTM HARQ processes.
3. Creates the PTM RLC entity.
4. Builds the multicast CORESET and search space.
5. Registers the group for the scheduler to schedule it every slot.

```bash
executables/nr-softmodem.c                                      main()
└─ executables/nr-softmodem.c                                   create_gNB_tasks()
   └─ openair2/GNB_APP/gnb_config.c                             RCconfig_nr_macrlc()
      └─ openair2/LAYER2/NR_MAC_gNB/nr_mbs_test_adapter.c       nr_mbs_test_adapter_init()
         └─ openair2/LAYER2/NR_MAC_gNB/nr_mbs_test_adapter.c    nr_mbs_test_thread()
            └─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_mbs.c   nr_mac_add_mbs_group() [TODO]
```

**[TODO]**

- `nr_mac_add_mbs_group()`:
  - Today: Creates the group from a `nr_mbs_group_config_t` (MRB ID, LCID, HARQ feedback on/off, number of PTM HARQ processes) and returns it with its G-RNTI. The test adapter calls it once at startup, with values read from the `MBS_TEST` section of the config file.
  - TODO: Keep the function as is but call it when the DU receives the F1AP Multicast Context Setup (TS 38.473 §8.14.6), with the values of the session. If F1AP gives more parameters, add fields to `nr_mbs_group_config_t` (`nr_mac_gNB.h`) and read them in this function.

### Joining a UE to MBS

1. Adds the multicast configuration (G-RNTI, CFR, PTM RLC bearer) to each UE.
2. Sends it to the CU, which sends the RRCReconfiguration to the UE.
3. Reserves the PTM HARQ processes of the UE.
4. Once the group has a member, creates the MRB PDCP.
5. The UE creates its MRB PDCP and configures the G-RNTI and the search space.

```bash
openair2/LAYER2/NR_MAC_gNB/nr_mbs_test_adapter.c              nr_mbs_test_thread()
├─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_mbs.c             nr_mac_add_mbs_member() [TODO]
│  ├─ openair2/LAYER2/NR_MAC_gNB/nr_radio_config.c            add_mbs_cellGroupConfig()
│  └─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_primitives.c   nr_mac_request_reconfiguration() [TODO]
└─ openair2/LAYER2/nr_pdcp/nr_pdcp_oai_api.c                  nr_pdcp_add_mrb() [TODO]
```

```bash
openair2/RRC/NR_UE/rrc_UE.c                   nr_rrc_ue_process_rrcReconfiguration()
├─ openair2/RRC/NR_UE/rrc_UE.c                nr_rrc_add_mbs_test_mrb() [TODO]
└─ openair2/LAYER2/NR_MAC_UE/config_ue.c      nr_rrc_mac_config_req_cg()
   ├─ openair2/LAYER2/NR_MAC_UE/config_ue.c   configure_g_rnti()
   └─ openair2/LAYER2/NR_MAC_UE/config_ue.c   configure_cfr_multicast()
```

**[TODO]**

- `nr_mac_add_mbs_member()`
  - Today: Adds the multicast part to the CellGroupConfig of the UE and sends it to the CU and reserves the PTM HARQ processes. The test adapter (`join_ues()`) calls it every 100 ms for every connected UE.
  - TODO: Keep the function as is but call it when the DU receives a UE Context Setup/Modification that carries the multicast session of the UE (Multicast MBS Session List, TS 38.473 §9.3.1.272).

- `nr_mac_request_reconfiguration()`
  - Today: Sends the new CellGroupConfig to the CU in a UE Context Modification Required. The CU sends it to the UE in an RRCReconfiguration, but without any MRB-ToAddMod. If the CU refuses, `ue_context_modification_refuse()` (`mac_rrc_dl_handler.c`) removes the UE from the group again.
  - TODO: The CU RRC adds the MRB-ToAddMod of the session to that RRCReconfiguration. If the CU builds the multicast CellGroupConfig itself, remove the call of `add_mbs_cellGroupConfig()` in `nr_mac_add_mbs_member()`, which then only keeps the MAC state.

- `nr_pdcp_add_mrb()`
  - Today: Creates the gNB MRB PDCP entity, without ciphering or integrity (TS 38.323 §5.8, §5.9). The test adapter calls it when the group gets its first member, with the fixed SN size and t-Reordering of `nr_pdcp_mbs_test.h`.
  - TODO: Keep the function as is but call it from the CU-UP bearer setup of the session, with the values of its PDCP-Config. Then delete `nr_pdcp_mbs_test.h`.

- `nr_rrc_add_mbs_test_mrb()`
  - Today: When the UE receives the PTM RLC bearer, it creates its own MRB PDCP with the fixed values of `nr_pdcp_mbs_test.h`, attached to its first PDU session. This is needed because no MRB-ToAddMod is received.
  - TODO: Delete this function and make the UE RRC create the MRB PDCP from the MRB-ToAddMod of the RadioBearerConfig (TS 38.331 §6.3.2), by calling `nr_pdcp_add_mrb()`.

### Data transmission

1. The DL data enters the MRB PDCP and is queued in the PTM RLC entity.
2. Every DL slot, the group is scheduled before unicast with DCI 4_1.
3. The UE decodes the DCI 4_1 and passes the data up to its MRB PDCP.

```bash
openair2/LAYER2/NR_MAC_gNB/nr_mbs_test_adapter.c   shared_delivery()
└─ openair2/LAYER2/nr_pdcp/nr_pdcp_oai_api.c       nr_pdcp_mrb_data_req() [TODO]
   └─ openair2/LAYER2/nr_rlc/nr_rlc_oai_api.c      nr_rlc_mrb_recv_sdu()
```

```bash
openair2/LAYER2/NR_MAC_gNB/gNB_scheduler.c                gNB_dlsch_ulsch_scheduler()
└─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_mbs.c         nr_schedule_mbs()
   └─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_mbs.c      nr_schedule_mbs_group()
      ├─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_mbs.c   nr_mbs_acknack_scheduling()
      ├─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_mbs.c   nr_mbs_generate_mac_pdu()
      └─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_mbs.c   nr_mbs_fill_pdcch_pdsch()
```

```bash
openair2/NR_UE_PHY_INTERFACE/NR_IF_Module.c       nr_ue_dl_processing()
├─ openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c   nr_ue_process_dci_dl_41()
└─ openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c   nr_ue_process_mac_pdu()
   └─ openair2/LAYER2/nr_pdcp/nr_pdcp_oai_api.c   nr_pdcp_mrb_data_ind()
      └─ openair2/SDAP/nr_sdap/nr_sdap.c          sdap_mrb_data_ind() [TODO]
```

**[TODO]**

- `nr_pdcp_mrb_data_req()`
  - Today: Takes one SDU of the session, applies PDCP and queues it in the PTM RLC entity. The `shared_delivery()` function calls it with the DL traffic of one PDU session (set by `shared_delivery_ue_id` / `shared_delivery_pdu_session` in `MBS_TEST`), through the redirect hook in `sdap_data_req()`.
  - TODO: Keep the function as is but call it with the packets of the shared N3 tunnel of the MBS session (TS 23.247 §7.2.1.4). Then delete `shared_delivery()` and the redirect hook (`nr_sdap_set_dl_redirect()` in `nr_sdap.c`).

- `nr_rlc_mrb_recv_sdu()`
  - Today: Puts a PDCP PDU in the PTM RLC entity of the G-RNTI. It works only when PDCP and RLC run in the same process. In `nr-cuup` it is an empty stub (`executables/nr-cuup.c`).
  - TODO: With a CU/DU split, F1-U must carry the MRB PDUs to the DU and call it there. Therefore, keep this function as is and consider how to call it when CU/DU is supported.

- `sdap_mrb_data_ind()`
  - Today: The UE writes the received packets to the tun of its first PDU session. To make the other UEs accept them, `mbs.sh ll` gives them the IP of UE 1.
  - TODO: Deliver the packets to the multicast IP group of the session. Then `mbs.sh ll` is not needed anymore.

### ACK/NACK and Retransmissions

1. Each UE sends its ACK/NACK for the PTM transport block on its own PUCCH.
2. On ACK, the HARQ process is freed for that UE.
3. On NACK, the transport block is retransmitted with PTP.

```bash
openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c      get_downlink_ack()
└─ openair2/LAYER2/NR_MAC_UE/nr_ue_procedures.c   get_multicast_downlink_ack()
```

```bash
openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_uci.c      handle_dl_harq()
└─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_mbs.c   nr_mbs_handle_dl_harq()
```

```bash
openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch.c         nr_schedule_ue_spec()
└─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch.c      nr_dl_schedule()
   └─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_dlsch.c   get_retx_mcs()
```

### Leaving MBS

1. When a UE is released, it is removed from every group.
2. The PTM HARQ processes go back to unicast.

```bash
openair2/LAYER2/NR_MAC_gNB/mac_rrc_dl_handler.c            ue_context_release_command()
└─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_primitives.c   mac_remove_nr_ue()
   └─ openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_mbs.c       nr_mac_remove_mbs_member() [TODO]
```

```bash
openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_mbs.c   nr_mac_release_mbs_group() [TODO]
```

**[TODO]**

- `nr_mac_remove_mbs_member()`
  - Today: Removes the UE from the group and gives its PTM HARQ processes back to unicast. It is called only when the UE is released (`mac_remove_nr_ue()`) or when the CU refuses the join.
  - TODO: Also call it when a UE Context Modification removes the multicast session of a UE that stays connected. The RRCReconfiguration that removes the G-RNTI and the PTM RLC bearer from the UE is not done yet.

- `nr_mac_release_mbs_group()`
  - Today: Removes all members, deletes the PTM RLC entity and frees the group. Nothing calls it yet.
  - TODO: Call it when the DU receives the F1AP Multicast Context Release (TS 38.473 §8.14.7).
