#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec4 params;    // x: time, y: width, z: height, w: mass
    vec4 cameraPos; // xyz: pos, w: spin_a
} pc;

const float M = 1.0;
const int MAX_STEPS = 300;
const float STEP_BASE = 0.8;
float p_t; 

void compute_K(in vec3 pos, in vec3 p, out float K) {
    float R2 = dot(pos, pos);
    float a = pc.cameraPos.w;
    float a2 = a * a;
    float y2 = pos.y * pos.y;
    
    float r2 = 0.5 * (R2 - a2 + sqrt((R2 - a2)*(R2 - a2) + 4.0 * a2 * y2));
    float r = sqrt(r2);
    float f = (2.0 * M * r * r2) / (r2 * r2 + a2 * y2 + 1e-6);
    
    float k_x = (r * pos.x + a * pos.z) / (r2 + a2 + 1e-6);
    float k_y = pos.y / (r + 1e-6);
    float k_z = (r * pos.z - a * pos.x) / (r2 + a2 + 1e-6);
    
    float k_dot_p = -p_t + k_x * p.x + k_y * p.y + k_z * p.z;
    K = f * k_dot_p * k_dot_p;
}

vec3 compute_force(in vec3 pos, in vec3 p) {
    float eps = 1e-3;
    float K_x_plus, K_x_minus, K_y_plus, K_y_minus, K_z_plus, K_z_minus;
    
    compute_K(pos + vec3(eps, 0.0, 0.0), p, K_x_plus);
    compute_K(pos - vec3(eps, 0.0, 0.0), p, K_x_minus);
    compute_K(pos + vec3(0.0, eps, 0.0), p, K_y_plus);
    compute_K(pos - vec3(0.0, eps, 0.0), p, K_y_minus);
    compute_K(pos + vec3(0.0, 0.0, eps), p, K_z_plus);
    compute_K(pos - vec3(0.0, 0.0, eps), p, K_z_minus);
    
    return 0.5 * vec3((K_x_plus - K_x_minus) / (2.0 * eps), (K_y_plus - K_y_minus) / (2.0 * eps), (K_z_plus - K_z_minus) / (2.0 * eps));
}

vec3 compute_velocity(in vec3 pos, in vec3 p) {
    float R2 = dot(pos, pos);
    float a = pc.cameraPos.w;
    float a2 = a * a;
    float y2 = pos.y * pos.y;
    float r2 = 0.5 * (R2 - a2 + sqrt((R2 - a2)*(R2 - a2) + 4.0 * a2 * y2));
    float r = sqrt(r2);
    
    float f = (2.0 * M * r * r2) / (r2 * r2 + a2 * y2 + 1e-6);
    
    float k_x = (r * pos.x + a * pos.z) / (r2 + a2 + 1e-6);
    float k_y = pos.y / (r + 1e-6);
    float k_z = (r * pos.z - a * pos.x) / (r2 + a2 + 1e-6);
    
    float k_dot_p = -p_t + k_x * p.x + k_y * p.y + k_z * p.z;
    return p - f * k_dot_p * vec3(k_x, k_y, k_z);
}

void RK4_step(inout vec3 x, inout vec3 p, float dt) {
    vec3 v1 = compute_velocity(x, p);
    vec3 f1 = compute_force(x, p);
    vec3 x2 = x + 0.5 * dt * v1;
    vec3 p2 = p + 0.5 * dt * f1;
    vec3 v2 = compute_velocity(x2, p2);
    vec3 f2 = compute_force(x2, p2);
    vec3 x3 = x + 0.5 * dt * v2;
    vec3 p3 = p + 0.5 * dt * f2;
    vec3 v3 = compute_velocity(x3, p3);
    vec3 f3 = compute_force(x3, p3);
    vec3 x4 = x + dt * v3;
    vec3 p4 = p + dt * f3;
    vec3 v4 = compute_velocity(x4, p4);
    vec3 f4 = compute_force(x4, p4);
    
    x += dt * (v1 + 2.0 * v2 + 2.0 * v3 + v4) / 6.0;
    p += dt * (f1 + 2.0 * f2 + 2.0 * f3 + f4) / 6.0;
}

float compute_isco() {
    float a = pc.cameraPos.w;
    float Z1 = 1.0 + pow(1.0 - a*a, 1.0/3.0) * (pow(1.0 + a, 1.0/3.0) + pow(1.0 - a, 1.0/3.0));
    float Z2 = sqrt(3.0 * a * a + Z1 * Z1);
    return 3.0 + Z2 - sqrt(max(0.0, (3.0 - Z1) * (3.0 + Z1 + 2.0 * Z2)));
}

// Employs Bardeen-Petterson Effect mathematical warping
// Precesses and aligns the disk to the fluid equator structurally based on radial distance.
float get_disk_height(float r, float phi) {
    float r_isco = compute_isco();
    // Tilt transitions from 0 (inner spin alignment) to large radial offset tilt mapping
    float tilt = 0.5 * smoothstep(r_isco + 0.5, r_isco + 12.0, r); 
    // Twist twists infinitely dense entering extreme relativistic orbital shells
    float twist = 15.0 / (r + 1e-4);
    return r * tan(tilt) * sin(phi - twist);
}

