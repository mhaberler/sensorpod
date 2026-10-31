#pragma once

// NIC / Rc (containment radius, metres; 0 = unknown) of an ADS-B position
// message, readsb semantics. metype: position TC (5-8 surface, 9-18
// airborne baro, 20-22 airborne GNSS height); version: ADS-B version (0-2);
// nic_a/nic_c from operational status, nic_b from the position message.

#ifdef __cplusplus
extern "C" {
#endif

unsigned compute_nic(unsigned metype, unsigned version, unsigned nic_a,
                     unsigned nic_b, unsigned nic_c);
unsigned compute_rc(unsigned metype, unsigned version, unsigned nic_a,
                    unsigned nic_b, unsigned nic_c);

#ifdef __cplusplus
}
#endif
