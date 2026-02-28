/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_table.cpp Command table definition. */

#define CMD_DEFINE

#include "stdafx.h"
#include "command_serialisation.h"
#include "command_table.h"
#include "core/format_variant.hpp"

#include "autoreplace_cmd.h"
#include "company_cmd.h"
#include "depot_cmd.h"
#include "engine_cmd.h"
#include "goal_cmd.h"
#include "group_cmd.h"
#include "industry_cmd.h"
#include "landscape_cmd.h"
#include "league_cmd.h"
#include "misc_cmd.h"
#include "news_cmd.h"
#include "object_cmd.h"
#include "order_cmd.h"
#include "plans_cmd.h"
#include "programmable_signals_cmd.h"
#include "rail_cmd.h"
#include "road_cmd.h"
#include "settings_cmd.h"
#include "signs_cmd.h"
#include "station_cmd.h"
#include "story_cmd.h"
#include "subsidy_cmd.h"
#include "tbtr_template_vehicle_cmd.h"
#include "terraform_cmd.h"
#include "timetable_cmd.h"
#include "town_cmd.h"
#include "tracerestrict_cmd.h"
#include "train_cmd.h"
#include "tree_cmd.h"
#include "tunnelbridge_cmd.h"
#include "vehicle_cmd.h"
#include "viewport_cmd.h"
#include "water_cmd.h"
#include "waypoint_cmd.h"

template <class T>
concept IsCmdDataT = requires(T payload) {
	[]<typename... X>(CmdDataT<X...>&){}(payload);
};

template <typename Parent, TupleCmdDataFlags flags, typename... X>
constexpr std::integral_constant<TupleCmdDataFlags, flags> GetAutoFmtTupleCmdDataFlagsAsType(AutoFmtTupleCmdData<Parent, flags, X...>&) {
	return {};
};

template <class T>
concept IsAutoFmtTupleCmdData = requires(T payload) {
	GetAutoFmtTupleCmdDataFlagsAsType(payload);
};

template <class T>
static constexpr TupleCmdDataFlags AutoFmtTupleCmdDataFlags = decltype(GetAutoFmtTupleCmdDataFlagsAsType(std::declval<T &>()))::value;

enum class CmdTypeID : uint8_t {
	Uint8,
	Int8,
	Uint16,
	Int16,
	Uint32,
	Int32,
	Uint64,
	Int64,
	Bool,
	String,
	EncodedString,
	Invalid,
};

template <typename T>
constexpr CmdTypeID GetCmdType()
{
	/* See: Recv_generic */
	if constexpr (std::is_same_v<T, bool>) {
		return CmdTypeID::Bool;
	} else if constexpr (std::is_same_v<T, std::string>) {
		return CmdTypeID::String;
	} else if constexpr (std::is_same_v<T, EncodedString>) {
		return CmdTypeID::EncodedString;
	} else if constexpr (SerialisationAsBase<T>) {
		return GetCmdType<typename T::BaseType>();
	} else if constexpr (requires { std::declval<T>().Deserialise(std::declval<DeserialisationBuffer &>(), StringValidationSettings{}); }) {
		return CmdTypeID::Invalid; // Can't handle this in simple path
	} else if constexpr (std::is_enum_v<T>) {
		return GetCmdType<std::underlying_type_t<T>>();
	} else if constexpr (std::is_integral_v<T>) {
		if constexpr (sizeof(T) == 1) {
			return std::is_signed<T>::value ? CmdTypeID::Int8 : CmdTypeID::Uint8;
		} else if constexpr (sizeof(T) == 2) {
			return std::is_signed<T>::value ? CmdTypeID::Int16 : CmdTypeID::Uint16;
		} else if constexpr (sizeof(T) == 4) {
			return std::is_signed<T>::value ? CmdTypeID::Int32 : CmdTypeID::Uint32;
		} else if constexpr (sizeof(T) == 8) {
			return std::is_signed<T>::value ? CmdTypeID::Int64 : CmdTypeID::Uint64;
		}
	}
	return CmdTypeID::Invalid;
}

