/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strings_internal.h Types and functions related to the internal workings of formatting OpenTTD's strings. */

#ifndef STRINGS_INTERNAL_H
#define STRINGS_INTERNAL_H

#include "string_func.h"
#include "strings_type.h"
#include "core/strong_typedef_type.hpp"

#include <array>

/** The data required to format and validate a single parameter of a string. */
struct StringParameter {
	uint64_t data; ///< The data of the parameter.
	std::unique_ptr<std::string> string; ///< Copied string value, if it has any.
	char32_t type; ///< The #StringControlCode to interpret this data with when it's the first parameter, otherwise '\0'.
};

class StringParameters {
protected:
	StringParameters *parent = nullptr; ///< If not nullptr, this instance references data from this parent instance.
	std::span<StringParameter> parameters = {}; ///< Array with the actual parameters.

	size_t offset = 0; ///< Current offset in the parameters span.
	char32_t next_type = 0; ///< The type of the next data that is retrieved.

	StringParameters(std::span<StringParameter> parameters = {}) :
		parameters(parameters)
	{}

	const StringParameter &GetNextParameterReference();

public:
	/**
	 * Create a new StringParameters instance that can reference part of the data of
	 * the given parent instance.
	 */
	StringParameters(StringParameters &parent, size_t size) :
		parent(&parent),
		parameters(parent.parameters.subspan(parent.offset, size))
	{}

	void PrepareForNextRun();
	void SetTypeOfNextParameter(char32_t type) { this->next_type = type; }

	/**
	 * Get the current offset, so it can be backed up for certain processing
	 * steps, or be used to offset the argument index within sub strings.
	 * @return The current offset.
	 */
	size_t GetOffset() { return this->offset; }

	/**
	 * Set the offset within the string from where to return the next result of
	 * \c GetInt64 or \c GetInt32.
	 * @param offset The offset.
	 */
	void SetOffset(size_t offset)
	{
		/*
		 * The offset must be fewer than the number of parameters when it is
		 * being set. Unless restoring a backup, then the original value is
		 * correct as well as long as the offset was not changed. In other
		 * words, when the offset was already at the end of the parameters and
		 * the string did not consume any parameters.
		 */
		assert(offset < this->parameters.size() || this->offset == offset);
		this->offset = offset;
	}

	/**
	 * Advance the offset within the string from where to return the next result of
	 * \c GetInt64 or \c GetInt32.
	 * @param advance The amount to advance the offset by.
	 */
	void AdvanceOffset(size_t advance)
	{
		this->offset += advance;
		assert(this->offset <= this->parameters.size());
	}

	/**
	 * Get the next parameter from our parameters.
	 * This updates the offset, so the next time this is called the next parameter
	 * will be read.
	 * @return The next parameter's value.
	 */
	template <typename T>
	T GetNextParameter()
	{
		const auto &param = GetNextParameterReference();
		return static_cast<T>(param.data);
	}

	/**
	 * Get the next string parameter from our parameters.
	 * This updates the offset, so the next time this is called the next parameter
	 * will be read.
	 * @return The next parameter's value.
	 */
	const char *GetNextParameterString()
	{
		const auto &param = GetNextParameterReference();
		return param.string != nullptr ? param.string->c_str() : nullptr;
	}

	/**
	 * Get a new instance of StringParameters that is a "range" into the
	 * remaining existing parameters. Upon destruction the offset in the parent
	 * is not updated. However, calls to SetDParam do update the parameters.
	 *
	 * The returned StringParameters must not outlive this StringParameters.
	 * @return A "range" of the string parameters.
	 */
	StringParameters GetRemainingParameters() { return GetRemainingParameters(this->offset); }

	/**
	 * Get a new instance of StringParameters that is a "range" into the
	 * remaining existing parameters from the given offset. Upon destruction the
	 * offset in the parent is not updated. However, calls to SetDParam do
	 * update the parameters.
	 *
	 * The returned StringParameters must not outlive this StringParameters.
	 * @param offset The offset to get the remaining parameters for.
	 * @return A "range" of the string parameters.
	 */
	StringParameters GetRemainingParameters(size_t offset)
	{
		return StringParameters(this->parameters.subspan(offset, this->parameters.size() - offset));
	}

