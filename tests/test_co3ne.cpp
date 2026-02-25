// Test program for GTE Co3Ne surface reconstruction
// Demonstrates point cloud to mesh reconstruction using GTE style headers

#include <GTE/Mathematics/Co3Ne.h>
#include <GTE/Mathematics/MeshValidation.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

// Simple point cloud with normals loader (custom format)
// Format: x y z nx ny nz
bool LoadPointCloud(
    std::string const& filename,
    std::vector<gte::Vector3<double>>& points,
    std::vector<gte::Vector3<double>>& normals)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return false;
    }

    points.clear();
    normals.clear();

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        gte::Vector3<double> p, n;
        if (iss >> p[0] >> p[1] >> p[2] >> n[0] >> n[1] >> n[2])
        {
            points.push_back(p);
            normals.push_back(n);
        }
    }

    return !points.empty();
}

// Load OBJ and compute normals (for testing with mesh input)
bool LoadOBJAsPointCloud(
    std::string const& filename,
    std::vector<gte::Vector3<double>>& points,
    std::vector<gte::Vector3<double>>& normals)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return false;
    }

    std::vector<gte::Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;

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

    if (vertices.empty() || triangles.empty())
    {
        return false;
    }

    // Compute vertex normals
    normals.resize(vertices.size());
    std::fill(normals.begin(), normals.end(), gte::Vector3<double>::Zero());

    for (auto const& tri : triangles)
    {
        gte::Vector3<double> const& v0 = vertices[tri[0]];
        gte::Vector3<double> const& v1 = vertices[tri[1]];
        gte::Vector3<double> const& v2 = vertices[tri[2]];

        gte::Vector3<double> e1 = v1 - v0;
        gte::Vector3<double> e2 = v2 - v0;
        gte::Vector3<double> normal = Cross(e1, e2);
        
        normals[tri[0]] += normal;
        normals[tri[1]] += normal;
        normals[tri[2]] += normal;
    }

    for (auto& normal : normals)
    {
        Normalize(normal);
    }

    points = vertices;
    return true;
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
        std::cout << "Usage: " << argv[0] << " <input> <output.obj> [search_radius] [max_angle]" << std::endl;
        std::cout << "  input: Point cloud file (x y z nx ny nz) or OBJ mesh" << std::endl;
        std::cout << "  search_radius: Optional search radius (0 = auto, default)" << std::endl;
        std::cout << "  max_angle: Optional max normal angle in degrees (default: 60)" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = argv[2];
    double searchRadius = 0.0;
    double maxAngle = 60.0;

    if (argc >= 4)
    {
        searchRadius = std::stod(argv[3]);
    }
    if (argc >= 5)
    {
        maxAngle = std::stod(argv[4]);
    }

    std::cout << "=== GTE Co3Ne Surface Reconstruction Test ===" << std::endl;

    // Load point cloud
    std::cout << "Loading point cloud from " << inputFile << "..." << std::endl;
    std::vector<gte::Vector3<double>> points;
    std::vector<gte::Vector3<double>> normals;

    // Try loading as point cloud first, then as OBJ
    if (!LoadPointCloud(inputFile, points, normals))
    {
        std::cout << "  Not a point cloud file, trying OBJ format..." << std::endl;
        if (!LoadOBJAsPointCloud(inputFile, points, normals))
        {
            std::cerr << "Failed to load input!" << std::endl;
            return 1;
        }
    }

    std::cout << "  Points: " << points.size() << std::endl;
    std::cout << "  Normals: " << normals.size() << std::endl;

    // Verify normals are normalized
    size_t unnormalizedCount = 0;
    for (auto& normal : normals)
    {
        double len = Length(normal);
        if (std::abs(len - 1.0) > 1e-6)
        {
            unnormalizedCount++;
            if (len > 1e-9)
            {
                Normalize(normal);
            }
            else
            {
                // Zero normal, set to default
                normal = gte::Vector3<double>{ 0.0, 0.0, 1.0 };
            }
        }
    }
    
    if (unnormalizedCount > 0)
    {
        std::cout << "  Normalized " << unnormalizedCount << " normals" << std::endl;
    }

    // Surface reconstruction
    std::cout << "\nReconstruction Parameters:" << std::endl;
    std::cout << "  Search radius: " << (searchRadius > 0 ? std::to_string(searchRadius) : "auto") << std::endl;
    std::cout << "  Max normal angle: " << maxAngle << " degrees" << std::endl;

    gte::Co3Ne<double>::Parameters params;
    params.searchRadius = searchRadius;
    params.maxNormalAngle = maxAngle;
    params.kNeighbors = 20;
    params.orientNormals = true;
    params.smoothWithRVD = true;

    std::vector<gte::Vector3<double>> outVertices;
    std::vector<std::array<int32_t, 3>> outTriangles;

    std::cout << "\nRunning Co3Ne reconstruction..." << std::endl;
    bool success = gte::Co3Ne<double>::Reconstruct(
        points, outVertices, outTriangles, params);

    if (!success)
    {
        std::cerr << "Reconstruction failed!" << std::endl;
        return 1;
    }

    std::cout << "  Generated vertices: " << outVertices.size() << std::endl;
    std::cout << "  Generated triangles: " << outTriangles.size() << std::endl;

    // Validation
    auto validation = gte::MeshValidation<double>::Validate(outVertices, outTriangles);
    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "  Vertices: " << outVertices.size() << std::endl;
    std::cout << "  Triangles: " << outTriangles.size() << std::endl;
    std::cout << "  Manifold: " << (validation.isManifold ? "Yes" : "No") << std::endl;
    std::cout << "  Closed: " << (validation.isClosed ? "Yes" : "No") << std::endl;
    
    if (!validation.isManifold)
    {
        std::cout << "  Non-manifold edges: " << validation.nonManifoldEdges << std::endl;
    }
    if (!validation.isClosed)
    {
        std::cout << "  Boundary edges: " << validation.boundaryEdges << std::endl;
    }

    // Save result
    std::cout << "\nSaving to " << outputFile << "..." << std::endl;
    if (!SaveOBJ(outputFile, outVertices, outTriangles))
    {
        std::cerr << "Failed to save mesh!" << std::endl;
        return 1;
    }

    std::cout << "Done!" << std::endl;
    return 0;
}
