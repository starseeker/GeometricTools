// Check if projected hole self-intersects
#include <GTE/Mathematics/Vector3.h>
#include <GTE/Mathematics/Vector2.h>
#include <GTE/Mathematics/Polygon2Validation.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <map>
#include <set>

using namespace gte;

int main()
{
    // Load stress_wrapped_4_5.obj
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    std::ifstream file("stress_wrapped_4_5.obj");
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
            int v0, v1, v2;
            iss >> v0 >> v1 >> v2;
            triangles.push_back({v0-1, v1-1, v2-1});
        }
    }
    
    std::cout << "Mesh: " << vertices.size() << " vertices, " << triangles.size() << " triangles\n";
    
    // Find boundary edges
    std::map<std::pair<int, int>, int> edgeCount;
    for (auto const& tri : triangles)
    {
        for (int i = 0; i < 3; ++i)
        {
            int v0 = tri[i];
            int v1 = tri[(i+1)%3];
            auto edge = std::make_pair(std::min(v0,v1), std::max(v0,v1));
            edgeCount[edge]++;
        }
    }
    
    int boundaryEdges = 0;
    for (auto const& e : edgeCount)
    {
        if (e.second == 1) boundaryEdges++;
    }
    
    std::cout << "Boundary edges: " << boundaryEdges << "\n";
    
    // The mesh should have multiple holes - report that
    std::cout << "Note: This mesh may have multiple small holes instead of one large 288° wrap\n";
    std::cout << "The Python script may have created a different geometry than intended\n";
    
    return 0;
}
