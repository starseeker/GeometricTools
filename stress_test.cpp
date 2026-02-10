// Stress test suite for triangulation methods
// Tests edge cases and failure modes

#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshPreprocessing.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <chrono>
#include <map>

using namespace gte;

// Test result structure
struct TestResult {
    std::string testName;
    std::string method;
    bool success;
    size_t trianglesGenerated;
    std::string errorMessage;
};

std::vector<TestResult> results;

// Helper to record test results
void RecordResult(std::string testName, std::string method, bool success, 
                  size_t triangles, std::string error = "") {
    results.push_back({testName, method, success, triangles, error});
}

// Test 1: Highly Non-Planar Hole (wraps around sphere)
void TestNonPlanarHole() {
    std::cout << "\n=== Test 1: Highly Non-Planar Hole (Spherical) ===" << std::endl;
    
    // Create hole that wraps around 90 degrees of a sphere
    std::vector<Vector3<double>> vertices;
    const int n = 12;
    const double radius = 1.0;
    
    for (int i = 0; i < n; ++i) {
        double theta = (M_PI / 2.0) * i / (n - 1);  // 0 to 90 degrees
        double phi = 0.0;
        vertices.push_back({
            radius * std::sin(theta) * std::cos(phi),
            radius * std::sin(theta) * std::sin(phi),
            radius * std::cos(theta)
        });
    }
    
    // Complete the loop
    for (int i = n - 1; i >= 0; --i) {
        double theta = (M_PI / 2.0) * i / (n - 1);
        double phi = M_PI / 4.0;  // 45 degrees
        vertices.push_back({
            radius * std::sin(theta) * std::cos(phi),
            radius * std::sin(theta) * std::sin(phi),
            radius * std::cos(theta)
        });
    }
    
    // Create hole boundary
    MeshHoleFilling<double>::HoleBoundary hole;
    for (size_t i = 0; i < vertices.size(); ++i) {
        hole.vertices.push_back(static_cast<int32_t>(i));
    }
    
    // No direct way to measure planarity from outside, just test the methods
    std::cout << "  Hole vertices: " << vertices.size() << std::endl;
    std::cout << "  Testing non-planar spherical hole..." << std::endl;
    
    // Test each method
    std::vector<std::array<int32_t, 3>> triangles;
    std::vector<std::array<int32_t, 3>> dummyTris;
    
    // This is private, so we'll test through FillHoles
    MeshHoleFilling<double>::Parameters params;
    params.autoFallback = false;  // Test each method individually
    
    std::cout << "\n  Testing each method:" << std::endl;
    
    // EC 2D - expected to struggle
    triangles = dummyTris;
    params.method = MeshHoleFilling<double>::TriangulationMethod::EarClipping;
    try {
        MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
        std::cout << "  EC (2D): " << (triangles.size() - dummyTris.size()) << " triangles" << std::endl;
        RecordResult("NonPlanar", "EC(2D)", true, triangles.size() - dummyTris.size());
    } catch (...) {
        std::cout << "  EC (2D): FAILED (exception)" << std::endl;
        RecordResult("NonPlanar", "EC(2D)", false, 0, "Exception");
    }
    
    // CDT 2D - expected to struggle
    triangles = dummyTris;
    params.method = MeshHoleFilling<double>::TriangulationMethod::CDT;
    try {
        MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
        std::cout << "  CDT (2D): " << (triangles.size() - dummyTris.size()) << " triangles" << std::endl;
        RecordResult("NonPlanar", "CDT(2D)", true, triangles.size() - dummyTris.size());
    } catch (...) {
        std::cout << "  CDT (2D): FAILED (exception)" << std::endl;
        RecordResult("NonPlanar", "CDT(2D)", false, 0, "Exception");
    }
    
    // EC3D - expected to succeed
    triangles = dummyTris;
    params.method = MeshHoleFilling<double>::TriangulationMethod::EarClipping3D;
    try {
        MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
        std::cout << "  EC3D: " << (triangles.size() - dummyTris.size()) << " triangles" << std::endl;
        RecordResult("NonPlanar", "EC3D", true, triangles.size() - dummyTris.size());
    } catch (...) {
        std::cout << "  EC3D: FAILED (exception)" << std::endl;
        RecordResult("NonPlanar", "EC3D", false, 0, "Exception");
    }
}

