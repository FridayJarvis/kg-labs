//----------------mainImage----------------
const int MAX_MARCHING_STEPS = 120;

const float PRECISION   = .0001;
const float MIN_DIST    = .0;
const float MAX_DIST    = 1000.;
float g_projActive      = 0.0;
float g_turretAngle     = 0.0;
float g_gunAngle        = 0.0;
float g_fireTurretAngle = 0.0;
float g_fireGunAngle    = 0.0;

const vec3 BG_COL       = vec3(130, 170, 255) / 255.;
vec3  g_projPos = vec3(0.0);

struct Material{
    vec3 ambientCol;
    vec3 diffuseCol;
    vec3 specularCol;
    float shiness;
};

struct Object {
    float sd;
    Material material;
};

Object sdScene(vec3 p);

mat3 rotateX(float angle){
    float c = cos(radians(angle));
    float s = sin(radians(angle));
    return mat3(
        vec3(1,  0,  0),
        vec3(0,  c, -s),
        vec3(0,  s,  c)
    );
}

mat3 rotateY(float angle){
    float c = cos(radians(angle));
    float s = sin(radians(angle));
    return mat3(
        vec3(c,  0, -s),
        vec3(0,  1,  0),
        vec3(s,  0,  c)
    );
}

mat3 rotateZ(float angle) {
    float c = cos(radians(angle));
    float s = sin(radians(angle));
    return mat3(
        vec3(c, -s,  0),
        vec3(s,  c,  0),
        vec3(0,  0,  1)
    );
}

mat3 identity() {
    return mat3(
        vec3(1, 0, 0),
        vec3(0, 1, 0),
        vec3(0, 0, 1)
    );
}

Object minObj(Object obj1, Object obj2) {
    if (obj2.sd < obj1.sd) return obj2;
    return obj1;
}

//Materials
Material greenTankMaterial() {
    vec3 aCol = 0.3 * vec3(0.4, 0.45, 0.35);
    vec3 dCol = 0.6 * vec3(0.3, 0.4, 0.3);
    vec3 sCol = 0.2 * vec3(0.8, 0.8, 0.8);
    float a = 8.0;
    return Material(aCol, dCol, sCol, a);
}

Material groundMaterial() {
    vec3 ambient = 0.25 * vec3(0.25, 0.35, 0.2);
    vec3 diffuse  = 0.75 * vec3(0.36, 0.63, 0.19);
    vec3 specular = 0.05 * vec3(1.0);
    float shininess = 2.;
    return Material(ambient, diffuse, specular, shininess);
}

Material abramsMaterial() {
    vec3 base = vec3(253, 231, 174) / 255.;
    vec3 ambient  = 0.25 * base;
    vec3 diffuse  = 0.75 * base;
    vec3 specular = 0.15 * vec3(1.0);
    float shininess = 12.0;
    return Material(ambient, diffuse, specular, shininess);
}

Material trackTreadMaterial() {
    vec3 rubberBase = vec3(0.08, 0.06, 0.04);
    vec3 metalBits = vec3(0.25, 0.23, 0.2);
    vec3 ambient = 0.1 * rubberBase;
    vec3 diffuse = 0.5 * mix(rubberBase, metalBits, 0.2);
    vec3 specular = 0.1 * vec3(0.8, 0.8, 0.75);
    float shininess = 8.0;
    return Material(ambient, diffuse, specular, shininess);
}

Material projectileMaterial1() {
    vec3 base = vec3(0.82, 0.83, 0.84);

    vec3 ambient  = 0.2  * base;
    vec3 diffuse  = 0.7  * base;
    vec3 specular = 0.3  * vec3(0.95, 0.95, 1.0);

    float shininess = 32.0;

    return Material(ambient, diffuse, specular, shininess);
}

Material projectileMaterial2() {
    return Material(
        vec3(0.4, 0.15, 0.0),
        vec3(0.85, 0.35, 0.05),
        vec3(1.0,  0.8,  0.5),
        48.0
    );
}

//Lighting
//Calculation of gradient
vec3 calculateNormal(vec3 p) {
    vec2 e = vec2(0.001, -0.001);
    return normalize(
        e.xyy * sdScene(p + e.xyy).sd +
        e.yyx * sdScene(p + e.yyx).sd +
        e.yxy * sdScene(p + e.yxy).sd +
        e.xxx * sdScene(p + e.xxx).sd
    );
}