constexpr bool IsCmdTypeString(CmdTypeID ct)
{
	return ct == CmdTypeID::String || ct == CmdTypeID::EncodedString;
}

template <typename H> struct UseSimplePathHelper;

template <typename... Targs>
struct UseSimplePathHelper<TypeList<Targs...>> {
	static constexpr bool AnyStrings = (IsCmdTypeString(GetCmdType<Targs>()) || ...);
	static constexpr bool AllValid = ((GetCmdType<Targs>() != CmdTypeID::Invalid) && ...);
};

template <typename T>
constexpr bool UseSimplePath()
{
	if constexpr (PayloadHasTupleCmdDataTag<T>) {
		return T::ValueCount < 64 && sizeof(T) < (1 << 10) && UseSimplePathHelper<typename T::Types>::AllValid;
	}
	return false;
}

template <bool no_tile, typename F, typename T, size_t... Tindices>
static inline CommandCost CommandExecTrampolineTuple(F proc, TileIndex tile, DoCommandFlags flags, const T &payload, std::index_sequence<Tindices...>)
{
	if constexpr (no_tile) {
		return proc(flags, payload.template GetValue<Tindices>()...);
	} else {
		return proc(flags, tile, payload.template GetValue<Tindices>()...);
	}
}

template <Commands Cmd>
static CommandCost CmdExecTrampoline(const CommandExecData &exec_data)
{
	using T = CommandTraits<Cmd>;
	using H = CommandHandlerTraits<Cmd>;
	using Payload = typename T::PayloadType;
	const Payload &data = static_cast<const Payload &>(exec_data.payload);
	if constexpr (PayloadHasTupleCmdDataTag<typename T::PayloadType>) {
		return CommandExecTrampolineTuple<T::output_no_tile>(H::proc, exec_data.tile, exec_data.flags, data, std::make_index_sequence<Payload::ValueCount>{});
	} else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(H::proc)>, CommandProcDirect<Payload>>) {
		return H::proc(exec_data.flags, exec_data.tile, data);
	} else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(H::proc)>, CommandProcDirectNoTile<Payload>>) {
		return H::proc(exec_data.flags, data);
	} else {
		static_assert(!H::proc, "Unexpected command proc type");
		static_assert(false, "Unexpected command proc type");
	}
}

template <typename T>
CommandPayloadBaseUniquePtr DeserialiseCmdPayload(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation)
{
	auto payload = std::make_unique<T>();
	if (!payload->Deserialise(buffer, default_string_validation)) payload = nullptr;
	return CommandPayloadBaseUniquePtr(payload.release());
}

template <typename T>
static constexpr CommandPayloadDeserialiser *MakePayloadDeserialiser()
{
	if constexpr (UseSimplePath<T>() && PayloadHasValueTupleCmdDataTag<T>) {
		return nullptr; // Use the simple path for deserialisation
	} else {
		return &DeserialiseCmdPayload<T>;
	}
}

/* Helpers to generate the master command table from the command traits. */
template <Commands Cmd>
inline constexpr CommandInfo CommandFromTrait() noexcept
{
	using T = CommandTraits<Cmd>;
	using H = CommandHandlerTraits<Cmd>;
	using Payload = typename T::PayloadType;
	static_assert(std::is_final_v<Payload>);
	return { &CmdExecTrampoline<Cmd>, MakePayloadDeserialiser<Payload>(), Payload::operations, H::name, T::flags, T::type, T::output_no_tile ? CIF_NO_OUTPUT_TILE : CIF_NONE };
};

template <typename T, T... i>
inline constexpr auto MakeCommandsFromTraits(std::integer_sequence<T, i...>) noexcept {
	return std::array<CommandInfo, sizeof...(i)>{{ CommandFromTrait<static_cast<Commands>(i)>()... }};
}

