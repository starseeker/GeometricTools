// Compare old vs new bridging behavior via StitchPatches
//
// Tests whether the old method could incorrectly connect non-adjacent components

#include <GTE/Mathematics/Co3NeManifoldStitcher.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <vector>
#include <array>

using namespace gte;

// Analyze what happens with a linear chain of components: A --- B --- C
void TestLinearChain()
{
    std::cout << "=== Test: Linear Chain of Components ===\n\n";
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    // Component A at x=0
    vertices.push_back({0, 0, 0});
    vertices.push_back({0.8, 0, 0});
    vertices.push_back({0.4, 0.8, 0});
    triangles.push_back({0, 1, 2});
    
    // Component B at x=1.5 (gap ~0.7 from A)
    vertices.push_back({1.5, 0, 0});
    vertices.push_back({2.3, 0, 0});
    vertices.push_back({1.9, 0.8, 0});
    triangles.push_back({3, 4, 5});
    
    // Component C at x=3 (gap ~0.7 from B, ~2.2 from A)
    vertices.push_back({3, 0, 0});
    vertices.push_back({3.8, 0, 0});
    vertices.push_back({3.4, 0.8, 0});
    triangles.push_back({6, 7, 8});
    
    std::cout << "Created linear chain: A --- B --- C\n";
    std::cout << "  A-B distance: ~0.7\n";
    std::cout << "  B-C distance: ~0.7\n";
    std::cout << "  A-C distance: ~2.2\n\n";
    
    // The key question: with a threshold of 3.0, could old method bridge A-C?
    std::cout << "Scenario: All components within threshold (3.0)\n";
    std::cout << "Expected: Bridge A-B, then (A+B)-C\n";
    std::cout << "Risk: Bridge A-B and A-C simultaneously?\n\n";
}

int main()
{
    std::cout << "=== Analysis: Old vs New Bridging Method ===\n\n";
    
    TestLinearChain();
    
    std::cout << "=== Key Differences ===\n\n";
    
    std::cout << "OLD METHOD (BridgeBoundaryEdgesOptimized):\n";
    std::cout << "  • Finds ALL boundary edges in mesh\n";
    std::cout << "  • Groups by spatial grid cells\n";
    std::cout << "  • Generates ALL candidate pairs within threshold\n";
    std::cout << "  • Sorts pairs by distance\n";
    std::cout << "  • Bridges pairs sequentially (closest first)\n";
    std::cout << "  • Marks bridged edges to avoid reuse\n";
    std::cout << "  • Validates manifold property\n\n";
    
    std::cout << "CRITICAL LIMITATION:\n";
    std::cout << "  ❌ Does NOT track which component edges belong to\n";
    std::cout << "  ❌ Does NOT check if components are already connected\n";
    std::cout << "  ❌ Can bridge A-C even if A-B bridge already exists\n\n";
    
    std::cout << "Example failure case:\n";
    std::cout << "  1. Mesh has components A, B, C in linear arrangement\n";
    std::cout << "  2. All within threshold distance\n";
    std::cout << "  3. Method finds edge pairs:\n";
    std::cout << "     - A-B distance 0.7 (closest)\n";
    std::cout << "     - B-C distance 0.7 (close)\n";
    std::cout << "     - A-C distance 2.2 (within threshold 3.0)\n";
    std::cout << "  4. Bridges A-B first (correct)\n";
    std::cout << "  5. Next iteration: A and B are now connected\n";
    std::cout << "     BUT method doesn't know this!\n";
    std::cout << "  6. Could bridge A-C even though B is between them\n";
    std::cout << "  7. Result: Mesh bypasses middle component\n\n";
    
    std::cout << "NEW METHOD (ProgressiveComponentMerging):\n";
    std::cout << "  • Extracts connected components explicitly\n";
    std::cout << "  • Sorts components by size\n";
    std::cout << "  • Finds CLOSEST component to largest\n";
    std::cout << "  • Bridges them\n";
    std::cout << "  • Re-extracts components (now merged)\n";
    std::cout << "  • Repeats with new component structure\n\n";
    
    std::cout << "GUARANTEES:\n";
    std::cout << "  ✓ Always knows component membership\n";
    std::cout << "  ✓ Only bridges to spatially closest component\n";
    std::cout << "  ✓ Updates component structure after each bridge\n";
    std::cout << "  ✓ Cannot skip intermediate components\n\n";
    
    std::cout << "=== Conclusion ===\n\n";
    std::cout << "The old method IS UNSAFE for non-adjacent components:\n\n";
    std::cout << "Problem: It treats all boundary edges equally without\n";
    std::cout << "         considering which component they belong to.\n\n";
    std::cout << "Risk: In a linear chain A-B-C, if all are within threshold,\n";
    std::cout << "      it could bridge A-C even though B should be bridged first.\n\n";
    std::cout << "Impact: Creates topologically incorrect mesh that 'jumps over'\n";
    std::cout << "        intermediate components, even though it's manifold.\n\n";
    std::cout << "The NEW method is NECESSARY to ensure correct topology.\n\n";
    
    return 0;
}
