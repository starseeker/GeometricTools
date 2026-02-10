// Compare GTE implementation with Geogram on same inputs
#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshValidation.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>

using namespace gte;

// Simple OBJ loader
bool LoadOBJ(std::string const& filename,
    std::vector<Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;
        
        if (prefix == "v")
        {
            double x, y, z;
            iss >> x >> y >> z;
            vertices.push_back(Vector3<double>{x, y, z});
        }
        else if (prefix == "f")
        {
            int32_t v0, v1, v2;
            iss >> v0 >> v1 >> v2;
            triangles.push_back({v0-1, v1-1, v2-1});
        }
    }
    return true;
}

// Save OBJ
void SaveOBJ(std::string const& filename,
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }
    for (auto const& t : triangles)
    {
        file << "f " << (t[0]+1) << " " << (t[1]+1) << " " << (t[2]+1) << "\n";
    }
}

// Compute signed volume using divergence theorem
double ComputeVolume(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    double volume = 0.0;
    
    for (auto const& tri : triangles)
    {
        Vector3<double> const& v0 = vertices[tri[0]];
        Vector3<double> const& v1 = vertices[tri[1]];
        Vector3<double> const& v2 = vertices[tri[2]];
        
        // Signed volume of tetrahedron formed by origin and triangle
        // V = (1/6) * dot(v0, cross(v1, v2))
        Vector3<double> cross_v1_v2 = Cross(v1, v2);
        volume += Dot(v0, cross_v1_v2);
    }
    
    return std::abs(volume / 6.0);
}

