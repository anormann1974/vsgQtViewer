#include "ReaderWriterStbi.h"

#include <vsg/io/ObjectCache.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <filesystem>

#include <QLoggingCategory>


namespace {
static QLoggingCategory lc("ReaderWriterStbi");
}


ReaderWriterStbi::ReaderWriterStbi()
{
    stbi_set_flip_vertically_on_load(true);
}

vsg::ref_ptr<vsg::Object> ReaderWriterStbi::read(const vsg::Path &filename, vsg::ref_ptr<const vsg::Options> options) const
{
    if (!std::filesystem::exists(filename))
        return {};

    int width, height, channels;
    if (const auto pixels = stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb); pixels)
    {
        auto vsg_data = vsg::ubvec4Array2D::create(width, height, vsg::Data::Layout{VK_FORMAT_R8G8B8_UNORM});
        std::memcpy(vsg_data->data(), pixels, vsg_data->dataSize());
        stbi_image_free(pixels);
        return vsg_data;
    }

    return {};
}

vsg::ref_ptr<vsg::Object> ReaderWriterStbi::read(std::istream &fin, vsg::ref_ptr<const vsg::Options> options) const
{
    std::string buffer(1<<16, 0); // 64kB
    std::string input;

    while (!fin.eof())
    {
        fin.read(&buffer[0], buffer.size());
        const auto bytes_readed = fin.gcount();
        input.append(&buffer[0], bytes_readed);
    }

    int width, height, channels;
    if (const auto pixels = stbi_load_from_memory((stbi_uc*)input.data(), input.size(), &width, &height, &channels, STBI_rgb_alpha); pixels)
    {
        auto vsg_data = vsg::ubvec4Array2D::create(width, height, vsg::Data::Layout{VK_FORMAT_R8G8B8A8_UNORM});
        std::memcpy(vsg_data->data(), pixels, vsg_data->dataSize());
        stbi_image_free(pixels);
        return vsg_data;
    }

    return {};
}
