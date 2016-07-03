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
 * \brief Splits an NFA graph into its connected components.
 *
 * This pass takes a NGHolder and splits its graph into a set of connected
 * components, returning them as individual NGHolder graphs. For example, the
 * graph for the regex /foo.*bar|[a-z]{7,13}|hatstand|teakettle$/ will be split
 * into four NGHolders, representing these four components:
 *
 * - /foo.*bar/
 * - /[a-z]{7,13}/
 * - /hatstand/
 * - /teakettle$/
 *
 * The pass operates by creating an undirected graph from the input graph, and
 * then using the BGL's connected_components algorithm to do the work, cloning
 * the identified components into their own graphs. A "shell" of vertices
 * is identified and removed first from the head and tail of the graph, in
 * order to handle cases where there is a common head/tail region.
 *
 * Trivial cases, such as an alternation of single vertices like /a|b|c|d|e|f/,
 * are not split, as later optimisations will handle these cases efficiently.
 */
#include "ng_calc_components.h"

#include "ng_depth.h"
#include "ng_holder.h"
#include "ng_prune.h"
#include "ng_undirected.h"
#include "ng_util.h"
#include "ue2common.h"
#include "util/graph_range.h"
#include "util/make_unique.h"

#include <map>
#include <vector>

#include <boost/graph/connected_components.hpp>

using namespace std;

