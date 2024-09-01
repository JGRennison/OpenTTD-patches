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
#include <vector>

struct CommandDeserialisationBuffer : public BufferDeserialisationHelper<CommandDeserialisationBuffer> {
	const uint8_t *buffer;
	size_t size;
	size_t pos = 0;
	bool error = false;

	CommandDeserialisationBuffer(const uint8_t *buffer, size_t size) : buffer(buffer), size(size) {}

	const uint8_t *GetDeserialisationBuffer() const { return this->buffer; }
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
	std::vector<uint8_t> &buffer;
	size_t limit;

	CommandSerialisationBuffer(std::vector<uint8_t> &buffer, size_t limit) : buffer(buffer), limit(limit) {}

	std::vector<uint8_t> &GetSerialisationBuffer() { return this->buffer; }
	size_t GetSerialisationLimit() const { return this->limit; }
};

struct CommandAuxiliarySerialised : public CommandAuxiliaryBase {
	std::vector<uint8_t> serialised_data;
	mutable std::string debug_summary;

	CommandAuxiliaryBase *Clone() const override
	{
		return new CommandAuxiliarySerialised(*this);
	}

	virtual std::optional<CommandAuxiliaryDeserialisationSrc> GetDeserialisationSrc() const override
	{
		return CommandAuxiliaryDeserialisationSrc{ std::span<const uint8_t>(this->serialised_data.data(), this->serialised_data.size()), this->debug_summary };
	}

	virtual void Serialise(CommandSerialisationBuffer &buffer) const override { buffer.Send_binary(this->serialised_data.data(), this->serialised_data.size()); }

	virtual std::string GetDebugSummary() const override { return std::move(this->debug_summary); }
};

template <typename T>
struct CommandAuxiliarySerialisable : public CommandAuxiliaryBase {
	virtual std::optional<CommandAuxiliaryDeserialisationSrc> GetDeserialisationSrc() const override { return {}; }

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
		std::optional<CommandAuxiliaryDeserialisationSrc> deserialise_from = base->GetDeserialisationSrc();
		if (deserialise_from.has_value()) {
			this->store = T();
			CommandDeserialisationBuffer buffer(deserialise_from->src.data(), deserialise_from->src.size());
			CommandCost res = this->store->Deserialise(buffer);
			if (res.Failed()) return res;
			if (buffer.error || buffer.pos != buffer.size) {
				/* Other deserialisation error or wrong number of bytes read */
				return CMD_ERROR;
			}
			this->data = &(*(this->store));
			deserialise_from->debug_summary = this->data->GetDebugSummary();
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

