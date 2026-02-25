/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file serialisation.hpp Functions related to (de)serialisation of buffers */

#ifndef SERIALISATION_HPP
#define SERIALISATION_HPP

#include "bitmath_func.hpp"
#include "../string_type.h"
#include "../string_func.h"

#include <vector>
#include <string>
#include <tuple>
#include <variant>
#include <limits>

template <typename T>
concept SerialisationAsBase = T::serialisation_as_base || false;

void   BufferSend_bool  (std::vector<uint8_t> &buffer, size_t limit, bool     data);
void   BufferSend_uint8 (std::vector<uint8_t> &buffer, size_t limit, uint8_t  data);
void   BufferSend_uint16(std::vector<uint8_t> &buffer, size_t limit, uint16_t data);
void   BufferSend_uint32(std::vector<uint8_t> &buffer, size_t limit, uint32_t data);
void   BufferSend_uint64(std::vector<uint8_t> &buffer, size_t limit, uint64_t data);
void   BufferSend_varuint(std::vector<uint8_t> &buffer, size_t limit, uint64_t data);
void   BufferSend_string(std::vector<uint8_t> &buffer, size_t limit, const std::string_view data);
size_t BufferSend_binary_until_full(std::vector<uint8_t> &buffer, size_t limit, const uint8_t *begin, const uint8_t *end);
void   BufferSend_binary(std::vector<uint8_t> &buffer, size_t limit, const uint8_t *data, const size_t size);
void   BufferSend_buffer(std::vector<uint8_t> &buffer, size_t limit, const uint8_t *data, const size_t size);
void   BufferSendAtOffset_uint16(std::vector<uint8_t> &buffer, size_t offset, uint16_t data);

