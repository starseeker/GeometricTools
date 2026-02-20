// David Eberly, Geometric Tools, Redmond WA 98052
// Copyright (c) 1998-2026
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
// https://www.geometrictools.com/License/Boost/LICENSE_1_0.txt
// File Version: 8.0.2026.02.20

#pragma once

// RTree is an N-dimensional spatial index that stores a set of primitives
// using Axis-Aligned Bounding Boxes (AABBs) as Minimum Bounding Rectangles
// (MBRs). The tree is built top-down using recursive spatial subdivision
// along the axis of greatest extent (the longest axis of the current node's
// bounding box), splitting at the median centroid of the primitives. This
// produces a balanced binary tree whose leaves each contain at most
// maxEntriesPerLeaf primitives.
//
// Unlike BVTree (which terminates at a user-specified height), RTree
// terminates when a node holds few enough primitives, giving fine-grained
// leaf coverage independent of tree height. This is well suited for
// localizing nearby-primitive candidates in spatial queries.
//
// Key feature: GetIntersectingLeafPairs(tree0, tree1, leafPairs)
//   Given two R-Trees (e.g., one built for each boundary polygon's edges in
//   the Co3Ne meshing workflow), this static method performs a simultaneous
//   traversal of both trees and collects all pairs (leafIndex0, leafIndex1)
//   where tree0's leaf and tree1's leaf have overlapping bounding boxes.
//   The caller can then limit pairwise primitive distance checks to those
//   candidate pairs, dramatically reducing the number of comparisons needed
//   to find the nearest cross-component edge pair during bridging.
//
// Primitive index convention:
//   After Create(), the primitives stored in leaf node L are those at
//   positions mPartition[L.minIndex], ..., mPartition[L.maxIndex] in the
//   original primitiveBounds array passed to Create(). Use
//   GetLeafPrimitives(leafIndex, ...) to retrieve them conveniently.
//
// Leaf index convention:
//   Leaf nodes are collected into mLeafNodes in the order they are first
//   created during the top-down build (DFS pre-order on leaf creation).
//   GetIntersectingLeafPairs returns pairs of indices into this array.

