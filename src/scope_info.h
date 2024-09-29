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

#include <vector>

struct Vehicle;
struct BaseStation;
struct Window;

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

/**
 * This is a set of helper functions to print useful info from within a SCOPE_INFO_FMT statement.
 * The use of a struct is so that when used as an argument to SCOPE_INFO_FMT/seprintf/etc, the buffer lives
 * on the stack with a lifetime which lasts until the end of the statement.
 * This avoids needing to call malloc(), which is technically unsafe within the crash logger signal handler,
 * writing directly into the seprintf buffer, or the use of a separate static buffer.
 */
struct scope_dumper {
	const char *CompanyInfo(int company_id);
	const char *VehicleInfo(const Vehicle *v);
	const char *StationInfo(const BaseStation *st);
	const char *TileInfo(TileIndex tile);
	const char *WindowInfo(const Window *w);

private:
	char buffer[512];
};

#endif /* SCOPE_INFO_H */