template <typename T>
struct BufferSerialisationHelper {
	void Send_bool(bool data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_bool(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_uint8(uint8_t data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_uint8(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_uint8(const SerialisationAsBase auto &data)
	{
		static_assert(sizeof(data.base()) == 1);
		this->Send_uint8((uint8_t)data.base());
	}

	void Send_uint16(uint16_t data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_uint16(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_uint16(const SerialisationAsBase auto &data)
	{
		static_assert(sizeof(data.base()) <= 2);
		this->Send_uint16((uint16_t)data.base());
	}

	void Send_uint32(uint32_t data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_uint32(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_uint32(const SerialisationAsBase auto &data)
	{
		static_assert(sizeof(data.base()) <= 4);
		this->Send_uint32((uint32_t)data.base());
	}

	void Send_uint64(uint64_t data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_uint64(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_varuint(uint64_t data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_varuint(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_string(const std::string_view data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_string(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	size_t Send_binary_until_full(const uint8_t *begin, const uint8_t *end)
	{
		T *self = static_cast<T *>(this);
		return BufferSend_binary_until_full(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), begin, end);
	}

	void Send_binary(const uint8_t *data, const size_t size)
	{
		T *self = static_cast<T *>(this);
		BufferSend_binary(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data, size);
	}

	void Send_binary(std::span<const uint8_t> data)
	{
		this->Send_binary(data.data(), data.size());
	}

	void Send_buffer(const uint8_t *data, const size_t size)
	{
		T *self = static_cast<T *>(this);
		BufferSend_buffer(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data, size);
	}

	void Send_buffer(const std::vector<uint8_t> &data)
	{
		this->Send_buffer(data.data(), data.size());
	}

	void SendAtOffset_uint16(size_t offset, uint16_t data)
	{
		T *self = static_cast<T *>(this);
		BufferSendAtOffset_uint16(self->GetSerialisationBuffer(), offset, data);
	}

	template <typename V>
	void Send_generic_integer(const V &data)
	{
		using Integral = typename std::conditional_t<std::is_enum_v<V>, std::underlying_type<V>, std::type_identity<V>>::type;
		static_assert(std::is_integral_v<Integral>);
		static_assert(sizeof(V) <= 8);
		if constexpr (sizeof(V) <= 1) {
			this->Send_uint8(static_cast<uint8_t>(data));
		} else if constexpr (sizeof(V) == 2) {
			this->Send_uint16(static_cast<uint16_t>(data));
		} else {
			if constexpr (std::is_signed<Integral>::value) {
				/* Zig-zag encode */
				using U = typename std::make_unsigned<Integral>::type;
				U zigzag = (static_cast<U>(data) << 1);
				if (data < 0) zigzag = ~zigzag;
				this->Send_varuint(zigzag);
			} else {
				this->Send_varuint(static_cast<uint64_t>(data));
			}
		}
	}

	template <typename V>
	void Send_generic(const V &data)
	{
		if constexpr (std::is_same_v<V, std::string> || std::is_same_v<V, std::string_view>) {
			this->Send_string(data);
		} else if constexpr (SerialisationAsBase<V>) {
			this->Send_generic_integer(data.base());
		} else if constexpr (requires { data.Serialise(*this); }) {
			data.Serialise(*this);
		} else {
			this->Send_generic_integer(data);
		}
	}

	template <typename... V>
	void Send_generic(const std::tuple<V...> &data)
	{
		auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
			((this->Send_generic(std::get<Tindices>(data))), ...);
		};
		handler(std::index_sequence_for<V...>{});
	}

	template <typename... V>
	void Send_generic(const std::variant<V...> &data)
	{
		static_assert(sizeof...(V) < 256);
		this->Send_uint8(static_cast<uint8_t>(data.index()));

		const size_t idx = data.index();
		auto subhandler = [&]<size_t Tidx>() {
			if (idx == Tidx) this->Send_generic(std::get<Tidx>(data));
		};
		auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
			((subhandler.template operator()<Tindices>()), ...);
		};
		handler(std::index_sequence_for<V...>{});
	}

	void Send_generic(const std::monostate &data)
	{
		/* Do nothing */
	}

	template <typename... V>
	void Send_generic_seq(const V&... data)
	{
		(this->Send_generic(data), ...);
	}

	size_t GetSendOffset() const
	{
		T *self = const_cast<T *>(static_cast<const T *>(this));
		return self->GetSerialisationBuffer().size();
	}

	struct BufferSerialisationRef AsBufferSerialisationRef();
};

void BufferRecvStringValidate(std::string &buffer, StringValidationSettings settings);

template <typename T>
struct BufferDeserialisationHelper {
private:
	const uint8_t *GetBuffer()
	{
		return static_cast<T *>(this)->GetDeserialisationBuffer();
	}

	size_t GetBufferSize()
	{
		return static_cast<T *>(this)->GetDeserialisationBufferSize();
	}

public:
	void RaiseRecvError()
	{
		return static_cast<T *>(this)->RaiseDeserialisationError();
	}

	bool CanRecvBytes(size_t bytes_to_read, bool raise_error = true)
	{
		return static_cast<T *>(this)->CanDeserialiseBytes(bytes_to_read, raise_error);
	}

	/**
	 * Read a boolean from the packet.
	 * @return The read data.
	 */
	bool Recv_bool()
	{
		return this->Recv_uint8() != 0;
	}

	/**
	 * Read a 8 bits integer from the packet.
	 * @return The read data.
	 */
	uint8_t Recv_uint8()
	{
		uint8_t n;

		if (!this->CanRecvBytes(sizeof(n), true)) return 0;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		n = this->GetBuffer()[pos++];
		return n;
	}

	/**
	 * Read a 16 bits integer from the packet.
	 * @return The read data.
	 */
	uint16_t Recv_uint16()
	{
		uint16_t n;

		if (!this->CanRecvBytes(sizeof(n), true)) return 0;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		n  = (uint16_t)this->GetBuffer()[pos++];
		n += (uint16_t)this->GetBuffer()[pos++] << 8;
		return n;
	}

	/**
	 * Read a 32 bits integer from the packet.
	 * @return The read data.
	 */
	uint32_t Recv_uint32()
	{
		uint32_t n;

		if (!this->CanRecvBytes(sizeof(n), true)) return 0;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		n  = (uint32_t)this->GetBuffer()[pos++];
		n += (uint32_t)this->GetBuffer()[pos++] << 8;
		n += (uint32_t)this->GetBuffer()[pos++] << 16;
		n += (uint32_t)this->GetBuffer()[pos++] << 24;
		return n;
	}

	/**
	 * Read a 64 bits integer from the packet.
	 * @return The read data.
	 */
	uint64_t Recv_uint64()
	{
		uint64_t n;

		if (!this->CanRecvBytes(sizeof(n), true)) return 0;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		n  = (uint64_t)this->GetBuffer()[pos++];
		n += (uint64_t)this->GetBuffer()[pos++] << 8;
		n += (uint64_t)this->GetBuffer()[pos++] << 16;
		n += (uint64_t)this->GetBuffer()[pos++] << 24;
		n += (uint64_t)this->GetBuffer()[pos++] << 32;
		n += (uint64_t)this->GetBuffer()[pos++] << 40;
		n += (uint64_t)this->GetBuffer()[pos++] << 48;
		n += (uint64_t)this->GetBuffer()[pos++] << 56;
		return n;
	}

	/**
	 * Read a variable-size encoded bits integer from the packet.
	 * @return The read data.
	 */
	uint64_t Recv_varuint()
	{
		uint8_t first_byte = this->Recv_uint8();
		uint extra_bytes = std::countl_one<uint8_t>(first_byte);
		if (extra_bytes == 0) return first_byte;

		if (!this->CanRecvBytes(extra_bytes, true)) return 0;

		uint64_t result = first_byte & (0x7F >> extra_bytes);

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();
		for (uint i = 0; i < extra_bytes; i++) {
			result <<= 8;
			result |= this->GetBuffer()[pos++];
		}
		return result;
	}

	/**
	 * Reads characters (bytes) from the packet until it finds a '\0', or reaches a
	 * maximum of \c length characters.
	 * When the '\0' has not been reached in the first \c length read characters,
	 * more characters are read from the packet until '\0' has been reached. However,
	 * these characters will not end up in the returned string.
	 * The length of the returned string will be at most \c length - 1 characters.
	 * @param length   The maximum length of the string including '\0'.
	 * @param settings The string validation settings.
	 * @return The validated string.
	 */
	std::string Recv_string(size_t length, StringValidationSettings settings = StringValidationSetting::ReplaceWithQuestionMark)
	{
		assert(length > 1);

		/* Both loops with Recv_uint8 terminate when reading past the end of the
		 * packet as Recv_uint8 then closes the connection and returns 0. */
		std::string str;
		char character;
		while (--length > 0 && (character = this->Recv_uint8()) != '\0') str.push_back(character);

		if (length == 0) {
			/* The string in the packet was longer. Read until the termination. */
			while (this->Recv_uint8() != '\0') {}
		}

		BufferRecvStringValidate(str, settings);
		return str;
	}

	/**
	 * Reads a string till it finds a '\0' in the stream.
	 * @param buffer The buffer to put the data into.
	 * @param settings The string validation settings.
	 */
	void Recv_string(std::string &buffer, StringValidationSettings settings = StringValidationSetting::ReplaceWithQuestionMark)
	{
		/* Don't allow reading from a closed socket */
		if (!this->CanRecvBytes(0, false)) return;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		size_t length = ttd_strnlen((const char *)(this->GetBuffer() + pos), this->GetBufferSize() - pos - 1);
		buffer.assign((const char *)(this->GetBuffer() + pos), length);
		pos += static_cast<std::remove_reference_t<decltype(pos)>>(length + 1);
		BufferRecvStringValidate(buffer, settings);
	}

	/**
	 * Reads binary data.
	 * @param buffer The buffer to put the data into.
	 * @param size   The size of the data.
	 */
	void Recv_binary(uint8_t *buffer, size_t size)
	{
		if (!this->CanRecvBytes(size, true)) return;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		memcpy(buffer, &this->GetBuffer()[pos], size);
		pos += static_cast<std::remove_reference_t<decltype(pos)>>(size);
	}

	/**
	 * Reads binary data.
	 * @param buffer The buffer to put the data into.
	 */
	void Recv_binary(std::span<uint8_t> buffer)
	{
		this->Recv_binary(buffer.data(), buffer.size());
	}

	/**
	 * Returns view of binary data.
	 * @param size   The size of the data.
	 * @return The view of the data.
	 */
	std::span<const uint8_t> Recv_binary_view(size_t size)
	{
		if (!this->CanRecvBytes(size, true)) return {};

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		std::span<const uint8_t> view { &this->GetBuffer()[pos], size };
		pos += static_cast<std::remove_reference_t<decltype(pos)>>(size);

		return view;
	}

	/**
	 * Reads binary data.
	 * @param size   The size of the data.
	 * @return The binary buffer.
	 */
	std::vector<uint8_t> Recv_binary(size_t size)
	{
		std::span<const uint8_t> view = this->Recv_binary_view(size);

		return { view.begin(), view.end() };
	}

	/**
	 * Returns a view of a length-prefixed binary buffer from the packet.
	 * @return The binary buffer.
	 */
	std::span<const uint8_t> Recv_buffer_view()
	{
		uint16_t length = this->Recv_uint16();

		if (!this->CanRecvBytes(length, true)) return {};

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();
		std::span<const uint8_t> buffer { &this->GetBuffer()[pos], length };
		pos += length;

		return buffer;
	}

	/**
	 * Reads a length-prefixed binary buffer from the packet.
	 * @return The binary buffer.
	 */
	std::vector<uint8_t> Recv_buffer()
	{
		std::span<const uint8_t> view = this->Recv_buffer_view();

		return { view.begin(), view.end() };
	}

	template <typename V>
	void Recv_generic_integer(V &data)
	{
		using Integral = typename std::conditional_t<std::is_enum_v<V>, std::underlying_type<V>, std::type_identity<V>>::type;
		static_assert(std::is_integral_v<Integral>);
		static_assert(sizeof(V) <= 8);
		if constexpr (std::is_same_v<Integral, bool>) {
			data = static_cast<V>(this->Recv_bool());
		} else if constexpr (sizeof(V) <= 1) {
			data = static_cast<V>(this->Recv_uint8());
		} else if constexpr (sizeof(V) == 2) {
			data = static_cast<V>(this->Recv_uint16());
		} else {
			uint64_t val = this->Recv_varuint();
			if (unlikely((val & GetBitMaskSC<uint64_t>(0, sizeof(V) * 8)) != val)) this->RaiseRecvError();
			if constexpr (std::is_signed<Integral>::value) {
				/* Zig-zag decode */
				using U = typename std::make_unsigned<Integral>::type;
				data = static_cast<V>((static_cast<U>(val) >> 1) ^ static_cast<U>(-(static_cast<V>(val) & 1)));
			} else {
				data = static_cast<V>(val);
			}
		}
	}

	template <typename V>
	void Recv_generic(V &data, StringValidationSettings settings = StringValidationSetting::ReplaceWithQuestionMark)
	{
		if constexpr (std::is_same_v<V, std::string>) {
			this->Recv_string(data, settings);
		} else if constexpr (SerialisationAsBase<V>) {
			this->Recv_generic_integer(data.edit_base());
		} else if constexpr (requires { data.Deserialise(*this, settings); }) {
			data.Deserialise(*this, settings);
		} else {
			this->Recv_generic_integer(data);
		}
	}

	template <typename... V>
	void Recv_generic(std::tuple<V...> &data, StringValidationSettings settings = StringValidationSetting::ReplaceWithQuestionMark)
	{
		auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
			((this->Recv_generic(std::get<Tindices>(data), settings)), ...);
		};
		handler(std::index_sequence_for<V...>{});
	}

	template <typename... V>
	void Recv_generic(std::variant<V...> &data, StringValidationSettings settings = StringValidationSetting::ReplaceWithQuestionMark)
	{
		static_assert(sizeof...(V) < 256);
		const size_t idx = Recv_uint8();
		auto subhandler = [&]<size_t Tidx>() {
			if (idx == Tidx) {
				std::variant_alternative_t<Tidx, std::variant<V...>> value;
				this->Recv_generic(value, settings);
				data = value;
			}
		};
		auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
			((subhandler.template operator()<Tindices>()), ...);
		};
		handler(std::index_sequence_for<V...>{});
	}

	void Recv_generic(std::monostate &data, StringValidationSettings settings = StringValidationSetting::ReplaceWithQuestionMark)
	{
		/* Do nothing */
	}

	template <typename... V>
	void Recv_generic_seq(StringValidationSettings settings, V&... data)
	{
		(this->Recv_generic(data, settings), ...);
	}

	struct DeserialisationBuffer BorrowAsDeserialisationBuffer();
	void ReturnDeserialisationBuffer(struct DeserialisationBuffer &&);
};

struct BufferSerialisationRef : public BufferSerialisationHelper<BufferSerialisationRef> {
	std::vector<uint8_t> &buffer;
	size_t limit;

	BufferSerialisationRef(std::vector<uint8_t> &buffer, size_t limit = std::numeric_limits<size_t>::max()) : buffer(buffer), limit(limit) {}

	std::vector<uint8_t> &GetSerialisationBuffer() { return this->buffer; }
	size_t GetSerialisationLimit() const { return this->limit; }
};

template <typename T>
BufferSerialisationRef BufferSerialisationHelper<T>::AsBufferSerialisationRef()
{
	T *self = static_cast<T *>(this);
	return BufferSerialisationRef(self->GetSerialisationBuffer(), self->GetSerialisationLimit());
}

struct DeserialisationBuffer : public BufferDeserialisationHelper<DeserialisationBuffer> {
	const uint8_t *buffer;
	size_t size;
	size_t pos = 0;
	bool error = false;

	DeserialisationBuffer(const uint8_t *buffer, size_t size) : buffer(buffer), size(size) {}

	const uint8_t *GetDeserialisationBuffer() const { return this->buffer; }
	size_t GetDeserialisationBufferSize() const { return this->size; }
	size_t &GetDeserialisationPosition() { return this->pos; }

	void RaiseDeserialisationError()
	{
		this->error = true;
	}

	bool CanDeserialiseBytes(size_t bytes_to_read, bool raise_error)
	{
		if (this->error) return false;

		/* Check if variable is within packet-size */
		if (this->pos + bytes_to_read > this->size) {
			if (raise_error) this->RaiseDeserialisationError();
			return false;
		}

		return true;
	}
};

template <typename T>
DeserialisationBuffer BufferDeserialisationHelper<T>::BorrowAsDeserialisationBuffer()
{
	T *self = static_cast<T *>(this);
	auto &pos = self->GetDeserialisationPosition();

	return DeserialisationBuffer(self->GetBuffer() + pos, self->GetBufferSize() - pos);
}

template <typename T>
void BufferDeserialisationHelper<T>::ReturnDeserialisationBuffer(DeserialisationBuffer &&b)
{
	T *self = static_cast<T *>(this);

	if (b.error) {
		/* Propagate error */
		self->RaiseDeserialisationError();
		return;
	}

	auto &pos = self->GetDeserialisationPosition();
	this->CanRecvBytes(b.pos);
	pos += static_cast<std::remove_reference_t<decltype(pos)>>(b.pos);
	b.buffer = nullptr;
}

#endif /* SERIALISATION_HPP */
