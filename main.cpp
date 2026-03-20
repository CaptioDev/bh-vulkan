#include <cmath>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <vector>
#include <omp.h>
#include <ncurses.h>
#include <atomic>

const int WIDTH = 4800;
const int HEIGHT = 4800;

const double rs = 1.0;
const double stepSize = 0.02;
const int maxSteps = 1000;

struct Vec3 {
    double x, y, z;
};

// Utility functions
double fract(double x) { return x - floor(x); }

double hash(double x, double y) {
    return fract(sin(x * 12.9898 + y * 78.233) * 43758.5453);
}

Vec3 normalize(Vec3 v) {
    double len = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    return {v.x / len, v.y / len, v.z / len};
}

// Black hole accretion disk color
Vec3 diskColor(double r) {
    double glow = exp(-fabs(r - 3.0));
    return {1.0 * glow, 0.6 * glow, 0.2 * glow};
}

// Starfield
Vec3 starfield(Vec3 dir) {
    double u = atan2(dir.z, dir.x);
    double v = asin(dir.y);
    double n = hash(u * 800.0, v * 800.0);  // denser sampling

    if (n > 0.85) {
        double brightness = 0.5 + 0.5 * hash(u * 1200.0, v * 1200.0);
        return {brightness, brightness, brightness};
    }
    return {0.0, 0.0, 0.0};
}

// Check if position is in accretion disk
bool inDisk(double r, Vec3 pos) {
    return (fabs(pos.y) < 0.02) && (r > 2.5 && r < 5.0);
}

int main() {
    std::ofstream out("blackhole.ppm");
    out << "P3\n" << WIDTH << " " << HEIGHT << "\n255\n";

    std::vector<Vec3> image(WIDTH * HEIGHT);

    // Initialize ncurses
    initscr();
    noecho();
    curs_set(0);
    std::atomic<int> rows_done(0);

    #pragma omp parallel for schedule(dynamic)
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            double u_screen = (double)x / WIDTH * 2.0 - 1.0;
            double v_screen = 1.0 - (double)y / HEIGHT * 2.0;

            Vec3 rayDir = normalize({u_screen * 1.2, v_screen * 1.2, -1.0});
            Vec3 pos = {0, 2, 10};
            Vec3 color = {0, 0, 0};

            for (int i = 0; i < maxSteps; i++) {
                pos.x += rayDir.x * stepSize;
                pos.y += rayDir.y * stepSize;
                pos.z += rayDir.z * stepSize;

                double r = sqrt(pos.x*pos.x + pos.y*pos.y + pos.z*pos.z);

                if (r < rs) { color = {0,0,0}; break; }

                Vec3 toCenter = normalize({-pos.x, -pos.y, -pos.z});
                double bend = rs / (r * r);

                rayDir.x += toCenter.x * bend * stepSize;
                rayDir.y += toCenter.y * bend * stepSize;
                rayDir.z += toCenter.z * bend * stepSize;
                rayDir = normalize(rayDir);

                if (inDisk(r, pos)) {
                    color = diskColor(r);
                    double doppler = 0.5 + 0.5 * rayDir.x;
                    color = {color.x * doppler, color.y * doppler, color.z * doppler};
                    break;
                }

                if (r > 50.0) { color = starfield(rayDir); break; }
            }

            image[y * WIDTH + x] = color;
        }

        // Update ncurses progress bar (only one thread prints)
        rows_done++;
        if (omp_get_thread_num() == 0) {
            float progress = float(rows_done) / HEIGHT;
            int barWidth = 50;
            int pos = int(barWidth * progress);

            move(0, 0);
            printw("[");
            for (int i = 0; i < barWidth; i++) {
                printw(i < pos ? "=" : " ");
            }
            printw("] %3d%%", int(progress * 100));
            refresh();
        }
    }

    endwin();  // close ncurses

    // Write image to file
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        auto gamma = [](double x) { return pow(x, 0.6); };
        int R = std::min(255, (int)(gamma(image[i].x) * 255));
        int G = std::min(255, (int)(gamma(image[i].y) * 255));
        int B = std::min(255, (int)(gamma(image[i].z) * 255));
        out << R << " " << G << " " << B << "\n";
    }

    out.close();
    std::cout << "Rendered to blackhole.ppm\n";
}