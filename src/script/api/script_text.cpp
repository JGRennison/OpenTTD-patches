/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_text.cpp Implementation of ScriptText. */

#include "../../stdafx.h"
#include "../../string_func.h"
#include "../../strings_func.h"
#include "../../game/game_text.hpp"
#include "script_text.hpp"
#include "script_log.hpp"
#include "../script_fatalerror.hpp"
#include "../script_instance.hpp"
#include "script_log.hpp"
#include "../../table/control_codes.h"
#include "../../core/format.hpp"

#include "table/strings.h"

#include "../../safeguards.h"


ScriptText::ScriptText(HSQUIRRELVM vm)
{
	int nparam = sq_gettop(vm) - 1;
	if (nparam < 1) {
		throw sq_throwerror(vm, "You need to pass at least a StringID to the constructor");
	}

	/* First resolve the StringID. */
	SQInteger sqstring;
	if (SQ_FAILED(sq_getinteger(vm, 2, &sqstring))) {
		throw sq_throwerror(vm, "First argument must be a valid StringID");
	}
	this->string = StringIndexInTab(sqstring);

	/* The rest of the parameters must be arguments. */
	for (int i = 0; i < nparam - 1; i++) {
		/* Push the parameter to the top of the stack. */
		sq_push(vm, i + 3);

		if (SQ_FAILED(this->_SetParam(i, vm))) {
			this->~ScriptText();
			throw sq_throwerror(vm, "Invalid parameter");
		}

		/* Pop the parameter again. */
		sq_pop(vm, 1);
	}
}

SQInteger ScriptText::_SetParam(int parameter, HSQUIRRELVM vm)
{
	if (parameter >= SCRIPT_TEXT_MAX_PARAMETERS) return SQ_ERROR;

	switch (sq_gettype(vm, -1)) {
		case OT_STRING: {
			const SQChar *value;
			sq_getstring(vm, -1, &value);

			this->param[parameter] = StrMakeValid(value);
			break;
		}

		case OT_INTEGER: {
			SQInteger value;
			sq_getinteger(vm, -1, &value);

			this->param[parameter] = value;
			break;
		}

		case OT_INSTANCE: {
			SQUserPointer real_instance = nullptr;
			HSQOBJECT instance;

			sq_getstackobj(vm, -1, &instance);

			/* Validate if it is a GSText instance */
			sq_pushroottable(vm);
			sq_pushstring(vm, "GSText", -1);
			sq_get(vm, -2);
			sq_pushobject(vm, instance);
			if (sq_instanceof(vm) != SQTrue) return SQ_ERROR;
			sq_pop(vm, 3);

			/* Get the 'real' instance of this class */
			sq_getinstanceup(vm, -1, &real_instance, nullptr);
			if (real_instance == nullptr) return SQ_ERROR;

			ScriptText *value = static_cast<ScriptText *>(real_instance);
			this->param[parameter] = ScriptTextRef(value);
			break;
		}

		default: return SQ_ERROR;
	}

	if (this->paramc <= parameter) this->paramc = parameter + 1;
	return 0;
}

SQInteger ScriptText::SetParam(HSQUIRRELVM vm)
{
	if (sq_gettype(vm, 2) != OT_INTEGER) return SQ_ERROR;

	SQInteger k;
	sq_getinteger(vm, 2, &k);

	if (k > SCRIPT_TEXT_MAX_PARAMETERS) return SQ_ERROR;
	if (k < 1) return SQ_ERROR;
	k--;

	return this->_SetParam(k, vm);
}

SQInteger ScriptText::AddParam(HSQUIRRELVM vm)
{
	SQInteger res;
	res = this->_SetParam(this->paramc, vm);
	if (res != 0) return res;

	/* Push our own instance back on top of the stack */
	sq_push(vm, 1);
	return 1;
}

SQInteger ScriptText::_set(HSQUIRRELVM vm)
{
	int32_t k;

	if (sq_gettype(vm, 2) == OT_STRING) {
		const SQChar *key_string;
		sq_getstring(vm, 2, &key_string);

		std::string str = StrMakeValid(key_string);
		if (!str.starts_with("param_") || str.size() > 8) return SQ_ERROR;

		k = stoi(str.substr(6));
	} else if (sq_gettype(vm, 2) == OT_INTEGER) {
		SQInteger key;
		sq_getinteger(vm, 2, &key);
		k = (int32_t)key;
	} else {
		return SQ_ERROR;
	}

	if (k > SCRIPT_TEXT_MAX_PARAMETERS) return SQ_ERROR;
	if (k < 1) return SQ_ERROR;
	k--;

	return this->_SetParam(k, vm);
}

std::string ScriptText::GetEncodedText()
{
	int param_count = 0;
	std::string result;
	auto output = std::back_inserter(result);
	if (this->GetActiveInstance()->IsTextParamMismatchAllowed()) {
		static StringIDList seen_ids;
		seen_ids.clear();
		this->_GetEncodedTextTraditional(output, param_count, seen_ids);
	} else {
		static ScriptTextList seen_texts;
		seen_texts.clear();
		ParamList params;
		this->_FillParamList(params, seen_texts);
		this->_GetEncodedText(output, param_count, params, true);
	}
	if (param_count > SCRIPT_TEXT_MAX_PARAMETERS) throw Script_FatalError(fmt::format("{}: Too many parameters", GetGameStringName(this->string)));
	return result;
}