/**
 * The master command table
 *
 * This table contains the CommandInfo for all possible commands.
 */
const std::array<CommandInfo, to_underlying(CMD_END)> _command_proc_table = MakeCommandsFromTraits(std::make_integer_sequence<std::underlying_type_t<Commands>, CMD_END>{});

/**
 * Set client ID for this command payload using the field returned by Payload::GetClientIDField().
 * This provided payload must have already been type-checked as valid for cmd.
 * Not many commands set CMD_CLIENT_ID so a series of ifs is not too onerous.
 */
void SetPreCheckedCommandPayloadClientID(Commands cmd, CommandPayloadBase &payload, ClientID client_id)
{
	static_assert(INVALID_CLIENT_ID == (ClientID)0);

	auto cmd_check = [&]<Commands Tcmd>() -> bool {
		if constexpr (CommandTraits<Tcmd>::flags.Test(CommandFlag::ClientID)) {
			if (cmd == Tcmd) {
				SetCommandPayloadClientID(static_cast<CmdPayload<Tcmd> &>(payload), client_id);
				return true;
			}
		}
		return false;
	};

	using Tseq = std::underlying_type_t<Commands>;
	auto cmd_loop = [&]<Tseq... Tindices>(std::integer_sequence<Tseq, Tindices...>) {
		(cmd_check.template operator()<static_cast<Commands>(Tindices)>() || ...);
	};
	cmd_loop(std::make_integer_sequence<Tseq, static_cast<Tseq>(CMD_END)>{});
}

void TupleCmdDataDetail::FmtSimpleTupleArgs(format_target &output, size_t count, fmt::format_args args)
{
	for (size_t i = 0; i < count; i++) {
		if (i != 0) output.append(", ");
		auto arg = args.get(static_cast<int>(i));
		output.vformat("{}", fmt::format_args(&arg, 1));
	}
}

/*
 * From: https://stackoverflow.com/questions/70647441/how-to-determine-the-offset-of-an-element-of-a-tuple-at-compile-time
 */
template <typename Payload, size_t I>
constexpr size_t GetPayloadElementOffset()
{
	union u {
		constexpr u() : a{} {}  // GCC bug needs a constructor definition
		constexpr ~u() {}
		char a[sizeof(Payload)]{};
		Payload t;
	} x;
	auto* p = std::addressof(x.t.template GetValue<I>());
	for (std::size_t i = 0;; ++i) {
		if (static_cast<void*>(x.a + i) == p) return i;
	}
}

template <typename Payload>
constexpr bool IsPayloadBaseAtStart()
{
	Payload *p = nullptr;
	CommandPayloadBase *base = p;
	return static_cast<void*>(p) == static_cast<void*>(base);
}

template <typename T>
struct SimpleDescriptorBuilder {
	using ArrayType = std::array<uint16_t, T::ValueCount + 1>;
	using Types = T::Types;

	template <size_t I>
	static constexpr uint16_t MakeDescriptorField()
	{
		constexpr CmdTypeID field_type = GetCmdType<std::tuple_element_t<I, Types>>();
		static_assert(field_type != CmdTypeID::Invalid);
		return static_cast<uint16_t>((to_underlying(field_type) << 10) | GetPayloadElementOffset<T, I>());
	}

	template <size_t... Tindices>
	static constexpr ArrayType MakeDescriptor(std::index_sequence<Tindices...>)
	{
		return ArrayType{ static_cast<uint16_t>((T::ValueCount << 10) | sizeof(T)), MakeDescriptorField<Tindices>()... };
	}

	static inline const ArrayType descriptor = MakeDescriptor(std::make_index_sequence<T::ValueCount>{});
};

struct SimpleDescriptorHelper {
	const uint16_t *descriptor;

	SimpleDescriptorHelper(const uint16_t *descriptor) : descriptor(descriptor) {}

	inline size_t GetSize()
	{
		return GB(this->descriptor[0], 0, 10);
	}