// Test 2: Nearly Degenerate Hole (almost collinear vertices)
void TestDegenerateHole() {
    std::cout << "\n=== Test 2: Nearly Degenerate Hole (Collinear) ===" << std::endl;
    
    std::vector<Vector3<double>> vertices;
    const int n = 8;
    
    // Create nearly collinear vertices with tiny deviations
    for (int i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / (n - 1);
        double epsilon = 1e-4;  // Very small deviation
        vertices.push_back({
            t * 10.0,
            epsilon * std::sin(t * 2 * M_PI),
            0.0
        });
    }
    
    MeshHoleFilling<double>::HoleBoundary hole;
    for (int i = 0; i < n; ++i) {
        hole.vertices.push_back(i);
    }
    
    std::cout << "  Testing near-collinear configuration..." << std::endl;
    
    std::vector<std::array<int32_t, 3>> triangles, dummyTris;
    MeshHoleFilling<double>::Parameters params;
    params.autoFallback = false;
    
    // Test each method
    for (auto method : {
        MeshHoleFilling<double>::TriangulationMethod::EarClipping,
        MeshHoleFilling<double>::TriangulationMethod::CDT,
        MeshHoleFilling<double>::TriangulationMethod::EarClipping3D
    }) {
        triangles = dummyTris;
        params.method = method;
        std::string methodName = (method == MeshHoleFilling<double>::TriangulationMethod::EarClipping) ? "EC(2D)" :
                                  (method == MeshHoleFilling<double>::TriangulationMethod::CDT) ? "CDT(2D)" : "EC3D";
        try {
            MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
            std::cout << "  " << methodName << ": " << (triangles.size() - dummyTris.size()) << " triangles" << std::endl;
            RecordResult("Degenerate", methodName, true, triangles.size() - dummyTris.size());
        } catch (...) {
            std::cout << "  " << methodName << ": FAILED" << std::endl;
            RecordResult("Degenerate", methodName, false, 0, "Exception");
        }
    }
}

// Test 3: Very Elongated Hole (extreme aspect ratio)
void TestElongatedHole() {
    std::cout << "\n=== Test 3: Very Elongated Hole (100:1 aspect ratio) ===" << std::endl;
    
    std::vector<Vector3<double>> vertices;
    const int n = 20;
    const double length = 100.0;
    const double width = 1.0;
    
    // Create long thin rectangle
    for (int i = 0; i <= n; ++i) {
        double t = static_cast<double>(i) / n;
        vertices.push_back({t * length, 0.0, 0.0});
    }
    for (int i = n; i >= 0; --i) {
        double t = static_cast<double>(i) / n;
        vertices.push_back({t * length, width, 0.0});
    }
    
    MeshHoleFilling<double>::HoleBoundary hole;
    for (size_t i = 0; i < vertices.size(); ++i) {
        hole.vertices.push_back(static_cast<int32_t>(i));
    }
    
    std::cout << "  Aspect ratio: 100:1" << std::endl;
    std::cout << "  Vertices: " << vertices.size() << std::endl;
    
    std::vector<std::array<int32_t, 3>> triangles, dummyTris;
    MeshHoleFilling<double>::Parameters params;
    params.autoFallback = false;
    
    for (auto method : {
        MeshHoleFilling<double>::TriangulationMethod::EarClipping,
        MeshHoleFilling<double>::TriangulationMethod::CDT,
        MeshHoleFilling<double>::TriangulationMethod::EarClipping3D
    }) {
        triangles = dummyTris;
        params.method = method;
        std::string methodName = (method == MeshHoleFilling<double>::TriangulationMethod::EarClipping) ? "EC(2D)" :
                                  (method == MeshHoleFilling<double>::TriangulationMethod::CDT) ? "CDT(2D)" : "EC3D";
        try {
            MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
            std::cout << "  " << methodName << ": " << (triangles.size() - dummyTris.size()) << " triangles" << std::endl;
            RecordResult("Elongated", methodName, true, triangles.size() - dummyTris.size());
        } catch (...) {
            std::cout << "  " << methodName << ": FAILED" << std::endl;
            RecordResult("Elongated", methodName, false, 0, "Exception");
        }
    }
}

