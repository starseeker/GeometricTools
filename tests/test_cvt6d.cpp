// Test program for CVT6D (6D Centroidal Voronoi Tessellation)
// Tests anisotropic CVT for mesh remeshing

#include <GTE/Mathematics/CVT6D.h>
#include <GTE/Mathematics/MeshAnisotropy.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>

// Simple OBJ file loader
bool LoadOBJ(
    std::string const& filename,
    std::vector<gte::Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
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
            gte::Vector3<double> v;
            iss >> v[0] >> v[1] >> v[2];
            vertices.push_back(v);
        }
        else if (type == "f")
        {
            std::array<int32_t, 3> face;
            for (int i = 0; i < 3; ++i)
            {
                std::string vertex;
                iss >> vertex;
                size_t slash = vertex.find('/');
                if (slash != std::string::npos)
                {
                    vertex = vertex.substr(0, slash);
                }
                face[i] = std::stoi(vertex) - 1;
            }
            triangles.push_back(face);
        }
    }

    return !vertices.empty() && !triangles.empty();
}

int main(int argc, char* argv[])
{
    std::cout << "Testing CVT6D (6D Centroidal Voronoi Tessellation)...\n\n";

    // Test 1: Basic 6D CVT
    std::cout << "Test 1: Basic 6D CVT\n";
    {
        // Create simple 6D sample points
        std::vector<gte::Vector<6, double>> samples;
        for (int i = 0; i < 100; ++i)
        {
            gte::Vector<6, double> p;
            p[0] = (i % 10) * 0.1;
            p[1] = (i / 10) * 0.1;
            p[2] = 0.5;
            p[3] = 0.1;
            p[4] = 0.1;
            p[5] = 0.1;
            samples.push_back(p);
        }

        // Create initial sites
        std::vector<gte::Vector<6, double>> sites;
        for (int i = 0; i < 10; ++i)
        {
            gte::Vector<6, double> s;
            s[0] = i * 0.1;
            s[1] = 0.5;
            s[2] = 0.5;
            s[3] = 0.1;
            s[4] = 0.1;
            s[5] = 0.1;
            sites.push_back(s);
        }

        gte::CVT6D<double> cvt;
        gte::CVT6D<double>::Parameters params;
        params.lloydIterations = 5;
        params.verbose = true;

        bool success = cvt.ComputeCVT(sites, samples, params);
        
        if (success)
        {
            std::cout << "✓ Basic 6D CVT succeeded\n";
            std::cout << "  Final sites count: " << sites.size() << "\n";
        }
        else
        {
            std::cout << "✗ Basic 6D CVT failed\n";
        }
    }

    // Test 2: Anisotropic CVT with simple mesh
    std::cout << "\nTest 2: Anisotropic CVT with simple mesh\n";
    {
        // Create simple 3D mesh (cube vertices)
        std::vector<gte::Vector3<double>> meshVertices = {
            {0.0, 0.0, 0.0},
            {1.0, 0.0, 0.0},
            {1.0, 1.0, 0.0},
            {0.0, 1.0, 0.0},
            {0.0, 0.0, 1.0},
            {1.0, 0.0, 1.0},
            {1.0, 1.0, 1.0},
            {0.0, 1.0, 1.0}
        };

        std::vector<std::array<int32_t, 3>> triangles = {
            {0, 1, 2}, {0, 2, 3},  // Bottom
            {4, 6, 5}, {4, 7, 6},  // Top
            {0, 4, 5}, {0, 5, 1},  // Front
            {2, 7, 3}, {2, 6, 7},  // Back
            {0, 3, 7}, {0, 7, 4},  // Left
            {1, 5, 6}, {1, 6, 2}   // Right
        };

        // Compute normals
        std::vector<gte::Vector3<double>> normals;
        gte::MeshAnisotropy<double>::SetAnisotropy(
            meshVertices, triangles, normals, 0.04);

        std::cout << "Mesh: " << meshVertices.size() << " vertices, "
                  << triangles.size() << " triangles\n";
        std::cout << "Computed " << normals.size() << " normals\n";

        // Create initial sites (subset of mesh vertices)
        std::vector<gte::Vector3<double>> sites = {
            {0.25, 0.25, 0.25},
            {0.75, 0.25, 0.25},
            {0.25, 0.75, 0.25},
            {0.75, 0.75, 0.25}
        };

        gte::CVT6D<double> cvt;
        gte::CVT6D<double>::Parameters params;
        params.lloydIterations = 10;
        params.verbose = true;

        bool success = cvt.ComputeAnisotropicCVT(
            meshVertices, normals, sites, 0.04, params);

        if (success)
        {
            std::cout << "✓ Anisotropic CVT succeeded\n";
            std::cout << "  Optimized " << sites.size() << " sites\n";
            std::cout << "  Final site positions:\n";
            for (size_t i = 0; i < sites.size(); ++i)
            {
                std::cout << "    Site " << i << ": ("
                          << sites[i][0] << ", "
                          << sites[i][1] << ", "
                          << sites[i][2] << ")\n";
            }
        }
        else
        {
            std::cout << "✗ Anisotropic CVT failed\n";
        }
    }

    // Test 3: Anisotropic CVT with mesh file (if provided)
    if (argc >= 2)
    {
        std::cout << "\nTest 3: Anisotropic CVT with mesh file\n";
        std::string inputFile = argv[1];
        
        std::vector<gte::Vector3<double>> vertices;
        std::vector<std::array<int32_t, 3>> triangles;
        
        if (LoadOBJ(inputFile, vertices, triangles))
        {
            std::cout << "Loaded mesh: " << vertices.size() << " vertices, "
                      << triangles.size() << " triangles\n";

            // Compute anisotropic normals
            std::vector<gte::Vector3<double>> normals;
            gte::MeshAnisotropy<double>::SetAnisotropy(
                vertices, triangles, normals, 0.04);

            // Create initial sites (sample from vertices)
            size_t numSites = std::min(size_t(100), vertices.size() / 10);
            std::vector<gte::Vector3<double>> sites;
            for (size_t i = 0; i < numSites; ++i)
            {
                size_t idx = (i * vertices.size()) / numSites;
                sites.push_back(vertices[idx]);
            }

            std::cout << "Using " << numSites << " initial sites\n";

            gte::CVT6D<double> cvt;
            gte::CVT6D<double>::Parameters params;
            params.lloydIterations = 10;
            params.verbose = false;  // Too verbose for large mesh

            bool success = cvt.ComputeAnisotropicCVT(
                vertices, normals, sites, 0.04, params);

            if (success)
            {
                std::cout << "✓ Mesh anisotropic CVT succeeded\n";
                std::cout << "  Optimized " << sites.size() << " sites\n";
            }
            else
            {
                std::cout << "✗ Mesh anisotropic CVT failed\n";
            }
        }
        else
        {
            std::cout << "Could not load mesh file: " << inputFile << "\n";
        }
    }

    std::cout << "\n=== CVT6D tests completed ===\n";
    std::cout << "\nThis implementation provides full 6D CVT for anisotropic remeshing.\n";
    std::cout << "The 6D distance metric naturally creates anisotropic Voronoi cells.\n";
    
    return 0;
}