	inline size_t GetFieldCount()
	{
		return GB(this->descriptor[0], 10, 6);
	}

	inline std::pair<CmdTypeID, size_t> GetField(size_t field_num)
	{
		const uint16_t field = this->descriptor[field_num + 1];
		return {static_cast<CmdTypeID>(GB(field, 10, 6)), GB(field, 0, 10)};
	}

	struct CopyConstructHelper {
		const char *from;
		char *to;

		template <typename T>
		void Exec()
		{
			new (this->to) T(*reinterpret_cast<const T *>(this->from));
		}
	};

	struct SerialiseHelper {
		const char *from;
		struct BufferSerialisationRef buffer;

		template <typename T>
		void Exec()
		{
			this->buffer.Send_generic(*reinterpret_cast<const T *>(this->from));
		}
	};

	struct ConstructDeserialiseHelper {
		char *to;
		DeserialisationBuffer &buffer;
		StringValidationSettings default_string_validation;

		template <typename T>
		void Exec()
		{
			T *ptr = new (this->to) T();
			this->buffer.Recv_generic(*ptr, this->default_string_validation);
		}
	};

	struct FormatHelper {
		const char *from;
		format_target &output;

		template <typename T>
		void Exec()
		{
			if constexpr (!std::is_same_v<T, EncodedString>) {
				output.format("{}", *reinterpret_cast<const T *>(this->from));
			}
		}
	};

	template <typename T>
	static void DestructField(char *ptr)
	{
		reinterpret_cast<T *>(ptr)->~T();
	}

	template <typename V>
	static void VisitType(CmdTypeID ftype, V &&visitor)
	{
		switch (ftype) {
			case CmdTypeID::Uint8:
				visitor.template Exec<uint8_t>();
				break;

			case CmdTypeID::Int8:
				visitor.template Exec<int8_t>();
				break;

			case CmdTypeID::Bool:
				visitor.template Exec<bool>();
				break;

			case CmdTypeID::Uint16:
				visitor.template Exec<uint16_t>();
				break;

			case CmdTypeID::Int16:
				visitor.template Exec<int16_t>();
				break;

			case CmdTypeID::Uint32:
				visitor.template Exec<uint32_t>();
				break;

			case CmdTypeID::Int32:
				visitor.template Exec<int32_t>();
				break;

			case CmdTypeID::Uint64:
				visitor.template Exec<uint64_t>();
				break;

			case CmdTypeID::Int64:
				visitor.template Exec<int64_t>();
				break;

			case CmdTypeID::String:
				visitor.template Exec<std::string>();
				break;

			case CmdTypeID::EncodedString:
				visitor.template Exec<EncodedString>();
				break;

			default:
				NOT_REACHED();
		}
	}
};

static CommandPayloadBaseUniquePtr SimpleCloner(const CommandPayloadBase *ptr)
{
	const CommandPayloadBase::Operations &ops = ptr->GetOperations();
	SimpleDescriptorHelper helper(ops.descriptor);
	const size_t fields = helper.GetFieldCount();

	const char *src = reinterpret_cast<const char *>(ptr);
	char *storage = static_cast<char *>(::operator new(helper.GetSize()));
	memcpy(storage, src, sizeof(CommandPayloadBase));
	for (size_t i = 0; i < fields; i++) {
		auto [ftype, offset] = helper.GetField(i);

		SimpleDescriptorHelper::CopyConstructHelper cch{src + offset, storage + offset};

		switch (ftype) {
			case CmdTypeID::Uint8:
			case CmdTypeID::Int8:
			case CmdTypeID::Bool:
				cch.Exec<uint8_t>();
				break;

			case CmdTypeID::Uint16:
			case CmdTypeID::Int16:
				cch.Exec<uint16_t>();
				break;

			case CmdTypeID::Uint32:
			case CmdTypeID::Int32:
				cch.Exec<uint32_t>();
				break;

			case CmdTypeID::Uint64:
			case CmdTypeID::Int64:
				cch.Exec<uint64_t>();
				break;

			case CmdTypeID::String:
			case CmdTypeID::EncodedString:
				cch.Exec<std::string>();
				break;

			default:
				NOT_REACHED();
		}
	}

	return CommandPayloadBaseUniquePtr(reinterpret_cast<CommandPayloadBase *>(storage));
}

