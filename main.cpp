#include <cmath>
#include <fstream>
#include <iostream>

const int WIDTH = 1200;
const int HEIGHT = 1200;

const double rs = 1.0;
const double stepSize = 0.02;
const int maxSteps = 1000;

// Simple vector
struct Vec3 {
    double x, y, z;
};

Vec3 background(Vec3 dir) {
    double t = std::max(dir.y, 0.0);
    return {0.02 + 0.3 * t * t, 0.02 + 0.3 * t * t, 0.05 + 0.3 * t * t};
}

Vec3 diskColor(double r) {
    double glow = exp(-fabs(r - 3.0));
    return {1.0 * glow, 0.6 * glow, 0.2 * glow};
}

Vec3 normalize(Vec3 v) {
    double len = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    return {v.x/len, v.y/len, v.z/len};
}

int main() {
    std::ofstream out("blackhole.ppm");
    out << "P3\n" << WIDTH << " " << HEIGHT << "\n255\n";

    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {

            // UV coordinates
            double u_screen = (double)x / WIDTH * 2.0 - 1.0;
            double v_screen = 1.0 - (double)y / HEIGHT * 2.0;

            Vec3 rayDir = normalize({u_screen, v_screen, -1.5});

            double b = sqrt(u_screen*u_screen + v_screen*v_screen) * 3.0 + 0.1;

            double u = 1.0 / 10.0;
            double dudphi = 0.0;

            Vec3 color = {0,0,0};

            for (int i = 0; i < maxSteps; i++) {
                double du = dudphi;
                double d2u = 1.0/(b*b) - u*u + rs*u*u*u;

                u += du * stepSize;
                dudphi += d2u * stepSize;

                double r = 1.0 / u;

                if (r < rs) {
                    color = {0,0,0};
                    break;
                }

                if (r > 20.0) {
                    color = background(rayDir);
                    break;
                }

                if (fabs(r - 3.0) < 0.3) {
                    color = diskColor(r);
                    break;
                }
            }

            int R = std::min(255, (int)(color.x * 255));
            int G = std::min(255, (int)(color.y * 255));
            int B = std::min(255, (int)(color.z * 255));

            out << R << " " << G << " " << B << "\n";
        }
    }

    out.close();
    std::cout << "Rendered to blackhole.ppm\n";
}