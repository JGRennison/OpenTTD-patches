/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph_sl.cpp Code handling saving and loading of link graphs */

#include "../stdafx.h"
#include "../linkgraph/linkgraph.h"
#include "../linkgraph/linkgraphjob.h"
#include "../linkgraph/linkgraphschedule.h"
#include "../network/network.h"
#include "../settings_internal.h"
#include "saveload.h"

#include "../safeguards.h"

typedef LinkGraph::BaseNode Node;
typedef LinkGraph::BaseEdge Edge;

static uint16_t _num_nodes;
static uint16_t _to_node;

/* Edges and nodes are saved in the correct order, so we don't need to save their IDs. */

/**
 * SaveLoad desc for a link graph edge.
 */
static const NamedSaveLoad _edge_desc[] = {
	NSLT("to", SLTAG(SLTAG_CUSTOM_0,         SLEG_VAR(_to_node, SLE_UINT16))),
	NSL("",                              SLE_CONDNULL(4, SL_MIN_VERSION, SLV_191)), // distance
	NSL("capacity",                           SLE_VAR(Edge, capacity,                 SLE_UINT32)),
	NSL("usage",                              SLE_VAR(Edge, usage,                    SLE_UINT32)),
	NSL("travel_time_sum",              SLE_CONDVAR_X(Edge, travel_time_sum,          SLE_UINT64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_TRAVEL_TIME))),
	NSL("last_unrestricted_update",           SLE_VAR(Edge, last_unrestricted_update, SLE_INT32)),
	NSL("last_restricted_update",         SLE_CONDVAR(Edge, last_restricted_update,   SLE_INT32, SLV_187, SL_MAX_VERSION)),
	NSL("last_aircraft_update",         SLE_CONDVAR_X(Edge, last_aircraft_update,     SLE_INT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_AIRCRAFT))),
	//SLE_VAR(Edge, next_edge, SLE_UINT16), // Removed since XSLFI_LINKGRAPH_SPARSE_EDGES
};

struct LinkGraphEdgeStructHandler final : public HeaderOnlySaveLoadStructHandler {
	NamedSaveLoadTable GetDescription() const override
	{
		return _edge_desc;
	}
};

/**
 * SaveLoad desc for a link graph node.
 */
static const NamedSaveLoad _node_desc[] = {
	NSL("xy",          SLE_CONDVAR(Node, xy,          SLE_UINT32, SLV_191, SL_MAX_VERSION)),
	NSL("supply",          SLE_VAR(Node, supply,      SLE_UINT32)),
	NSL("demand",          SLE_VAR(Node, demand,      SLE_UINT32)),
	NSL("station",         SLE_VAR(Node, station,     SLE_UINT16)),
	NSL("last_update",     SLE_VAR(Node, last_update, SLE_INT32)),
	NSLTAG(SLTAG_CUSTOM_0, NSLT_STRUCTLIST<LinkGraphEdgeStructHandler>("edges")),
};

struct LinkGraphNodeStructHandler final : public TypedSaveLoadStructHandler<LinkGraphNodeStructHandler, LinkGraph> {
	SaveLoadTable edge_description;

	NamedSaveLoadTable GetDescription() const override
	{
		return _node_desc;
	}

	void Save(LinkGraph *lg) const override
	{
		uint16_t size = lg->Size();
		SlSetStructListLength(size);

		auto edge_iter = lg->edges.begin();
		auto edge_end = lg->edges.end();
		for (NodeID from = 0; from < size; ++from) {
			Node *node = &lg->nodes[from];
			SlObjectSaveFiltered(node, this->GetLoadDescription());

			auto edge_start = edge_iter;
			uint count = 0;
			while (edge_iter != edge_end && edge_iter->first.first == from) {
				count++;
				++edge_iter;
			}
			SlWriteSimpleGamma(count);
			auto edge_end = edge_iter;
			for (auto it = edge_start; it != edge_end; ++it) {
				SlWriteUint16(it->first.second);
				Edge *edge = &it->second;
				SlObjectSaveFiltered(edge, this->edge_description);
			}
		}
	}

