// Test program for GTE Anisotropic Mesh Remeshing
// Demonstrates anisotropic remeshing using 6D metric tensors

#include <GTE/Mathematics/MeshRemesh.h>
#include <GTE/Mathematics/MeshAnisotropy.h>
#include <GTE/Mathematics/MeshRepair.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Simple OBJ file loader
bool LoadOBJ(
    std::string const& filename,
    std::vector<gte::Vector3<double>>& vertices,
    std::vector<std::array<int32_t, 3>>& triangles)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file " << filename << std::endl;
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
                
                // Handle face formats: v, v/vt, v/vt/vn, v//vn
                size_t slash = vertex.find('/');
                if (slash != std::string::npos)
                {
                    vertex = vertex.substr(0, slash);
                }
                
                face[i] = std::stoi(vertex) - 1; // OBJ indices are 1-based
            }
            triangles.push_back(face);
        }
    }

    return !vertices.empty() && !triangles.empty();
}

// Simple OBJ file saver
bool SaveOBJ(
    std::string const& filename,
    std::vector<gte::Vector3<double>> const& vertices,
    std::vector<std::array<int32_t, 3>> const& triangles)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not write to file " << filename << std::endl;
        return false;
    }

    // Write vertices
    for (auto const& v : vertices)
    {
        file << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
    }

    // Write faces
    for (auto const& f : triangles)
    {
        file << "f " << (f[0] + 1) << " " << (f[1] + 1) << " " << (f[2] + 1) << "\n";
    }

    return true;
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "Usage: " << argv[0] << " input.obj output.obj [anisotropy_scale]\n";
        std::cout << "\n";
        std::cout << "Tests anisotropic remeshing functionality.\n";
        std::cout << "\n";
        std::cout << "Parameters:\n";
        std::cout << "  input.obj         - Input mesh file\n";
        std::cout << "  output.obj        - Output remeshed file\n";
        std::cout << "  anisotropy_scale  - Anisotropy scale (default: 0.04)\n";
        std::cout << "                      0 = isotropic, 0.02-0.1 = anisotropic\n";
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = argv[2];
    double anisotropyScale = 0.04;
    
    if (argc >= 4)
    {
        anisotropyScale = std::stod(argv[3]);
    }

    std::cout << "Loading mesh from " << inputFile << "...\n";
    
    std::vector<gte::Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    if (!LoadOBJ(inputFile, vertices, triangles))
    {
        return 1;
    }

    std::cout << "Input mesh: " << vertices.size() << " vertices, " 
              << triangles.size() << " triangles\n";

    // Test anisotropy computation
    std::cout << "\nTesting anisotropy computation...\n";
    
    std::vector<gte::Vector3<double>> normals;
    
    if (anisotropyScale > 0.0)
    {
        std::cout << "Computing anisotropic normals with scale = " << anisotropyScale << "\n";
        gte::MeshAnisotropy<double>::SetAnisotropy(vertices, triangles, normals, anisotropyScale);
        std::cout << "Computed " << normals.size() << " scaled normals\n";
        
        // Test 6D point creation
        auto points6D = gte::MeshAnisotropy<double>::Create6DPoints(vertices, normals);
        std::cout << "Created " << (points6D.size() / 6) << " points in 6D space\n";
        
        // Test extraction
        auto extractedPos = gte::MeshAnisotropy<double>::Extract3DPositions(points6D);
        auto extractedNorms = gte::MeshAnisotropy<double>::Extract3DNormals(points6D);
        std::cout << "Extracted " << extractedPos.size() << " positions and " 
                  << extractedNorms.size() << " normals\n";
    }
    else
    {
        std::cout << "Using isotropic mode (anisotropy scale = 0)\n";
        gte::MeshAnisotropy<double>::ComputeVertexNormals(vertices, triangles, normals);
    }

    // Test anisotropic remeshing
    std::cout << "\nTesting anisotropic remeshing...\n";
    
    gte::MeshRemesh<double>::Parameters params;
    params.targetVertexCount = vertices.size();  // Keep similar vertex count
    params.lloydIterations = 5;
    params.useRVD = true;
    params.useAnisotropic = (anisotropyScale > 0.0);
    params.anisotropyScale = anisotropyScale;
    params.curvatureAdaptive = false;  // Use uniform anisotropy for now
    
    std::cout << "Remeshing with parameters:\n";
    std::cout << "  Target vertices: " << params.targetVertexCount << "\n";
    std::cout << "  Lloyd iterations: " << params.lloydIterations << "\n";
    std::cout << "  Use RVD: " << (params.useRVD ? "yes" : "no") << "\n";
    std::cout << "  Anisotropic: " << (params.useAnisotropic ? "yes" : "no") << "\n";
    if (params.useAnisotropic)
    {
        std::cout << "  Anisotropy scale: " << params.anisotropyScale << "\n";
    }
    
    auto remeshedVertices = vertices;
    auto remeshedTriangles = triangles;
    
    bool success = gte::MeshRemesh<double>::Remesh(
        remeshedVertices, remeshedTriangles, params);
    
    if (!success)
    {
        std::cerr << "Remeshing failed!\n";
        return 1;
    }

    std::cout << "\nOutput mesh: " << remeshedVertices.size() << " vertices, " 
              << remeshedTriangles.size() << " triangles\n";

    // Save result
    std::cout << "\nSaving result to " << outputFile << "...\n";
    if (!SaveOBJ(outputFile, remeshedVertices, remeshedTriangles))
    {
        return 1;
    }

    std::cout << "Success!\n";
    
    // Test curvature-adaptive mode
    std::cout << "\nTesting curvature-adaptive anisotropy...\n";
    std::vector<gte::Vector3<double>> curvatureNormals;
    gte::MeshAnisotropy<double>::ComputeCurvatureAdaptiveAnisotropy(
        vertices, triangles, curvatureNormals, anisotropyScale);
    std::cout << "Computed " << curvatureNormals.size() << " curvature-adaptive normals\n";

    return 0;
}