#include <Mathematics/AlignedBox.h>
#include <Mathematics/Vector.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace gte
{
    template <int32_t N, typename T>
    class RTree
    {
    public:
        struct Node
        {
            Node()
                :
                box{},
                minIndex(std::numeric_limits<size_t>::max()),
                maxIndex(std::numeric_limits<size_t>::max()),
                leftChild(std::numeric_limits<size_t>::max()),
                rightChild(std::numeric_limits<size_t>::max()),
                leafIndex(std::numeric_limits<size_t>::max()),
                isLeaf(false)
            {
            }

            // Minimum bounding box (MBR) for all primitives in this subtree.
            AlignedBox<N, T> box;

            // Indices into mPartition: this node covers
            // mPartition[minIndex], ..., mPartition[maxIndex].
            size_t minIndex, maxIndex;

            // Children (valid only when isLeaf == false).
            size_t leftChild, rightChild;

            // Position of this node in mLeafNodes (valid only when
            // isLeaf == true).
            size_t leafIndex;

            bool isLeaf;
        };

        RTree()
            :
            mBoxes{},
            mMaxEntriesPerLeaf(8),
            mNodes{},
            mPartition{},
            mLeafNodes{}
        {
        }

        // Build the R-Tree from a set of primitive bounding boxes.
        //
        // primitiveBounds[i] is the AABB of primitive i. After the tree is
        // built, the index of each primitive is its position in this array.
        //
        // maxEntriesPerLeaf: maximum number of primitives stored in a single
        //   leaf node. Values of 4-16 are typical. Smaller values produce
        //   shallower trees with finer-grained leaf coverage (better
        //   localization), at the cost of more leaf nodes and nodes overall.
        //   For the Co3Ne bridging use case, a small value (e.g., 4-8) is
        //   recommended so that intersecting leaf pairs contain only a few
        //   candidate edges to distance-check.
        void Create(
            std::vector<AlignedBox<N, T>> const& primitiveBounds,
            size_t maxEntriesPerLeaf = 8)
        {
            mBoxes.clear();
            mNodes.clear();
            mPartition.clear();
            mLeafNodes.clear();

            if (primitiveBounds.empty())
            {
                return;
            }

            mBoxes = primitiveBounds;
            mMaxEntriesPerLeaf = (maxEntriesPerLeaf >= 1) ? maxEntriesPerLeaf : 1;

            // Initialize the partition array with 0..n-1.
            mPartition.resize(mBoxes.size());
            std::iota(mPartition.begin(), mPartition.end(), 0);

            // Reserve an upper bound for the node count. In the worst case
            // (maxEntriesPerLeaf == 1) there are 2*n - 1 nodes for n
            // primitives (complete binary tree). The reserve avoids
            // mid-build reallocations.
            mNodes.reserve(2 * mBoxes.size());

            // Build the tree recursively.
            BuildTree(0, mBoxes.size() - 1);
        }

        // Member access.
        inline std::vector<AlignedBox<N, T>> const& GetBoxes() const
        {
            return mBoxes;
        }

        inline size_t GetMaxEntriesPerLeaf() const
        {
            return mMaxEntriesPerLeaf;
        }

        inline std::vector<Node> const& GetNodes() const
        {
            return mNodes;
        }

        inline std::vector<size_t> const& GetPartition() const
        {
            return mPartition;
        }

        // Returns the node indices of all leaf nodes, in the order they were
        // created during the top-down build. Use leafNodes[i] to index into
        // GetNodes() to obtain the leaf's bounding box and primitive range.
        inline std::vector<size_t> const& GetLeafNodes() const
        {
            return mLeafNodes;
        }

        // Retrieve the primitive indices stored in leaf number leafIndex.
        // These are indices into the primitiveBounds array originally passed
        // to Create().
        void GetLeafPrimitives(size_t leafIndex,
            std::vector<size_t>& primitiveIndices) const
        {
            primitiveIndices.clear();
            if (leafIndex >= mLeafNodes.size())
            {
                return;
            }
            Node const& node = mNodes[mLeafNodes[leafIndex]];
            for (size_t i = node.minIndex; i <= node.maxIndex; ++i)
            {
                primitiveIndices.push_back(mPartition[i]);
            }
        }

        // Find all pairs of leaf nodes — one from tree0 and one from tree1 —
        // whose bounding boxes overlap. The result is returned as pairs
        // (leafIndex0, leafIndex1) where each index is a position in the
        // corresponding tree's GetLeafNodes() array.
        //
        // This is the key spatial-index feature for the Co3Ne boundary-
        // polygon bridging optimization: build an RTree for each boundary
        // polygon's edges, call GetIntersectingLeafPairs, and then only
        // check distances between edge pairs that fall into an intersecting
        // leaf pair. This confines the nearest-cross-component-edge search
        // to spatially local candidates.
        //
        // After two boundary polygons are joined by bridging they form a
        // single new polygon, so discard the two old RTrees and build a
        // fresh RTree for the merged polygon.
        static void GetIntersectingLeafPairs(
            RTree const& tree0,
            RTree const& tree1,
            std::vector<std::pair<size_t, size_t>>& leafPairs)
        {
            leafPairs.clear();
            if (tree0.mNodes.empty() || tree1.mNodes.empty())
            {
                return;
            }
            FindLeafPairs(tree0, 0, tree1, 0, leafPairs);
        }

    private:
        // Recursive top-down tree builder. Returns the index of the node
        // created for range [i0, i1] in mPartition.
        size_t BuildTree(size_t i0, size_t i1)
        {
            // Allocate a new node slot. All subsequent writes go through
            // mNodes[nodeIndex] (index-based) to remain valid after any
            // vector reallocation caused by recursive BuildTree calls.
            size_t const nodeIndex = mNodes.size();
            mNodes.emplace_back();
            mNodes[nodeIndex].minIndex = i0;
            mNodes[nodeIndex].maxIndex = i1;
            mNodes[nodeIndex].box = ComputeBox(i0, i1);

            size_t const numPrimitives = i1 - i0 + 1;
            if (numPrimitives <= mMaxEntriesPerLeaf)
            {
                // Leaf node.
                mNodes[nodeIndex].isLeaf = true;
                mNodes[nodeIndex].leafIndex = mLeafNodes.size();
                mLeafNodes.push_back(nodeIndex);
                return nodeIndex;
            }

            // Interior node: split along the longest axis at the median
            // centroid. Compute the split axis before recursive calls so
            // we don't need to access the node's box afterward (the vector
            // may reallocate).
            size_t const splitAxis = LongestAxis(mNodes[nodeIndex].box);
            size_t const mid = SplitByMedian(i0, i1, splitAxis);

            size_t const leftIndex = BuildTree(i0, mid);
            size_t const rightIndex = BuildTree(mid + 1, i1);

            // Assign children. mNodes[nodeIndex] is still accessible by
            // index even if the vector was reallocated during recursion.
            mNodes[nodeIndex].leftChild = leftIndex;
            mNodes[nodeIndex].rightChild = rightIndex;
            mNodes[nodeIndex].isLeaf = false;
            return nodeIndex;
        }

        // Compute the AABB enclosing all primitive boxes in mPartition[i0..i1].
        AlignedBox<N, T> ComputeBox(size_t i0, size_t i1) const
        {
            T const posInf = std::numeric_limits<T>::max();
            T const negInf = -std::numeric_limits<T>::max();
            AlignedBox<N, T> box;
            for (int32_t d = 0; d < N; ++d)
            {
                box.min[d] = posInf;
                box.max[d] = negInf;
            }
            for (size_t i = i0; i <= i1; ++i)
            {
                AlignedBox<N, T> const& prim = mBoxes[mPartition[i]];
                for (int32_t d = 0; d < N; ++d)
                {
                    if (prim.min[d] < box.min[d]) { box.min[d] = prim.min[d]; }
                    if (prim.max[d] > box.max[d]) { box.max[d] = prim.max[d]; }
                }
            }
            return box;
        }

        // Return the dimension index of the longest side of the box.
        static size_t LongestAxis(AlignedBox<N, T> const& box)
        {
            size_t axis = 0;
            T maxExtent = box.max[0] - box.min[0];
            for (int32_t d = 1; d < N; ++d)
            {
                T const extent = box.max[d] - box.min[d];
                if (extent > maxExtent)
                {
                    maxExtent = extent;
                    axis = static_cast<size_t>(d);
                }
            }
            return axis;
        }

        // Partially sort mPartition[i0..i1] so that the left half
        // [i0..mid] has smaller centroid projections along 'axis' than the
        // right half [mid+1..i1]. Returns mid = i0 + (count-1)/2.
        size_t SplitByMedian(size_t i0, size_t i1, size_t const axis)
        {
            T const half = static_cast<T>(0.5);
            size_t const mid = i0 + (i1 - i0) / 2;
            std::nth_element(
                mPartition.begin() + static_cast<std::ptrdiff_t>(i0),
                mPartition.begin() + static_cast<std::ptrdiff_t>(mid),
                mPartition.begin() + static_cast<std::ptrdiff_t>(i1 + 1),
                [&](size_t a, size_t b)
                {
                    T const ca = half * (mBoxes[a].min[axis] + mBoxes[a].max[axis]);
                    T const cb = half * (mBoxes[b].min[axis] + mBoxes[b].max[axis]);
                    return ca < cb;
                });
            return mid;
        }

        // Returns true if the two axis-aligned boxes overlap (touching
        // counts as overlap).
        static bool BoxesOverlap(
            AlignedBox<N, T> const& a,
            AlignedBox<N, T> const& b)
        {
            for (int32_t d = 0; d < N; ++d)
            {
                if (a.max[d] < b.min[d] || a.min[d] > b.max[d])
                {
                    return false;
                }
            }
            return true;
        }

        // Simultaneous DFS traversal of two R-Trees to collect overlapping
        // leaf-node pairs. At each step, if the bounding boxes of the two
        // current nodes do not overlap we prune that branch immediately.
        // When both nodes are leaves, we record the pair. Otherwise we
        // recurse into the children of the larger (interior) node.
        static void FindLeafPairs(
            RTree const& tree0, size_t nodeIndex0,
            RTree const& tree1, size_t nodeIndex1,
            std::vector<std::pair<size_t, size_t>>& leafPairs)
        {
            Node const& node0 = tree0.mNodes[nodeIndex0];
            Node const& node1 = tree1.mNodes[nodeIndex1];

            // Prune if bounding boxes do not overlap.
            if (!BoxesOverlap(node0.box, node1.box))
            {
                return;
            }

            if (node0.isLeaf && node1.isLeaf)
            {
                leafPairs.emplace_back(node0.leafIndex, node1.leafIndex);
                return;
            }

            if (node0.isLeaf)
            {
                // tree0 is a leaf; descend into tree1's children.
                FindLeafPairs(tree0, nodeIndex0,
                    tree1, node1.leftChild, leafPairs);
                FindLeafPairs(tree0, nodeIndex0,
                    tree1, node1.rightChild, leafPairs);
            }
            else if (node1.isLeaf)
            {
                // tree1 is a leaf; descend into tree0's children.
                FindLeafPairs(tree0, node0.leftChild,
                    tree1, nodeIndex1, leafPairs);
                FindLeafPairs(tree0, node0.rightChild,
                    tree1, nodeIndex1, leafPairs);
            }
            else
            {
                // Both nodes are interior; recurse into all four child pairs.
                FindLeafPairs(tree0, node0.leftChild,
                    tree1, node1.leftChild, leafPairs);
                FindLeafPairs(tree0, node0.leftChild,
                    tree1, node1.rightChild, leafPairs);
                FindLeafPairs(tree0, node0.rightChild,
                    tree1, node1.leftChild, leafPairs);
                FindLeafPairs(tree0, node0.rightChild,
                    tree1, node1.rightChild, leafPairs);
            }
        }

        std::vector<AlignedBox<N, T>> mBoxes;
        size_t mMaxEntriesPerLeaf;
        std::vector<Node> mNodes;
        std::vector<size_t> mPartition;

        // Leaf node indices in creation order. mLeafNodes[i] is the index
        // into mNodes of the i-th leaf.
        std::vector<size_t> mLeafNodes;
    };

    // Template aliases for convenience.
    template <typename T>
    using RTree2 = RTree<2, T>;

    template <typename T>
    using RTree3 = RTree<3, T>;
}
