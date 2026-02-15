//
// wire_format.h
// Shared little-endian serialization helpers for wire protocol encoding/decoding.
// by Dylan Kress
//

#pragma once

#include <stdint.h>

static inline void WriteUint64LE(uint8_t *buffer, uint64_t value)
{
	buffer[0] = (uint8_t)(value);
	buffer[1] = (uint8_t)(value >> 8);
	buffer[2] = (uint8_t)(value >> 16);
	buffer[3] = (uint8_t)(value >> 24);
	buffer[4] = (uint8_t)(value >> 32);
	buffer[5] = (uint8_t)(value >> 40);
	buffer[6] = (uint8_t)(value >> 48);
	buffer[7] = (uint8_t)(value >> 56);
}

static inline uint64_t ReadUint64LE(const uint8_t *buffer)
{
	return (uint64_t)buffer[0] |
		((uint64_t)buffer[1] << 8) |
		((uint64_t)buffer[2] << 16) |
		((uint64_t)buffer[3] << 24) |
		((uint64_t)buffer[4] << 32) |
		((uint64_t)buffer[5] << 40) |
		((uint64_t)buffer[6] << 48) |
		((uint64_t)buffer[7] << 56);
}

static inline void WriteUint32LE(uint8_t *buffer, uint32_t value)
{
	buffer[0] = (uint8_t)(value);
	buffer[1] = (uint8_t)(value >> 8);
	buffer[2] = (uint8_t)(value >> 16);
	buffer[3] = (uint8_t)(value >> 24);
}

static inline uint32_t ReadUint32LE(const uint8_t *buffer)
{
	return (uint32_t)buffer[0] |
		((uint32_t)buffer[1] << 8) |
		((uint32_t)buffer[2] << 16) |
		((uint32_t)buffer[3] << 24);
}

static inline void WriteUint16LE(uint8_t *buffer, uint16_t value)
{
	buffer[0] = (uint8_t)(value);
	buffer[1] = (uint8_t)(value >> 8);
}

static inline uint16_t ReadUint16LE(const uint8_t *buffer)
{
	return (uint16_t)buffer[0] |
		((uint16_t)buffer[1] << 8);
}
