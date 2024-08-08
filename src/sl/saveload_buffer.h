/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file saveload_buffer.h Functions/types related to buffers used for saving and loading games. */

#ifndef SL_SAVELOAD_BUFFER_H
#define SL_SAVELOAD_BUFFER_H

#include "../core/alloc_func.hpp"
#include "../core/endian_type.hpp"
#include "../core/endian_func.hpp"
#include "../core/math_func.hpp"

#include <vector>
#include <utility>

struct LoadFilter;
struct SaveFilter;

/** Save in chunks of 128 KiB. */
static const size_t MEMORY_CHUNK_SIZE = 128 * 1024;

namespace SlSerialise {
	inline uint16_t RawReadUint16At(uint8_t *b)
	{
#if OTTD_ALIGNMENT == 0
		return FROM_BE16(*((const unaligned_uint16 *)b));
#else
		return (b[0] << 8) | b[1];
#endif
	}

	inline uint32_t RawReadUint32At(uint8_t *b)
	{
#if OTTD_ALIGNMENT == 0
		return FROM_BE32(*((const unaligned_uint32 *)b));
#else
		return (RawReadUint16At(b) << 16) | RawReadUint16At(b + 2);
#endif
	}

	inline uint64_t RawReadUint64At(uint8_t *b)
	{
#if OTTD_ALIGNMENT == 0
		return FROM_BE64(*((const unaligned_uint64 *)b));
#else
		uint32_t x = this->RawReadUint32At(b);
		uint32_t y = this->RawReadUint32At(b + 4);
		return (uint64_t)x << 32 | y;
#endif
	}
}

struct RawReadBuffer {
	uint8_t *bufp;                      ///< Location we're at reading the buffer.

	RawReadBuffer(uint8_t *b) : bufp(b) {}

	inline uint8_t RawReadByte()
	{
		return *this->bufp++;
	}

	inline uint16_t RawReadUint16()
	{
		uint16_t x = SlSerialise::RawReadUint16At(this->bufp);
		this->bufp += 2;
		return x;
	}

	inline uint32_t RawReadUint32()
	{
		uint32_t x = SlSerialise::RawReadUint32At(this->bufp);
		this->bufp += 4;
		return x;
	}

	inline uint64_t RawReadUint64()
	{
		uint64_t x = SlSerialise::RawReadUint64At(this->bufp);
		this->bufp += 8;
		return x;
	}
};

/** A buffer for reading (and buffering) savegame data. */
struct ReadBuffer {
	uint8_t *bufp;                      ///< Location we're at reading the buffer.
	uint8_t *bufe;                      ///< End of the buffer we can read from.
	std::shared_ptr<LoadFilter> reader; ///< The filter used to actually read.
	size_t read;                        ///< The amount of read bytes so far from the filter.
	uint8_t buf[MEMORY_CHUNK_SIZE];     ///< Buffer we're going to read from.

	/**
	 * Initialise our variables.
	 * @param reader The filter to actually read data.
	 */
	ReadBuffer(std::shared_ptr<LoadFilter> reader) : bufp(nullptr), bufe(nullptr), reader(std::move(reader)), read(0)
	{
	}

	static ReadBuffer *GetCurrent();

	void SkipBytesSlowPath(size_t bytes);
	void AcquireBytes(size_t bytes = 0);

	inline void SkipBytes(size_t bytes)
	{
		uint8_t *b = this->bufp + bytes;
		if (likely(b <= this->bufe)) {
			this->bufp = b;
		} else {
			SkipBytesSlowPath(bytes);
		}
	}

	inline uint8_t ReadByte()
	{
		if (unlikely(this->bufp == this->bufe)) {
			this->AcquireBytes();
		}

		return *this->bufp++;;
	}

	inline uint8_t PeekByte()
	{
		if (unlikely(this->bufp == this->bufe)) {
			this->AcquireBytes();
		}

		return *this->bufp;
	}

	uint ReadSimpleGamma();

	inline void CheckBytes(size_t bytes)
	{
		if (unlikely(this->bufp + bytes > this->bufe)) this->AcquireBytes(bytes);
	}