// Test 4: Large Complex Hole (many vertices)
void TestLargeHole() {
    std::cout << "\n=== Test 4: Large Complex Hole (100 vertices) ===" << std::endl;
    
    std::vector<Vector3<double>> vertices;
    const int n = 100;
    const double radius = 10.0;
    
    // Create irregular polygon with varying radius
    for (int i = 0; i < n; ++i) {
        double angle = 2.0 * M_PI * i / n;
        double r = radius * (1.0 + 0.3 * std::sin(5 * angle));  // Wavy boundary
        vertices.push_back({
            r * std::cos(angle),
            r * std::sin(angle),
            0.1 * std::sin(angle * 3)  // Slight non-planarity
        });
    }
    
    MeshHoleFilling<double>::HoleBoundary hole;
    for (int i = 0; i < n; ++i) {
        hole.vertices.push_back(i);
    }
    
    std::cout << "  Vertices: " << n << std::endl;
    
    std::vector<std::array<int32_t, 3>> triangles, dummyTris;
    MeshHoleFilling<double>::Parameters params;
    params.autoFallback = false;
    
    for (auto method : {
        MeshHoleFilling<double>::TriangulationMethod::EarClipping,
        MeshHoleFilling<double>::TriangulationMethod::CDT,
        MeshHoleFilling<double>::TriangulationMethod::EarClipping3D
    }) {
        triangles = dummyTris;
        params.method = method;
        std::string methodName = (method == MeshHoleFilling<double>::TriangulationMethod::EarClipping) ? "EC(2D)" :
                                  (method == MeshHoleFilling<double>::TriangulationMethod::CDT) ? "CDT(2D)" : "EC3D";
        
        auto start = std::chrono::high_resolution_clock::now();
        try {
            MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "  " << methodName << ": " << (triangles.size() - dummyTris.size()) 
                      << " triangles in " << duration.count() << "ms" << std::endl;
            RecordResult("Large", methodName, true, triangles.size() - dummyTris.size());
        } catch (...) {
            std::cout << "  " << methodName << ": FAILED" << std::endl;
            RecordResult("Large", methodName, false, 0, "Exception");
        }
    }
}

// Test 5: Concave Hole with Sharp Angles
void TestConcaveHole() {
    std::cout << "\n=== Test 5: Concave Hole with Sharp Angles ===" << std::endl;
    
    std::vector<Vector3<double>> vertices;
    
    // Create star-shaped concave polygon
    const int points = 10;
    const double outerRadius = 5.0;
    const double innerRadius = 2.0;
    
    for (int i = 0; i < points * 2; ++i) {
        double angle = M_PI * i / points;
        double r = (i % 2 == 0) ? outerRadius : innerRadius;
        vertices.push_back({
            r * std::cos(angle),
            r * std::sin(angle),
            0.0
        });
    }
    
    MeshHoleFilling<double>::HoleBoundary hole;
    for (size_t i = 0; i < vertices.size(); ++i) {
        hole.vertices.push_back(static_cast<int32_t>(i));
    }
    
    std::cout << "  Star-shaped polygon with " << vertices.size() << " vertices" << std::endl;
    
    std::vector<std::array<int32_t, 3>> triangles, dummyTris;
    MeshHoleFilling<double>::Parameters params;
    params.autoFallback = false;
    
    for (auto method : {
        MeshHoleFilling<double>::TriangulationMethod::EarClipping,
        MeshHoleFilling<double>::TriangulationMethod::CDT,
        MeshHoleFilling<double>::TriangulationMethod::EarClipping3D
    }) {
        triangles = dummyTris;
        params.method = method;
        std::string methodName = (method == MeshHoleFilling<double>::TriangulationMethod::EarClipping) ? "EC(2D)" :
                                  (method == MeshHoleFilling<double>::TriangulationMethod::CDT) ? "CDT(2D)" : "EC3D";
        try {
            MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
            std::cout << "  " << methodName << ": " << (triangles.size() - dummyTris.size()) << " triangles" << std::endl;
            RecordResult("Concave", methodName, true, triangles.size() - dummyTris.size());
        } catch (...) {
            std::cout << "  " << methodName << ": FAILED" << std::endl;
            RecordResult("Concave", methodName, false, 0, "Exception");
        }
    }
}