	/** Return the amount of elements which can still be read. */
	size_t GetDataLeft() const
	{
		return this->parameters.size() - this->offset;
	}

	/** Get the type of a specific element. */
	char32_t GetTypeAtOffset(size_t offset) const
	{
		assert(offset < this->parameters.size());
		return this->parameters[offset].type;
	}

	void SetParam(size_t n, uint64_t v)
	{
		assert(n < this->parameters.size());
		this->parameters[n].data = v;
		this->parameters[n].string.reset();
	}

	template <typename T, std::enable_if_t<std::is_base_of<StrongTypedefBase, T>::value, int> = 0>
	void SetParam(size_t n, T v)
	{
		SetParam(n, v.base());
	}

	void SetParam(size_t n, const char *str)
	{
		assert(n < this->parameters.size());
		this->parameters[n].data = 0;
		this->parameters[n].string = std::make_unique<std::string>(str);
	}

	void SetParam(size_t n, std::string_view str)
	{
		assert(n < this->parameters.size());
		this->parameters[n].data = 0;
		this->parameters[n].string = std::make_unique<std::string>(str);
	}

	void SetParam(size_t n, std::string &&str)
	{
		assert(n < this->parameters.size());
		this->parameters[n].data = 0;
		this->parameters[n].string = std::make_unique<std::string>(std::move(str));
	}

	uint64_t GetParam(size_t n) const
	{
		assert(n < this->parameters.size());
		assert(this->parameters[n].string == nullptr);
		return this->parameters[n].data;
	}

	/**
	 * Get the stored string of the parameter, or \c nullptr when there is none.
	 * @param n The index into the parameters.
	 * @return The stored string.
	 */
	const char *GetParamStr(size_t n) const
	{
		assert(n < this->parameters.size());
		auto &param = this->parameters[n];
		return param.string != nullptr ? param.string->c_str() : nullptr;
	}
};

/**
 * Extension of StringParameters with its own statically sized buffer for
 * the parameters.
 */
template <size_t N>
class ArrayStringParameters : public StringParameters {
	std::array<StringParameter, N> params{}; ///< The actual parameters

public:
	ArrayStringParameters()
	{
		this->parameters = std::span(params.data(), params.size());
	}

	ArrayStringParameters(ArrayStringParameters&& other) noexcept
	{
		*this = std::move(other);
	}

	ArrayStringParameters& operator=(ArrayStringParameters &&other) noexcept
	{
		this->offset = other.offset;
		this->next_type = other.next_type;
		this->params = std::move(other.params);
		this->parameters = std::span(params.data(), params.size());
		return *this;
	}

	ArrayStringParameters(const ArrayStringParameters &other) = delete;
	ArrayStringParameters& operator=(const ArrayStringParameters &other) = delete;
};

/**
 * Helper to create the StringParameters with its own buffer with the given
 * parameter values.
 * @param args The parameters to set for the to be created StringParameters.
 * @return The constructed StringParameters.
 */
template <typename... Args>
static auto MakeParameters(const Args&... args)
{
	ArrayStringParameters<sizeof...(args)> parameters;
	size_t index = 0;
	(parameters.SetParam(index++, std::forward<const Args&>(args)), ...);
	return parameters;
}

class StringBuilder;

void GetStringWithArgs(StringBuilder builder, StringID string, StringParameters &args, uint case_index = 0, bool game_script = false);
std::string GetStringWithArgs(StringID string, StringParameters &args);

void GetString(StringBuilder builder, StringID string);

/* Do not leak the StringBuilder to everywhere. */
void GenerateTownNameString(StringBuilder builder, size_t lang, uint32_t seed);
void GetTownName(StringBuilder builder, const struct Town *t);
void GRFTownNameGenerate(StringBuilder builder, uint32_t grfid, uint16_t gen, uint32_t seed);

#endif /* STRINGS_INTERNAL_H */
