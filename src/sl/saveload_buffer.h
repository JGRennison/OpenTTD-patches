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

/** A buffer for reading (and buffering) savegame data. */
struct ReadBuffer {
	uint8_t buf[MEMORY_CHUNK_SIZE];     ///< Buffer we're going to read from.
	uint8_t *bufp;                      ///< Location we're at reading the buffer.
	uint8_t *bufe;                      ///< End of the buffer we can read from.
	std::shared_ptr<LoadFilter> reader; ///< The filter used to actually read.
	size_t read;                        ///< The amount of read bytes so far from the filter.

	/**
	 * Initialise our variables.
	 * @param reader The filter to actually read data.
	 */
	ReadBuffer(std::shared_ptr<LoadFilter> reader) : bufp(nullptr), bufe(nullptr), reader(std::move(reader)), read(0)
	{
	}

	static ReadBuffer *GetCurrent();

	void SkipBytesSlowPath(size_t bytes);
	void AcquireBytes();

	inline void SkipBytes(size_t bytes)
	{
		uint8_t *b = this->bufp + bytes;
		if (likely(b <= this->bufe)) {
			this->bufp = b;
		} else {
			SkipBytesSlowPath(bytes);
		}
	}

	inline uint8_t RawReadByte()
	{
		return *this->bufp++;
	}

	inline uint8_t ReadByte()
	{
		if (unlikely(this->bufp == this->bufe)) {
			this->AcquireBytes();
		}

		return RawReadByte();
	}

	inline uint8_t PeekByte()
	{
		if (unlikely(this->bufp == this->bufe)) {
			this->AcquireBytes();
		}

		return *this->bufp;
	}

	inline void CheckBytes(size_t bytes)
	{
		while (unlikely(this->bufp + bytes > this->bufe)) this->AcquireBytes();
	}

	inline int RawReadUint16()
	{
#if OTTD_ALIGNMENT == 0
		int x = FROM_BE16(*((const unaligned_uint16 *) this->bufp));
		this->bufp += 2;
		return x;
#else
		int x = this->RawReadByte() << 8;
		return x | this->RawReadByte();
#endif
	}

	inline uint32_t RawReadUint32()
	{
#if OTTD_ALIGNMENT == 0
		uint32_t x = FROM_BE32(*((const unaligned_uint32 *) this->bufp));
		this->bufp += 4;
		return x;
#else
		uint32_t x = this->RawReadUint16() << 16;
		return x | this->RawReadUint16();
#endif
	}

	inline uint64_t RawReadUint64()
	{
#if OTTD_ALIGNMENT == 0
		uint64_t x = FROM_BE64(*((const unaligned_uint64 *) this->bufp));
		this->bufp += 8;
		return x;
#else
		uint32_t x = this->RawReadUint32();
		uint32_t y = this->RawReadUint32();
		return (uint64_t)x << 32 | y;
#endif
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
			for (size_t i = 0; i < to_copy; i++) {
				handler(this->RawReadByte());
			}
			length -= to_copy;
		}
	}

	template <typename F>
	inline void ReadUint16sToHandler(size_t length, F handler)
	{
		while (length) {
			this->CheckBytes(2);
			size_t to_copy = std::min<size_t>((this->bufe - this->bufp) / 2, length);
			for (size_t i = 0; i < to_copy; i++) {
				handler(this->RawReadUint16());
			}
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
	inline void UnWriteByte()
	{
		this->buf--;
	}

	inline void RawWriteByte(uint8_t b)
	{
		*this->buf++ = b;
	}

	inline void RawWriteUint16(uint16_t v)
	{
#if OTTD_ALIGNMENT == 0
		*((unaligned_uint16 *) this->buf) = TO_BE16(v);
#else
		this->buf[0] = GB(v, 8, 8);
		this->buf[1] = GB(v, 0, 8);
#endif
		this->buf += 2;
	}

	inline void RawWriteUint32(uint32_t v)
	{
#if OTTD_ALIGNMENT == 0
		*((unaligned_uint32 *) this->buf) = TO_BE32(v);
#else
		this->buf[0] = GB(v, 24, 8);
		this->buf[1] = GB(v, 16, 8);
		this->buf[2] = GB(v, 8, 8);
		this->buf[3] = GB(v, 0, 8);
#endif
		this->buf += 4;
	}

	inline void RawWriteUint64(uint64_t v)
	{
#if OTTD_ALIGNMENT == 0
		*((unaligned_uint64 *) this->buf) = TO_BE64(v);
#else
		this->buf[0] = GB(v, 56, 8);
		this->buf[1] = GB(v, 48, 8);
		this->buf[2] = GB(v, 40, 8);
		this->buf[3] = GB(v, 32, 8);
		this->buf[4] = GB(v, 24, 8);
		this->buf[5] = GB(v, 16, 8);
		this->buf[6] = GB(v, 8, 8);
		this->buf[7] = GB(v, 0, 8);
#endif
		this->buf += 8;
	}

	template <typename F>
	inline void WriteBytesFromHandler(size_t length, F handler)
	{
		while (length) {
			this->CheckBytes(1);
			size_t to_copy = std::min<size_t>(this->bufe - this->buf, length);
			for (size_t i = 0; i < to_copy; i++) {
				this->RawWriteByte(handler());
			}
			length -= to_copy;
		}
	}

	template <typename F>
	inline void WriteUint16sFromHandler(size_t length, F handler)
	{
		while (length) {
			this->CheckBytes(2);
			size_t to_copy = std::min<size_t>((this->bufe - this->buf) / 2, length);
			for (size_t i = 0; i < to_copy; i++) {
				this->RawWriteUint16(handler());
			}
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
