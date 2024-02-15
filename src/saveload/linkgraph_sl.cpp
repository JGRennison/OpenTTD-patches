/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph_sl.cpp Code handling saving and loading of link graphs */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/linkgraph_sl_compat.h"

#include "../linkgraph/linkgraph.h"
#include "../linkgraph/linkgraphjob.h"
#include "../linkgraph/linkgraphschedule.h"
#include "../network/network.h"

#include "../safeguards.h"

namespace upstream_sl {

typedef LinkGraph::BaseNode Node;
typedef LinkGraph::BaseEdge Edge;

static uint16_t _num_nodes;
static LinkGraph *_linkgraph; ///< Contains the current linkgraph being saved/loaded.
static NodeID _linkgraph_from; ///< Contains the current "from" node being saved/loaded.
static NodeID _edge_dest_node;
static NodeID _edge_next_edge;

class SlLinkgraphEdge : public DefaultSaveLoadHandler<SlLinkgraphEdge, Node> {
public:
	inline static const SaveLoad description[] = {
		    SLE_VAR(Edge, capacity,                 SLE_UINT32),
		    SLE_VAR(Edge, usage,                    SLE_UINT32),
		SLE_CONDVAR(Edge, travel_time_sum,          SLE_UINT64, SLV_LINKGRAPH_TRAVEL_TIME, SL_MAX_VERSION),
		    SLE_VAR(Edge, last_unrestricted_update, SLE_INT32),
		SLE_CONDVAR(Edge, last_restricted_update,   SLE_INT32, SLV_187, SL_MAX_VERSION),
		   SLEG_VAR("dest_node", _edge_dest_node,   SLE_UINT16),
		SLEG_CONDVAR("next_edge", _edge_next_edge,   SLE_UINT16, SL_MIN_VERSION, SLV_LINKGRAPH_EDGES),
	};
	inline const static SaveLoadCompatTable compat_description = _linkgraph_edge_sl_compat;

	void Save(Node *bn) const override
	{
		NOT_REACHED();
	}

	void Load(Node *bn) const override
	{
		uint16_t max_size = _linkgraph->Size();

		if (IsSavegameVersionBefore(SLV_191)) {
			NOT_REACHED();
		}

		if (IsSavegameVersionBefore(SLV_LINKGRAPH_EDGES)) {
			size_t used_size = IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH) ? max_size : SlGetStructListLength(UINT16_MAX);

			/* ... but as that wasted a lot of space we save a sparse matrix now. */
			for (NodeID to = _linkgraph_from; to != INVALID_NODE; to = _edge_next_edge) {
				if (used_size == 0) SlErrorCorrupt("Link graph structure overflow");
				used_size--;

				if (to >= max_size) SlErrorCorrupt("Link graph structure overflow");
				SlObject(&_linkgraph->edges[std::make_pair(_linkgraph_from, to)], this->GetLoadDescription());
			}

			if (!IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH) && used_size > 0) SlErrorCorrupt("Corrupted link graph");
		} else {
			/* Edge data is now a simple vector and not any kind of matrix. */
			size_t size = SlGetStructListLength(UINT16_MAX);
			for (size_t i = 0; i < size; i++) {
				Edge edge;
				SlObject(&edge, this->GetLoadDescription());
				if (_edge_dest_node >= max_size) SlErrorCorrupt("Link graph structure overflow");
				_linkgraph->edges[std::make_pair(_linkgraph_from, _edge_dest_node)] = edge;
			}
		}
	}
};

class SlLinkgraphNode : public DefaultSaveLoadHandler<SlLinkgraphNode, LinkGraph> {
public:
	inline static const SaveLoad description[] = {
		SLE_CONDVAR(Node, xy,          SLE_UINT32, SLV_191, SL_MAX_VERSION),
		    SLE_VAR(Node, supply,      SLE_UINT32),
		    SLE_VAR(Node, demand,      SLE_UINT32),
		    SLE_VAR(Node, station,     SLE_UINT16),
		    SLE_VAR(Node, last_update, SLE_INT32),
		SLEG_STRUCTLIST("edges", SlLinkgraphEdge),
	};
	inline const static SaveLoadCompatTable compat_description = _linkgraph_node_sl_compat;