vec3 phong(vec3 p, vec3 rd, vec3 normal, Material mat, float shadow, vec3 lightPos, vec3 lightDir) {

    vec3 ambient  = mat.ambientCol;

    float dot_LN  = clamp(dot(lightDir, normal), 0., 1.);
    vec3 diffuse  = mat.diffuseCol * dot_LN;

    vec3 reflectDir          = reflect(-lightDir, normal);
    float dotReflectedLD_NRD = clamp(dot(reflectDir, -rd), 0., 1.);
    vec3 specular            = mat.specularCol * pow(dotReflectedLD_NRD, mat.shiness);

    return ambient + shadow * (diffuse + specular);
}

float getShadow(vec3 ro, vec3 rd, float tmax) {
    float res = 1.;
    float t = 0.02;
    for(int i = 0; i < 16; i++) {
        float h = sdScene(ro + rd * t).sd;
        res = min(res, 8. * h / t);
        t += clamp(h, 0.02, 0.10);
        if(h < PRECISION || t > tmax) break;
    }
    return clamp(res, 0., 1.0);
}

vec3 shadeObject(vec3 p, vec3 rd, Material mat) {
    vec3 lightPos   = vec3(10, 30, 20);
    vec3 lightDir   = normalize(lightPos - p);
    float lightDist = length(lightPos - p);

    vec3 normal      = calculateNormal(p);
    float shadow = getShadow(p + normal*0.9, lightDir, lightDist);

    return phong(p, rd, normal, mat, shadow, lightPos, lightDir);
}

//SDFs:
Object rayMarch(vec3 ro, vec3 rd){
    float depth = MIN_DIST;
    Object obj;
    {
        vec3 p;
        for(int i = 0; i < MAX_MARCHING_STEPS; ++i){
            p      = ro + rd * depth;
            obj    = sdScene(p);
            depth += obj.sd;
            if (obj.sd < PRECISION || depth > MAX_DIST) break;
        }
    }
    obj.sd = depth;
    return obj;
}

Object sdSphere(vec3 p, float r, vec3 pos, Material mat) {
    return Object(length(p - pos) - r, mat);
}

float terrainHeight(vec2 xz){
    float h = 7.0;
    float a = 10.0;
    float f = 0.05;
    for(int i = 0; i < 4; i++) {
        h += a * sin(xz.x * f) * cos(xz.y * f);
        f *= 2.0;
        a *= 0.5;
    }
    float radius = 80.0;
    float sm = 10.0;
    float dist = length(xz) - radius;
    float k = smoothstep(-sm, sm, dist);
    return h * k;
}

Object sdTerrain(vec3 p, Material mat) {
    float h = terrainHeight(p.xz);
    float d = p.y - h;
    return Object(d, mat);
}

Object sdTankBody(vec3 p, vec3 b, vec3 pos, mat3 transform, Material mat) {
    b      /= 2.;
    p       = (p - pos) * transform;
    vec3 q  = abs(p) - b;
    float d = length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);
    return Object(d, mat);
}

Object sdBeak(vec3 p, vec3 size, vec3 pos, mat3 transform, Material mat) {
    p = transform * (p - pos);
    float l2 = size.x * 0.5;
    float w2 = size.z * 0.5;
    vec3 q = p;
    q.y -= size.y * 0.5;
    vec3 bounds = vec3(l2, size.y * 0.5, w2);
    vec3 d = abs(q) - bounds;
    float boxDist = length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
    vec3 normal = normalize(vec3(size.y/size.x, 1.0, 0.0));
    float planeDist = dot(p - vec3(-l2, size.y, 0.0), normal);
    float wedgeDist = max(boxDist, -planeDist);
    wedgeDist = max(wedgeDist, -p.y);
    return Object(wedgeDist, mat);
}

//                          (rx, ry, rz) 
Object sdTankTurret(vec3 p, vec3 radii, vec3 pos, mat3 transform, Material mat) {
    p = transpose(transform) * (p - pos);
    vec3 k0 = p / radii;
    vec3 k1 = p / (radii * radii);
    float k0_len = length(k0);
    float k1_len = length(k1);
    float d = k0_len * (k0_len - 1.0) / k1_len;
    return Object(d, mat);
}

