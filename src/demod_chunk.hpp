#pragma once

// Chunked Mode-S demodulation for the RTL-SDR path: magnitudes are computed
// into a small internal-RAM buffer chunk by chunk, so only the raw I/Q (in
// PSRAM) is read, once. Header-only and libmodes-only so the host test
// (verifying it against whole-buffer demodulation) can include it.
//
// mode_s_detect() starts preambles only at j < maglen - DEMOD_OVERLAP
// (2 * MODE_S_FULL_LEN: 8 us preamble + 112 bits at 2 samples/us), and
// detect_out_of_phase() looks one sample back. Each chunk is therefore
// prefixed with the previous chunk's last DEMOD_OVERLAP + 1 magnitudes and
// detection starts one sample in: every start position is examined exactly
// once, with the same look-back as a whole-buffer call. The carry also spans
// consecutive buffers (sequence numbers), so messages straddling a 32.8 ms
// buffer boundary are no longer lost; a sequence gap drops the carry so
// non-adjacent samples are never spliced.

#include <mode-s.h>
#include <stdint.h>
#include <string.h>

#define DEMOD_CHUNK 8192  // new samples per chunk
#define DEMOD_OVERLAP 240 // 2 * MODE_S_FULL_LEN (private in mode-s.c)
#define DEMOD_CARRY (DEMOD_OVERLAP + 1) // + 1 look-back sample
#define DEMOD_MAG_LEN (DEMOD_CARRY + DEMOD_CHUNK)

struct DemodChunker {
  uint16_t *mag;      // DEMOD_MAG_LEN samples, internal RAM
  uint32_t carry = 0; // samples carried at mag[0] (0 or DEMOD_CARRY)
  uint32_t last_seq = UINT32_MAX - 1; // so seq 0 isn't "continuous" by luck
  bool started = false;
};

// Demodulates one buffer of nbytes I/Q (seq: reader's per-buffer counter,
// incremented for every read attempt). clipped / iq_power: totals over the
// buffer's new samples (see mode_s_compute_magnitude_vector_stats()).
static inline void demod_buffer(DemodChunker &d, mode_s_t *ctx,
                                const uint8_t *iq, uint32_t nbytes,
                                uint32_t seq, mode_s_callback_t cb,
                                uint32_t *clipped, uint32_t *iq_power) {
  if (!d.started || seq != d.last_seq + 1)
    d.carry = 0; // discontinuity: overrun, read error or new session
  d.started = true;
  d.last_seq = seq;

  uint32_t clip_total = 0, pwr_total = 0;
  const uint32_t nsamples = nbytes / 2;
  for (uint32_t off = 0; off < nsamples;) {
    uint32_t n = nsamples - off < DEMOD_CHUNK ? nsamples - off : DEMOD_CHUNK;
    uint32_t c, p;
    mode_s_compute_magnitude_vector_stats((unsigned char *)iq + 2 * off,
                                          d.mag + d.carry, 2 * n, &c, &p);
    clip_total += c;
    pwr_total += p;
    uint32_t total = d.carry + n;
    // Skip the look-back sample when carrying; detect() needs more than
    // DEMOD_OVERLAP samples or its unsigned bound underflows.
    uint16_t *start = d.carry ? d.mag + 1 : d.mag;
    uint32_t len = d.carry ? total - 1 : total;
    if (len > DEMOD_OVERLAP)
      mode_s_detect(ctx, start, len, cb);
    uint32_t keep = total < DEMOD_CARRY ? total : DEMOD_CARRY;
    memmove(d.mag, d.mag + total - keep, keep * sizeof(uint16_t));
    // A short tail (< DEMOD_CARRY) can't serve as look-back + overlap.
    d.carry = keep == DEMOD_CARRY ? keep : 0;
    off += n;
  }
  *clipped = clip_total;
  *iq_power = pwr_total;
}