	void Load(LinkGraph *lg) const override
	{
		uint num_nodes = SlGetStructListLength(UINT16_MAX);
		lg->Init(num_nodes);

		for (NodeID from = 0; from < num_nodes; ++from) {
			Node *node = &lg->nodes[from];
			SlObjectLoadFiltered(node, this->GetLoadDescription());

			uint num_edges = SlGetStructListLength(UINT16_MAX);
			for (uint i = 0; i < num_edges; i++) {
				NodeID to = SlReadUint16();
				SlObjectLoadFiltered(&lg->edges[std::make_pair(from, to)], this->edge_description);
			}
		}
	}

	void LoadedTableDescription() override
	{
		if (!SlXvIsFeaturePresent(XSLFI_LINKGRAPH_SPARSE_EDGES, 2)) {
			SlErrorCorrupt("XSLFI_LINKGRAPH_SPARSE_EDGES v2 unexpectedly not present");
		}

		assert(!this->table_data.empty() && this->table_data.back().label_tag == SLTAG_CUSTOM_0);
		this->edge_description = this->table_data.back().struct_handler->GetLoadDescription();
		assert (!this->edge_description.empty() && this->edge_description.front().label_tag == SLTAG_CUSTOM_0);
		this->edge_description = this->edge_description.subspan(1);
		this->table_data.pop_back();
	}

	void SavedTableDescription() override
	{
		if (this->table_data.empty() || this->table_data.back().label_tag != SLTAG_CUSTOM_0) SlErrorCorrupt("Link graph node format not as expected");
		this->edge_description = this->table_data.back().struct_handler->GetLoadDescription();
		if (this->edge_description.empty() || this->edge_description.front().label_tag != SLTAG_CUSTOM_0) SlErrorCorrupt("Link graph edge format not as expected");
		this->edge_description = this->edge_description.subspan(1);
		this->table_data.pop_back();
	}
};

/**
 * Get a SaveLoad array for a link graph.
 * @return SaveLoad array for link graph.
 */
NamedSaveLoadTable GetLinkGraphDesc()
{
	static const NamedSaveLoad link_graph_desc[] = {
		NSL("last_compression",     SLE_CONDVAR_X(LinkGraph, last_compression, SLE_VAR_I64 | SLE_FILE_I32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_DAY_SCALE, 0, 3))),
		NSL("last_compression",     SLE_CONDVAR_X(LinkGraph, last_compression,                  SLE_INT64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_DAY_SCALE, 4, 5))),
		NSL("last_compression",     SLE_CONDVAR_X(LinkGraph, last_compression,                 SLE_UINT64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_DAY_SCALE, 6))),
		NSL("",                          SLEG_VAR(_num_nodes,                  SLE_UINT16)),
		NSL("cargo",                      SLE_VAR(LinkGraph, cargo,            SLE_UINT8)),
		NSLT_STRUCTLIST<LinkGraphNodeStructHandler>("nodes"),
	};
	return link_graph_desc;
}

struct LinkGraphJobStructHandler final : public TypedSaveLoadStructHandler<LinkGraphJobStructHandler, LinkGraphJob> {
	NamedSaveLoadTable GetDescription() const override
	{
		return GetLinkGraphDesc();
	}

	void Save(LinkGraphJob *lgj) const override
	{
		SlObjectSaveFiltered(const_cast<LinkGraph *>(&lgj->Graph()), this->GetLoadDescription());
	}

	void Load(LinkGraphJob *lgj) const override
	{
		SlObjectLoadFiltered(const_cast<LinkGraph *>(&lgj->Graph()), this->GetLoadDescription());
	}
};

