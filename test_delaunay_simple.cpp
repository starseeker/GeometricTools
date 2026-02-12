#include <GTE/Mathematics/Delaunay3.h>
#include <GTE/Mathematics/Vector3.h>
#include <iostream>
#include <vector>

using namespace gte;

int main() {
    std::vector<Vector3<double>> sites = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},
        {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1},
        {0.5,0.5,0.5}
    };
    
    Delaunay3<double> del;
    if (del(sites)) {
        std::cout << "Delaunay built successfully" << std::endl;
        auto const& tets = del.GetIndices();
        std::cout << "Number of tetrahedra: " << tets.size() / 4 << std::endl;
    } else {
        std::cout << "Delaunay failed" << std::endl;
    }
    
    return 0;
}
