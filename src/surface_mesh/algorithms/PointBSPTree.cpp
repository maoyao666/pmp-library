//=============================================================================
// Copyright (C) 2011-2016 by Graphics & Geometry Group, Bielefeld University
// Copyright (C) 2017 Daniel Sieger
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//=============================================================================

#include <surface_mesh/algorithms/PointBSPTree.h>

#include <algorithm>
#include <cfloat>

//=============================================================================

namespace surface_mesh {

//=============================================================================

unsigned int PointBSPTree::build(unsigned int maxHandles, unsigned int maxDepth)
{
    // copy points to element array
    m_elements.clear();
    m_elements.reserve(m_pointSet.nVertices());

    for (auto v : m_pointSet.vertices())
        m_elements.push_back(Element(m_pointSet.position(v), v.idx()));

    // init
    delete m_root;
    m_root   = new Node(m_elements.begin(), m_elements.end());
    m_nNodes = 0;

    // call recursive helper
    buildRecurse(m_root, maxHandles, maxDepth);

    return m_nNodes;
}

//-----------------------------------------------------------------------------

void PointBSPTree::buildRecurse(Node* node, unsigned int maxHandles,
                                unsigned int depth)
{
    const unsigned int n = node->m_end - node->m_begin;

    // should we stop at this level ?
    if ((depth == 0) || (n < maxHandles))
        return;

    // compute bounding box
    ElementIter it(node->m_begin);
    Point       bb_min = it->m_point;
    Point       bb_max = it->m_point;
    for (; it != node->m_end; ++it)
    {
        bb_min.minimize(it->m_point);
        bb_max.maximize(it->m_point);
    }

    // split longest side of bounding box
    Point  bb     = bb_max - bb_min;
    Scalar length = bb[0];
    int    axis   = 0;
    if (bb[1] > length)
        length = bb[axis = 1];
    if (bb[2] > length)
        length = bb[axis = 2];
    Scalar cv = 0.5 * (bb_min[axis] + bb_max[axis]);

    // store cut dimension and value
    node->m_cutDimension = axis;
    node->m_cutValue     = cv;

    // partition for left and right child
    it =
        std::partition(node->m_begin, node->m_end, PartitioningPlane(axis, cv));

    // create children
    m_nNodes += 2;
    node->m_leftChild  = new Node(node->m_begin, it);
    node->m_rightChild = new Node(it, node->m_end);

    // recurse to childen
    buildRecurse(node->m_leftChild, maxHandles, depth - 1);
    buildRecurse(node->m_rightChild, maxHandles, depth - 1);
}

//-----------------------------------------------------------------------------

int PointBSPTree::nearest(const Point& p, Point& result, int& idx) const
{
    // init data
    NearestNeighborData data;
    data.m_ref       = p;
    data.m_dist      = FLT_MAX;
    data.m_leafTests = 0;

    // recursive search
    nearestRecurse(m_root, data);

    // dist was computed as sqr-dist
    data.m_dist = sqrt(data.m_dist);

    // set output params
    idx    = data.m_nearest;
    result = m_pointSet.position(PointSet::Vertex(idx));

    return data.m_leafTests;
}

//-----------------------------------------------------------------------------

void PointBSPTree::nearestRecurse(Node* node, NearestNeighborData& data) const
{
    if (node->m_leftChild)
    {
        int    cd  = node->m_cutDimension;
        Scalar off = data.m_ref[cd] - node->m_cutValue;

        if (off > 0.0)
        {
            nearestRecurse(node->m_leftChild, data);
            if (off * off < data.m_dist)
            {
                nearestRecurse(node->m_rightChild, data);
            }
        }
        else
        {
            nearestRecurse(node->m_rightChild, data);
            if (off * off < data.m_dist)
            {
                nearestRecurse(node->m_leftChild, data);
            }
        }
    }

    // terminal node
    else
    {
        ++data.m_leafTests;
        Scalar dist;

        for (ElementIter it = node->m_begin; it != node->m_end; ++it)
        {
            dist = sqrnorm(it->m_point - data.m_ref);
            if (dist < data.m_dist)
            {
                data.m_dist    = dist;
                data.m_nearest = it->m_idx;
            }
        }
    }
}

//-----------------------------------------------------------------------------

int PointBSPTree::kNearest(const Point& p, unsigned int k,
                           std::vector<int>& knn) const
{
    KNearestNeighborData data;
    data.m_ref  = p;
    data.m_dist = FLT_MAX;
    data.m_kNearest.push(std::make_pair<int, float>(-1, FLT_MAX));
    data.m_leafTests = 0;

    kNearestRecurse(m_root, data);

    knn.resize(k);
    for (int i = k - 1; i >= 0; --i)
    {
        knn[i] = data.m_kNearest.top().first;
        data.m_kNearest.pop();
    }

    return data.m_leafTests;
}

//-----------------------------------------------------------------------------

void PointBSPTree::kNearestRecurse(Node* node, KNearestNeighborData& data) const
{
    // non-terminal node
    if (node->m_leftChild)
    {
        int    cd  = node->m_cutDimension;
        Scalar off = data.m_ref[cd] - node->m_cutValue;

        if (off > 0.0)
        {
            kNearestRecurse(node->m_leftChild, data);
            if (off * off < data.m_dist)
            {
                kNearestRecurse(node->m_rightChild, data);
            }
        }
        else
        {
            kNearestRecurse(node->m_rightChild, data);
            if (off * off < data.m_dist)
            {
                kNearestRecurse(node->m_leftChild, data);
            }
        }
    }

    // terminal node
    else
    {
        ++data.m_leafTests;
        Scalar dist;

        for (ConstElementIter it = node->m_begin; it != node->m_end; ++it)
        {
            dist = sqrnorm(it->m_point - data.m_ref);
            if (dist < data.m_dist)
            {
                data.m_kNearest.push(std::make_pair(it->m_idx, dist));
                data.m_dist = data.m_kNearest.top().second;
            }
        }
    }
}

//-----------------------------------------------------------------------------

int PointBSPTree::ball(const Point& p, Scalar radius,
                       std::vector<int>& ball) const
{
    ball.clear();
    Scalar squaredRadius = radius * radius;

    BallData data;
    data.m_ref       = p;
    data.m_dist      = FLT_MAX;
    data.m_leafTests = 0;

    ballRecurse(m_root, data, squaredRadius, ball);

    return data.m_leafTests;
}

//-----------------------------------------------------------------------------

void PointBSPTree::ballRecurse(Node* node, BallData& data, Scalar squaredRadius,
                               std::vector<int>& ball) const
{
    // non-terminal node
    if (node->m_leftChild)
    {
        int    cd  = node->m_cutDimension;
        Scalar off = data.m_ref[cd] - node->m_cutValue;

        if (off > 0.0)
        {
            ballRecurse(node->m_leftChild, data, squaredRadius, ball);
            if (off * off < squaredRadius)
            {
                ballRecurse(node->m_rightChild, data, squaredRadius, ball);
            }
        }
        else
        {
            ballRecurse(node->m_rightChild, data, squaredRadius, ball);
            if (off * off < squaredRadius)
            {
                ballRecurse(node->m_leftChild, data, squaredRadius, ball);
            }
        }
    }

    // terminal node
    else
    {
        ++data.m_leafTests;
        Scalar dist;

        for (ConstElementIter it = node->m_begin; it != node->m_end; ++it)
        {
            dist = sqrnorm(it->m_point - data.m_ref);
            if (dist < squaredRadius)
            {
                ball.push_back(it->m_idx);
            }
        }
    }
}

//=============================================================================
} // namespace surface_mesh
//=============================================================================