void GetLinkGraphJobDayLengthScaleAfterLoad(LinkGraphJob *lgj)
{
	lgj->join_tick *= DAY_TICKS;
	lgj->join_tick += LinkGraphSchedule::SPAWN_JOIN_TICK;

	uint recalc_scale;
	if (IsSavegameVersionBefore(SLV_LINKGRAPH_SECONDS) && SlXvIsFeatureMissing(XSLFI_LINKGRAPH_DAY_SCALE, 3)) {
		/* recalc time is in days */
		recalc_scale = DAY_TICKS;
	} else {
		/* recalc time is in seconds */
		recalc_scale = DAY_TICKS / SECONDS_PER_DAY;
	}
	lgj->start_tick = lgj->join_tick - (lgj->Settings().recalc_time * recalc_scale);
}

/**
 * Get a SaveLoad array for a link graph job. The settings struct is derived from
 * the global settings saveload array. The exact entries are calculated when the function
 * is called the first time.
 * It's necessary to keep a copy of the settings for each link graph job so that you can
 * change the settings while in-game and still not mess with current link graph runs.
 * Of course the settings have to be saved and loaded, too, to avoid desyncs.
 * @return Array of SaveLoad structs.
 */
NamedSaveLoadTable GetLinkGraphJobDesc()
{
	static std::vector<NamedSaveLoad> saveloads;

	/* Build the SaveLoad array on first call and don't touch it later on */
	if (saveloads.size() == 0) {
		size_t offset_gamesettings = cpp_offsetof(GameSettings, linkgraph);
		size_t offset_component = cpp_offsetof(LinkGraphJob, settings);

		const SettingTable &linkgraph_table = GetLinkGraphSettingTable();
		for (const auto &desc : linkgraph_table) {
			SaveLoad sl = desc->save;
			if (GetVarMemType(sl.conv) != SLE_VAR_NULL) {
				char *&address = reinterpret_cast<char *&>(sl.address);
				address -= offset_gamesettings;
				address += offset_component;
			}
			saveloads.push_back(NSL(desc->name, sl));
		}

		const NamedSaveLoad job_desc[] = {
			NSL("join_tick",        SLE_CONDVAR_X(LinkGraphJob, join_tick,        SLE_FILE_I32 | SLE_VAR_I64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_DAY_SCALE, 0, 4))),
			NSL("join_tick",        SLE_CONDVAR_X(LinkGraphJob, join_tick,        SLE_FILE_I64 | SLE_VAR_U64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_DAY_SCALE, 5, 5))),
			NSL("join_tick",        SLE_CONDVAR_X(LinkGraphJob, join_tick,        SLE_UINT64,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_DAY_SCALE, 6))),
			NSL("start_tick",       SLE_CONDVAR_X(LinkGraphJob, start_tick,       SLE_FILE_I32 | SLE_VAR_U64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_DAY_SCALE, 1, 4))),
			NSL("start_tick",       SLE_CONDVAR_X(LinkGraphJob, start_tick,       SLE_FILE_I64 | SLE_VAR_U64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_DAY_SCALE, 5, 5))),
			NSL("start_tick",       SLE_CONDVAR_X(LinkGraphJob, start_tick,       SLE_UINT64,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LINKGRAPH_DAY_SCALE, 6))),
			NSL("link_graph.index",       SLE_VAR(LinkGraphJob, link_graph.index, SLE_UINT16)),
			NSLT_STRUCT<LinkGraphJobStructHandler>("linkgraph"),
		};

		for (auto &sld : job_desc) {
			saveloads.push_back(sld);
		}
	}

	return saveloads;
}

/**
 * Get a SaveLoad array for the link graph schedule.
 * @return SaveLoad array for the link graph schedule.
 */
NamedSaveLoadTable GetLinkGraphScheduleDesc()
{
	static const NamedSaveLoad schedule_desc[] = {
		NSL("schedule", SLE_REFLIST(LinkGraphSchedule, schedule, REF_LINK_GRAPH)),
		NSL("running",  SLE_REFLIST(LinkGraphSchedule, running,  REF_LINK_GRAPH_JOB)),
	};
	return schedule_desc;
}