// Compute total surface area
double ComputeSurfaceArea(
    std::vector<Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    double area = 0.0;
    
    for (auto const& tri : triangles)
    {
        Vector3<double> const& v0 = vertices[tri[0]];
        Vector3<double> const& v1 = vertices[tri[1]];
        Vector3<double> const& v2 = vertices[tri[2]];
        
        Vector3<double> edge1 = v1 - v0;
        Vector3<double> edge2 = v2 - v0;
        Vector3<double> cross_product = Cross(edge1, edge2);
        
        area += Length(cross_product) * 0.5;
    }
    
    return area;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <input.obj> [geogram_output.obj]\n";
        std::cerr << "  If geogram_output.obj is provided, compares with it\n";
        std::cerr << "  Otherwise, just reports GTE metrics\n";
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string geogramFile = (argc >= 3) ? argv[2] : "";
    
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "=== Mesh Comparison: GTE vs Geogram ===\n\n";
    
    // Load input
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    if (!LoadOBJ(inputFile, vertices, triangles))
    {
        std::cerr << "Failed to load " << inputFile << "\n";
        return 1;
    }
    
    std::cout << "Input: " << inputFile << "\n";
    std::cout << "  Vertices: " << vertices.size() << "\n";
    std::cout << "  Triangles: " << triangles.size() << "\n";
    
    double inputVolume = ComputeVolume(vertices, triangles);
    double inputArea = ComputeSurfaceArea(vertices, triangles);
    
    std::cout << "  Volume: " << inputVolume << "\n";
    std::cout << "  Surface Area: " << inputArea << "\n";
    
    auto inputValidation = MeshValidation<double>::Validate(vertices, triangles, false);
    std::cout << "  Manifold: " << (inputValidation.isManifold ? "Yes" : "No") << "\n";
    std::cout << "  Boundary Edges: " << inputValidation.boundaryEdges << "\n\n";
    
    // Process with GTE
    std::cout << "Processing with GTE...\n";
    auto gteVertices = vertices;
    auto gteTriangles = triangles;
    
    // Repair
    MeshRepair<double>::Parameters repairParams;
    repairParams.epsilon = 1e-6;
    MeshRepair<double>::Repair(gteVertices, gteTriangles, repairParams);
    
    // Fill holes
    MeshHoleFilling<double>::Parameters fillParams;
    fillParams.method = MeshHoleFilling<double>::TriangulationMethod::CDT;
    fillParams.autoFallback = true;
    fillParams.validateOutput = true;
    fillParams.requireManifold = true;
    
    MeshHoleFilling<double>::FillHoles(gteVertices, gteTriangles, fillParams);
    
    double gteVolume = ComputeVolume(gteVertices, gteTriangles);
    double gteArea = ComputeSurfaceArea(gteVertices, gteTriangles);
    
    auto gteValidation = MeshValidation<double>::Validate(gteVertices, gteTriangles, false);
    
    std::cout << "\nGTE Output:\n";
    std::cout << "  Vertices: " << gteVertices.size() << "\n";
    std::cout << "  Triangles: " << gteTriangles.size() << "\n";
    std::cout << "  Volume: " << gteVolume << "\n";
    std::cout << "  Surface Area: " << gteArea << "\n";
    std::cout << "  Manifold: " << (gteValidation.isManifold ? "Yes" : "No") << "\n";
    std::cout << "  Boundary Edges: " << gteValidation.boundaryEdges << "\n";
    
    // Save GTE output
    SaveOBJ("gte_output.obj", gteVertices, gteTriangles);
    std::cout << "  Saved to: gte_output.obj\n";
    
    // Compare with Geogram if provided
    if (!geogramFile.empty())
    {
        std::vector<Vector3<double>> geoVertices;
        std::vector<std::array<int32_t, 3>> geoTriangles;
        
        if (!LoadOBJ(geogramFile, geoVertices, geoTriangles))
        {
            std::cerr << "\nFailed to load Geogram output: " << geogramFile << "\n";
            return 1;
        }
        
        double geoVolume = ComputeVolume(geoVertices, geoTriangles);
        double geoArea = ComputeSurfaceArea(geoVertices, geoTriangles);
        
        std::cout << "\nGeogram Output: " << geogramFile << "\n";
        std::cout << "  Vertices: " << geoVertices.size() << "\n";
        std::cout << "  Triangles: " << geoTriangles.size() << "\n";
        std::cout << "  Volume: " << geoVolume << "\n";
        std::cout << "  Surface Area: " << geoArea << "\n";
        
        std::cout << "\n=== COMPARISON ===\n";
        std::cout << "Metric             | GTE          | Geogram      | Difference   | % Diff\n";
        std::cout << "-------------------|--------------|--------------|--------------|--------\n";
        
        // Vertices
        int vertDiff = (int)gteVertices.size() - (int)geoVertices.size();
        std::cout << "Vertices           | " << std::setw(12) << gteVertices.size()
                  << " | " << std::setw(12) << geoVertices.size()
                  << " | " << std::setw(12) << vertDiff << " |\n";
        
        // Triangles
        int triDiff = (int)gteTriangles.size() - (int)geoTriangles.size();
        double triPct = (geoTriangles.size() > 0) ? 
            100.0 * triDiff / geoTriangles.size() : 0.0;
        std::cout << "Triangles          | " << std::setw(12) << gteTriangles.size()
                  << " | " << std::setw(12) << geoTriangles.size()
                  << " | " << std::setw(12) << triDiff
                  << " | " << std::setw(6) << triPct << "%\n";
        
        // Volume
        double volDiff = gteVolume - geoVolume;
        double volPct = (geoVolume > 1e-10) ? 100.0 * volDiff / geoVolume : 0.0;
        std::cout << "Volume             | " << std::setw(12) << gteVolume
                  << " | " << std::setw(12) << geoVolume
                  << " | " << std::setw(12) << volDiff
                  << " | " << std::setw(6) << volPct << "%\n";
        
        // Surface Area
        double areaDiff = gteArea - geoArea;
        double areaPct = (geoArea > 1e-10) ? 100.0 * areaDiff / geoArea : 0.0;
        std::cout << "Surface Area       | " << std::setw(12) << gteArea
                  << " | " << std::setw(12) << geoArea
                  << " | " << std::setw(12) << areaDiff
                  << " | " << std::setw(6) << areaPct << "%\n";
        
        std::cout << "\n=== ASSESSMENT ===\n";
        
        // Volume comparison
        if (std::abs(volPct) < 1.0)
        {
            std::cout << "✅ Volume match: Excellent (< 1% difference)\n";
        }
        else if (std::abs(volPct) < 5.0)
        {
            std::cout << "✅ Volume match: Good (< 5% difference)\n";
        }
        else if (std::abs(volPct) < 10.0)
        {
            std::cout << "⚠️  Volume match: Acceptable (< 10% difference)\n";
        }
        else
        {
            std::cout << "❌ Volume match: Poor (>= 10% difference)\n";
        }
        
        // Triangle count comparison
        if (std::abs(triPct) < 5.0)
        {
            std::cout << "✅ Triangle count: Similar (< 5% difference)\n";
        }
        else if (std::abs(triPct) < 20.0)
        {
            std::cout << "⚠️  Triangle count: Different (< 20% difference)\n";
        }
        else
        {
            std::cout << "⚠️  Triangle count: Significantly different (>= 20% difference)\n";
        }
        
        std::cout << "\nNote: Different triangle counts are OK if volumes match.\n";
        std::cout << "      CDT may produce more/better quality triangles than Geogram.\n";
    }
    
    return 0;
}