static CommandPayloadBaseUniquePtr TrivialCloner(const CommandPayloadBase *ptr)
{
	const CommandPayloadBase::Operations &ops = ptr->GetOperations();
	SimpleDescriptorHelper helper(ops.descriptor);
	const size_t size = helper.GetSize();

	char *storage = static_cast<char *>(::operator new(size));
	memcpy(storage, ptr, size);
	return CommandPayloadBaseUniquePtr(reinterpret_cast<CommandPayloadBase *>(storage));
}

static void SimpleDeleter(CommandPayloadBase *ptr)
{
	const CommandPayloadBase::Operations &ops = ptr->GetOperations();
	SimpleDescriptorHelper helper(ops.descriptor);
	const size_t fields = helper.GetFieldCount();

	char *src = reinterpret_cast<char *>(ptr);
	for (size_t i = 0; i < fields; i++) {
		auto [ftype, offset] = helper.GetField(i);

		switch (ftype) {
			case CmdTypeID::String:
			case CmdTypeID::EncodedString:
				SimpleDescriptorHelper::DestructField<std::string>(src + offset);
				break;

			default:
				break;
		}
	}

	::operator delete(static_cast<void *>(ptr));
}

static void TrivialDeleter(CommandPayloadBase *ptr)
{
	::operator delete(static_cast<void *>(ptr));
}

CommandPayloadBaseUniquePtr DeserialiseSimpleCommandPayload(const CommandPayloadBase::Operations &ops, DeserialisationBuffer &buffer, StringValidationSettings default_string_validation)
{
	SimpleDescriptorHelper helper(ops.descriptor);
	const size_t size = helper.GetSize();
	const size_t fields = helper.GetFieldCount();

	char *storage = static_cast<char *>(::operator new(size));

	struct CommandPayloadWrap : public CommandPayloadBase {
		CommandPayloadWrap(const CommandPayloadBase::Operations &ops) : CommandPayloadBase(ops) {}
	};
	new (storage) CommandPayloadWrap(ops);

	for (size_t i = 0; i < fields; i++) {
		auto [ftype, offset] = helper.GetField(i);

		SimpleDescriptorHelper::VisitType(ftype, SimpleDescriptorHelper::ConstructDeserialiseHelper{storage + offset, buffer, default_string_validation});
	}

	return CommandPayloadBaseUniquePtr(reinterpret_cast<CommandPayloadBase *>(storage));
}

static void SimpleSerialiser(const CommandPayloadBase *ptr, struct BufferSerialisationRef buffer)
{
	const CommandPayloadBase::Operations &ops = ptr->GetOperations();
	SimpleDescriptorHelper helper(ops.descriptor);
	const size_t fields = helper.GetFieldCount();

	const char *src = reinterpret_cast<const char *>(ptr);
	for (size_t i = 0; i < fields; i++) {
		auto [ftype, offset] = helper.GetField(i);

		SimpleDescriptorHelper::VisitType(ftype, SimpleDescriptorHelper::SerialiseHelper{src + offset, buffer});
	}
}

static void SimpleSanitiseStrings(CommandPayloadBase *ptr, StringValidationSettings settings)
{
	const CommandPayloadBase::Operations &ops = ptr->GetOperations();
	SimpleDescriptorHelper helper(ops.descriptor);
	const size_t fields = helper.GetFieldCount();

	char *src = reinterpret_cast<char *>(ptr);
	for (size_t i = 0; i < fields; i++) {
		auto [ftype, offset] = helper.GetField(i);

		switch (ftype) {
			case CmdTypeID::String:
				TupleCmdDataDetail::SanitiseGeneric(*reinterpret_cast<std::string *>(src + offset), settings);
				break;

			case CmdTypeID::EncodedString:
				TupleCmdDataDetail::SanitiseGeneric(*reinterpret_cast<EncodedString *>(src + offset), settings);
				break;

			default:
				break;
		}
	}
}

