#include <GTE/Mathematics/Co3Ne.h>
#include <iostream>
#include <cmath>

using namespace gte;

std::vector<Vector3<double>> CreateSpherePoints(int n) {
    std::vector<Vector3<double>> points;
    double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    
    for (int i = 0; i < n; ++i) {
        double theta = 2.0 * M_PI * i / phi;
        double phi_val = std::acos(1.0 - 2.0 * (i + 0.5) / n);
        double x = std::cos(theta) * std::sin(phi_val);
        double y = std::sin(theta) * std::sin(phi_val);
        double z = std::cos(phi_val);
        points.push_back({x, y, z});
    }
    return points;
}

int main() {
    auto points = CreateSpherePoints(50);
    std::cout << "Created " << points.size() << " sphere points\n";
    
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
    
    Co3Ne<double>::Parameters params;
    
    std::cout << "Running reconstruction...\n";
    bool success = Co3Ne<double>::Reconstruct(points, vertices, triangles, params);
    
    std::cout << "Success: " << (success ? "YES" : "NO") << "\n";
    std::cout << "Output vertices: " << vertices.size() << "\n";
    std::cout << "Output triangles: " << triangles.size() << "\n";
    
    return success ? 0 : 1;
}
