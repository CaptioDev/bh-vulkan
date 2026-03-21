#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec4 time_res;
    vec4 cameraPos;
} pc;

const float rs = 1.0;
const int MAX_STEPS = 2000;
const float STEP_SIZE = 0.02;

// Hash for noise
float hash12(vec2 p) {
    vec3 p3  = fract(vec3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash13(vec3 p3) {
    p3  = fract(p3 * .1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

// 2D Value Noise
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i + vec2(0.0, 0.0)), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Fractal Brownian Motion
float fbm(vec2 p) {
    float f = 0.0;
    float amp = 0.5;
    mat2 m = mat2(1.6,  1.2, -1.2,  1.6);
    for(int i=0; i<4; i++) {
        f += amp * vnoise(p);
        p = m * p;
        amp *= 0.5;
    }
    return f;
}

// Improved Starfield background (Extremely dim as per reality)
vec3 background(vec3 dir) {
    vec3 col = vec3(0.0);
    float n = hash13(dir * 200.0);
    // Extremely sparse and dim stars
    if(n > 0.999) {
        float intensity = (n - 0.999) * 100.0;
        col = vec3(intensity);
    }
    
    // Milky way band approximation (almost invisible, very dark)
    float band = exp(-pow(dir.y * 3.0, 2.0));
    float gas = hash13(dir * 5.0);
    col += vec3(0.02, 0.03, 0.05) * band * (0.1 + 0.3*gas);
    return col * 0.3;
}

// Accretion disk
vec4 diskColor(vec3 pos, vec3 vel) {
    float r = length(pos);
    float thickness = 0.08 + 0.02 * (r - 2.5);
    
    float distToPlane = abs(pos.y);
    if(distToPlane > thickness * 4.0 || r < 2.5 || r > 14.0) return vec4(0.0);

    float verticalDensity = exp(-pow(distToPlane / thickness, 2.0));
    
    // Keplerian Differential Rotation
    float omega = 12.0 / pow(r, 1.5);
    float angle = atan(pos.z, pos.x) - omega * pc.time_res.x * 0.1;
    vec2 rotated_pos = vec2(r * cos(angle), r * sin(angle));
    
    // Chaotic plasma structure (No repetitive sine waves)
    // 1. Low frequency turbulent clouds
    float clouds = fbm(rotated_pos * 1.2);
    // 2. High frequency fine details (streaks)
    float streaks = fbm(rotated_pos * 3.5 + pc.time_res.x * 0.05);
    // 3. Radial bands (purely distance based pseudo-randomness)
    float bands = fbm(vec2(r * 2.0, 0.0));
    
    // Combine for organic plasma
    float structure = 0.2 + 0.4 * clouds + 0.3 * bands + 0.1 * streaks;
    
    // Envelope limiting the disk extent
    float radialDensity = exp(-pow((r - 5.5) / 3.0, 2.0));
    radialDensity *= (structure * 2.0); 
    
    // Relativistic Doppler and Gravitational Redshift (g-factor)
    vec3 diskVel = normalize(vec3(-pos.z, 0.0, pos.x));
    float v_fluid = sqrt(rs / (2.0 * r)); // Keplerian velocity
    float gamma = 1.0 / sqrt(1.0 - v_fluid * v_fluid);
    
    // Photons travel radially AWAY from camera, into the disk.
    // The relative emission angle cos_alpha compares photon approach vs fluid velocity
    float cos_alpha = dot(-normalize(vel), diskVel);
    float D = 1.0 / (gamma * (1.0 - v_fluid * cos_alpha));
    
    // Gravitational redshift
    float r_cam = length(pc.cameraPos.xyz);
    float Z_grav = sqrt(max(0.001, 1.0 - rs / r_cam)) / sqrt(max(0.001, 1.0 - rs / r));
    
    float g = D * Z_grav;
    
    // Invariant bolometric intensity demands I scales as g^4
    float intensity = verticalDensity * radialDensity * pow(g, 4.0) * 2.5;
    
    // Interstellar 'Gargantua' cinematic color palette
    vec3 hot = vec3(1.0, 0.95, 0.8);
    vec3 mid = vec3(1.0, 0.5, 0.05);
    vec3 cold = vec3(0.3, 0.05, 0.0);
    
    float tempProgress = clamp((12.0 - r) / 8.0, 0.0, 1.0);
    vec3 baseColor = mix(cold, mid, tempProgress);
    baseColor = mix(baseColor, hot, clamp((4.5 - r)/1.5, 0.0, 1.0));
    
    // Apply spectral shift via g-factor
    // Blueshifts (g > 1) push towards blue/white, redshifts (g < 1) push towards red/black
    vec3 spectralShift = vec3(pow(g, -1.0), 1.0, pow(g, 1.0));
    vec3 shiftedColor = baseColor * spectralShift;
    shiftedColor = normalize(shiftedColor + 0.001) * length(baseColor);
    
    vec3 col = shiftedColor * intensity;
    float alpha = clamp(intensity * 0.5, 0.0, 1.0);
    return vec4(col, alpha);
}

vec3 render(vec2 in_uv) {
    vec2 uv = in_uv * 2.0 - 1.0;
    uv.x *= pc.time_res.y / pc.time_res.z;
    
    vec3 pos = pc.cameraPos.xyz;
    
    vec3 forward = normalize(-pos);
    vec3 right = normalize(cross(forward, vec3(0,1.0,0)));
    vec3 up = cross(right, forward);
    
    // Gravitational aberration (FOV distortion) based on camera depth in Schwarzschild metric
    float r_cam = length(pos);
    float lapse = sqrt(max(0.001, 1.0 - rs / r_cam));
    
    // The lapse function squishes the radial axis relative to angular axes
    vec3 vel = normalize(forward * lapse + uv.x * right * 0.5 + uv.y * up * 0.5);
    vec3 col = vec3(0.0);
    float alpha = 1.0;
    float r = length(pos);
    
    for(int i = 0; i < MAX_STEPS; i++) {
        r = length(pos);
        if(r < rs) break;
        
        vec3 h = cross(pos, vel);
        float h2 = dot(h, h);
        vec3 acc = -1.5 * rs * h2 / pow(r, 5.0) * pos;
        
        // Adaptive step limits disk exhaustion
        float dt = STEP_SIZE * max(r * 0.4, 0.5);
        if(r > 2.0 && r < 15.0) {
            float approach = max(abs(pos.y) * 0.6, 0.015);
            dt = min(dt, approach);
        }
        
        vel += acc * dt;
        vel = normalize(vel);
        pos += vel * dt;
        
        vec4 dcol = diskColor(pos, vel);
        if(dcol.a > 0.0) {
            col += dcol.rgb * alpha * dt; 
            alpha *= (1.0 - dcol.a * dt);
            if(alpha < 0.005) break; // Reached full opacity block
        }
        
        if(r > 250.0) break;
    }
    
    if(r >= rs) {
        col += background(vel) * alpha;
    }
    
    // Enhanced ACES HDR Tone mapping 
    col = col * 1.2;
    col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);
    col = clamp(col, 0.0, 1.0);
    col = pow(col, vec3(1.0/2.2));
    
    return col;
}

void main() {
    vec3 totalCol = vec3(0.0);
    
    // 2x2 SSAA (Supersampling Anti-Aliasing)
    vec2 pixelSize = 1.0 / pc.time_res.yz;
    
    vec2 offsets[4] = vec2[](
        vec2(-0.25, -0.25),
        vec2( 0.25, -0.25),
        vec2(-0.25,  0.25),
        vec2( 0.25,  0.25)
    );
    
    for(int i = 0; i < 4; i++) {
        totalCol += render(inUV + offsets[i] * pixelSize);
    }
    
    outColor = vec4(totalCol * 0.25, 1.0);
}