Object sdTankTurretRing(vec3 p, vec2 t, vec3 radii, vec3 pos, mat3 transform, Material mat) {
    p = transform * (p - pos);
    vec3 scaledP = vec3(p.x / radii.x, p.y, p.z / radii.y);
    float scaledLength = length(scaledP.xz);
    vec2 q = vec2(scaledLength - t.x, scaledP.y);
    float d = length(q) - t.y;
    d *= min(radii.x, radii.y);
    return Object(d, mat);
}

Object sdCilinder(vec3 p, float r, float h, vec3 pos, mat3 transform, Material mat){
    h /= 2.;
    p = transform * (p - pos);
    vec2 d = abs(vec2(length(p.xz),p.y)) - vec2(r,h);
    float sdf = min(max(d.x,d.y),0.0) + length(max(d,0.0));
    return Object(sdf, mat);
}

Object sdCone(vec3 p, float r, float h, vec3 pos, mat3 transform, Material mat) {
    h /= 2.;
    p = transform * (p - pos);
    
    vec2 q   = vec2(length(p.xz), p.y);
    vec2 tip = vec2(0.0, h);
    vec2 s   = normalize(vec2(r, -h));
    float L  = sqrt(r*r + h*h);
    
    float proj    = clamp(dot(q - tip, s), 0.0, L);
    vec2  closest = tip + proj * s;
    
    float dSlant = length(q - closest);
    float cr     = r*(q.y - h) + h*q.x; 
    if (cr < 0.0) dSlant = -dSlant;
    
    float dBase = -q.y;
    
    float d = (dSlant > 0.0 && dBase > 0.0)
              ? length(vec2(dSlant, dBase))
              : max(dSlant, dBase);
    
    return Object(d, mat);
}

Object sdUpperTankPart(vec3 p, float scale, vec3 pos, mat3 transform,
                        float turretAngle, float gunAngle, Material mat) {
    p = transform * (p - pos) * scale;

    vec3 tp = rotateY(-turretAngle) * p;

    Object turret     = sdTankTurret(tp, vec3(0.75,.6,.55), vec3(0,1.1,0), identity(), mat);
    Object turretRing = sdTankTurretRing(tp, vec2(1,.05), vec3(0.75,0.56,1), vec3(0,0.98,0), identity(), mat);

    vec3 gunPivot = vec3(0.0, 1.3, 0.0);
    vec3 gp = rotateZ(-gunAngle) * (tp - gunPivot) + gunPivot;
    Object gun = sdCilinder(gp, 0.1, 3., vec3(2, 1.3, 0), rotateZ(90.), mat);

    Object d = minObj(turret, turretRing);
    d = minObj(d, gun);
    return d;
}

Object sdTrack(vec3 p, vec3 size, vec2 truncation, float thick, vec3 pos, mat3 transform, Material mat) {
    size /= 2.;
    thick /= 2.;
    p = transform * (p - pos);
    float lx   = size.x * 0.5;
    float hy   = size.y * 0.5;
    float hz   = size.z * 0.5;
    float nlen = sqrt(lx*lx + hy*hy);
    float hw   = max(lx - hy, 0.0);
    float rStad = length(vec2(max( p.x - hw, 0.0), p.y)) - hy;
    float rRhom = ( p.x * hy + abs(p.y) * lx - lx * hy) / nlen;
    float rCap  = mix(rRhom, rStad, truncation.y);
    float lStad = length(vec2(max(-p.x - hw, 0.0), p.y)) - hy;
    float lRhom = (-p.x * hy + abs(p.y) * lx - lx * hy) / nlen;
    float lCap  = mix(lRhom, lStad, truncation.x);
    float profile = max(rCap, lCap);
    float shell = max(profile, -(profile + thick));
    float dz = abs(p.z) - hz;
    float d  = length(vec2(max(shell, 0.0), max(dz, 0.0))) + min(max(shell, dz), 0.0);
    return Object(d, mat);
}

