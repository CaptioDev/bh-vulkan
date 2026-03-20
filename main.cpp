#include <cmath>
#include <fstream>
#include <iostream>
#include <algorithm>

const int WIDTH = 1200;
const int HEIGHT = 1200;

const double rs = 1.0;
const double stepSize = 0.06;
const int maxSteps = 500;

struct Vec3 {
    double x, y, z;
};

double fract(double x) {
    return x - floor(x);
}

double hash(double x, double y) {
    return fract(sin(x*12.9898 + y*78.233) * 43758.5453);
}

Vec3 starfield(Vec3 dir) {
    double u = atan2(dir.z, dir.x);
    double v = asin(dir.y);

    double n = hash(u * 100.0, v * 100.0);

    if (n > 0.98) return {1.0, 1.0, 1.0};
    return {0.0, 0.0, 0.0};
}

Vec3 diskColor(double r) {
    double glow = exp(-fabs(r - 3.0));
    return {1.0 * glow, 0.6 * glow, 0.2 * glow};
}

Vec3 normalize(Vec3 v) {
    double len = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    return {v.x/len, v.y/len, v.z/len};
}

bool inDisk(double r, Vec3 pos) {
    return (fabs(pos.y) < 0.02) && (r > 2.5 && r < 5.0);
}

int main() {
    std::ofstream out("blackhole.ppm");
    out << "P3\n" << WIDTH << " " << HEIGHT << "\n255\n";

    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {

            // Screen space
            double u_screen = (double)x / WIDTH * 2.0 - 1.0;
            double v_screen = 1.0 - (double)y / HEIGHT * 2.0;

            Vec3 rayDir = normalize({u_screen * 1.2, v_screen * 1.2, -1.0});
            Vec3 pos = {0, 2, 10}; // elevated camera

            Vec3 color = {0,0,0};

            for (int i = 0; i < maxSteps; i++) {

                // Move ray forward
                pos.x += rayDir.x * stepSize;
                pos.y += rayDir.y * stepSize;
                pos.z += rayDir.z * stepSize;

                double r = sqrt(pos.x*pos.x + pos.y*pos.y + pos.z*pos.z);

                // Absorbed by black hole
                if (r < rs) {
                    color = {0,0,0};
                    break;
                }

                Vec3 toCenter = {-pos.x, -pos.y, -pos.z};
                toCenter = normalize(toCenter);

                double bend = rs / (r * r);

                // bend toward black hole center
                rayDir.x += toCenter.x * bend * stepSize * 5.0;
                rayDir.y += toCenter.y * bend * stepSize * 5.0;
                rayDir.z += toCenter.z * bend * stepSize * 5.0;

                rayDir = normalize(rayDir);

                // Accretion disk
                if (inDisk(r, pos)) {
                    color = diskColor(r);

                    // Doppler brightness
                    double doppler = 0.5 + 0.5 * rayDir.x;
                    color = {
                        color.x * doppler,
                        color.y * doppler,
                        color.z * doppler
                    };
                    break;
                }

                // Escaped to space
                if (r > 50.0) {
                    color = starfield(rayDir);
                    break;
                }
            }

            // Gamma correction
            auto gamma = [](double x) {
                return pow(x, 0.6);
            };

            int R = std::min(255, (int)(gamma(color.x) * 255));
            int G = std::min(255, (int)(gamma(color.y) * 255));
            int B = std::min(255, (int)(gamma(color.z) * 255));

            out << R << " " << G << " " << B << "\n";
        }
    }

    out.close();
    std::cout << "Rendered to blackhole.ppm\n";
}