/**
  Rainbow hash function - 256-bit internal state, 128-bit input chunks, up to 256-bit output
  Stream based
  Can also be utilized as an eXtensible Output Function (XOF). 

  https://github.com/dosyago/rain

  Copyright 2023 Cris Stringfellow (and DOSYAGO)
  Rainstorm hash is licensed under Apache-2.0
  
    Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
**/
#define __RAINBNOWVERSION__ "1.0.6"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define KEEPALIVE
#endif

#include "common.h"

namespace rainbow {
  // P to W are primes chosen for their excellent avalanche properties
	// 445674c37b63d6c1cb0c0d16b3ecb14208a1e0f4b5d0de831ef41003620cef91 LICENSE.txt
  static constexpr uint64_t P        = UINT64_C(   0xFFFFFFFFFFFFFFFF) - 58;
  static constexpr uint64_t Q        = UINT64_C( 13166748625691186689);
  static constexpr uint64_t R        = UINT64_C(  1573836600196043749);
  static constexpr uint64_t S        = UINT64_C(  1478582680485693857);
  static constexpr uint64_t T        = UINT64_C(  1584163446043636637);
  static constexpr uint64_t U        = UINT64_C(  1358537349836140151);
  static constexpr uint64_t V        = UINT64_C(  2849285319520710901);
  static constexpr uint64_t W        = UINT64_C(  2366157163652459183);

  static inline void mixA(uint64_t* s) {
    uint64_t a = s[0], b = s[1], c = s[2], d = s[3];

    a *= P;
    a = ROTR64(a, 23);
    a *= Q;

    b ^= a;

    b *= R;
    b = ROTR64(b, 29);
    b *= S;

    c *= T;
    c = ROTR64(c, 31);
    c *= U;

    d ^= c;

    d *= V;
    d = ROTR64(d, 37);
    d *= W;

    s[0] = a; s[1] = b; s[2] = c; s[3] = d;
  }

  static inline void mixB(uint64_t* s, uint64_t iv) {
    uint64_t a = s[1], b = s[2];

    a *= V;
    a = ROTR64(a, 23);
    a *= W;

    b ^= a + iv;

    b *= R;
    b = ROTR64(b, 23);
    b *= S;

    s[1] = a; s[2] = b; 
  }

  // streaming mode affordances
  struct HashState : IHashState {
    uint64_t  h[4];
    seed_t    seed;
    size_t    len;                  // length processed so far
    uint32_t  hashsize;
    bool      inner = 0;
    bool      final_block = false;
    bool      finalized = false;

    // Initialize the state with known length
    static HashState initialize(const seed_t seed, size_t olen, uint32_t hashsize) {
      HashState state;
      state.h[0] = seed + olen + 1;
      state.h[1] = seed + olen + 3;
      state.h[2] = seed + olen + 5;
      state.h[3] = seed + olen + 7;
      state.len = 0;  // initialize length counter
      state.seed = seed;
      state.hashsize = hashsize;
      //std::cout << "Len: " << olen << std::endl;
      return state;
    }

    /*
      // Initialize the state with unknown length (streaming mode)
      // for this initialization we invert the normal monotonic increasing pattern of the IV
      // by combining a decreasing sequence of primes, with the original increasing one
      // this means that, if we stream a file without knowing its length (say by stdin)
      // and we hash a file directly (by knowing its length)
      // the result will be different
      // This is not ideal, and may be considered a design flaw
      // For now the way we work around that is, if we read from stdin
      // we do not use streaming mode. We only use streaming mode when we
      // are reading from a known file (and have the length)
      static HashState initialize(const seed_t seed) {
        HashState state;
        h[0] = seed + 1002;   // 1001 + 1;
        h[1] = seed + 1000;   // 997 + 3;
        h[2] = seed + 988;    // 983 + 5;
        h[3] = seed + 984;    // 977 + 7;
        len = 0;  // initialize length counter
        return state;
      }
    */