Object sdTankChassis(vec3 p, float scale, vec3 pos, mat3 transform){
    p = transform * (p - pos) * scale;

    Object track1 = sdTrack(p, vec3(7.7, 2, 0.5), vec2(0.8, 0.7), 0.2, vec3(0.1, 0.5,  0.9), identity(), trackTreadMaterial());
    Object track2 = sdTrack(p, vec3(7.7, 2, 0.5), vec2(0.8, 0.7), 0.2, vec3(0.1, 0.5, -0.9), identity(), trackTreadMaterial());

    const float d_wheel = 0.82;
    Object r_b_drivingWheel = sdCilinder(p, .22, .25, vec3(-1.5,.5, .9), rotateX(90.), trackTreadMaterial());
    Object r_f_drivingWheel = sdCilinder(p, .20, .25, vec3( 1.7,.5, .9), rotateX(90.), trackTreadMaterial());
    Object l_b_drivingWheel = sdCilinder(p, .22, .25, vec3(-1.5,.5,-.9), rotateX(90.), trackTreadMaterial());
    Object l_f_drivingWheel = sdCilinder(p, .20, .25, vec3( 1.7,.5,-.9), rotateX(90.), trackTreadMaterial());

    Object r_wheel1 = sdCilinder(p, .25, .25, vec3(-1.5+d_wheel,    .31,-.9), rotateX(90.), trackTreadMaterial());
    Object r_wheel2 = sdCilinder(p, .25, .25, vec3(-1.5+d_wheel*2., .31,-.9), rotateX(90.), trackTreadMaterial());
    Object r_wheel3 = sdCilinder(p, .25, .25, vec3(-1.5+d_wheel*3., .31,-.9), rotateX(90.), trackTreadMaterial());
    Object l_wheel1 = sdCilinder(p, .25, .25, vec3(-1.5+d_wheel,    .31, .9), rotateX(90.), trackTreadMaterial());
    Object l_wheel2 = sdCilinder(p, .25, .25, vec3(-1.5+d_wheel*2., .31, .9), rotateX(90.), trackTreadMaterial());
    Object l_wheel3 = sdCilinder(p, .25, .25, vec3(-1.5+d_wheel*3., .31, .9), rotateX(90.), trackTreadMaterial());

    const float d_s_wheel = 0.5;
    Object r_smallWheel1 = sdCilinder(p, .1, .15, vec3(-1.2,0.8,.825), rotateX(90.), trackTreadMaterial());
    Object r_smallWheel2 = sdCilinder(p, .1, .15, vec3(-1.2+d_s_wheel,0.8,.825), rotateX(90.), trackTreadMaterial());
    Object r_smallWheel3 = sdCilinder(p, .1, .15, vec3(-1.2+d_s_wheel*2.,0.8,.825), rotateX(90.), trackTreadMaterial());
    Object r_smallWheel4 = sdCilinder(p, .1, .15, vec3(-1.2+d_s_wheel*3.,0.8,.825), rotateX(90.), trackTreadMaterial());
    Object r_smallWheel5 = sdCilinder(p, .1, .15, vec3(-1.2+d_s_wheel*4.,0.8,.825), rotateX(90.), trackTreadMaterial());
    Object r_smallWheel6 = sdCilinder(p, .1, .15, vec3(-1.2+d_s_wheel*5.,0.8,.825), rotateX(90.), trackTreadMaterial());
    
    Object l_smallWheel1 = sdCilinder(p, .1, .15, vec3(-1.2,0.8,-.825), rotateX(90.), trackTreadMaterial());
    Object l_smallWheel2 = sdCilinder(p, .1, .15, vec3(-1.2+d_s_wheel,0.8,-.825), rotateX(90.), trackTreadMaterial());
    Object l_smallWheel3 = sdCilinder(p, .1, .15, vec3(-1.2+d_s_wheel*2.,0.8,-.825), rotateX(90.), trackTreadMaterial());
    Object l_smallWheel4 = sdCilinder(p, .1, .15, vec3(-1.2+d_s_wheel*3.,0.8,-.825), rotateX(90.), trackTreadMaterial());
    Object l_smallWheel5 = sdCilinder(p, .1, .15, vec3(-1.2+d_s_wheel*4.,0.8,-.825), rotateX(90.), trackTreadMaterial());
    Object l_smallWheel6 = sdCilinder(p, .1, .15, vec3(-1.2+d_s_wheel*5.,0.8,-.825), rotateX(90.), trackTreadMaterial());

    Object d = minObj(r_b_drivingWheel, r_f_drivingWheel);
    d = minObj(d, l_b_drivingWheel);
    d = minObj(d, l_f_drivingWheel);

    d = minObj(d, r_wheel1);
    d = minObj(d, r_wheel2);
    d = minObj(d, r_wheel3);

    d = minObj(d, l_wheel1);
    d = minObj(d, l_wheel2);
    d = minObj(d, l_wheel3);

    d = minObj(d, r_smallWheel1);
    d = minObj(d, r_smallWheel2);
    d = minObj(d, r_smallWheel3);
    d = minObj(d, r_smallWheel4);
    d = minObj(d, r_smallWheel5);
    d = minObj(d, r_smallWheel6);

    d = minObj(d, l_smallWheel1);
    d = minObj(d, l_smallWheel2);
    d = minObj(d, l_smallWheel3);
    d = minObj(d, l_smallWheel4);
    d = minObj(d, l_smallWheel5);
    d = minObj(d, l_smallWheel6);

    d = minObj(d, track1);
    d = minObj(d, track2);
    return d;
}

