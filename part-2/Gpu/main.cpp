
#pragma warning (disable: 4996)

#define _USE_MATH_DEFINES

#include "../library/benchmark.hpp"
#include "../library/ppm.hpp"

#include <cl/cl.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <math.h>

namespace cl {
    template<int L>
    cl::size_t<L> new_size_t(std::vector<int> numbers) {
        cl::size_t<L> sz;
        for (int i = 0; i < L; i++)
            sz[i] = numbers[i];
        return sz;
    }
}

const auto arg = [](auto argc, auto argv, auto index, auto value)
{
    return argc > index ? argv[index] : value;
};

auto kernel(const std::string& filename)
{
    std::ifstream file(filename);
    std::stringstream ss;
    std::string str;
    while (std::getline(file, str))
    {
        ss << str << std::endl;
    }
    return ss.str();
}

auto rgb_to_rgba(std::vector<std::uint8_t>& rgb, int width, int height)
{
    std::vector<std::uint8_t> rgba(width*height * 4);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            auto rgb_index = (y * width + x) * 3;
            auto rgba_index = (y * width + x) * 4;

            rgba[rgba_index + 0] = rgb[rgb_index + 0];
            rgba[rgba_index + 1] = rgb[rgb_index + 1];
            rgba[rgba_index + 2] = rgb[rgb_index + 2];
            rgba[rgba_index + 3] = 255;
        }
    }

    return rgba;
}
auto rgb_from_rgba(std::vector<std::uint8_t>& rgba, int width, int height)
{
    std::vector<std::uint8_t> rgb(width*height * 3);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            auto rgb_index = (y * width + x) * 3;
            auto rgba_index = (y * width + x) * 4;

            rgb[rgb_index + 0] = rgba[rgba_index + 0];
            rgb[rgb_index + 1] = rgba[rgba_index + 1];
            rgb[rgb_index + 2] = rgba[rgba_index + 2];
        }
    }

    return rgb;
}

std::vector<float> filter(const int radius, const float weight = 1.0f)
{
    std::vector<float> matrix;
    matrix.reserve(radius*radius);

    float stdv = weight, s = 2.0 * stdv * stdv;  
    float sum = 0.0;  

    const int size = floor(radius / 2.0);

    for (int x = -size; x <= size; x++)
    {
        for (int y = -size; y <= size; y++)
        {
            float r = sqrt(x*x + y*y);
            auto value = (exp(-(r*r) / s)) * 1.0 / (sqrt(2.0 * M_PI) * stdv);
            sum += value;
            matrix.push_back(value);
        }
    }

    for (int i = 0; i < matrix.size(); i++)
        matrix[i] /= sum;

    return matrix;
}

int main(int argc, const char * argv[])
{
    ppm image(arg(argc, argv, 1, "../Library/lena.ppm"));

    auto radius = (int)pow(std::atoi(arg(argc, argv, 3, "3")), 2);
    auto output = arg(argc, argv, 2, "./cl-out.ppm");
    auto rgba = rgb_to_rgba(image.data, image.w, image.h);

    std::string src = kernel("kernels.cl");
    std::vector<cl::Device> devices;
    std::vector<std::uint8_t> outputPixels(rgba.size());
    std::vector<float> mask = filter(radius, 5.0f);

    cl::Program::Sources kernel(1, std::make_pair(src.c_str(), src.length()));
    cl::Platform p = cl::Platform::getDefault();
    p.getDevices(CL_DEVICE_TYPE_GPU, &devices);

    cl::Device device = devices.front();
    cl::Context context = cl::Context(device);
    cl::CommandQueue queue(context, device);
    cl::Program program(context, kernel);

    try
    {
        program.build();
    }
    catch (const cl::Error& e)
    {
        std::cerr << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << std::endl;
    }

    try
    {
        cl::ImageFormat format{ CL_RGBA, CL_UNORM_INT8 };
        cl::Buffer maskBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, mask.size() * sizeof(float), mask.data());
        cl::Image2D imageBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, format, image.w, image.h, 0, rgba.data());
        cl::Image2D outputBuffer(context, CL_MEM_READ_WRITE, format, image.w, image.h);

        cl::size_t<3> region = cl::new_size_t<3>({ (int)image.w, (int)image.h, 1 });
        cl::size_t<3> origin = cl::new_size_t<3>({ 0, 0, 0 });
        cl::NDRange local(16, 16), global(image.w, image.h);
        cl::Kernel blur(program, "unsharp_mask");

        blur.setArg(0, imageBuffer);
        blur.setArg(1, outputBuffer);
        blur.setArg(2, maskBuffer);
        blur.setArg(3, radius);

        queue.enqueueNDRangeKernel(blur, cl::NullRange, global, local);
        queue.enqueueReadImage(outputBuffer, CL_TRUE, origin, region, 0, 0, outputPixels.data());
    }
    catch (const cl::Error& e)
    {
        std::cerr << "Exception Caught: " << e.what() << std::endl;
    }
   
    image.write(output, rgb_from_rgba(outputPixels, image.w, image.h));
    return 0;
}