    // Update the state with a new chunk of data
    void update(const uint8_t* chunk, size_t chunk_len) {
      // update state based on chunk
      bool last_block = chunk_len < CHUNK_SIZE;
      if ( final_block ) {
        // throw
      }
      len += chunk_len;

      //printf("h: %016llx %016llx %016llx %016llx\n", h[0], h[1], h[2], h[3]);

      while (chunk_len >= 16) {
        uint64_t g =  GET_U64<bswap>(chunk, 0);

        h[0] -= g;
        h[1] += g;

        chunk += 8;

        g =  GET_U64<bswap>(chunk, 0);

        h[2] += g;
        h[3] -= g;

        if ( inner ) {
          mixB(h, seed);
        } else {
          mixA(h);
        }
        inner ^= 1;

        chunk += 8;
        chunk_len -= 16;
      }

      //printf("h: %016llx %016llx %016llx %016llx\n", h[0], h[1], h[2], h[3]);
      
      if ( last_block ) {
        final_block = true;
        mixB(h, seed);

        switch (chunk_len) {
	   case 15:
	     h[0] += static_cast<uint64_t>(chunk[14]) << 56;
	     [[fallthrough]];

	   case 14:
	     h[1] += static_cast<uint64_t>(chunk[13]) << 48;
	     [[fallthrough]];

	   case 13:
	     h[2] += static_cast<uint64_t>(chunk[12]) << 40;
	     [[fallthrough]];

	   case 12:
	     h[3] += static_cast<uint64_t>(chunk[11]) << 32;
	     [[fallthrough]];

	   case 11:
	     h[0] += static_cast<uint64_t>(chunk[10]) << 24;
	     [[fallthrough]];

	   case 10:
	     h[1] += static_cast<uint64_t>(chunk[9]) <<  16;
	     [[fallthrough]];

	   case 9:
	     h[2] += static_cast<uint64_t>(chunk[8]) << 8;
	     [[fallthrough]];

	   case 8:
	     h[3] += chunk[7];
	     [[fallthrough]];

	   case 7:
	     h[0] += static_cast<uint64_t>(chunk[6]) << 48;
	     [[fallthrough]];

	   case 6:
	     h[1] += static_cast<uint64_t>(chunk[5]) << 40;
	     [[fallthrough]];

	   case 5:
	     h[2] += static_cast<uint64_t>(chunk[4]) << 32;
	     [[fallthrough]];

	   case 4:
	     h[3] += static_cast<uint64_t>(chunk[3]) << 24;
	     [[fallthrough]];

	   case 3:
	     h[0] += static_cast<uint64_t>(chunk[2]) << 16;
	     [[fallthrough]];

	   case 2:
	     h[1] += static_cast<uint64_t>(chunk[1]) <<  8;
	     [[fallthrough]];

	   case 1:
	     h[2] += chunk[0];
        }

        mixA(h);
        mixB(h, seed);
        mixA(h);

        //printf("h: %016llx %016llx %016llx %016llx\n", h[0], h[1], h[2], h[3]);
      }
    }

    // Finalize the hash and return the result
    void finalize(void* out) {
      // finalize hash
      if ( finalized ) {
        return;
      } 
      
      uint64_t g = 0;
      g -= h[2];
      g -= h[3];

      PUT_U64<bswap>(g, static_cast<uint8_t *>(out), 0);
      if (this->hashsize == 128) {
        mixA(h);
        g = 0;
        g -= h[3];
        g -= h[2];
        PUT_U64<bswap>(g, static_cast<uint8_t *>(out), 8);
      } else if ( this->hashsize == 256) {
        mixA(h);
        g = 0;
        g -= h[3];
        g -= h[2];
        PUT_U64<bswap>(g, static_cast<uint8_t *>(out), 8);
        mixA(h);
        mixB(h, seed);
        mixA(h);
        g = 0;
        g -= h[3];
        g -= h[2];
        PUT_U64<bswap>(g, static_cast<uint8_t *>(out), 16);
        mixA(h);
        g = 0;
        g -= h[3];
        g -= h[2];
        PUT_U64<bswap>(g, static_cast<uint8_t *>(out), 24);
      }

      finalized = true;
    }
  };