Object sdTank(vec3 p, float scale, vec3 pos, mat3 transform, Material mat, float turretAngle, float gunAngle) {
    float bound = length(p - pos) - 5.5;
    if (bound > 1.0) return Object(bound, mat);

    p = transform * (p - pos) * scale;

    Object body     = sdTankBody(p, vec3(3,1,1.5), vec3(0,.5,0),      identity(),    mat);
    Object t_f_Beak = sdBeak(p, vec3(1,.7,1.5),    vec3(2,1,0),       rotateZ(180.), mat);
    Object b_f_Beak = sdBeak(p, vec3(1,.3,1.5),    vec3(2,0,0),       rotateY(180.), mat);
    Object t_b_Beak = sdBeak(p, vec3(.45,.7,1.5),  vec3(-1.725,1,0),  rotateX(180.), mat);
    Object b_b_Beak = sdBeak(p, vec3(.45,.3,1.5),  vec3(-1.725,0,0),  identity(),    mat);
    Object upperPart = sdUpperTankPart(p, 1., vec3(0,0,0), identity(), turretAngle, gunAngle, mat);
    Object chassis   = sdTankChassis(p, 1., vec3(0,0,0), identity());

    Object d = minObj(body, t_f_Beak);
    d = minObj(d, b_f_Beak);
    d = minObj(d, t_b_Beak);
    d = minObj(d, b_b_Beak);
    d = minObj(d, upperPart);
    d = minObj(d, chassis);
    return d;
}

Object sdScene(vec3 p) {
    Object ground = sdTerrain(p, groundMaterial());
    Object tank1  = sdTank(p, 1., vec3(0,0,0),   identity(),    greenTankMaterial(), g_turretAngle, g_gunAngle);
    Object tank2  = sdTank(p, 1., vec3(10,0,10), rotateY(-135.), abramsMaterial(),    0.0, 0.0);

    Object d = minObj(ground, tank1);
    d = minObj(d, tank2);
    
    if (g_projActive > 0.5){
        if (g_projActive > 0.5) {
        // Направление полёта снаряда (зафиксированное в момент выстрела)
        vec3 fireDir = rotateY(g_fireTurretAngle) * rotateZ(g_fireGunAngle) * vec3(1.0, 0.0, 0.0);

        mat3 cylRot  = rotateZ(90.0) * rotateZ(-g_fireGunAngle) * rotateY(-g_fireTurretAngle);
        mat3 coneRot = rotateZ(-90.0) * rotateZ(-g_fireGunAngle) * rotateY(-g_fireTurretAngle);

        vec3 coneBase = g_projPos + fireDir * 0.15;

        d = minObj(d, sdCilinder(p, 0.09, 0.3, g_projPos, cylRot, projectileMaterial1()));
        d = minObj(d, sdCone(p, 0.09, 0.5, coneBase,    coneRot, projectileMaterial2()));
    }
    }

    return d;
}

