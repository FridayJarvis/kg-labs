#include <exception>
#include <iostream>

#include "camera.hpp"
#include "model.hpp"
#include "rasterizer.hpp"
#include "shader.hpp"

int main() {
    try {
        const int width = 1024;
        const int height = 1024;

        Model model("models/Sponsa.obj");

        Camera camera({ 0.f, 10.1f, 1.f },
            { 0.f, -20.f, 0.f },
            { 0.f, 1.f, 0.f },
            45.f,
            static_cast<float>(width) / static_cast<float>(height),
            0.1f,
            20.f);

        Mat4f model_matrix = Mat4f::translation({ 0.f, -0.05f, 0.f }) *
            Mat4f::scale({ 1.4f, 1.4f, 1.4f });

        PhongShader shader;
        shader.set_matrices(model_matrix, camera.view_matrix(), camera.projection_matrix());
        shader.set_light_direction(normalize(Vec3f{ 0.4f, 0.8f, 0.1f }));
        shader.set_light_color({ 1.f, 0.96f, 0.9f });
        shader.set_fill_light(normalize(Vec3f{ -0.3f, 0.4f, -0.2f }), { 0.45f, 0.5f, 0.6f });
        shader.set_view_position(camera.position());
        shader.set_material({ 0.15f, 0.1f, 0.08f },
            { 0.7f, 0.5f, 0.45f },
            { 0.4f, 0.35f, 0.3f },
            42.f);
        shader.set_exposure(1.8f);

        Rasterizer raster(width, height);
        raster.render(model, shader);

        if (!raster.write_png("output.png")) {
            std::cerr << "Failed to write output.png" << std::endl;
            return 1;
        }

        std::cout << "Rendered image saved to output.png" << std::endl;
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}