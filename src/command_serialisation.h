/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_serialisation.h Internal template implementations related to command serialisation.
 *
 * Generally only needed by command_table.cpp and non-trivial commmand payload implementations.
 */

#ifndef COMMAND_SERIALISATION_H
#define COMMAND_SERIALISATION_H

#include "command_type.h"
#include "core/format.hpp"

template <typename T>
inline bool EncodedString::Deserialise(T &buffer, StringValidationSettings default_string_validation)
{
	buffer.Recv_string(this->string, default_string_validation.Set(StringValidationSetting::AllowControlCode));
	return true;
}

inline void EncodedString::Sanitise(StringValidationSettings settings)
{
	StrMakeValidInPlace(this->string, settings.Set(StringValidationSetting::AllowControlCode));
}

namespace TupleCmdDataDetail {
	template <typename U>
	void SanitiseGeneric(U &value, StringValidationSettings settings)
	{
		if constexpr (std::is_same_v<U, std::string>) {
			StrMakeValidInPlace(value, settings);
		}
		if constexpr (std::is_same_v<U, EncodedString>) {
			value.Sanitise(settings);
		}
	}

	template <typename T, size_t... Tindices>
	void SanitiseStringsTuple(const T &payload, StringValidationSettings settings, std::index_sequence<Tindices...>)
	{
		((SanitiseGeneric(payload.template GetValue<Tindices>(), settings)), ...);
	}

	template<typename T, size_t I, size_t N, size_t... integers>
	struct NonStringTupleIndexSequenceHelper {
		using type = std::conditional_t<
				CommandPayloadStringType<std::remove_cvref_t<std::tuple_element_t<I, T>>>,
				typename NonStringTupleIndexSequenceHelper<T, I + 1, N, integers...>::type,
				typename NonStringTupleIndexSequenceHelper<T, I + 1, N, integers..., I>::type>;
	};

	template<typename T, size_t N, size_t... integers>
	struct NonStringTupleIndexSequenceHelper<T, N, N, integers...> {
		using type = std::integer_sequence<size_t, integers...>;
	};

	template<typename T>
	using NonStringTupleIndexSequence = NonStringTupleIndexSequenceHelper<T, 0, std::tuple_size_v<T>>::type;

	template <auto fmt_str, typename T, size_t... Tindices>
	inline void FmtTupleDataTuple(format_target &output, const T &payload, std::index_sequence<Tindices...>)
	{
		output.format(fmt_str, payload.template GetValue<Tindices>()...);
	}

	void FmtSimpleTupleArgs(format_target &output, size_t count, fmt::format_args args);

	template <typename T, size_t... Tindices>
	inline void FmtSimpleTupleData(format_target &output, const T &payload, std::index_sequence<Tindices...> seq)
	{
		FmtSimpleTupleArgs(output, seq.size(), make_preprocessed_format_args(payload.template GetValue<Tindices>()...));
	}
};

template <typename Parent, typename... T>
void TupleCmdData<Parent, T...>::SerialisePayload(const CommandPayloadBase *ptr, BufferSerialisationRef buffer)
{
	buffer.Send_generic(static_cast<const Self *>(ptr)->values);
}

template <typename Parent, typename... T>
void TupleCmdData<Parent, T...>::SanitisePayloadStrings(CommandPayloadBase *ptr, StringValidationSettings settings)
{
	TupleCmdDataDetail::SanitiseStringsTuple(*static_cast<Self *>(ptr), settings, std::index_sequence_for<T...>{});
}

template <typename Parent, typename... T>
bool TupleCmdData<Parent, T...>::Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation)
{
	buffer.Recv_generic(this->values, default_string_validation);
	return true;
}

template <typename Parent, typename T>
void TupleRefCmdData<Parent, T>::SerialisePayload(const CommandPayloadBase *ptr, BufferSerialisationRef buffer)
{
	const Self *self = static_cast<const Self *>(ptr);
	auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
		((buffer.Send_generic(self->GetValue<Tindices>())), ...);
	};
	handler(std::make_index_sequence<Parent::ValueCount>{});
}

template <typename Parent, typename T>
void TupleRefCmdData<Parent, T>::SanitisePayloadStrings(CommandPayloadBase *ptr, StringValidationSettings settings)
{
	TupleCmdDataDetail::SanitiseStringsTuple(*static_cast<Self *>(ptr), settings, std::make_index_sequence<Parent::ValueCount>{});
}

template <typename Parent, typename T>
bool TupleRefCmdData<Parent, T>::Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation)
{
	auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
		((buffer.Recv_generic(this->GetValue<Tindices>(), default_string_validation)), ...);
	};
	handler(std::make_index_sequence<Parent::ValueCount>{});
	return true;
}

template <typename Parent, TupleCmdDataFlags flags, typename... T>
void AutoFmtTupleCmdData<Parent, flags, T...>::FormatDebugSummary(const CommandPayloadBase *ptr, format_target &output)
{
	const Self *self = static_cast<const Self *>(ptr);
	if constexpr (std::string_view(Parent::fmt_str).size() == 0) {
		if constexpr ((flags & TCDF_STRINGS) || !TupleCmdData<Parent, T...>::HasStringType) {
			TupleCmdDataDetail::FmtSimpleTupleData(output, *self, std::index_sequence_for<T...>{});
		} else {
			TupleCmdDataDetail::FmtSimpleTupleData(output, *self, TupleCmdDataDetail::NonStringTupleIndexSequence<typename TupleCmdData<Parent, T...>::Tuple>{});
		}
	} else {
		if constexpr ((flags & TCDF_STRINGS) || !TupleCmdData<Parent, T...>::HasStringType) {
			TupleCmdDataDetail::FmtTupleDataTuple<Parent::fmt_str>(output, *self, std::index_sequence_for<T...>{});
		} else {
			TupleCmdDataDetail::FmtTupleDataTuple<Parent::fmt_str>(output, *self, TupleCmdDataDetail::NonStringTupleIndexSequence<typename TupleCmdData<Parent, T...>::Tuple>{});
		}
	}
}

template <typename... T>
void CmdDataT<T...>::FormatDebugSummary(const CommandPayloadBase *ptr, format_target &output)
{
	const Self *self = static_cast<const Self *>(ptr);
	if constexpr (!TupleCmdData<void, T...>::HasStringType) {
		TupleCmdDataDetail::FmtSimpleTupleData(output, *self, std::index_sequence_for<T...>{});
	} else {
		TupleCmdDataDetail::FmtSimpleTupleData(output, *self, TupleCmdDataDetail::NonStringTupleIndexSequence<typename TupleCmdData<void, T...>::Tuple>{});
	}
}

#endif /* COMMAND_SERIALISATION_H */
