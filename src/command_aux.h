/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_aux.h Command auxiliary data. */

#ifndef COMMAND_AUX_H
#define COMMAND_AUX_H

#include "command_type.h"
#include "command_func.h"
#include "string_type.h"
#include "core/serialisation.hpp"
#include <optional>

struct CommandDeserialisationBuffer : public BufferDeserialisationHelper<CommandDeserialisationBuffer> {
	const uint8 *buffer;
	size_t size;
	size_t pos = 0;
	bool error = false;

	CommandDeserialisationBuffer(const uint8 *buffer, size_t size) : buffer(buffer), size(size) {}

	const byte *GetDeserialisationBuffer() const { return this->buffer; }
	size_t GetDeserialisationBufferSize() const { return this->size; }
	size_t &GetDeserialisationPosition() { return this->pos; }

	bool CanDeserialiseBytes(size_t bytes_to_read, bool raise_error)
	{
		if (this->error) return false;

		/* Check if variable is within packet-size */
		if (this->pos + bytes_to_read > this->size) {
			if (raise_error) this->error = true;
			return false;
		}

		return true;
	}
};

struct CommandSerialisationBuffer : public BufferSerialisationHelper<CommandSerialisationBuffer> {
	std::vector<byte> &buffer;
	size_t limit;

	CommandSerialisationBuffer(std::vector<byte> &buffer, size_t limit) : buffer(buffer), limit(limit) {}

	std::vector<byte> &GetSerialisationBuffer() { return this->buffer; }
	size_t GetSerialisationLimit() const { return this->limit; }
};

struct CommandAuxiliarySerialised : public CommandAuxiliaryBase {
	std::vector<byte> serialised_data;

	CommandAuxiliaryBase *Clone() const override
	{
		return new CommandAuxiliarySerialised(*this);
	}

	virtual std::optional<span<const uint8>> GetDeserialisationSrc() const override { return span<const uint8>(this->serialised_data.data(), this->serialised_data.size()); }

	virtual void Serialise(CommandSerialisationBuffer &buffer) const override { buffer.Send_binary((const char *)this->serialised_data.data(), this->serialised_data.size()); }
};

template <typename T>
struct CommandAuxiliarySerialisable : public CommandAuxiliaryBase {
	virtual std::optional<span<const uint8>> GetDeserialisationSrc() const override { return {}; }

	CommandAuxiliaryBase *Clone() const override
	{
		return new T(*static_cast<const T *>(this));
	}
};

template <typename T>
struct CommandAuxData {
private:
	std::optional<T> store;
	const T *data = nullptr;

public:
	inline CommandCost Load(const CommandAuxiliaryBase *base)
	{
		if (base == nullptr) return CMD_ERROR;
		std::optional<span<const uint8>> deserialise_from = base->GetDeserialisationSrc();
		if (deserialise_from.has_value()) {
			this->store = T();
			CommandDeserialisationBuffer buffer(deserialise_from->begin(), deserialise_from->size());
			CommandCost res = this->store->Deserialise(buffer);
			if (res.Failed()) return res;
			if (buffer.error || buffer.pos != buffer.size) {
				/* Other deserialisation error or wrong number of bytes read */
				return CMD_ERROR;
			}
			this->data = &(*(this->store));
			return res;
		} else {
			this->data = dynamic_cast<const T*>(base);
			if (this->data == nullptr) return CMD_ERROR;
			return CommandCost();
		}
	}

	inline const T *operator->() const
	{
		return this->data;
	}

	inline const T &operator*() const
	{
		return *(this->data);
	}
};

#endif /* COMMAND_AUX_H */

