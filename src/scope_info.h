/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file scope_info.h Scope info debug functions. */

#ifndef SCOPE_INFO_H
#define SCOPE_INFO_H

#include "tile_type.h"

#if !defined(DISABLE_SCOPE_INFO)

struct ScopeStackRecord {
	using ScopeStackFunctor = void (*)(ScopeStackRecord *, struct format_target &);

	ScopeStackFunctor functor;
	ScopeStackRecord *next;
};

extern ScopeStackRecord *_scope_stack_head;

template <typename T>
struct FunctorScopeStackRecord : public ScopeStackRecord {
private:
	T func;

public:
	FunctorScopeStackRecord(T func) : func(std::move(func))
	{
		this->functor = [](ScopeStackRecord *record, struct format_target &buffer) {
			FunctorScopeStackRecord *self = static_cast<FunctorScopeStackRecord *>(record);
			self->func(buffer);
		};
		this->next = _scope_stack_head;
		_scope_stack_head = this;
	}

	FunctorScopeStackRecord(const FunctorScopeStackRecord &copysrc) = delete;

	~FunctorScopeStackRecord()
	{
		_scope_stack_head = this->next;
	}
};

void WriteScopeLog(struct format_target &buffer);

/**
 * This creates a lambda in the current scope with the specified capture which outputs the given args as a fmt format string.
 * This lambda is then captured by pointer in a ScopeStackRecord which is pushed onto the scope stack
 * The scope stack is popped at the end of the scope
 */
#define SCOPE_INFO_FMT(capture, ...) \
	FunctorScopeStackRecord _sc_lm_ ## __LINE__ (capture (struct format_target &buffer) { \
		buffer.format(__VA_ARGS__); \
	});

#else /* defined(DISABLE_SCOPE_INFO) */

#define SCOPE_INFO_FMT(...) { }

#endif /* DISABLE_SCOPE_INFO */

template <typename TAG, typename T>
struct GeneralFmtDumper : public fmt_formattable {
	T value;
	GeneralFmtDumper(T value) : value(value) {}

	void fmt_format_value(struct format_target &output) const;
};


struct DumpTileInfoTag{};

using CompanyInfoDumper = GeneralFmtDumper<struct Company, int>;
using VehicleInfoDumper = GeneralFmtDumper<struct Vehicle, const struct Vehicle *>;
using StationInfoDumper = GeneralFmtDumper<struct BaseStation, const struct BaseStation *>;
using TileInfoDumper = GeneralFmtDumper<DumpTileInfoTag, TileIndex>;
using WindowInfoDumper = GeneralFmtDumper<struct Window, const struct Window *>;

#endif /* SCOPE_INFO_H */