void ScriptText::_TextParamError(std::string msg)
{
	if (this->GetActiveInstance()->IsTextParamMismatchAllowed()) {
		ScriptLog::LogOnce(ScriptLogTypes::LOG_ERROR, std::move(msg));
	} else {
		throw Script_FatalError(std::move(msg));
	}
}

void ScriptText::_GetEncodedTextTraditional(std::back_insert_iterator<std::string> &output, int &param_count, StringIDList &seen_ids)
{
	const std::string &name = GetGameStringName(this->string);

	if (std::find(seen_ids.begin(), seen_ids.end(), this->string) != seen_ids.end()) throw Script_FatalError(fmt::format("{}: Circular reference detected", name));
	seen_ids.push_back(this->string);

	Utf8Encode(output, SCC_ENCODED);
	fmt::format_to(output, "{:X}", this->string);

	auto write_param_fallback = [&](int idx) {
		if (std::holds_alternative<ScriptTextRef>(this->param[idx])) {
			int count = 1; // 1 because the string id is included in consumed parameters
			std::get<ScriptTextRef>(this->param[idx])->_GetEncodedTextTraditional(output, count, seen_ids);
			param_count += count;
		} else if (std::holds_alternative<SQInteger>(this->param[idx])) {
			Utf8Encode(output, SCC_ENCODED_NUMERIC);
			fmt::format_to(output, "{:X}", std::get<SQInteger>(this->param[idx]));
			param_count++;
		} else {
			/* Fallback value */
			Utf8Encode(output, SCC_ENCODED_NUMERIC);
			fmt::format_to(output, "0");
			param_count++;
		}
	};

	const StringParams &params = GetGameStringParams(this->string);
	int cur_idx = 0;

	for (const StringParam &cur_param : params) {
		if (cur_idx >= this->paramc) {
			this->_TextParamError(fmt::format("{}: Not enough parameters", name));
			break;
		}

		*output = SCC_RECORD_SEPARATOR;

		switch (cur_param.type) {
			case StringParam::RAW_STRING:
				if (!std::holds_alternative<std::string>(this->param[cur_idx])) {
					this->_TextParamError(fmt::format("{}: Parameter {} expects a raw string", name, cur_idx));
					write_param_fallback(cur_idx++);
					break;
				}
				Utf8Encode(output, SCC_ENCODED_STRING);
				fmt::format_to(output, "{}", std::get<std::string>(this->param[cur_idx++]));
				param_count++;
				break;

			case StringParam::STRING: {
				if (!std::holds_alternative<ScriptTextRef>(this->param[cur_idx])) {
					this->_TextParamError(fmt::format("{}: Parameter {} expects a substring", name, cur_idx));
					write_param_fallback(cur_idx++);
					break;
				}
				int count = 1; // 1 because the string id is included in consumed parameters
				std::get<ScriptTextRef>(this->param[cur_idx++])->_GetEncodedTextTraditional(output, count, seen_ids);
				if (count != cur_param.consumes) {
					this->_TextParamError(fmt::format("{}: Parameter {} substring consumes {}, but expected {} to be consumed", name, cur_idx, count - 1, cur_param.consumes - 1));
				}
				param_count += count;
				break;
			}

			default:
				if (cur_idx + cur_param.consumes > this->paramc) {
					this->_TextParamError(fmt::format("{}: Not enough parameters", name));
				}
				for (int i = 0; i < cur_param.consumes && cur_idx < this->paramc; i++) {
					if (!std::holds_alternative<SQInteger>(this->param[cur_idx])) {
						this->_TextParamError(fmt::format("{}: Parameter {} expects an integer", name, cur_idx));
						write_param_fallback(cur_idx++);
						continue;
					}
					Utf8Encode(output, SCC_ENCODED_NUMERIC);
					fmt::format_to(output, "{:X}", std::get<SQInteger>(this->param[cur_idx++]));
					param_count++;
				}
				break;
		}
	}

	for (int i = cur_idx; i < this->paramc; i++) {
		*output = SCC_RECORD_SEPARATOR;
		write_param_fallback(i);
	}

	seen_ids.pop_back();
}

void ScriptText::_FillParamList(ParamList &params, ScriptTextList &seen_texts)
{
	if (std::ranges::find(seen_texts, this) != seen_texts.end()) throw Script_FatalError(fmt::format("{}: Circular reference detected", GetGameStringName(this->string)));
	seen_texts.push_back(this);

	for (int i = 0; i < this->paramc; i++) {
		Param *p = &this->param[i];
		params.emplace_back(this->string, i, p);
		if (!std::holds_alternative<ScriptTextRef>(*p)) continue;
		std::get<ScriptTextRef>(*p)->_FillParamList(params, seen_texts);
	}

	seen_texts.pop_back();

	/* Fill with dummy parameters to match FormatString() behaviour. */
	if (seen_texts.empty()) {
		static Param dummy = 0;
		int nb_extra = SCRIPT_TEXT_MAX_PARAMETERS - (int)params.size();
		for (int i = 0; i < nb_extra; i++)
			params.emplace_back(StringIndexInTab(-1), i, &dummy);
	}
}

