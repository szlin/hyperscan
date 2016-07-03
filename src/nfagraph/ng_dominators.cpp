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
 * \brief Calculate dominator and post-dominator trees.
 *
 * A small wrapper around the BGL's lengauer_tarjan_dominator_tree algorithm.
 */
#include "ng_dominators.h"

#include "ue2common.h"
#include "ng_holder.h"
#include "ng_util.h"
#include "util/ue2_containers.h"

#include <boost-patched/graph/dominator_tree.hpp> // locally patched version
#include <boost/graph/reverse_graph.hpp>

using namespace std;
using boost::make_assoc_property_map;
using boost::make_iterator_property_map;

namespace ue2 {

template <class Graph>
ue2::unordered_map<NFAVertex, NFAVertex> calcDominators(const Graph &g,
                                                        NFAVertex source) {
    const size_t num_verts = num_vertices(g);
    auto index_map = get(&NFAGraphVertexProps::index, g);

    vector<size_t> dfnum(num_verts, 0);
    vector<NFAVertex> parents(num_verts, Graph::null_vertex());

    auto dfnum_map = make_iterator_property_map(dfnum.begin(), index_map);
    auto parent_map = make_iterator_property_map(parents.begin(), index_map);
    vector<NFAVertex> vertices_by_dfnum(num_verts, Graph::null_vertex());

    // Output map.
    unordered_map<NFAVertex, NFAVertex> doms;
    auto dom_map = make_assoc_property_map(doms);

    boost_ue2::lengauer_tarjan_dominator_tree(g, source, index_map, dfnum_map,
                                              parent_map, vertices_by_dfnum,
                                              dom_map);

    return doms;
}

ue2::unordered_map<NFAVertex, NFAVertex> findDominators(const NGHolder &g) {
    assert(hasCorrectlyNumberedVertices(g));
    return calcDominators(g.g, g.start);
}

ue2::unordered_map<NFAVertex, NFAVertex> findPostDominators(const NGHolder &g) {
    assert(hasCorrectlyNumberedVertices(g));
    return calcDominators(boost::reverse_graph<NFAGraph, const NFAGraph &>(g.g),
                          g.acceptEod);
}

} // namespace ue2
