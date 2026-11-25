# SPDX-License-Identifier: LicenseRef-CSSL-1.0

"""Check a broadcast session and a multicast group of the same cell at the same time.

Use the deployment of doc/MBS_Tutorial.md, whose gNB has the MBS_TEST adapter
(join_ues = 1), create the broadcast session at the core, and wait for all three
UEs to announce it and to join the multicast group. The broadcast session is
scheduled with DCI format 4_0, the multicast group with the adapter's format.
Twenty packets are sent on each, interleaved, and every UE must deliver all forty
on oaitun_ue1, where both sessions end up.
"""

import argparse
import json
import re
import subprocess
import time


def docker(*args):
    return subprocess.check_output(["docker", *args], text=True, stderr=subprocess.STDOUT)


def logs(container):
    started = docker("inspect", "--format", "{{.State.StartedAt}}", container).strip()
    return docker("logs", "--since", started, container)


def received():
    return [int(docker("exec", f"mbs-ue{n}", "cat", "/sys/class/net/oaitun_ue1/statistics/rx_packets")) for n in (1, 2, 3)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dci-format", choices=(41, 42), type=int, default=42, help="DCI format of the multicast group")
    args = parser.parse_args()
    gnb_log = logs("mbs-gnb")
    if not re.search(r"added MBS broadcast session G-RNTI [0-9a-f]+: .*DCI 4_0", gnb_log):
        raise RuntimeError("The gNB has no broadcast session on DCI format 4_0")
    if not re.search(rf"added MBS group G-RNTI [0-9a-f]+: .*DCI 4_{args.dci_format % 10}", gnb_log):
        raise RuntimeError("The gNB has no multicast group on the requested DCI format")
    stats = re.findall(r"MBS test: .* members (\d+) SDUs injected \d+ PTM transmissions (\d+)", gnb_log)
    if not stats or stats[-1][0] != "3":
        raise RuntimeError("The multicast group needs three members")
    for n in (1, 2, 3):
        if "MBS: broadcast session with service ID" not in logs(f"mbs-ue{n}"):
            raise RuntimeError(f"UE {n} has not announced the broadcast session")
    first_ue = re.findall(r"UE RNTI ([0-9a-f]+)\b[^\n]*\bCU-UE-ID 1\b", gnb_log)
    if not first_ue:
        raise RuntimeError("Cannot identify CU UE 1 in the gNB's MAC statistics")
    destination = None
    for n in (1, 2, 3):
        if re.search(r"RNTI 0x" + first_ue[-1], logs(f"mbs-ue{n}"), re.IGNORECASE):
            interface = json.loads(docker("exec", f"mbs-ue{n}", "ip", "-j", "-4", "addr", "show", "oaitun_ue1"))
            destination = interface[0]["addr_info"][0]["local"]
            break
    if destination is None:
        raise RuntimeError("Cannot identify the first CU UE's PDU session address")

    before = received()
    for packet in range(1, 21):
        # broadcast: from the application server to the session's group; multicast: CU UE 1's DL traffic, put on the MRB
        docker("exec", "mbs-as", "sendip", "-p", "ipv4", "-is", "192.168.77.20", "-id", "239.0.0.25", "239.0.0.25")
        docker("exec", "mbs-cn", "sendip", "-p", "ipv4", "-is", "192.168.77.10", "-id", destination, destination)
        time.sleep(0.2)
        delta = [a - b for a, b in zip(received(), before)]
        print(f"Sent {packet:2} + {packet:2}: UE counters {delta}", flush=True)
    time.sleep(2)
    delta = [a - b for a, b in zip(received(), before)]
    for n, count in enumerate(delta, 1):
        print(f"ue{n}: {count} of 40")
    return 0 if delta == [40, 40, 40] else 1


if __name__ == "__main__":
    raise SystemExit(main())