static void NullFormatDebugSummary(const CommandPayloadBase *, struct format_target &) {}

template <typename Payload, bool SKIP_STRINGS>
static void CommandFormatDebugSummaryList(const CommandPayloadBase *ptr, format_target &output)
{
	static_assert(std::is_final_v<Payload>);
	const Payload *payload = static_cast<const Payload *>(ptr);
	if constexpr (SKIP_STRINGS) {
		TupleCmdDataDetail::FmtSimpleTupleData(output, *payload, TupleCmdDataDetail::NonStringTypeIndexSequence<typename Payload::Types>{});
	} else {
		TupleCmdDataDetail::FmtSimpleTupleData(output, *payload, std::make_index_sequence<Payload::ValueCount>{});
	}
}

template <typename Payload, bool SKIP_STRINGS>
static void CommandFormatDebugSummaryCustom(const CommandPayloadBase *ptr, format_target &output)
{
	static_assert(std::is_final_v<Payload>);
	const Payload *payload = static_cast<const Payload *>(ptr);
	if constexpr (SKIP_STRINGS) {
		TupleCmdDataDetail::FmtTupleDataTuple<Payload::fmt_str>(output, *payload, TupleCmdDataDetail::NonStringTypeIndexSequence<typename Payload::Types>{});
	} else {
		TupleCmdDataDetail::FmtTupleDataTuple<Payload::fmt_str>(output, *payload, std::make_index_sequence<Payload::ValueCount>{});
	}
}

template <typename T>
constexpr bool CommandFormatDebugUsingDescriptorIsValid()
{
	if constexpr (std::is_same_v<T, bool>) {
		return true;
	} else if constexpr (std::is_integral_v<T>) {
		return sizeof(T) >= 1 && sizeof(T) <= 8;
	} else if constexpr (std::is_same_v<T, std::string>) {
		return true;
	} else if constexpr (std::is_same_v<T, EncodedString>) {
		return true;
	} else if constexpr (format_detail::FmtAsBase<T>) {
		return CommandFormatDebugUsingDescriptorIsValid<typename T::BaseType>();
	} else if constexpr (std::is_enum_v<T>) {
		return CommandFormatDebugUsingDescriptorIsValid<std::underlying_type_t<T>>();
	}
	return false;
}

template <typename H> struct CommandFormatDebugUsingDescriptorHelper;

template <typename... Targs>
struct CommandFormatDebugUsingDescriptorHelper<TypeList<Targs...>> {
	static constexpr bool AllValid = (CommandFormatDebugUsingDescriptorIsValid<Targs>() && ...);
};

static void CommandFormatDebugSummaryUsingDescriptor(const CommandPayloadBase *ptr, format_target &output, bool skip_strings)
{
	const CommandPayloadBase::Operations &ops = ptr->GetOperations();
	SimpleDescriptorHelper helper(ops.descriptor);
	const size_t fields = helper.GetFieldCount();

	const char *src = reinterpret_cast<const char *>(ptr);
	bool written_any = false;
	for (size_t i = 0; i < fields; i++) {
		auto [ftype, offset] = helper.GetField(i);

		if (skip_strings && (ftype == CmdTypeID::String || ftype == CmdTypeID::EncodedString)) continue;

		if (written_any) {
			output.append(", ");
		} else {
			written_any = true;
		}
		SimpleDescriptorHelper::VisitType(ftype, SimpleDescriptorHelper::FormatHelper{src + offset, output});
	}
}