struct LinkGraphNonTableHelper {
	std::vector<SaveLoad> node_desc;
	std::vector<SaveLoad> edge_desc;
	std::vector<SaveLoad> graph_desc;

	void Setup()
	{
		if (SlXvIsFeaturePresent(XSLFI_LINKGRAPH_SPARSE_EDGES, 2)) SlErrorCorrupt("XSLFI_LINKGRAPH_SPARSE_EDGES v2 should not be present for non-table chunks");
		this->node_desc = SlFilterNamedSaveLoadTable(_node_desc);
		this->edge_desc = SlFilterNamedSaveLoadTable(_edge_desc);
		this->graph_desc = SlFilterNamedSaveLoadTable(GetLinkGraphDesc());
	}

	void Load_LinkGraph(LinkGraph &lg);
};

/**
 * Load a link graph.
 * @param lg Link graph to be saved or loaded.
 */
void LinkGraphNonTableHelper::Load_LinkGraph(LinkGraph &lg)
{
	uint size = lg.Size();
	if (SlXvIsFeaturePresent(XSLFI_LINKGRAPH_SPARSE_EDGES)) {
		for (NodeID from = 0; from < size; ++from) {
			Node *node = &lg.nodes[from];
			SlObjectLoadFiltered(node, this->node_desc);
			while (true) {
				NodeID to = SlReadUint16();
				if (to == INVALID_NODE) break;
				SlObjectLoadFiltered(&lg.edges[std::make_pair(from, to)], this->edge_desc);
			}
		}
	} else if (IsSavegameVersionBefore(SLV_191)) {
		std::vector<Edge> temp_edges;
		std::vector<NodeID> temp_next_edges;
		temp_edges.resize(size);
		temp_next_edges.resize(size);
		for (NodeID from = 0; from < size; ++from) {
			Node *node = &lg.nodes[from];
			SlObjectLoadFiltered(node, this->node_desc);
			/* We used to save the full matrix ... */
			for (NodeID to = 0; to < size; ++to) {
				SlObjectLoadFiltered(&temp_edges[to], this->edge_desc);
				temp_next_edges[to] = SlReadUint16();
			}
			for (NodeID to = from; to != INVALID_NODE; to = temp_next_edges[to]) {
				lg.edges[std::make_pair(from, to)] = temp_edges[to];
			}
		}
	} else {
		for (NodeID from = 0; from < size; ++from) {
			Node *node = &lg.nodes[from];
			SlObjectLoadFiltered(node, this->node_desc);
			/* ... but as that wasted a lot of space we save a sparse matrix now. */
			for (NodeID to = from; to != INVALID_NODE;) {
				if (to >= size) SlErrorCorrupt("Link graph structure overflow");
				SlObjectLoadFiltered(&lg.edges[std::make_pair(from, to)], this->edge_desc);
				to = SlReadUint16();
			}
		}
	}
}

/**
 * Load all link graphs.
 */
static void Load_LGRP()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(GetLinkGraphDesc());
	const bool is_table = SlIsTableChunk();

	LinkGraphNonTableHelper helper;
	if (!is_table) helper.Setup();

	int index;
	while ((index = SlIterateArray()) != -1) {
		if (!LinkGraph::CanAllocateItem()) {
			/* Impossible as they have been present in previous game. */
			NOT_REACHED();
		}
		LinkGraph *lg = new (index) LinkGraph();
		SlObjectLoadFiltered(lg, slt);
		if (!is_table) {
			lg->Init(_num_nodes);
			helper.Load_LinkGraph(*lg);
		}
	}
}

/**
 * Load all link graph jobs.
 */
