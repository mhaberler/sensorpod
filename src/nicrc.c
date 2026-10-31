// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// nicrc.c - NIC / Rc from position message type, ADS-B version and NIC
// supplements. compute_nic() and compute_rc() ported verbatim from readsb
// track.c (dev branch); only `static` dropped and RC_UNKNOWN defined here.
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2014-2016 Oliver Jowett <oliver@mutability.co.uk>
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This file is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "nicrc.h"

#define RC_UNKNOWN 0

unsigned compute_nic(unsigned metype, unsigned version, unsigned nic_a, unsigned nic_b, unsigned nic_c) {
    switch (metype) {
        case 5: // surface
        case 9: // airborne
        case 20: // airborne, GNSS altitude
            return 11;

        case 6: // surface
        case 10: // airborne
        case 21: // airborne, GNSS altitude
            return 10;

        case 7: // surface
            if (version == 2) {
                if (nic_a && !nic_c) {
                    return 9;
                } else {
                    return 8;
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 9;
                } else {
                    return 8;
                }
            } else {
                return 8;
            }

        case 8: // surface
            if (version == 2) {
                if (nic_a && nic_c) {
                    return 7;
                } else if (nic_a && !nic_c) {
                    return 6;
                } else if (!nic_a && nic_c) {
                    return 6;
                } else {
                    return 0;
                }
            } else {
                return 0;
            }

        case 11: // airborne
            if (version == 2) {
                if (nic_a && nic_b) {
                    return 9;
                } else {
                    return 8;
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 9;
                } else {
                    return 8;
                }
            } else {
                return 8;
            }

        case 12: // airborne
            return 7;

        case 13: // airborne
            return 6;

        case 14: // airborne
            return 5;

        case 15: // airborne
            return 4;

        case 16: // airborne
            if (nic_a && nic_b) {
                return 3;
            } else {
                return 2;
            }

        case 17: // airborne
            return 1;

        default:
            return 0;
    }
}
unsigned compute_rc(unsigned metype, unsigned version, unsigned nic_a, unsigned nic_b, unsigned nic_c) {
    switch (metype) {
        case 5: // surface
        case 9: // airborne
        case 20: // airborne, GNSS altitude
            return 8; // 7.5m

        case 6: // surface
        case 10: // airborne
        case 21: // airborne, GNSS altitude
            return 25;

        case 7: // surface
            if (version == 2) {
                if (nic_a && !nic_c) {
                    return 75;
                } else {
                    return 186; // 185.2m, 0.1NM
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 75;
                } else {
                    return 186; // 185.2m, 0.1NM
                }
            } else {
                return 186; // 185.2m, 0.1NM
            }

        case 8: // surface
            if (version == 2) {
                if (nic_a && nic_c) {
                    return 371; // 370.4m, 0.2NM
                } else if (nic_a && !nic_c) {
                    return 556; // 555.6m, 0.3NM
                } else if (!nic_a && nic_c) {
                    return 926; // 926m, 0.5NM
                } else {
                    return RC_UNKNOWN;
                }
            } else {
                return RC_UNKNOWN;
            }

        case 11: // airborne
            if (version == 2) {
                if (nic_a && nic_b) {
                    return 75;
                } else {
                    return 186; // 370.4m, 0.2NM
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 75;
                } else {
                    return 186; // 370.4m, 0.2NM
                }
            } else {
                return 186; // 370.4m, 0.2NM
            }

        case 12: // airborne
            return 371; // 370.4m, 0.2NM

        case 13: // airborne
            if (version == 2) {
                if (!nic_a && nic_b) {
                    return 556; // 555.6m, 0.3NM
                } else if (!nic_a && !nic_b) {
                    return 926; // 926m, 0.5NM
                } else if (nic_a && nic_b) {
                    return 1112; // 1111.2m, 0.6NM
                } else {
                    return RC_UNKNOWN; // bad combination, assume worst Rc
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 1112; // 1111.2m, 0.6NM
                } else {
                    return 926; // 926m, 0.5NM
                }
            } else {
                return 926; // 926m, 0.5NM
            }

        case 14: // airborne
            return 1852; // 1.0NM

        case 15: // airborne
            return 3704; // 2NM

        case 16: // airborne
            if (version == 2) {
                if (nic_a && nic_b) {
                    return 7408; // 4NM
                } else {
                    return 14816; // 8NM
                }
            } else if (version == 1) {
                if (nic_a) {
                    return 7408; // 4NM
                } else {
                    return 14816; // 8NM
                }
            } else {
                return 18520; // 10NM
            }

        case 17: // airborne
            return 37040; // 20NM

        default:
            return RC_UNKNOWN;
    }
}