	void Save(LinkGraph *lg) const override
	{
		_linkgraph = lg;

		SlSetStructListLength(lg->Size());
		for (NodeID from = 0; from < lg->Size(); ++from) {
			_linkgraph_from = from;
			SlObject(&lg->nodes[from], this->GetDescription());
		}
	}

	void Load(LinkGraph *lg) const override
	{
		_linkgraph = lg;

		uint16_t length = IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH) ? _num_nodes : (uint16_t)SlGetStructListLength(UINT16_MAX);
		lg->Init(length);
		for (NodeID from = 0; from < length; ++from) {
			_linkgraph_from = from;
			SlObject(&lg->nodes[from], this->GetLoadDescription());
		}
	}
};

/**
 * Get a SaveLoad array for a link graph.
 * @return SaveLoad array for link graph.
 */
SaveLoadTable GetLinkGraphDesc()
{
	static const SaveLoad link_graph_desc[] = {
		 SLE_VAR(LinkGraph, last_compression, SLE_VAR_I64 | SLE_FILE_I32),
		SLEG_CONDVAR("num_nodes", _num_nodes, SLE_UINT16, SL_MIN_VERSION, SLV_SAVELOAD_LIST_LENGTH),
		 SLE_VAR(LinkGraph, cargo,            SLE_UINT8),
		SLEG_STRUCTLIST("nodes", SlLinkgraphNode),
	};
	return link_graph_desc;
}

/**
 * Proxy to reuse LinkGraph to save/load a LinkGraphJob.
 * One of the members of a LinkGraphJob is a LinkGraph, but SLEG_STRUCT()
 * doesn't allow us to select a member. So instead, we add a bit of glue to
 * accept a LinkGraphJob, get the LinkGraph, and use that to call the
 * save/load routines for a regular LinkGraph.
 */
class SlLinkgraphJobProxy : public DefaultSaveLoadHandler<SlLinkgraphJobProxy, LinkGraphJob> {
public:
	inline static const SaveLoad description[] = {{}}; // Needed to keep DefaultSaveLoadHandler happy.
	SaveLoadTable GetDescription() const override { return GetLinkGraphDesc(); }
	inline const static SaveLoadCompatTable compat_description = _linkgraph_sl_compat;

	void Save(LinkGraphJob *lgj) const override
	{
		SlObject(const_cast<LinkGraph *>(&lgj->Graph()), this->GetDescription());
	}

	void Load(LinkGraphJob *lgj) const override
	{
		SlObject(const_cast<LinkGraph *>(&lgj->Graph()), this->GetLoadDescription());
	}
};

/**
 * Get a SaveLoad array for a link graph job. The settings struct is derived from
 * the global settings saveload array. The exact entries are calculated when the function
 * is called the first time.
 * It's necessary to keep a copy of the settings for each link graph job so that you can
 * change the settings while in-game and still not mess with current link graph runs.
 * Of course the settings have to be saved and loaded, too, to avoid desyncs.
 * @return Array of SaveLoad structs.
 */