//Rendering of color
vec3 render(vec3 ro, vec3 rd){
    vec3 col   = BG_COL;

    Object obj = rayMarch(ro, rd);

    if (obj.sd > MAX_DIST) return col;

    vec3 p     = ro + rd * obj.sd;

    return shadeObject(p, rd, obj.material);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    vec4 projData  = texelFetch(iChannel1, ivec2(0, 0), 0);
    vec4 angleData = texelFetch(iChannel1, ivec2(3, 0), 0);
    vec4 fireAngleData  = texelFetch(iChannel1, ivec2(4, 0), 0);
    g_projPos     = projData.xyz;
    g_projActive  = projData.w;
    g_turretAngle = angleData.x;
    g_gunAngle    = angleData.y;
    g_fireTurretAngle   = fireAngleData.x;
    g_fireGunAngle      = fireAngleData.y;

    vec3 ro      = texelFetch(iChannel0, ivec2(0, 0), 0).xyz;
    vec3 forward = normalize(texelFetch(iChannel0, ivec2(1, 0), 0).xyz);

    // Normalized pixel coordinates (x in <-1.78, 1.78>, y in <-1, 1>)
    vec2 uv      = (fragCoord-.5*iResolution.xy)*2./iResolution.y;

    vec3 worldUp = vec3(0, 1, 0);
    vec3 right   = normalize(cross(forward, worldUp));
    vec3 up      = cross(right, forward);

    vec3 rd      = normalize(uv.x * right + uv.y * up + forward);

    vec3 col     = render(ro, rd);

    //Output color
    fragColor    = vec4(col, 1);
}



//Buffer B
const int KEY_ENTER = 13;
const int KEY_LEFT  = 37;
const int KEY_UP    = 38;
const int KEY_RIGHT = 39;
const int KEY_DOWN  = 40;

float getKey(int key) {
    return texelFetch(iChannel1, ivec2(key, 0), 0).x;
}

mat3 rotateY(float angle) {
    float c = cos(radians(angle));
    float s = sin(radians(angle));
    return mat3(vec3(c,0,-s),vec3(0,1,0),vec3(s,0,c));
}

mat3 rotateZ(float angle) {
    float c = cos(radians(angle));
    float s = sin(radians(angle));
    return mat3(vec3(c,-s,0),vec3(s,c,0),vec3(0,0,1));
}

// Пиксели:
// (0,0): pos.xyz,              active
// (1,0): vel.xyz,              bounces
// (2,0): prevEnter, 0, 0, 0
// (3,0): turretAngle,          gunAngle, 0, 0
// (4,0): fireTurretAngle,      fireGunAngle, 0, 0

void mainImage(out vec4 fragColor, in vec2 fragCoord) {

    ivec2 fc = ivec2(fragCoord);

    vec4 s0 = texelFetch(iChannel0, ivec2(0, 0), 0);
    vec4 s1 = texelFetch(iChannel0, ivec2(1, 0), 0);
    vec4 s2 = texelFetch(iChannel0, ivec2(2, 0), 0);
    vec4 s3 = texelFetch(iChannel0, ivec2(3, 0), 0);
    vec4 s4 = texelFetch(iChannel0, ivec2(4, 0), 0); // углы в момент выстрела

    vec3  pos             = s0.xyz;
    float active_         = s0.w;
    vec3  vel             = s1.xyz;
    float bounces         = s1.w;
    float prevEnter       = s2.x;
    float turretAngle     = s3.x;
    float gunAngle        = s3.y;
    float fireTurretAngle = s4.x;
    float fireGunAngle    = s4.y;

    float enterKey = getKey(KEY_ENTER);
    float leftKey  = getKey(KEY_LEFT);
    float rightKey = getKey(KEY_RIGHT);
    float upKey    = getKey(KEY_UP);
    float downKey  = getKey(KEY_DOWN);

    if (iFrame == 0) {
        pos = vec3(0.0); vel = vec3(0.0);
        active_ = 0.0; bounces = 0.0;
        turretAngle = 0.0; gunAngle = 0.0;
        fireTurretAngle = 0.0; fireGunAngle = 0.0;
    } else {
        float dt = clamp(iTimeDelta, 0.0, 0.05);

        // Вращение башни
        float turretSpeed = 30.0;
        if (leftKey  > 0.5) turretAngle += turretSpeed * dt;
        if (rightKey > 0.5) turretAngle -= turretSpeed * dt;

        // Подъём/опускание пушки
        float gunSpeed = 30.0;
        if (upKey   > 0.5) gunAngle = clamp(gunAngle - gunSpeed * dt, -30.0, 7.0);
        if (downKey > 0.5) gunAngle = clamp(gunAngle + gunSpeed * dt, -30.0, 7.0);

        if (enterKey > 0.5 && prevEnter < 0.5) {
            vec3 fireDir = rotateY(turretAngle) * rotateZ(gunAngle) * vec3(1.0, 0.0, 0.0);

            vec3 localTip = rotateZ(gunAngle) * vec3(2.6, 0.0, 0.0) + vec3(0.0, 1.3, 0.0);
            vec3 gunTip   = rotateY(turretAngle) * localTip;

            pos     = gunTip;
            vel     = fireDir * 50.0;
            active_ = 1.0;
            bounces = 0.0;

            //углы в момент выстрела
            fireTurretAngle = turretAngle;
            fireGunAngle    = gunAngle;
        }

        // Физика снаряда
        if (active_ > 0.5) {
            vel.y -= 80.0 * dt;
            pos   += vel * dt;

            if (pos.y < 0.0) {
                pos.y   = 0.0;
                vel.y   = abs(vel.y) * 1.;
                bounces += 1.0;
                if (bounces >= 5.0) active_ = 0.0;
            }
        }
    }

    if      (fc == ivec2(0, 0)) fragColor = vec4(pos,             active_);
    else if (fc == ivec2(1, 0)) fragColor = vec4(vel,             bounces);
    else if (fc == ivec2(2, 0)) fragColor = vec4(enterKey,        0.0, 0.0, 1.0);
    else if (fc == ivec2(3, 0)) fragColor = vec4(turretAngle,     gunAngle,    0.0, 1.0);
    else if (fc == ivec2(4, 0)) fragColor = vec4(fireTurretAngle, fireGunAngle, 0.0, 1.0);
    else                        fragColor = vec4(0.0);
}