namespace ue2 {

static constexpr u32 MAX_HEAD_SHELL_DEPTH = 3;
static constexpr u32 MAX_TAIL_SHELL_DEPTH = 3;

/**
 * \brief Returns true if the whole graph is just an alternation of character
 * classes.
 */
bool isAlternationOfClasses(const NGHolder &g) {
    for (auto v : vertices_range(g)) {
        if (is_special(v, g)) {
            continue;
        }
        // Vertex must have in edges from starts only.
        for (auto u : inv_adjacent_vertices_range(v, g)) {
            if (!is_any_start(u, g)) {
                return false;
            }
        }
        // Vertex must have out edges to accepts only.
        for (auto w : adjacent_vertices_range(v, g)) {
            if (!is_any_accept(w, g)) {
                return false;
            }
        }
    }

    DEBUG_PRINTF("alternation of single states, treating as one comp\n");
    return true;
}

/**
 * \brief Compute initial max distance to v from start (i.e. ignoring its own
 * self-loop).
 */
static
depth max_dist_from_start(const NGHolder &g,
                          const vector<NFAVertexBidiDepth> &depths,
                          NFAVertex v) {
    depth max_depth(0);
    for (const auto u : inv_adjacent_vertices_range(v, g)) {
        if (u == v) {
            continue;
        }
        const auto &d = depths.at(g[u].index);
        if (d.fromStart.max.is_reachable()) {
            max_depth = max(max_depth, d.fromStart.max);
        }
        if (d.fromStartDotStar.max.is_reachable()) {
            max_depth = max(max_depth, d.fromStartDotStar.max);
        }
    }
    return max_depth + 1;
}

/**
 * \brief Compute initial max depth from v from accept (i.e. ignoring its own
 * self-loop).
 */
static
depth max_dist_to_accept(const NGHolder &g,
                         const vector<NFAVertexBidiDepth> &depths,
                         NFAVertex v) {
    depth max_depth(0);
    for (const auto w : adjacent_vertices_range(v, g)) {
        if (w == v) {
            continue;
        }
        const auto &d = depths.at(g[w].index);
        if (d.toAccept.max.is_reachable()) {
            max_depth = max(max_depth, d.toAccept.max);
        }
        if (d.toAcceptEod.max.is_reachable()) {
            max_depth = max(max_depth, d.toAcceptEod.max);
        }
    }
    return max_depth + 1;
}

static
flat_set<NFAVertex> findHeadShell(const NGHolder &g,
                                  const vector<NFAVertexBidiDepth> &depths,
                                  const depth &max_dist) {
    flat_set<NFAVertex> shell;

    for (auto v : vertices_range(g)) {
        if (is_special(v, g)) {
            continue;
        }
        if (max_dist_from_start(g, depths, v) <= max_dist) {
            shell.insert(v);
        }
    }

    for (UNUSED auto v : shell) {
        DEBUG_PRINTF("shell: %u\n", g[v].index);
    }

    return shell;
}

static
flat_set<NFAVertex> findTailShell(const NGHolder &g,
                                  const vector<NFAVertexBidiDepth> &depths,
                                  const depth &max_dist) {
    flat_set<NFAVertex> shell;

    for (auto v : vertices_range(g)) {
        if (is_special(v, g)) {
            continue;
        }
        if (max_dist_to_accept(g, depths, v) <= max_dist) {
            shell.insert(v);
        }
    }

    for (UNUSED auto v : shell) {
        DEBUG_PRINTF("shell: %u\n", g[v].index);
    }

    return shell;
}

static
vector<NFAEdge> findShellEdges(const NGHolder &g,
                               const flat_set<NFAVertex> &head_shell,
                               const flat_set<NFAVertex> &tail_shell) {
    vector<NFAEdge> shell_edges;

    for (const auto &e : edges_range(g)) {
        auto u = source(e, g);
        auto v = target(e, g);

        if (v == g.startDs && is_any_start(u, g)) {
            continue;
        }
        if (u == g.accept && v == g.acceptEod) {
            continue;
        }

        if ((is_special(u, g) || contains(head_shell, u)) &&
            (is_special(v, g) || contains(tail_shell, v))) {
            DEBUG_PRINTF("edge (%u,%u) is a shell edge\n", g[u].index, g[v].index);
            shell_edges.push_back(e);
        }
    }

    return shell_edges;
}

static
void removeVertices(const flat_set<NFAVertex> &verts, NFAUndirectedGraph &ug,
                    ue2::unordered_map<NFAVertex, NFAUndirectedVertex> &old2new,
                    ue2::unordered_map<NFAVertex, NFAUndirectedVertex> &new2old) {
    for (auto v : verts) {
        assert(contains(old2new, v));
        auto uv = old2new.at(v);
        clear_vertex(uv, ug);
        remove_vertex(uv, ug);
        old2new.erase(v);
        new2old.erase(uv);
    }
}

static
void renumberVertices(NFAUndirectedGraph &ug) {
    u32 vertexIndex = 0;
    for (auto uv : vertices_range(ug)) {
        put(boost::vertex_index, ug, uv, vertexIndex++);
    }
}

/**
 * Common code called by calc- and recalc- below. Splits the given holder into
 * one or more connected components, adding them to the comps deque.
 */
static
void splitIntoComponents(const NGHolder &g, deque<unique_ptr<NGHolder>> &comps,
                         const depth &max_head_depth,
                         const depth &max_tail_depth, bool *shell_comp) {
    DEBUG_PRINTF("graph has %zu vertices\n", num_vertices(g));

    assert(shell_comp);
    *shell_comp = false;

    // Compute "shell" head and tail subgraphs.
    vector<NFAVertexBidiDepth> depths;
    calcDepths(g, depths);
    auto head_shell = findHeadShell(g, depths, max_head_depth);
    auto tail_shell = findTailShell(g, depths, max_tail_depth);
    for (auto v : head_shell) {
        tail_shell.erase(v);
    }

    if (head_shell.size() + tail_shell.size() + N_SPECIALS >= num_vertices(g)) {
        DEBUG_PRINTF("all in shell component\n");
        comps.push_back(cloneHolder(g));
        *shell_comp = true;
        return;
    }

    vector<NFAEdge> shell_edges = findShellEdges(g, head_shell, tail_shell);

    DEBUG_PRINTF("%zu vertices in head, %zu in tail, %zu shell edges\n",
                 head_shell.size(), tail_shell.size(), shell_edges.size());

    NFAUndirectedGraph ug;
    ue2::unordered_map<NFAVertex, NFAUndirectedVertex> old2new;
    ue2::unordered_map<u32, NFAVertex> newIdx2old;

    createUnGraph(g.g, true, true, ug, old2new, newIdx2old);

    // Construct reverse mapping.
    ue2::unordered_map<NFAVertex, NFAUndirectedVertex> new2old;
    for (const auto &m : old2new) {
        new2old.emplace(m.second, m.first);
    }

    // Remove shells from undirected graph and renumber so we have dense
    // vertex indices.
    removeVertices(head_shell, ug, old2new, new2old);
    removeVertices(tail_shell, ug, old2new, new2old);
    renumberVertices(ug);

    map<NFAUndirectedVertex, u32> split_components;
    const u32 num = connected_components(
        ug, boost::make_assoc_property_map(split_components));

    assert(num > 0);
    if (num == 1 && shell_edges.empty()) {
        DEBUG_PRINTF("single component\n");
        comps.push_back(cloneHolder(g));
        return;
    }

    DEBUG_PRINTF("broke graph into %u components\n", num);

    vector<deque<NFAVertex>> verts(num);

    // Collect vertex lists per component.
    for (const auto &m : split_components) {
        NFAVertex uv = m.first;
        u32 c = m.second;
        assert(contains(new2old, uv));
        NFAVertex v = new2old.at(uv);
        verts[c].push_back(v);
        DEBUG_PRINTF("vertex %u is in comp %u\n", g[v].index, c);
    }

    ue2::unordered_map<NFAVertex, NFAVertex> v_map; // temp map for fillHolder
    for (auto &vv : verts) {
        // Shells are in every component.
        vv.insert(vv.end(), begin(head_shell), end(head_shell));
        vv.insert(vv.end(), begin(tail_shell), end(tail_shell));

        // Sort by vertex index for determinism.
        sort(begin(vv), end(vv), VertexIndexOrdering<NGHolder>(g));

        auto gc = ue2::make_unique<NGHolder>();
        v_map.clear();
        fillHolder(gc.get(), g, vv, &v_map);

        // Remove shell edges, which will get their own component.
        for (const auto &e : shell_edges) {
            auto cu = v_map.at(source(e, g));
            auto cv = v_map.at(target(e, g));
            assert(edge(cu, cv, *gc).second);
            remove_edge(cu, cv, *gc);
        }

        pruneUseless(*gc);
        DEBUG_PRINTF("component %zu has %zu vertices\n", comps.size(),
                     num_vertices(*gc));
        comps.push_back(move(gc));
    }

    // Another component to handle the direct shell-to-shell edges.
    if (!shell_edges.empty()) {
        deque<NFAVertex> vv;
        vv.insert(vv.end(), begin(head_shell), end(head_shell));
        vv.insert(vv.end(), begin(tail_shell), end(tail_shell));

        // Sort by vertex index for determinism.
        sort(begin(vv), end(vv), VertexIndexOrdering<NGHolder>(g));

        auto gc = ue2::make_unique<NGHolder>();
        v_map.clear();
        fillHolder(gc.get(), g, vv, &v_map);

        pruneUseless(*gc);
        DEBUG_PRINTF("shell edge component %zu has %zu vertices\n",
                     comps.size(), num_vertices(*gc));
        comps.push_back(move(gc));
        *shell_comp = true;
    }

    // We should never produce empty component graphs.
    assert(all_of(begin(comps), end(comps),
                  [](const unique_ptr<NGHolder> &g_comp) {
                      return num_vertices(*g_comp) > N_SPECIALS;
                  }));
}

deque<unique_ptr<NGHolder>> calcComponents(const NGHolder &g) {
    deque<unique_ptr<NGHolder>> comps;

    // For trivial cases, we needn't bother running the full
    // connected_components algorithm.
    if (isAlternationOfClasses(g)) {
        comps.push_back(cloneHolder(g));
        return comps;
    }

    bool shell_comp = false;
    splitIntoComponents(g, comps, MAX_HEAD_SHELL_DEPTH, MAX_TAIL_SHELL_DEPTH,
                        &shell_comp);

    if (shell_comp) {
        DEBUG_PRINTF("re-running on shell comp\n");
        assert(!comps.empty());
        auto sc = move(comps.back());
        comps.pop_back();
        splitIntoComponents(*sc, comps, 0, 0, &shell_comp);
    }

    DEBUG_PRINTF("finished; split into %zu components\n", comps.size());
    return comps;
}

void recalcComponents(deque<unique_ptr<NGHolder>> &comps) {
    deque<unique_ptr<NGHolder>> out;

    for (auto &gc : comps) {
        if (!gc) {
            continue; // graph has been consumed already.
        }

        if (isAlternationOfClasses(*gc)) {
            out.push_back(move(gc));
            continue;
        }

        auto gc_comps = calcComponents(*gc);
        for (auto &elem : gc_comps) {
            out.push_back(move(elem));
        }
    }

    // Replace comps with our recalculated list.
    comps.swap(out);
}

} // namespace ue2