// Test 6: Auto-Fallback Test
void TestAutoFallback() {
    std::cout << "\n=== Test 6: Auto-Fallback on Non-Planar Hole ===" << std::endl;
    
    // Create non-planar hole
    std::vector<Vector3<double>> vertices;
    const int n = 12;
    for (int i = 0; i < n; ++i) {
        double angle = 2.0 * M_PI * i / n;
        vertices.push_back({
            std::cos(angle),
            std::sin(angle),
            0.5 * std::sin(angle * 2)  // Significant non-planarity
        });
    }
    
    MeshHoleFilling<double>::HoleBoundary hole;
    for (int i = 0; i < n; ++i) {
        hole.vertices.push_back(i);
    }
    
    std::vector<std::array<int32_t, 3>> triangles, dummyTris;
    
    // Test with auto-fallback disabled
    MeshHoleFilling<double>::Parameters params;
    params.method = MeshHoleFilling<double>::TriangulationMethod::CDT;
    params.autoFallback = false;
    
    triangles = dummyTris;
    try {
        MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
        std::cout << "  CDT without fallback: " << (triangles.size() - dummyTris.size()) << " triangles" << std::endl;
    } catch (...) {
        std::cout << "  CDT without fallback: FAILED (as expected for non-planar)" << std::endl;
    }
    
    // Test with auto-fallback enabled
    params.autoFallback = true;
    params.planarityThreshold = 0.1;
    
    triangles = dummyTris;
    try {
        MeshHoleFilling<double>::FillHoles(vertices, triangles, params);
        std::cout << "  CDT with auto-fallback: " << (triangles.size() - dummyTris.size()) 
                  << " triangles (should use EC3D)" << std::endl;
        RecordResult("AutoFallback", "CDT→EC3D", true, triangles.size() - dummyTris.size());
    } catch (...) {
        std::cout << "  CDT with auto-fallback: FAILED" << std::endl;
        RecordResult("AutoFallback", "CDT→EC3D", false, 0, "Exception");
    }
}

// Print summary
void PrintSummary() {
    std::cout << "\n\n=== STRESS TEST SUMMARY ===" << std::endl;
    std::cout << std::setw(15) << "Test" 
              << std::setw(10) << "Method" 
              << std::setw(10) << "Result" 
              << std::setw(12) << "Triangles" 
              << "  Error" << std::endl;
    std::cout << std::string(70, '-') << std::endl;
    
    for (auto const& r : results) {
        std::cout << std::setw(15) << r.testName
                  << std::setw(10) << r.method
                  << std::setw(10) << (r.success ? "PASS" : "FAIL")
                  << std::setw(12) << r.trianglesGenerated
                  << "  " << r.errorMessage << std::endl;
    }
    
    // Calculate success rates
    std::cout << "\n=== SUCCESS RATES ===" << std::endl;
    std::map<std::string, std::pair<int, int>> methodStats;  // success, total
    
    for (auto const& r : results) {
        methodStats[r.method].second++;
        if (r.success) methodStats[r.method].first++;
    }
    
    for (auto const& stat : methodStats) {
        double rate = 100.0 * stat.second.first / stat.second.second;
        std::cout << "  " << std::setw(10) << stat.first << ": " 
                  << stat.second.first << "/" << stat.second.second 
                  << " (" << std::fixed << std::setprecision(1) << rate << "%)" << std::endl;
    }
}

int main() {
    std::cout << "=== GTE MESH HOLE FILLING - STRESS TEST SUITE ===" << std::endl;
    std::cout << "Testing robustness of triangulation methods" << std::endl;
    
    TestNonPlanarHole();
    TestDegenerateHole();
    TestElongatedHole();
    TestLargeHole();
    TestConcaveHole();
    TestAutoFallback();
    
    PrintSummary();
    
    return 0;
}
