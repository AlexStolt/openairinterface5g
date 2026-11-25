<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# MBS tutorial

> This work is part of the ESA activity **AD-SAT** (*Provision of Broadband
> Control Centre Services for Autonomous Driving over Satellite
> Constellations*, activity 3F.049) with Frequentis AG. See the
> [project page](https://connectivity.esa.int/?p=26285).

One gNB sends a single transmission on a G-RNTI and three UEs receive it. This
tutorial runs both MBS service types in one cell, over rfsimulator in Docker:
a broadcast session that the core creates, and a multicast group that every
UE joins. It ends with both running at the same time.

[[_TOC_]]

## Where this is today

MBS has two service types. A **broadcast** session is sent to everyone in the
service area: receivers do not join, the network does not know they exist,
and they find the session by reading what the cell announces, so they can be
idle. A **multicast** session is sent only to UEs that joined it. They are
known individually, which is what makes HARQ feedback and retransmission
possible, and each is told about the session in its own RRCReconfiguration.

**Broadcast** comes from the core:

* **The core creates a broadcast session.** It allocates a TMGI, distributes
  the session to a multicast group with one common TEID shared by every gNB
  in the area, and announces it with `BroadcastSessionSetup` over NGAP.
* **The cell announces it.** The MCCH lists the sessions of the cell, each
  with its TMGI, its G-RNTI and the configuration of its radio bearer. SIB20
  says where the MCCH is (every 16 radio frames, in the first 10 slots, changed
  only every 64 radio frames) and gives the CORESET of the PDCCHs; SIB1 names
  the search space. The MCCH and the sessions are both scheduled with DCI
  format 4_0, the MCCH on the MCCH-RNTI and each session on its G-RNTI.
* **Receivers configure themselves** from that, with no signalling of their
  own. The gNB does not know who receives, transmits whether or not anybody
  is connected, and a UE keeps receiving in RRC_IDLE.

HARQ feedback is off, because a broadcast session has no members to report
with, and the session is sent at a fixed MCS for the same reason: 9 unless
`mbs_broadcast_mcs` in the `MACRLCs` section says otherwise.

**Multicast** comes from a test adapter in the gNB (`MBS_TEST` in its
configuration), as the shared N3 tunnel of a multicast session does not exist
yet:

* **The adapter creates the group** when the gNB starts, and makes every
  connected UE join it: each gets the G-RNTI, the search space and the bearer
  of the group in an RRCReconfiguration.
* **Its data is the DL traffic of the first UE's PDU session**, which the gNB
  puts on the radio bearer of the group instead, so every member receives it.
* **Every member acknowledges each transport block** on its own PUCCH. After a
  NACK, only that UE gets the block again, point-to-point on its C-RNTI.

The group is scheduled with DCI format 4_2 (or 4_1, see `dci_format` in
`MBS_TEST`), in a search space of its own: broadcast and multicast run side by
side in the cell. The remaining gaps are listed under
[Known gaps](#known-gaps).

## What this needs

* **A core that can create an MBS session** — see the next section. Upstream
  Open5GS cannot: it has no MB-SMF, so there is nothing to allocate a TMGI or
  set up the shared tunnel.
* **A monolithic gNB.** The MRB's PDCP PDUs have to reach the RLC entity that
  transmits them, and `nr_rlc_mrb_recv_sdu()` only works when PDCP and RLC
  run in the same process. Neither a CU/DU nor a CU-CP/CU-UP split can carry
  the MRB payload yet: broadcast setup signalling over F1AP and E1AP is
  implemented, F1-U delivery of the MRB is not.
* **SIB20 switched on in the gNB**, with `du_sibs = [20];` in its
  configuration. A cell that schedules any other SIB also has to schedule one
  of SIB2 to SIB14, hence `cu_sibs = [2];` next to it. The configuration file
  of this tutorial has both, and the `MBS_TEST` section of the multicast group.
* Three subscribers, IMSI `999700123456701`, `702` and `703`. They are in
  `subscribers.json` next to the compose file and are imported automatically.

## The core network

A ready image of the core described below is on Docker Hub:

```bash
docker pull phinetech/open5gs_core:mbs
docker tag phinetech/open5gs_core:mbs open5gs_core:mbs
```

Use this image, or a core built as below from the current `5mbs-development`
branch. Older images of the 5G-MAG core write the broadcast GTP-TEID
byte-swapped: the gNB then listens on the wrong TEID, and the session is set
up but nothing arrives (`0 PTM transmissions` in the gNB statistics).

With it, continue at [Build the images](#build-the-images). To build the core
yourself, use the 5G-MAG fork of Open5GS, which adds the MBS functions:

```bash
git clone --branch 5mbs-development https://github.com/5G-MAG/open5gs.git
```

Build it the way Open5GS documents (`meson` and `ninja`); the fork adds no
extra steps. The MBS functions are not separate daemons — the MB-SMF and
MB-UPF are merged into `open5gs-smfd` and `open5gs-upfd`, which is why the
session API below is served by the SMF.

**Do not mix binaries with upstream Open5GS.** The fork changes shared
libraries in incompatible ways, and because every daemon loads the same ones,
an upstream binary next to the fork's libraries will crash at startup. Build
everything from the fork.

For MBS you need nine network functions:

```
open5gs-nrfd  open5gs-scpd  open5gs-amfd  open5gs-smfd  open5gs-upfd
open5gs-ausfd open5gs-udmd  open5gs-udrd  open5gs-pcfd
```

`open5gs-pcfd` is easy to overlook and not optional: the AMF asks it for an AM
policy during registration, and without it every UE is rejected with cause 95,
"semantically incorrect message", which says nothing about the real problem.
It also needs MongoDB to be up before it starts, so the compose file waits for
the database to answer before starting the core.

The core also needs a `tun` interface for the user plane (`ogstun`, as
Open5GS documents). The MB-UPF routes the session's multicast group into it
by itself (`upf.mbs.multicast_router` in `core.yaml`); for that it needs the
kernel's multicast routing to itself, so do not run a routing daemon such as
`smcrouted` next to it.

Package the result however you like. The compose file expects the image as
`open5gs_core:mbs`; to use another name, set `CN_IMG`. What it expects of
that image:

* the core reachable at `192.168.77.10`, with the AMF listening for NGAP and
  the SMF's service-based interface on `127.0.0.4:7777`;
* its configuration read from `/container/open5gs/core.yaml`, which the
  compose file mounts from `core.yaml` next to it. That file is an ordinary
  Open5GS configuration; if your packaging reads its configuration somewhere
  else, change the mount to match.

The subscribers are imported into MongoDB on first start, by
`provision-subscribers.sh` in the same directory.

## Build the images

The gNB and UE images are built on the shared `ran-base` and `ran-build`
images; see [docker/README.md](../docker/README.md).

```bash
cd ~/openairinterface5g
docker build . -f docker/Dockerfile.base.ubuntu  --tag ran-base:latest
docker build . -f docker/Dockerfile.build.ubuntu --tag ran-build:latest
docker build . -f docker/Dockerfile.gNB.ubuntu   --tag oai-gnb:mbs
docker build . -f docker/Dockerfile.nrUE.ubuntu  --tag oai-nr-ue:mbs
```

The compose file picks those up through `TAG`, which defaults to `mbs`. After
a code change, build `ran-build` and both images again.

## Start everything

```bash
cd ci-scripts/yaml_files/5g_rfsimulator_mbs_broadcast
docker compose up -d
```

Wait until each UE has an address:

```bash
for n in 1 2 3; do docker exec mbs-ue$n ip -4 addr show oaitun_ue1; done
```

If no `oaitun_ue1` appears and the UE log ends in a registration reject,
check the core first — see the note on `open5gs-pcfd` above.

The gNB creates the multicast group as it starts, and adds each UE to it once
the UE is connected. Wait until all three are members:

```bash
docker logs mbs-gnb 2>&1 | grep "MBS test:" | tail -1
```

```
[NR_MAC] MBS test: G-RNTI 626e members 3 SDUs injected 0 PTM transmissions 0 SDU bytes 0 pending 0 ACK 0 NACK 0
```

## Multicast

The data of the group is the DL traffic of the first UE: a packet the core
sends to that UE's address goes to all three members, in one transmission on
the G-RNTI. Run the check from the repository root, on a fresh deployment that
has not sent anything on the group yet:

```bash
cd ~/openairinterface5g
python3 ci-scripts/tests/mbs-multicast.py --dci-format 42
```

It finds the address of the first UE, sends twenty packets to it from the
core, and counts what each UE's `oaitun_ue1` received. Expect 20 of 20 on every
UE. `--payload-bytes 1000` sends larger packets.

The gNB statistics show the transmissions and the HARQ-ACKs of the members:

```
[NR_MAC] MBS test: G-RNTI 626e members 3 SDUs injected 0 PTM transmissions 20 SDU bytes 460 pending 0 ACK 57 NACK 3
```

Three members acknowledge twenty transport blocks, so ACK and NACK add up to
60. Each NACK was followed by a retransmission to that UE alone, which is why
every UE still counts 20.

For DCI format 4_1, set `dci_format = 41;` in `MBS_TEST` in
`gnb.mbs.band78.106prb.rfsim.conf`, restart (`docker compose down -v`, then
`up -d`), and run the check with `--dci-format 41`.

## Create the broadcast session

The MB-SMF allocates the TMGI and tells the gNB over NGAP. The application
server `mbs-as` (`192.168.77.20`) sends to `239.0.0.25`; together they are the
source-specific multicast address of the session.

```bash
docker exec mbs-cn curl -s --http2-prior-knowledge -X POST \
  -H 'Content-Type: application/json' \
  -d '{"mbsSession":{"ssm":{"sourceIpAddr":{"ipv4Addr":"192.168.77.20"},
       "destIpAddr":{"ipv4Addr":"239.0.0.25"}},"tmgiAllocReq":true,
       "serviceType":"BROADCAST","snssai":{"sst":1},
       "mbsServiceArea":{"tais":[{"plmnId":{"mcc":"999","mnc":"70"},"tac":"000001"}]}}}' \
  127.0.0.4:7777/nmbsmf-mbssession/v1/mbs-sessions
```

The gNB log now shows the session arriving and the group being created. The
multicast group has LCID 32, so the session gets the next free one:

```
[NGAP]   Broadcast session setup: TMGI bed4da99f907 (6 octets)
[NGAP]   Broadcast session user plane: group 239.0.0.4 source 192.168.77.10 C-TEID 0x00002b17
[NGAP]   Broadcast session setup acknowledged
[E1AP]   MBS: broadcast bearer context for TMGI bed4da99f907, MRB 1, SST 1
[GTPU]   MBS: joined group 239.0.0.4 from 192.168.77.10 on 192.168.77.101, port 2152
[GTPU]   MBS: listening for the broadcast session on TEID 0x2b17
[RLC]    fb30: added PTM RLC entity of MRB 1 on LCID 31
[NR_MAC] added MBS broadcast session G-RNTI fb30: LCID 31, HARQ feedback disabled, PTM HARQ processes 0x0000, DCI 4_0
[PDCP]   10000fb30: added MRB 1 (SN size 12)
```

Nothing is sent to the UEs for it. The gNB puts it on the MCCH from the next
modification period on, at most 640 ms later, and flags the change in the
PDCCHs of the MCCH. Each UE then reads the MCCH again and says so in its own
log:

```
[NR_RRC] MBS: MCCH acquired
[RLC]    0000: added PTM RLC entity of MRB 31 on LCID 31
[PDCP]   0: added MRB 31 (SN size 12)
[NR_RRC] MBS: broadcast session with service ID bed4da announced on G-RNTI fb30, MTCH on LCID 31
```

Wait for this last line on all three UEs before sending:

```bash
for n in 1 2 3; do docker logs mbs-ue$n 2>&1 | grep "broadcast session with service ID"; done
```

Note the group address the core chose (`239.0.0.4` above) is not the one the
application sends to: the core distributes the session to a group of its own.

## Send broadcast traffic and watch it arrive

What a UE receives on the group is written to `oaitun_ue1`, so counting the
packets the interface took in is enough. Read the counter before sending:

```bash
for n in 1 2 3; do
  docker exec mbs-ue$n cat /sys/class/net/oaitun_ue1/statistics/rx_packets > /tmp/ue$n.before
done
```

Send twenty packets from the application server:

```bash
for i in $(seq 20); do
  docker exec mbs-as sendip -p ipv4 -is 192.168.77.20 -id 239.0.0.25 239.0.0.25
  sleep 0.2
done
```

Count what each UE received:

```bash
for n in 1 2 3; do
  after=$(docker exec mbs-ue$n cat /sys/class/net/oaitun_ue1/statistics/rx_packets)
  echo "ue$n: $((after - $(cat /tmp/ue$n.before))) of 20"
done
```

All three receive the same packets from **one** transmission on the G-RNTI,
and the three counts are the same for that reason.

Expect 20 of 20. Broadcast has no feedback: the gNB does not know who is
listening, so nothing is retransmitted and what is lost stays lost.

To check the first batch automatically, run `python3 ci-scripts/tests/mbs-broadcast.py`
from the repository root instead of the counter and send loops above. Use a fresh
session, wait for all three UEs to announce it, and send no warm-up traffic: the
check expects the session's first PTM transmissions.

The gNB counts what it sent in its periodic statistics, and does so with no
UE in the cell as well:

```
MBS broadcast session G-RNTI fb30: 20 PTM transmissions, 460 bytes, MCS 9
```

## Broadcast and multicast together

The broadcast session (DCI 4_0) and the multicast group (DCI 4_2 or 4_1) are
now both in the cell, each with its own G-RNTI, search space and MTCH LCID. A
UE decodes one PDSCH per slot, so the gNB sends one of them per slot, the two
taking turns. Send to both at once:

```bash
cd ~/openairinterface5g
python3 ci-scripts/tests/mbs-broadcast-multicast.py --dci-format 42
```

It sends twenty packets on each, interleaved, and expects 40 of 40 on every
UE, as both sessions end up on `oaitun_ue1`. The gNB statistics count 20 more
transmissions for each of the two.

## Release the session

Delete the session at the core:

```bash
docker exec mbs-cn curl -s --http2-prior-knowledge -X DELETE \
  127.0.0.4:7777/nmbsmf-mbssession/v1/mbs-sessions/1
```

The core sends `BroadcastSessionReleaseRequest`. The gNB leaves the group and
removes the session from the MCCH from its next modification period on, and
every UE stops receiving it when it reads the changed MCCH:

```
[NGAP]   Broadcast Session Release initiating message
[E1AP]   MBS: broadcast user plane released, TEID 0x2b17
[NR_MAC] MBS: broadcast session for TMGI bed4da99f907 released, G-RNTI fb30
[NR_RRC] MBS: broadcast session with MTCH on LCID 31 not announced anymore
```

A session created afterwards is announced and received like the first one.
The gNB can also release a session without the core, through the telnet
command `rrc release_mbs_broadcast`. The multicast group stays, as it belongs
to the gNB's test adapter.

## Stop

```bash
cd ci-scripts/yaml_files/5g_rfsimulator_mbs_broadcast
docker compose down -v
```

`-v` throws away the MongoDB volume, so the subscribers are imported again on
the next start.

## Unit tests

The DCI formats, the MCCH and SIB20 have unit tests that need no deployment:

```bash
cd ~/openairinterface5g
mkdir -p build && cd build
cmake .. -GNinja -DENABLE_TESTS=ON && ninja tests
ctest -R 'DciFormat|McchMessage' --output-on-failure
```

They pack DCI formats 4_0, 4_1 and 4_2 with the gNB code and read them back
with the UE code, check the MCCH change notification, and decode the
MCCH-Message and SIB20 the gNB encodes.

## Known gaps

* **The MCCH is sent on one beam**, that of the first SSB, in the first slot
  of its window that has room, and fits in one transport block (no RLC
  segmentation). Its schedule is fixed (`nr_radio_config.h`), the CFR is the
  initial DL BWP, and `searchSpaceMCCH` and `searchSpaceMTCH` are the same
  search space, which may not be zero.
* **Multicast data comes from the test adapter.** The shared N3 tunnel of a
  multicast session (TS 23.247 §7.2.1.4) does not exist yet, so the group
  carries the DL traffic of one UE's PDU session, and every connected UE is
  made a member rather than joining through the core.
* **Multicast needs a monolithic gNB too**: F1AP Multicast Context Setup and
  the multicast part of UE Context Modification (TS 38.473) are not
  implemented. NACK-only HARQ-ACK, PTM retransmissions and a CFR smaller than
  the BWP (TS 38.213 §18) are not either.
* **The UE delivers what it receives to the interface of its first PDU
  session**, broadcast and multicast alike, as it has no interface for MBS
  sessions. A UE that never had a PDU session receives a broadcast session but
  has nowhere to deliver it.
