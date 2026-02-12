#include <GTE/Mathematics/Vector3.h>
#include <vector>
#include <iostream>

using namespace gte;

int main() {
    std::vector<Vector3<double>> vecs = {{0,0,0}, {1,1,1}};
    auto ptr = vecs.data();
    std::cout << "Type: " << typeid(ptr).name() << std::endl;
    return 0;
}