void ScriptText::ParamCheck::Encode(std::back_insert_iterator<std::string> &output, const char *cmd)
{
	if (this->cmd == nullptr) this->cmd = cmd;
	if (this->used) return;

	struct visitor {
		std::back_insert_iterator<std::string> &output;

		void operator()(const std::string &value)
		{
			Utf8Encode(this->output, SCC_ENCODED_STRING);
			fmt::format_to(this->output, "{}", value);
		}

		void operator()(const SQInteger &value)
		{
			Utf8Encode(this->output, SCC_ENCODED_NUMERIC);
			/* Sign-extend the value, then store as unsigned */
			fmt::format_to(this->output, "{:X}", static_cast<uint64_t>(static_cast<int64_t>(value)));
		}

		void operator()(const ScriptTextRef &value)
		{
			Utf8Encode(this->output, SCC_ENCODED);
			fmt::format_to(this->output, "{:X}", value->string);
		}
	};

	*output = SCC_RECORD_SEPARATOR;
	std::visit(visitor{output}, *this->param);
	this->used = true;
}

void ScriptText::_GetEncodedText(std::back_insert_iterator<std::string> &output, int &param_count, ParamSpan args, bool first)
{
	const std::string &name = GetGameStringName(this->string);

	if (first) {
		Utf8Encode(output, SCC_ENCODED);
		fmt::format_to(output, "{:X}", this->string);
	}

	const StringParams &params = GetGameStringParams(this->string);

	size_t idx = 0;
	auto get_next_arg = [&]() {
		if (idx >= args.size()) throw Script_FatalError(fmt::format("{}({}): Not enough parameters", name, param_count + 1));
		ParamCheck &pc = args[idx++];
		if (pc.owner != this->string) ScriptLog::Warning(fmt::format("{}({}): Consumes {}({})", name, param_count + 1, GetGameStringName(pc.owner), pc.idx + 1));
		return &pc;
	};
	auto skip_args = [&](size_t nb) { idx += nb; };

	for (const StringParam &cur_param : params) {
		try {
			switch (cur_param.type) {
				case StringParam::UNUSED:
					skip_args(cur_param.consumes);
					break;

				case StringParam::RAW_STRING:
				{
					ParamCheck &p = *get_next_arg();
					p.Encode(output, cur_param.cmd);
					if (p.cmd != cur_param.cmd) throw 1;
					if (!std::holds_alternative<std::string>(*p.param)) ScriptLog::Error(fmt::format("{}({}): {{{}}} expects a raw string", name, param_count + 1, cur_param.cmd));
					break;
				}

				case StringParam::STRING:
				{
					ParamCheck &p = *get_next_arg();
					p.Encode(output, cur_param.cmd);
					if (p.cmd != cur_param.cmd) throw 1;
					if (!std::holds_alternative<ScriptTextRef>(*p.param)) {
						ScriptLog::Error(fmt::format("{}({}): {{{}}} expects a GSText", name, param_count + 1, cur_param.cmd));
						param_count++;
						continue;
					}
					int count = 0;
					ScriptTextRef &ref = std::get<ScriptTextRef>(*p.param);
					ref->_GetEncodedText(output, count, args.subspan(idx), false);
					if (++count != cur_param.consumes) {
						ScriptLog::Warning(fmt::format("{}({}): {{{}}} expects {} to be consumed, but {} consumes {}", name, param_count + 1, cur_param.cmd, cur_param.consumes - 1, GetGameStringName(ref->string), count - 1));
						/* Fill missing params if needed. */
						for (int i = count; i < cur_param.consumes; i++) fmt::format_to(output, ":0");
					}
					skip_args(cur_param.consumes - 1);
					break;
				}

				default:
					for (int i = 0; i < cur_param.consumes; i++) {
						ParamCheck &p = *get_next_arg();
						p.Encode(output, i == 0 ? cur_param.cmd : nullptr);
						if (i == 0 && p.cmd != cur_param.cmd) throw 1;
						if (!std::holds_alternative<SQInteger>(*p.param)) ScriptLog::Error(fmt::format("{}({}): {{{}}} expects an integer", name, param_count + i + 1, cur_param.cmd));
					}
			}

			param_count += cur_param.consumes;
		} catch (int nb) {
			param_count += nb;
			ScriptLog::Warning(fmt::format("{}({}): Invalid parameter", name, param_count));
		}
	}
}

const std::string Text::GetDecodedText()
{
	::SetDParamStr(0, this->GetEncodedText());
	return ::GetString(STR_JUST_RAW_STRING);
}