	inline RawReadBuffer ReadRawBytes(size_t bytes)
	{
		this->CheckBytes(bytes);
		RawReadBuffer buf(this->bufp);
		this->bufp += bytes;
		return buf;
	}

	inline void CopyBytes(uint8_t *ptr, size_t length)
	{
		while (length) {
			if (unlikely(this->bufp == this->bufe)) {
				this->AcquireBytes();
			}
			size_t to_copy = std::min<size_t>(this->bufe - this->bufp, length);
			memcpy(ptr, this->bufp, to_copy);
			this->bufp += to_copy;
			ptr += to_copy;
			length -= to_copy;
		}
	}

	inline void CopyBytes(std::span<uint8_t> buffer)
	{
		this->CopyBytes(buffer.data(), buffer.size());
	}

	template <typename F>
	inline void ReadBytesToHandler(size_t length, F handler)
	{
		while (length) {
			if (unlikely(this->bufp == this->bufe)) {
				this->AcquireBytes();
			}
			size_t to_copy = std::min<size_t>(this->bufe - this->bufp, length);
			uint8_t *b = this->bufp;
			for (size_t i = 0; i < to_copy; i++) {
				handler(*b++);
			}
			this->bufp = b;
			length -= to_copy;
		}
	}

	template <typename F>
	inline void ReadUint16sToHandler(size_t length, F handler)
	{
		while (length) {
			this->CheckBytes(2);
			size_t to_copy = std::min<size_t>((this->bufe - this->bufp) / 2, length);
			uint8_t *b = this->bufp;
			for (size_t i = 0; i < to_copy; i++) {
				uint16_t val = SlSerialise::RawReadUint16At(b);
				b += 2;
				handler(val);
			}
			this->bufp = b;
			length -= to_copy;
		}
	}

	/**
	 * Get the size of the memory dump made so far.
	 * @return The size.
	 */
	inline size_t GetSize() const
	{
		return this->read - (this->bufe - this->bufp);
	}
};


namespace SlSerialise {
	inline void RawWriteUint16At(uint8_t *b, uint16_t v)
	{
#if OTTD_ALIGNMENT == 0
		*((unaligned_uint16 *)b) = TO_BE16(v);
#else
		b[0] = GB(v, 8, 8);
		b[1] = GB(v, 0, 8);
#endif
	}

	inline void RawWriteUint32At(uint8_t *b, uint32_t v)
	{
#if OTTD_ALIGNMENT == 0
		*((unaligned_uint32 *)b) = TO_BE32(v);
#else
		b[0] = GB(v, 24, 8);
		b[1] = GB(v, 16, 8);
		b[2] = GB(v, 8, 8);
		b[3] = GB(v, 0, 8);
#endif
	}

	inline void RawWriteUint64At(uint8_t *b, uint64_t v)
	{
#if OTTD_ALIGNMENT == 0
		*((unaligned_uint64 *)b) = TO_BE64(v);
#else
		b[0] = GB(v, 56, 8);
		b[1] = GB(v, 48, 8);
		b[2] = GB(v, 40, 8);
		b[3] = GB(v, 32, 8);
		b[4] = GB(v, 24, 8);
		b[5] = GB(v, 16, 8);
		b[6] = GB(v, 8, 8);
		b[7] = GB(v, 0, 8);
#endif
	}
}

struct RawMemoryDumper {
	uint8_t *buf;                       ///< Buffer we're going to write to.

	RawMemoryDumper(uint8_t *b) : buf(b) {}

	inline void RawWriteByte(uint8_t b)
	{
		*this->buf++ = b;
	}

	inline void RawWriteUint16(uint16_t v)
	{
		SlSerialise::RawWriteUint16At(this->buf, v);
		this->buf += 2;
	}

	inline void RawWriteUint32(uint32_t v)
	{
		SlSerialise::RawWriteUint32At(this->buf, v);
		this->buf += 4;
	}

	inline void RawWriteUint64(uint64_t v)
	{
		SlSerialise::RawWriteUint64At(this->buf, v);
		this->buf += 8;
	}

	void RawWriteSimpleGamma(size_t i);
};

/** Container for dumping the savegame (quickly) to memory. */
struct MemoryDumper {
	struct BufferInfo {
		uint8_t *data;
		size_t size = 0;