  // one big func mode (memory inefficient, but simple call)
  template <uint32_t hashsize, bool bswap>
  static void rainbow(const void* in, const size_t olen, const seed_t seed, void* out) {
    const uint8_t * data = static_cast<const uint8_t *>(in);
    uint64_t h[4] = {seed + olen + 1, seed + olen + 3, seed + olen + 5, seed + olen + 7};
    size_t len = olen;
    uint64_t g = 0;
    bool inner = 0;

    //std::cout << "Len: " << olen << std::endl;
    //printf("h: %016llx %016llx %016llx %016llx\n", h[0], h[1], h[2], h[3]);

    while (len >= 16) {
      g =  GET_U64<bswap>(data, 0);

      h[0] -= g;
      h[1] += g;

      data += 8;

      g =  GET_U64<bswap>(data, 0);

      h[2] += g;
      h[3] -= g;

      if ( inner ) {
        mixB(h, seed);
      } else {
        mixA(h);
      }
      inner ^= 1;

      data += 8;
      len  -= 16;
    }

    //printf("h: %016llx %016llx %016llx %016llx\n", h[0], h[1], h[2], h[3]);

    mixB(h, seed);

    switch (len) {
      case 15:
	h[0] += static_cast<uint64_t>(data[14]) << 56;
	[[fallthrough]];

      case 14:
	h[1] += static_cast<uint64_t>(data[13]) << 48;
	[[fallthrough]];

      case 13:
	h[2] += static_cast<uint64_t>(data[12]) << 40;
	[[fallthrough]];

      case 12:
	h[3] += static_cast<uint64_t>(data[11]) << 32;
	[[fallthrough]];

      case 11:
	h[0] += static_cast<uint64_t>(data[10]) << 24;
	[[fallthrough]];

      case 10:
	h[1] += static_cast<uint64_t>(data[9]) << 16;
	[[fallthrough]];

      case 9:
	h[2] += static_cast<uint64_t>(data[8]) << 8;
	[[fallthrough]];

      case 8:
	h[3] += data[7];
	[[fallthrough]];

      case 7:
	h[0] += static_cast<uint64_t>(data[6]) << 48;
	[[fallthrough]];

      case 6:
	h[1] += static_cast<uint64_t>(data[5]) << 40;
	[[fallthrough]];

      case 5:
	h[2] += static_cast<uint64_t>(data[4]) << 32;
	[[fallthrough]];

      case 4:
	h[3] += static_cast<uint64_t>(data[3]) << 24;
	[[fallthrough]];

      case 3:
	h[0] += static_cast<uint64_t>(data[2]) << 16;
	[[fallthrough]];

      case 2:
	h[1] += static_cast<uint64_t>(data[1]) << 8;
	[[fallthrough]];

      case 1:
	h[2] += data[0];
    }

    mixA(h);
    mixB(h, seed);
    mixA(h);

    //printf("h: %016llx %016llx %016llx %016llx\n", h[0], h[1], h[2], h[3]);

    g = 0;
    g -= h[2];
    g -= h[3];

    PUT_U64<bswap>(g, static_cast<uint8_t *>(out), 0);
    if (hashsize == 128) {
      mixA(h);
      g = 0;
      g -= h[3];
      g -= h[2];
      PUT_U64<bswap>(g, static_cast<uint8_t *>(out), 8);
    } else if ( hashsize == 256) {
      mixA(h);
      g = 0;
      g -= h[3];
      g -= h[2];
      PUT_U64<bswap>(g, static_cast<uint8_t *>(out), 8);
      mixA(h);
      mixB(h, seed);
      mixA(h);
      g = 0;
      g -= h[3];
      g -= h[2];
      PUT_U64<bswap>(g, static_cast<uint8_t *>(out), 16);
      mixA(h);
      g = 0;
      g -= h[3];
      g -= h[2];
      PUT_U64<bswap>(g, static_cast<uint8_t *>(out), 24);
    }
  }
}

#ifdef __EMSCRIPTEN__
// Then outside the namespace, you declare the rainstorm function with C linkage.
extern "C" {
  KEEPALIVE void rainbowHash64(const void* in, const size_t len, const seed_t seed, void* out) {
    rainbow::rainbow<64, false>(in, len, seed, out);
  }

  KEEPALIVE void rainbowHash128(const void* in, const size_t len, const seed_t seed, void* out) {
    rainbow::rainbow<128, false>(in, len, seed, out);
  }

  KEEPALIVE void rainbowHash256(const void* in, const size_t len, const seed_t seed, void* out) {
    rainbow::rainbow<256, false>(in, len, seed, out);
  }
}
#endif
