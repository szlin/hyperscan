/*
 * Copyright (c) 2015, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/** \file
 * \brief Functions for pruning unreachable vertices or reports from the graph.
 */
#include "ng_prune.h"

#include "ng_dominators.h"
#include "ng_holder.h"
#include "ng_reports.h"
#include "ng_util.h"
#include "util/container.h"
#include "util/graph.h"
#include "util/graph_range.h"
#include "util/report_manager.h"

#include <deque>
#include <map>

#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/reverse_graph.hpp>

using namespace std;
using boost::default_color_type;
using boost::reverse_graph;

namespace ue2 {

/** Remove any vertices that can't be reached by traversing the graph in
 * reverse from acceptEod. */
void pruneUnreachable(NGHolder &g) {
    deque<NFAVertex> dead;

    if (!hasGreaterInDegree(1, g.acceptEod, g) &&
            !hasGreaterInDegree(0, g.accept, g) &&
            edge(g.accept, g.acceptEod, g).second) {
        // Trivial case: there are no in-edges to our accepts (other than
        // accept->acceptEod), so all non-specials are unreachable.
        for (auto v : vertices_range(g)) {
            if (!is_special(v, g)) {
                dead.push_back(v);
            }
        }
    } else {
        // Walk a reverse graph from acceptEod with Boost's depth_first_visit
        // call.
        typedef reverse_graph<NFAGraph, NFAGraph&> RevNFAGraph;
        RevNFAGraph revg(g.g);

        map<NFAVertex, default_color_type> colours;

        depth_first_visit(revg, g.acceptEod,
                          make_dfs_visitor(boost::null_visitor()),
                          make_assoc_property_map(colours));

        DEBUG_PRINTF("color map has %zu entries after DFV\n", colours.size());

        // All non-special vertices that aren't in the colour map (because they
        // weren't reached) can be removed.
        for (auto v : vertices_range(revg)) {
            if (is_special(v, revg)) {
                continue;
            }
            if (!contains(colours, v)) {
                dead.push_back(v);
            }
        }
    }

    if (dead.empty()) {
        DEBUG_PRINTF("no unreachable vertices\n");
        return;
    }

    remove_vertices(dead, g, false);
    DEBUG_PRINTF("removed %zu unreachable vertices\n", dead.size());
}

template<class nfag_t>
static
bool pruneForwardUseless(NGHolder &h, const nfag_t &g, NFAVertex s,
                         vector<default_color_type> &vertexColor) {
    // Begin with all vertices set to white, as DFV only marks visited
    // vertices.
    fill(vertexColor.begin(), vertexColor.end(), boost::white_color);

    auto index_map = get(&NFAGraphVertexProps::index, g);

    depth_first_visit(g, s, make_dfs_visitor(boost::null_visitor()),
                      make_iterator_property_map(vertexColor.begin(),
                                                 index_map));

    vector<NFAVertex> dead;

    // All non-special vertices that are still white can be removed.
    for (auto v : vertices_range(g)) {
        u32 idx = g[v].index;
        if (!is_special(v, g) && vertexColor[idx] == boost::white_color) {
            DEBUG_PRINTF("vertex %u is unreachable from %u\n",
                         g[v].index, g[s].index);
            dead.push_back(v);
        }
    }

    if (dead.empty()) {
        return false;
    }

    DEBUG_PRINTF("removing %zu vertices\n", dead.size());
    remove_vertices(dead, h, false);
    return true;
}

/** Remove any vertices which can't be reached by traversing the graph forward
 * from start or in reverse from acceptEod. If \p renumber is false, no
 * vertex/edge renumbering is done. */
void pruneUseless(NGHolder &g, bool renumber) {
    DEBUG_PRINTF("pruning useless vertices\n");
    assert(hasCorrectlyNumberedVertices(g));
    vector<default_color_type> vertexColor(num_vertices(g));

    bool work_done = pruneForwardUseless(g, g.g, g.start, vertexColor);
    work_done |= pruneForwardUseless(
        g, reverse_graph<NFAGraph, NFAGraph &>(g.g), g.acceptEod, vertexColor);

    if (!work_done) {
        return;
    }

    if (renumber) {
        g.renumberEdges();
        g.renumberVertices();
    }
}

/** This code removes any vertices which do not accept any symbols. Any
 * vertices which no longer lie on a path from a start to an accept are also
 * pruned. */
void pruneEmptyVertices(NGHolder &g) {
    DEBUG_PRINTF("pruning empty vertices\n");
    vector<NFAVertex> dead;
    for (auto v : vertices_range(g)) {
        if (is_special(v, g)) {
            continue;
        }

        const CharReach &cr = g[v].char_reach;
        if (cr.none()) {
            DEBUG_PRINTF("empty: %u\n", g[v].index);
            dead.push_back(v);
        }
    }

    if (dead.empty()) {
        return;
    }

    remove_vertices(dead, g);
    pruneUseless(g);
}

/** Remove any edges from vertices that generate accepts (for Highlander
 * graphs). */
void pruneHighlanderAccepts(NGHolder &g, const ReportManager &rm) {
    // Safety check: all reports must be simple exhaustible reports, or this is
    // not safe. This optimisation should be called early enough that no
    // internal reports have been added.
    for (auto report_id : all_reports(g)) {
        const Report &ir = rm.getReport(report_id);

        if (ir.ekey == INVALID_EKEY || ir.hasBounds() ||
            !isExternalReport(ir)) {
            DEBUG_PRINTF("report %u is not external highlander with "
                         "no bounds\n", report_id);
            return;
        }
    }

    vector<NFAEdge> dead;
    for (auto u : inv_adjacent_vertices_range(g.accept, g)) {
        if (is_special(u, g)) {
            continue;
        }

        // We can prune any out-edges that aren't accepts
        for (const auto &e : out_edges_range(u, g)) {
            if (!is_any_accept(target(e, g), g)) {
                dead.push_back(e);
            }
        }
    }

    if (dead.empty()) {
        return;
    }

    DEBUG_PRINTF("found %zu removable edges due to single match\n", dead.size());
    remove_edges(dead, g);
    pruneUseless(g);
}

static
bool isDominatedByReporter(const NGHolder &g,
                           const ue2::unordered_map<NFAVertex, NFAVertex> &dom,
                           NFAVertex v, ReportID report_id) {
    for (auto it = dom.find(v); it != end(dom); it = dom.find(v)) {
        NFAVertex u = it->second;
        // Note: reporters with edges only to acceptEod are not considered to
        // dominate.
        if (edge(u, g.accept, g).second && contains(g[u].reports, report_id)) {
            DEBUG_PRINTF("%u is dominated by %u, and both report %u\n",
                          g[v].index, g[u].index, report_id);
            return true;
        }
        v = u;
    }
    return false;
}

/**
 * True if the vertex has (a) a self-loop, (b) only out-edges to accept and
 * itself and (c) only simple exhaustible reports.
 */
static
bool hasOnlySelfLoopAndExhaustibleAccepts(const NGHolder &g,
                                          const ReportManager &rm,
                                          NFAVertex v) {
    if (!edge(v, v, g).second) {
        return false;
    }

    for (auto w : adjacent_vertices_range(v, g)) {
        if (w != v && w != g.accept) {
            return false;
        }
    }

    for (const auto &report_id : g[v].reports) {
        if (!isSimpleExhaustible(rm.getReport(report_id))) {
            return false;
        }
    }

    return true;
}

void pruneHighlanderDominated(NGHolder &g, const ReportManager &rm) {
    vector<NFAVertex> reporters;
    for (auto v : inv_adjacent_vertices_range(g.accept, g)) {
        for (const auto &report_id : g[v].reports) {
            const Report &r = rm.getReport(report_id);
            if (isSimpleExhaustible(r)) {
                reporters.push_back(v);
                break;
            }
        }
    }
    for (auto v : inv_adjacent_vertices_range(g.acceptEod, g)) {
        for (const auto &report_id : g[v].reports) {
            const Report &r = rm.getReport(report_id);
            if (isSimpleExhaustible(r)) {
                reporters.push_back(v);
                break;
            }
        }
    }

    if (reporters.empty()) {
        return;
    }


    sort(begin(reporters), end(reporters), make_index_ordering(g));
    reporters.erase(unique(begin(reporters), end(reporters)), end(reporters));

    DEBUG_PRINTF("%zu vertices have simple exhaustible reports\n",
                 reporters.size());

    const auto &dom = findDominators(g);
    bool modified = false;

    // If a reporter vertex is dominated by another with the same report, we
    // can remove that report; if all reports are removed, we can remove the
    // vertex entirely.
    for (const auto v : reporters) {
        const auto reports = g[v].reports; // copy, as we're going to mutate
        for (const auto &report_id : reports) {
            if (!isSimpleExhaustible(rm.getReport(report_id))) {
                continue;
            }
            if (isDominatedByReporter(g, dom, v, report_id)) {
                DEBUG_PRINTF("removed dominated report %u from vertex %u\n",
                             report_id, g[v].index);
                g[v].reports.erase(report_id);
            }
        }

        if (g[v].reports.empty()) {
            DEBUG_PRINTF("removed edges to accepts from %u, no reports left\n",
                          g[v].index);
            remove_edge(v, g.accept, g);
            remove_edge(v, g.acceptEod, g);
            modified = true;
        }
    }

    // If a reporter vertex has a self-loop, but otherwise only leads to accept
    // (note: NOT acceptEod) and has simple exhaustible reports, we can delete
    // the self-loop.
    for (const auto v : reporters) {
        if (hasOnlySelfLoopAndExhaustibleAccepts(g, rm, v)) {
            remove_edge(v, v, g);
            modified = true;
            DEBUG_PRINTF("removed self-loop on %u\n", g[v].index);
        }
    }

    if (!modified) {
        return;
    }

    pruneUseless(g);

    // We may have only removed self-loops, in which case pruneUseless wouldn't
    // renumber, so we do edge renumbering explicitly here.
    g.renumberEdges();
}

/** Removes the given Report ID from vertices connected to accept, and then
 * prunes useless vertices that have had their report sets reduced to empty. */
void pruneReport(NGHolder &g, ReportID report) {
    set<NFAEdge> dead;

    for (const auto &e : in_edges_range(g.accept, g)) {
        NFAVertex u = source(e, g);
        auto &reports = g[u].reports;
        if (contains(reports, report)) {
            reports.erase(report);
            if (reports.empty()) {
                dead.insert(e);
            }
        }
    }

    for (const auto &e : in_edges_range(g.acceptEod, g)) {
        NFAVertex u = source(e, g);
        if (u == g.accept) {
            continue;
        }
        auto &reports = g[u].reports;
        if (contains(reports, report)) {
            reports.erase(report);
            if (reports.empty()) {
                dead.insert(e);
            }
        }
    }

    if (dead.empty()) {
        return;
    }

    remove_edges(dead, g);
    pruneUnreachable(g);
    g.renumberVertices();
    g.renumberEdges();
}

/** Removes all Report IDs bar the given one from vertices connected to accept,
 * and then prunes useless vertices that have had their report sets reduced to
 * empty. */
void pruneAllOtherReports(NGHolder &g, ReportID report) {
    set<NFAEdge> dead;

    for (const auto &e : in_edges_range(g.accept, g)) {
        NFAVertex u = source(e, g);
        auto &reports = g[u].reports;
        if (contains(reports, report)) {
            reports.clear();
            reports.insert(report);
        } else {
            reports.clear();
            dead.insert(e);
        }
    }

    for (const auto &e : in_edges_range(g.acceptEod, g)) {
        NFAVertex u = source(e, g);
        if (u == g.accept) {
            continue;
        }
        auto &reports = g[u].reports;
        if (contains(reports, report)) {
            reports.clear();
            reports.insert(report);
        } else {
            reports.clear();
            dead.insert(e);
        }
    }

    if (dead.empty()) {
        return;
    }

    remove_edges(dead, g);
    pruneUnreachable(g);
    g.renumberVertices();
    g.renumberEdges();
}

} // namespace ue2