vec3 evaluate_disk(vec3 pos, vec3 p, uint winding_number) {
    float a = pc.cameraPos.w;
    float r_isco = compute_isco();
    float r = length(vec2(pos.x, pos.z));
    
    if (r <= r_isco || r > 25.0) return vec3(0.0);
    
    float U_t = (pow(r, 1.5) + a) / sqrt(pow(r, 3.0) - 3.0*r*r + 2.0*a*pow(r, 1.5));
    float U_phi = 1.0 / sqrt(pow(r, 3.0) - 3.0*r*r + 2.0*a*pow(r, 1.5));
    
    float denominator = p_t * U_t - p.x * pos.z * U_phi + p.z * pos.x * U_phi;
    if (denominator == 0.0) return vec3(0.0);
    float g_factor = p_t / denominator;
    
    if (g_factor <= 0.0) return vec3(0.0);
    
    // Novikov-Thorne strict smooth envelope
    float T_base = pow(max(0.0, 1.0 - sqrt(r_isco / r)) / (r * r * r), 0.25);
    
    // Lin-Shu analytical spiral density map (m=2 two arms mapping logical cluster turbulence)
    float phi = atan(pos.z, pos.x);
    float spiral = 1.0 + 0.85 * cos(2.0 * phi - 4.0 * log(r) - pc.params.x * 2.0);
    float T = T_base * max(0.05, spiral);
    
    float effective_T = T * g_factor;
    float intensity = pow(g_factor, 4.0) * T * 1200.0;
    
    vec3 hot = vec3(0.1, 0.6, 1.0);
    vec3 mid = vec3(1.0, 0.8, 0.3);
    vec3 cold = vec3(0.7, 0.1, 0.0);
    
    vec3 col = mix(cold, mid, smoothstep(0.0, 0.3, effective_T));
    col = mix(col, hot, smoothstep(0.3, 1.0, effective_T));
    
    vec3 emission = col * intensity;
    
    // Boundary Separatrix isolation: Highlights highly critical chaotic mappings with extreme non-blackbody visuals
    if (winding_number >= 3) {
        emission = mix(emission, vec3(0.0, 1.0, 0.8) * intensity * 3.0, 0.85); 
    }
    
    return emission;
}

vec3 render(vec2 uv) {
    vec3 pos = pc.cameraPos.xyz;
    vec3 forward = normalize(-pos);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(right, forward);
    
    vec3 p = normalize(forward + uv.x * right * 0.5 + uv.y * up * 0.5);
    
    float a = pc.cameraPos.w;
    float a2 = a * a;
    
    float R2 = dot(pos, pos);
    float y2 = pos.y * pos.y;
    float r2 = 0.5 * (R2 - a2 + sqrt((R2 - a2)*(R2 - a2) + 4.0 * a2 * y2));
    float r = sqrt(r2);
    float f = (2.0 * M * r * r2) / (r2 * r2 + a2 * y2 + 1e-6);
    float k_x = (r * pos.x + a * pos.z) / (r2 + a2 + 1e-6);
    float k_y = pos.y / (r + 1e-6);
    float k_z = (r * pos.z - a * pos.x) / (r2 + a2 + 1e-6);
    float S = k_x * p.x + k_y * p.y + k_z * p.z;
    float denom = 1.0 + f;
    float A = (-f * S + sqrt(max(0.0, 1.0 + f - f * S * S))) / denom;
    p_t = -A;
    
    vec3 col = vec3(0.0);
    float alpha = 1.0;
    float r_H = M + sqrt(max(0.0, M*M - a*a));
    
    vec3 prev_pos = pos;
    float prev_r = length(vec2(prev_pos.x, prev_pos.z));
    float prev_phi = atan(prev_pos.z, prev_pos.x);
    float prev_disk_y = get_disk_height(prev_r, prev_phi);
    
    uint winding_number = 0;
    
    for (int i = 0; i < MAX_STEPS; i++) {
        float curr_R2 = dot(pos, pos);
        float curr_y2 = pos.y * pos.y;
        float curr_r2 = 0.5 * (curr_R2 - a2 + sqrt((curr_R2 - a2)*(curr_R2 - a2) + 4.0 * a2 * curr_y2));
        float curr_r = sqrt(curr_r2);
        
        if (curr_r <= r_H * 1.01) break; 
        if (curr_r > 85.0) break;
        
        float dt = STEP_BASE * max(0.1, curr_r / 10.0);
        RK4_step(pos, p, dt);
        
        float hit_r = length(vec2(pos.x, pos.z));
        float hit_phi = atan(pos.z, pos.x);
        float disk_y = get_disk_height(hit_r, hit_phi);
        
        if ((prev_pos.y - prev_disk_y) * (pos.y - disk_y) < 0.0) {
            float t_cross = (prev_pos.y - prev_disk_y) / ((prev_pos.y - prev_disk_y) - (pos.y - disk_y));
            vec3 hit_pos = mix(prev_pos, pos, t_cross);
            vec3 hit_p = p; 
            
            winding_number++;
            vec3 disk_col = evaluate_disk(hit_pos, hit_p, winding_number);
            if (length(disk_col) > 0.0) {
                float opacity = 0.85; 
                col += disk_col * alpha;
                alpha *= (1.0 - opacity);
                if (alpha < 0.005) break;
            }
        }
        
        prev_pos = pos;
        prev_disk_y = disk_y;
    }
    
    if (alpha > 0.005) {
        vec3 bg_dir = normalize(p); 
        vec3 cell = floor(bg_dir * 180.0);
        vec3 offset = bg_dir * 180.0 - cell;
        
        float n = fract(sin(dot(cell, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
        if (n > 0.95) {
            vec3 star_pos = vec3(fract(n * 134.5), fract(n * 253.2), fract(n * 312.1));
            float dist = length(offset - star_pos);
            float star_intensity = smoothstep(0.4, 0.0, dist) * (n - 0.95) * 55.0;
            col += vec3(star_intensity) * alpha;
        }
    }
    
    col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);
    col = clamp(col, 0.0, 1.0);
    return pow(col, vec3(1.0/2.2));
}

void main() {
    float a = pc.cameraPos.w; 
    vec2 uv = inUV * 2.0 - 1.0;
    uv.x *= pc.params.y / pc.params.z;
    
    outColor = vec4(render(uv), 1.0);
}