		BufferInfo(uint8_t *d) : data(d) {}
		~BufferInfo() { free(this->data); }

		BufferInfo(const BufferInfo &) = delete;
		BufferInfo(BufferInfo &&other) : data(other.data), size(other.size) { other.data = nullptr; };
	};

	std::vector<BufferInfo> blocks;         ///< Buffer with blocks of allocated memory.
	uint8_t *buf = nullptr;                 ///< Buffer we're going to write to.
	uint8_t *bufe = nullptr;                ///< End of the buffer we write to.
	size_t completed_block_bytes = 0;       ///< Total byte count of completed blocks.

	uint8_t *autolen_buf = nullptr;
	uint8_t *autolen_buf_end = nullptr;
	uint8_t *saved_buf = nullptr;
	uint8_t *saved_bufe = nullptr;

	MemoryDumper()
	{
		const size_t size = 8192;
		this->autolen_buf = CallocT<uint8_t>(size);
		this->autolen_buf_end = this->autolen_buf + size;
	}

	~MemoryDumper()
	{
		free(this->autolen_buf);
	}

	static MemoryDumper *GetCurrent();

	void FinaliseBlock();
	void AllocateBuffer();

	inline void CheckBytes(size_t bytes)
	{
		if (unlikely(this->buf + bytes > this->bufe)) this->AllocateBuffer();
	}

	/**
	 * Write a single byte into the dumper.
	 * @param b The byte to write.
	 */
	inline void WriteByte(uint8_t b)
	{
		/* Are we at the end of this chunk? */
		if (unlikely(this->buf == this->bufe)) {
			this->AllocateBuffer();
		}

		*this->buf++ = b;
	}

	inline void CopyBytes(const uint8_t *ptr, size_t length)
	{
		while (length) {
			if (unlikely(this->buf == this->bufe)) {
				this->AllocateBuffer();
			}
			size_t to_copy = std::min<size_t>(this->bufe - this->buf, length);
			memcpy(this->buf, ptr, to_copy);
			this->buf += to_copy;
			ptr += to_copy;
			length -= to_copy;
		}
	}

	inline void CopyBytes(std::span<const uint8_t> buffer)
	{
		this->CopyBytes(buffer.data(), buffer.size());
	}

	/** For limited/special purposes only */
	inline void ReplaceLastWrittenByte(uint8_t b)
	{
		*(this->buf - 1) = b;
	}

	inline RawMemoryDumper RawWriteBytes(size_t bytes)
	{
		this->CheckBytes(bytes);
		RawMemoryDumper raw_dumper(this->buf);
		this->buf += bytes;
		return raw_dumper;
	}

	inline RawMemoryDumper BorrowRawWriteBytes(size_t bytes)
	{
		this->CheckBytes(bytes);
		return RawMemoryDumper(this->buf);
	}

	inline void ReturnRawWriteBytes(RawMemoryDumper raw_dumper)
	{
		this->buf = raw_dumper.buf;
	}

	template <typename F>
	inline void WriteBytesFromHandler(size_t length, F handler)
	{
		while (length) {
			this->CheckBytes(1);
			size_t to_copy = std::min<size_t>(this->bufe - this->buf, length);
			uint8_t *b = this->buf;
			for (size_t i = 0; i < to_copy; i++) {
				*b = handler();
				b++;
			}
			this->buf = b;
			length -= to_copy;
		}
	}

	template <typename F>
	inline void WriteUint16sFromHandler(size_t length, F handler)
	{
		while (length) {
			this->CheckBytes(2);
			size_t to_copy = std::min<size_t>((this->bufe - this->buf) / 2, length);
			uint8_t *b = this->buf;
			for (size_t i = 0; i < to_copy; i++) {
				SlSerialise::RawWriteUint16At(b, handler());
				b += 2;
			}
			this->buf = b;
			length -= to_copy;
		}
	}


	void Flush(SaveFilter &writer);
	size_t GetSize() const;
	size_t GetWriteOffsetGeneric() const;
	void StartAutoLength();
	std::span<uint8_t> StopAutoLength();
	bool IsAutoLengthActive() const { return this->saved_buf != nullptr; }
};

#endif /* SL_SAVELOAD_BUFFER_H */