SaveLoadTable GetLinkGraphJobDesc()
{
	static const SaveLoad job_desc[] = {
		SLE_VAR2(LinkGraphJob, "linkgraph.recalc_interval",       settings.recalc_interval,       SLE_UINT16),
		SLE_VAR2(LinkGraphJob, "linkgraph.recalc_time",           settings.recalc_time,           SLE_UINT16),
		SLE_VAR2(LinkGraphJob, "linkgraph.distribution_pax",      settings.distribution_pax,      SLE_UINT8),
		SLE_VAR2(LinkGraphJob, "linkgraph.distribution_mail",     settings.distribution_mail,     SLE_UINT8),
		SLE_VAR2(LinkGraphJob, "linkgraph.distribution_armoured", settings.distribution_armoured, SLE_UINT8),
		SLE_VAR2(LinkGraphJob, "linkgraph.distribution_default",  settings.distribution_default,  SLE_UINT8),
		SLE_VAR2(LinkGraphJob, "linkgraph.accuracy",              settings.accuracy,              SLE_UINT8),
		SLE_VAR2(LinkGraphJob, "linkgraph.demand_distance",       settings.demand_distance,       SLE_UINT8),
		SLE_VAR2(LinkGraphJob, "linkgraph.demand_size",           settings.demand_size,           SLE_UINT8),
		SLE_VAR2(LinkGraphJob, "linkgraph.short_path_saturation", settings.short_path_saturation, SLE_UINT8),

		SLE_VAR2(LinkGraphJob, "join_date",                       join_tick,                      SLE_FILE_I32 | SLE_VAR_U64),
		SLE_VAR(LinkGraphJob, link_graph.index, SLE_UINT16),
		SLEG_STRUCT("linkgraph", SlLinkgraphJobProxy),
	};

	return job_desc;
}

/**
 * Get a SaveLoad array for the link graph schedule.
 * @return SaveLoad array for the link graph schedule.
 */
SaveLoadTable GetLinkGraphScheduleDesc()
{
	static const SaveLoad schedule_desc[] = {
		SLE_REFLIST(LinkGraphSchedule, schedule, REF_LINK_GRAPH),
		SLE_REFLIST(LinkGraphSchedule, running,  REF_LINK_GRAPH_JOB),
	};
	return schedule_desc;
}

/**
 * All link graphs.
 */
struct LGRPChunkHandler : ChunkHandler {
	LGRPChunkHandler() : ChunkHandler('LGRP', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(GetLinkGraphDesc());

		for (LinkGraph *lg : LinkGraph::Iterate()) {
			SlSetArrayIndex(lg->index);
			SlObject(lg, GetLinkGraphDesc());
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(GetLinkGraphDesc(), _linkgraph_sl_compat);

		int index;
		while ((index = SlIterateArray()) != -1) {
			LinkGraph *lg = new (index) LinkGraph();
			SlObject(lg, slt);
		}
	}
};

/**
 * All link graph jobs.
 */
struct LGRJChunkHandler : ChunkHandler {
	LGRJChunkHandler() : ChunkHandler('LGRJ', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(GetLinkGraphJobDesc());

		for (LinkGraphJob *lgj : LinkGraphJob::Iterate()) {
			SlSetArrayIndex(lgj->index);
			SlObject(lgj, GetLinkGraphJobDesc());
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(GetLinkGraphJobDesc(), _linkgraph_job_sl_compat);

		int index;
		while ((index = SlIterateArray()) != -1) {
			LinkGraphJob *lgj = new (index) LinkGraphJob();
			SlObject(lgj, slt);

			GetLinkGraphJobDayLengthScaleAfterLoad(lgj);
		}
	}
};

/**
 * Link graph schedule.
 */
struct LGRSChunkHandler : ChunkHandler {
	LGRSChunkHandler() : ChunkHandler('LGRS', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(GetLinkGraphScheduleDesc());

		SlSetArrayIndex(0);
		SlObject(&LinkGraphSchedule::instance, GetLinkGraphScheduleDesc());
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(GetLinkGraphScheduleDesc(), _linkgraph_schedule_sl_compat);

		if (!IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY) && SlIterateArray() == -1) return;
		SlObject(&LinkGraphSchedule::instance, slt);
		if (!IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY) && SlIterateArray() != -1) SlErrorCorrupt("Too many LGRS entries");
	}

	void FixPointers() const override
	{
		SlObject(&LinkGraphSchedule::instance, GetLinkGraphScheduleDesc());
	}
};

static const LGRPChunkHandler LGRP;
static const LGRJChunkHandler LGRJ;
static const LGRSChunkHandler LGRS;
static const ChunkHandlerRef linkgraph_chunk_handlers[] = {
	LGRP,
	LGRJ,
	LGRS,
};

extern const ChunkHandlerTable _linkgraph_chunk_handlers(linkgraph_chunk_handlers);

}