static void Load_LGRJ()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(GetLinkGraphJobDesc());
	const bool is_table = SlIsTableChunk();

	LinkGraphNonTableHelper helper;
	if (!is_table) helper.Setup();

	int index;
	while ((index = SlIterateArray()) != -1) {
		if (!LinkGraphJob::CanAllocateItem()) {
			/* Impossible as they have been present in previous game. */
			NOT_REACHED();
		}
		LinkGraphJob *lgj = new (index) LinkGraphJob();
		SlObjectLoadFiltered(lgj, slt);
		if (SlXvIsFeatureMissing(XSLFI_LINKGRAPH_DAY_SCALE)) {
			extern void GetLinkGraphJobDayLengthScaleAfterLoad(LinkGraphJob *lgj);
			GetLinkGraphJobDayLengthScaleAfterLoad(lgj);
		}
		if (!is_table) {
			LinkGraph &lg = const_cast<LinkGraph &>(lgj->Graph());
			SlObjectLoadFiltered(&lg, helper.graph_desc);
			lg.Init(_num_nodes);
			helper.Load_LinkGraph(lg);
		}
	}
}

/**
 * Spawn the threads for running link graph calculations.
 * Has to be done after loading as the cargo classes might have changed.
 */
void AfterLoadLinkGraphs()
{
	if (IsSavegameVersionBefore(SLV_191)) {
		for (LinkGraph *lg : LinkGraph::Iterate()) {
			for (NodeID node_id = 0; node_id < lg->Size(); ++node_id) {
				const Station *st = Station::GetIfValid((*lg)[node_id].Station());
				if (st != nullptr) (*lg)[node_id].UpdateLocation(st->xy);
			}
		}

		for (LinkGraphJob *lgj : LinkGraphJob::Iterate()) {
			LinkGraph *lg = &(const_cast<LinkGraph &>(lgj->Graph()));
			for (NodeID node_id = 0; node_id < lg->Size(); ++node_id) {
				const Station *st = Station::GetIfValid((*lg)[node_id].Station());
				if (st != nullptr) (*lg)[node_id].UpdateLocation(st->xy);
			}
		}
	}

	LinkGraphSchedule::instance.SpawnAll();

	if (!_networking || _network_server) {
		AfterLoad_LinkGraphPauseControl();
	}
}

/**
 * Save all link graphs.
 */
static void Save_LGRP()
{
	SaveLoadTableData slt = SlTableHeader(GetLinkGraphDesc());

	for (LinkGraph *lg : LinkGraph::Iterate()) {
		SlSetArrayIndex(lg->index);
		SlObjectSaveFiltered(lg, slt);
	}
}

/**
 * Save all link graph jobs.
 */
static void Save_LGRJ()
{
	SaveLoadTableData slt = SlTableHeader(GetLinkGraphJobDesc());

	for (LinkGraphJob *lgj : LinkGraphJob::Iterate()) {
		SlSetArrayIndex(lgj->index);
		SlObjectSaveFiltered(lgj, slt);
	}
}

/**
 * Load the link graph schedule.
 */
static void Load_LGRS()
{
	SlLoadTableOrRiffFiltered(GetLinkGraphScheduleDesc(), &LinkGraphSchedule::instance);
}

/**
 * Save the link graph schedule.
 */
static void Save_LGRS()
{
	SlSaveTableObjectChunk(GetLinkGraphScheduleDesc(), &LinkGraphSchedule::instance);
}

/**
 * Substitute pointers in link graph schedule.
 */
static void Ptrs_LGRS()
{
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(GetLinkGraphScheduleDesc());
	SlObjectPtrOrNullFiltered(&LinkGraphSchedule::instance, slt);
}

static const ChunkHandler linkgraph_chunk_handlers[] = {
	{ 'LGRP', Save_LGRP, Load_LGRP, nullptr,   nullptr, CH_TABLE },
	{ 'LGRJ', Save_LGRJ, Load_LGRJ, nullptr,   nullptr, CH_TABLE },
	{ 'LGRS', Save_LGRS, Load_LGRS, Ptrs_LGRS, nullptr, CH_TABLE }
};

extern const ChunkHandlerTable _linkgraph_chunk_handlers(linkgraph_chunk_handlers);
