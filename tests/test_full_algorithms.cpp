// Test program for full Geogram algorithm implementations
// Tests MeshRemesh

#include <GTE/Mathematics/MeshRemesh.h>
#include <GTE/Mathematics/MeshValidation.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace gte;

// Load OBJ file
bool LoadOBJ(std::string const& filename,
             std::vector<Vector3<double>>& vertices,
             std::vector<std::array<int32_t, 3>>& triangles)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to open: " << filename << std::endl;
        return false;
    }

    vertices.clear();
    triangles.clear();

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        if (type == "v")
        {
            double x, y, z;
            iss >> x >> y >> z;
            vertices.push_back(Vector3<double>{ x, y, z });
        }
        else if (type == "f")
        {
            int32_t v0, v1, v2;
            iss >> v0 >> v1 >> v2;
            // OBJ indices are 1-based
            triangles.push_back({ v0 - 1, v1 - 1, v2 - 1 });
        }
    }

    std::cout << "Loaded " << vertices.size() << " vertices, "
              << triangles.size() << " triangles" << std::endl;
    return true;
}

// Save OBJ file
bool SaveOBJ(std::string const& filename,
             std::vector<Vector3<double>> const& vertices,
             std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to create: " << filename << std::endl;
        return false;
    }

    // Write vertices
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }

    // Write faces (OBJ uses 1-based indexing)
    for (auto const& tri : triangles)
    {
        file << "f " << (tri[0] + 1) << " " << (tri[1] + 1) << " " << (tri[2] + 1) << "\n";
    }

    std::cout << "Saved " << vertices.size() << " vertices, "
              << triangles.size() << " triangles to " << filename << std::endl;
    return true;
}

// Test MeshRemesh
void TestMeshRemesh()
{
    std::cout << "\n===== Testing MeshRemesh =====" << std::endl;

    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;

    // Try to load a test mesh
    bool loaded = false;
    // Test file priority: tiny (simple), concave (complex), ec3d (validated)
    std::vector<std::string> testFiles = {
        "test_tiny.obj",         // Small mesh for quick testing
        "stress_concave.obj",    // Concave features test
        "test_ec3d.obj"          // Ear-clipping validated mesh
    };

    for (auto const& filename : testFiles)
    {
        if (LoadOBJ(filename, vertices, triangles))
        {
            loaded = true;
            std::cout << "Loaded test mesh: " << filename << std::endl;
            break;
        }
    }

    if (!loaded)
    {
        std::cout << "No test mesh found, creating simple mesh" << std::endl;
        // Create a simple triangulated square
        vertices = {
            {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {2.0, 2.0, 0.0}, {0.0, 2.0, 0.0},
            {1.0, 0.0, 0.0}, {2.0, 1.0, 0.0}, {1.0, 2.0, 0.0}, {0.0, 1.0, 0.0},
            {1.0, 1.0, 0.0}
        };
        triangles = {
            {0, 4, 8}, {4, 1, 8}, {1, 5, 8}, {5, 2, 8},
            {2, 6, 8}, {6, 3, 8}, {3, 7, 8}, {7, 0, 8}
        };
    }

    std::cout << "Input: " << vertices.size() << " vertices, "
              << triangles.size() << " triangles" << std::endl;

    MeshRemesh<double>::Parameters params;
    params.lloydIterations = 5;
    params.smoothIterations = 3;
    params.smoothingFactor = 0.5;
    params.preserveBoundary = true;
    params.projectToSurface = true;
    params.targetVertexCount = vertices.size(); // Keep similar size

    bool success = MeshRemesh<double>::Remesh(vertices, triangles, params);

    if (success)
    {
        std::cout << "MeshRemesh SUCCESS: " << vertices.size() << " vertices, "
                  << triangles.size() << " triangles" << std::endl;
        SaveOBJ("test_remesh_full_output.obj", vertices, triangles);

        // Validate mesh
        MeshValidation<double> validation;
        auto result = validation.Validate(vertices, triangles);
        std::cout << "Validation: " << (result.isValid ? "VALID" : "INVALID") << std::endl;
        if (!result.isValid)
        {
            std::cout << "  - " << result.errorMessage << std::endl;
        }
    }
    else
    {
        std::cout << "MeshRemesh FAILED" << std::endl;
    }
}

int main(int argc, char* argv[])
{
    std::cout << "===== Full Geogram Algorithms Test =====" << std::endl;
    std::cout << "Testing MeshRemesh implementation" << std::endl;

    TestMeshRemesh();

    std::cout << "\n===== Tests Complete =====" << std::endl;
    return 0;
}
