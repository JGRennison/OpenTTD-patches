/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file tracerestrict_backup.h Header file for Trace Restrict backup handling */

#ifndef TRACERESTRICT_BACKUP_H
#define TRACERESTRICT_BACKUP_H

#include "tracerestrict.h"
#include "core/typed_container.hpp"
#include "3rdparty/cpp-ring-buffer/ring_buffer.hpp"
#include <vector>

static constexpr uint TRACERESTRICT_MAX_BACKUPS = 16;

struct TraceRestrictProgramBackup {
	uint32_t backup_index;
	TraceRestrictProgramID program_id;
};

struct TraceRestrictCompanyBackups {
	uint32_t next_index = 0;
	jgr::ring_buffer<TraceRestrictProgramBackup> programs;

	void Reset()
	{
		this->next_index = 0;
		this->programs.clear();
	}

	void Append(TraceRestrictProgramID program_id);
	void EvictOldBackups();
};

extern TypedIndexContainer<std::array<TraceRestrictCompanyBackups, MAX_COMPANIES>, CompanyID> _tracerestrict_backups;

bool TraceRestrictTryRegisterBackup(TraceRestrictProgram *prog, CompanyID owner);
void TraceRestrictTryCreateBackupOfProgram(const TraceRestrictProgram *prog, CompanyID owner);
void TraceRestrictDeleteBackup(TraceRestrictProgramID backup_program_id);

#endif /* TRACERESTRICT_BACKUP_H */
