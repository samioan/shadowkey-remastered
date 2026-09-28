#pragma once
#include <cstdint>

namespace sk {

// M119: a number that tells one projectile or effect apart from every other
// for the life of the process -- the identity engine/frame_interpolation.h
// pairs across two ticks. Handed out by a default member initializer, so a
// copy (vector growth, a spawn returned by value) keeps its original's.
// Nothing in the simulation reads it.
inline uint32_t NextInterpSerial() {
    static uint32_t next = 0;
    return ++next;
}

}  // namespace sk