//Buffer A
const int KEY_W = 87;
const int KEY_S = 83;
const int KEY_A = 65;
const int KEY_D = 68;
const int KEY_SHIFT = 16;
const int KEY_SPACE = 32;

float getKey(int key) {
    return texelFetch(iChannel1, ivec2(key, 0), 0).x;
}

mat3 rotateX(float angle){
    float c = cos(radians(angle));
    float s = sin(radians(angle));

    return mat3(
        vec3(1,  0,  0),
        vec3(0,  c, -s),
        vec3(0,  s,  c)
    );
}

mat3 rotateY(float angle){
    float c = cos(radians(angle));
    float s = sin(radians(angle));

    return mat3(
        vec3(c,  0, -s),
        vec3(0,  1,  0),
        vec3(s,  0,  c)
    );
}

void updateCamera(inout vec3 ro, inout vec3 rd, vec2 mouse) {
    float yaw    = -mouse.x * 180.0;
    float pitch  = clamp(-mouse.y * 90.0, -89.0, 89.0);

    vec3 forward;
    forward      = vec3(0, 0, -1);
    forward      = rotateY(yaw) * rotateX(pitch) * forward;
    forward      = normalize(forward);
    rd           = forward;

    vec3 worldUp = vec3(0, 1, 0);
    vec3 right   = normalize(cross(forward, worldUp));
    vec3 up      = cross(right, forward);

    float speed  = 10.0 * iTimeDelta;

    if(getKey(KEY_W) > 0.5)     ro += forward * speed;
    if(getKey(KEY_S) > 0.5)     ro -= forward * speed;
    if(getKey(KEY_A) > 0.5)     ro -= right   * speed;
    if(getKey(KEY_D) > 0.5)     ro += right   * speed;
    if(getKey(KEY_SPACE) > 0.5) ro += worldUp * speed;
    if(getKey(KEY_SHIFT) > 0.5) ro -= worldUp * speed;

    ro.xz = clamp(ro.xz, -50., 50.);
    ro.y = clamp(ro.y, 0.02, 25.);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    vec3 ro;
    vec3 forward;
    if (iFrame == 0){
        ro = vec3(0, 2, -2);
        forward = vec3(0, 0, -1);
    } else {
        ro = texelFetch(iChannel0, ivec2(0, 0), 0).xyz;
        forward = texelFetch(iChannel0, ivec2(1, 0), 0).xyz;
    }

    vec2 mouse = (iMouse.xy - .5 * iResolution.xy) * 2.0 / iResolution.y;

    updateCamera(ro, forward, mouse);

    if (fragCoord.x < 1. && fragCoord.y < 1.){
        fragColor = vec4(ro, 1);
    } else if (fragCoord.x < 2. && fragCoord.y < 1.) {
        fragColor = vec4(forward, 1);
    } else {
        fragColor = vec4(0);
    }
}