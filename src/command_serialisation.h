/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_serialisation.h Internal template implementations related to command serialisation.
 *
 * Generally only needed by command.cpp and non-trivial commmand payload implementations.
 */

#ifndef COMMAND_SERIALISATION_H
#define COMMAND_SERIALISATION_H

#include "command_type.h"
#include "core/format.hpp"
#include "3rdparty/fmt/ranges.h"

template <typename T>
inline bool EncodedString::Deserialise(T &buffer, StringValidationSettings default_string_validation)
{
	buffer.Recv_string(this->string, default_string_validation | SVS_ALLOW_CONTROL_CODE);
	return true;
}

namespace TupleCmdDataDetail {
	template <typename U>
	void SanitiseGeneric(U &value, StringValidationSettings settings)
	{
		if constexpr (std::is_same_v<U, std::string>) {
			StrMakeValidInPlace(value, settings);
		}
	}

	template <typename T, size_t... Tindices>
	void SanitiseStringsTuple(const T &values, StringValidationSettings settings, std::index_sequence<Tindices...>)
	{
		((SanitiseGeneric(std::get<Tindices>(values), settings)), ...);
	}

	template <typename T, size_t Tindex>
	constexpr auto MakeRefTupleWithoutStringsItem(const T &values)
	{
		const auto &val = std::get<Tindex>(values);
		if constexpr (std::is_same_v<std::remove_cvref_t<decltype(val)>, std::string>) {
			return std::tuple<>();
		} else {
			return std::forward_as_tuple(val);
		}
	}

	template <typename T, size_t... Tindices>
	constexpr auto MakeRefTupleWithoutStrings(const T &values, std::index_sequence<Tindices...>)
	{
		return std::tuple_cat(MakeRefTupleWithoutStringsItem<T, Tindices>(values)...);
	}

	template <auto fmt_str, typename T, size_t... Tindices>
	void FmtTupleDataTuple(format_target &output, const T &values, std::index_sequence<Tindices...>)
	{
		output.format(fmt_str, std::get<Tindices>(values)...);
	}

	template <auto fmt_str, typename T>
	void FmtTupleData(format_target &output, const T &values)
	{
		if constexpr (std::string_view(fmt_str).size() == 0) {
			output.format("{}", fmt::join(values, ", "));
		} else {
			FmtTupleDataTuple<fmt_str, T>(output, values, std::make_index_sequence<std::tuple_size_v<T>>{});
		}
	}
};

template <typename... T>
void TupleCmdDataDetail::BaseTupleCmdData<T...>::Serialise(BufferSerialisationRef buffer) const
{
	buffer.Send_generic(this->values);
}

template <typename... T>
void TupleCmdDataDetail::BaseTupleCmdData<T...>::SanitiseStrings(StringValidationSettings settings)
{
	TupleCmdDataDetail::SanitiseStringsTuple(this->values, settings, std::index_sequence_for<T...>{});
}

template <typename... T>
bool TupleCmdDataDetail::BaseTupleCmdData<T...>::Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation)
{
	buffer.Recv_generic(this->values, default_string_validation);
	return true;
}

template <typename Parent, typename T>
void TupleRefCmdData<Parent, T>::Serialise(BufferSerialisationRef buffer) const
{
	buffer.Send_generic(this->GetValues());
}

template <typename Parent, typename T>
void TupleRefCmdData<Parent, T>::SanitiseStrings(StringValidationSettings settings)
{
	TupleCmdDataDetail::SanitiseStringsTuple(this->GetValues(), settings, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template <typename Parent, typename T>
bool TupleRefCmdData<Parent, T>::Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation)
{
	auto values = this->GetValues(); // This is a std::tie type tuple
	buffer.Recv_generic(values, default_string_validation);
	return true;
}

template <typename Parent, TupleCmdDataFlags flags, typename... T>
void AutoFmtTupleCmdData<Parent, flags, T...>::FormatDebugSummary(format_target &output) const
{
	if constexpr (flags & TCDF_STRINGS) {
		TupleCmdDataDetail::FmtTupleData<Parent::fmt_str>(output, this->values);
	} else {
		TupleCmdDataDetail::FmtTupleData<Parent::fmt_str>(output, TupleCmdDataDetail::MakeRefTupleWithoutStrings(this->values, std::index_sequence_for<T...>{}));
	}
}

#endif /* COMMAND_SERIALISATION_H */