static void CommandFormatDebugSummarySimple(const CommandPayloadBase *ptr, format_target &output)
{
	CommandFormatDebugSummaryUsingDescriptor(ptr, output, false);
}

static void CommandFormatDebugSummarySimpleSkipStrings(const CommandPayloadBase *ptr, format_target &output)
{
	CommandFormatDebugSummaryUsingDescriptor(ptr, output, true);
}

struct PayloadOpsBuilder {
	template <typename T>
	static CommandPayloadBaseUniquePtr Cloner(const CommandPayloadBase *ptr)
	{
		static_assert(std::is_final_v<T>);
		return CommandPayloadBaseUniquePtr(new T(*static_cast<const T *>(ptr)));
	}

	template <typename T>
	static void Deleter(CommandPayloadBase *ptr)
	{
		static_assert(std::is_final_v<T>);
		delete static_cast<T *>(ptr);
	}

	template <typename T>
	static void SerialiseTuplePayload(const CommandPayloadBase *ptr, BufferSerialisationRef buffer)
	{
		static_assert(std::is_final_v<T>);
		static_assert(PayloadHasTupleCmdDataTag<T>);
		const T *payload = static_cast<const T *>(ptr);
		auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
			((buffer.Send_generic(payload->template GetValue<Tindices>())), ...);
		};
		handler(std::make_index_sequence<T::ValueCount>{});
	}

	template <typename T>
	static void SanitiseTuplePayloadStrings(CommandPayloadBase *ptr, StringValidationSettings settings)
	{
		static_assert(std::is_final_v<T>);
		static_assert(PayloadHasTupleCmdDataTag<T>);
		T *payload = static_cast<T *>(ptr);
		auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
			((TupleCmdDataDetail::SanitiseGeneric(payload->template GetValue<Tindices>(), settings)), ...);
		};
		handler(std::make_index_sequence<T::ValueCount>{});
	}

	template <typename T>
	static void Serialiser(const CommandPayloadBase *ptr, struct BufferSerialisationRef buffer)
	{
		static_cast<const T *>(ptr)->SerialisePayload(buffer);
	}

	template <typename T>
	static void SanitiseStrings(CommandPayloadBase *ptr, StringValidationSettings settings)
	{
		static_cast<T *>(ptr)->SanitisePayloadStrings(settings);
	}

	template <typename T>
	static void FormatDebugSummary(const CommandPayloadBase *ptr, struct format_target &output)
	{
		static_cast<const T *>(ptr)->FormatDebugSummary(output);
	}

	template <typename T, bool skip_strings, bool enable_simple>
	static constexpr CommandPayloadBase::FormatDebugSummaryFn GetFormatDebugSummaryList()
	{
		if constexpr (enable_simple && CommandFormatDebugUsingDescriptorHelper<typename T::Types>::AllValid) {
			return skip_strings ? &CommandFormatDebugSummarySimpleSkipStrings : &CommandFormatDebugSummarySimple;
		} else {
			return &CommandFormatDebugSummaryList<T, skip_strings>;
		}
	}

	template <typename T, bool enable_simple>
	static constexpr CommandPayloadBase::FormatDebugSummaryFn GetFormatDebugSummary()
	{
		static_assert(std::is_final_v<T>);
		if constexpr (!T::HasFormatDebugSummary) {
			return &NullFormatDebugSummary;
		} else if constexpr (requires { T::FormatDebugSummary(nullptr, std::declval<struct format_target &>()); }) {
			return &T::FormatDebugSummary;
		} else if constexpr (requires { std::declval<T>().FormatDebugSummary(std::declval<struct format_target &>()); }) {
			return &FormatDebugSummary<T>;
		} else if constexpr (IsCmdDataT<T>) {
			return GetFormatDebugSummaryList<T, T::HasStringType, enable_simple>();
		} else if constexpr (IsAutoFmtTupleCmdData<T>) {
			constexpr bool skip_strings = !(AutoFmtTupleCmdDataFlags<T> & TCDF_STRINGS) && T::HasStringType;
			if constexpr (std::string_view(T::fmt_str).size() == 0) {
				return GetFormatDebugSummaryList<T, skip_strings, enable_simple>();
			} else {
				return &CommandFormatDebugSummaryCustom<T, skip_strings>;
			}
		} else {
			static_assert(false, "Command payload: FormatDebugSummary implementation missing or incorrect");
		}
	}

	template <typename T>
	static constexpr CommandPayloadBase::Operations BuildCustom()
	{
		CommandPayloadBase::SerialiseFn serialise;
		if constexpr (PayloadHasTupleCmdDataTag<T>) {
			serialise = &SerialiseTuplePayload<T>;
		} else if constexpr (requires { T::SerialisePayload(nullptr, std::declval<struct BufferSerialisationRef>()); }) {
			serialise = &T::SerialisePayload;
		} else {
			serialise = &Serialiser<T>;
		}

		CommandPayloadBase::SanitiseStringsFn sanitise_strings = nullptr;
		if constexpr (T::HasStringSanitiser) {
			if constexpr (PayloadHasTupleCmdDataTag<T>) {
				sanitise_strings = &SanitiseTuplePayloadStrings<T>;
			} else if constexpr (requires { T::SanitisePayloadStrings(nullptr, StringValidationSettings{}); }) {
				sanitise_strings = &T::SanitisePayloadStrings;
			} else {
				sanitise_strings = &SanitiseStrings<T>;
			}
		}

		return {
			&Cloner<T>,
			&Deleter<T>,
			serialise,
			sanitise_strings,
			GetFormatDebugSummary<T, false>(),
			nullptr
		};
	}

	template <typename T>
	static constexpr CommandPayloadBase::Operations BuildSimple()
	{
		static_assert(std::is_final_v<T>);

		using Helper = UseSimplePathHelper<typename T::Types>;

		CommandPayloadBase::CloneFn cloner;
		CommandPayloadBase::DeleterFn deleter;
		if constexpr (!PayloadHasValueTupleCmdDataTag<T>) {
			cloner = &Cloner<T>; // Need to use instantiated clone method
			deleter = &Deleter<T>;
		} else {
			static_assert(IsPayloadBaseAtStart<T>());
			static_assert(sizeof(typename T::TupleCmdDataType) == sizeof(T));
			if constexpr (Helper::AnyStrings) {
				cloner = &SimpleCloner;
				deleter = &SimpleDeleter;
			} else {
				cloner = &TrivialCloner;
				deleter = &TrivialDeleter;
			}
		}

		CommandPayloadBase::SanitiseStringsFn sanitise_strings = nullptr;
		if constexpr (T::HasStringSanitiser) {
			sanitise_strings = &SimpleSanitiseStrings;
		}

		return {
			cloner,
			deleter,
			&SimpleSerialiser,
			sanitise_strings,
			GetFormatDebugSummary<T, true>(),
			SimpleDescriptorBuilder<T>::descriptor.data(),
		};
	}

	template <typename T>
	static constexpr CommandPayloadBase::Operations Build()
	{
		if constexpr (UseSimplePath<T>()) {
			return BuildSimple<T>();
		} else {
			return BuildCustom<T>();
		}
	}
};

template<typename T>
const CommandPayloadBase::Operations CommandPayloadSerialisable<T>::operations = PayloadOpsBuilder::Build<T>();

template <typename Parent, typename... T>
const CommandPayloadBase::Operations TupleCmdData<Parent, T...>::operations = PayloadOpsBuilder::Build<TupleCmdData<Parent, T...>::RealParent>();

/* This isn't directly referenced in the command table, so ensure it is instantiated here. */
template<>
const CommandPayloadBase::Operations CommandPayloadSerialisable<TraceRestrictFollowUpCmdData>::operations = PayloadOpsBuilder::Build<TraceRestrictFollowUpCmdData>();
